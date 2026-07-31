#!/usr/bin/env python3
"""树莓派 ↔ STM32/MSPM0 串口回路测试 —— PING/PONG + 帧解析。

    python3 test-serial-loopback.py --self-test              # 无硬件, CI 里跑
    python3 test-serial-loopback.py --port /dev/ttyAMA0      # 实机
    CHASSIS=mecanum python3 test-serial-loopback.py --port /dev/ttyAMA0

退出码: 0 通过 / 1 失败 / 2 缺 pyserial 或用法错误。

--------------------------------------------------------------------------
为什么是独立 .py 而不是 .sh 里的 heredoc
--------------------------------------------------------------------------

task-15 原文把这段 Python 塞在 shell heredoc 里, 并且用 `${SERIAL}` 做
shell 插值。那样写有三个问题, 每一个都在本仓库里已经踩过:

  1. heredoc 里的 Python **不会被任何语法检查覆盖** —— `bash -n` 只看 shell,
     `py_compile` 看不到它。改错一个缩进要到实机上才发现。
     (同样的理由让 validate_consistency.py 从 validate.sh 的 heredoc 里搬了出来。)
  2. 没法单独调试, 更没法写单元测试。而这段代码是**串口协议的第三份实现**
     (另两份是 STM32 与 MSPM0 固件), 恰恰是最需要测试的那种代码。
  3. shell 插值把设备路径直接拼进 Python 源码。

现在这份文件的帧编解码是纯函数, `--self-test` 不碰任何硬件就能把黄金帧
钉死。`.sh` 入口保留, 只是转发。

--------------------------------------------------------------------------
黄金帧 —— 与固件的 test_protocol.c 同源
--------------------------------------------------------------------------

ADR-0003 的帧结构有三份独立实现: STM32 固件、MSPM0 固件、本文件。
固件那两份由 `src/firmware/mspm0_diff/test/test_protocol.c` 的
`test_pong_golden_frame` 逐字节钉住; 本文件由 `--self-test` 钉住同一组字节。
任何一端"顺手优化"了字节序或 CRC 参数, 两边总有一处会红。
"""

import argparse
import os
import sys
from typing import List, Optional, Tuple

# --- 帧结构常量 (ADR-0003, 与 src/firmware/common/protocol_frame.h 一致) ---
FRAME_SOF = 0xA5
FRAME_EOF = 0x5A
FRAME_LEN_OVERHEAD = 4          # CMD(1) + CRC(2) + EOF(1)
FRAME_MAX_DATA_LEN = 251
FRAME_MIN_LEN_FIELD = FRAME_LEN_OVERHEAD
FRAME_MAX_LEN_FIELD = 255

CMD_SET_VELOCITY = 0x01
CMD_EMERGENCY_STOP = 0x02
CMD_PING = 0x03
CMD_TELEMETRY = 0x11
CMD_ACK = 0x12
CMD_PONG = 0x13
CMD_ERROR = 0xFF

PAYLOAD_LEN_PONG = 5

BOARD_NAMES = {0x01: "STM32F407", 0x02: "MSPM0G3507"}
CHASSIS_NAMES = {0x01: "mecanum", 0x02: "differential"}

#: CRC-16/CCITT-FALSE 的标准测试向量 (与 crc16.h 的 CRC16_CCITT_CHECK_VALUE 一致)
CRC16_CHECK_VALUE = 0x29B1

DEFAULT_BAUDRATE = 115200


def crc16_ccitt(data: bytes) -> int:
    """CRC-16/CCITT-FALSE: poly=0x1021, init=0xFFFF, 不反射, 不异或输出。

    与 `src/firmware/common/crc16.c` 完全一致。校验向量:
    `crc16_ccitt(b"123456789") == 0x29B1`。
    """
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode_frame(cmd: int, data: bytes = b"") -> bytes:
    """打包一帧。

    Args:
        cmd: 命令字。
        data: DATA 段, 最长 251 字节。

    Returns:
        完整帧 SOF|LEN|CMD|DATA|CRC(小端)|EOF。

    Raises:
        ValueError: DATA 超长。
    """
    if len(data) > FRAME_MAX_DATA_LEN:
        raise ValueError(f"DATA 最长 {FRAME_MAX_DATA_LEN} 字节, 收到 {len(data)}")
    payload = bytes([cmd]) + data
    crc = crc16_ccitt(payload)
    # LEN = CMD + DATA + CRC + EOF = len(data) + 4
    return (bytes([FRAME_SOF, len(data) + FRAME_LEN_OVERHEAD]) + payload
            + bytes([crc & 0xFF, (crc >> 8) & 0xFF]) + bytes([FRAME_EOF]))


class FrameParser:
    """拆帧状态机 —— 与 `protocol_frame.c` 的 FrameParser 行为一致。

    刻意**不**用"找第一个 0xA5 然后按偏移读"的写法。载荷里完全可以出现
    0xA5/0x5A (协议不做字节填充, 见 protocol_frame.h 的说明), 按首个 0xA5
    定位会在遥测帧里随机对错。而且固件在 PONG 之外还会周期上报 TELEMETRY,
    收到的缓冲里本来就不止一帧。

    统计量与固件同名, 便于两端对着看链路质量。
    """

    def __init__(self) -> None:
        """初始化状态机与统计量。"""
        self.reset(clear_stats=True)

    def reset(self, clear_stats: bool = False) -> None:
        """复位状态机。clear_stats=False 时保留统计量 (对应空闲重同步)。"""
        self._state = "SOF"
        self._len_field = 0
        self._buffer = bytearray()
        if clear_stats:
            self.frames_ok = 0
            self.err_len = 0
            self.err_crc = 0
            self.err_eof = 0

    def push(self, chunk: bytes) -> List[Tuple[int, bytes]]:
        """喂入一段字节, 返回本段中解析出的全部 (cmd, data)。"""
        frames: List[Tuple[int, bytes]] = []
        for byte in chunk:
            frame = self._push_byte(byte)
            if frame is not None:
                frames.append(frame)
        return frames

    def _push_byte(self, byte: int) -> Optional[Tuple[int, bytes]]:
        """喂入一个字节。返回 (cmd, data) 表示这一字节让一帧完成。"""
        if self._state == "SOF":
            if byte == FRAME_SOF:
                self._state = "LEN"
            return None

        if self._state == "LEN":
            if not FRAME_MIN_LEN_FIELD <= byte <= FRAME_MAX_LEN_FIELD:
                self.err_len += 1
                # LEN 非法时**不能只是丢掉**: 这个字节本身可能就是真正的 SOF
                # (线路上多了一个杂散 0xA5 时正是如此)。重新按 SOF 判一次。
                self._state = "LEN" if byte == FRAME_SOF else "SOF"
                return None
            self._len_field = byte
            self._buffer = bytearray()
            self._state = "PAYLOAD"
            return None

        self._buffer.append(byte)
        if len(self._buffer) < self._len_field:
            return None

        # 收满了: CMD + DATA + CRC(2) + EOF(1)
        payload = bytes(self._buffer[:self._len_field - 3])
        crc_received = self._buffer[self._len_field - 3] | (self._buffer[self._len_field - 2] << 8)
        eof = self._buffer[self._len_field - 1]
        self.reset()

        if crc_received != crc16_ccitt(payload):
            self.err_crc += 1
            return None
        if eof != FRAME_EOF:
            self.err_eof += 1
            return None
        self.frames_ok += 1
        return payload[0], payload[1:]


def decode_pong(data: bytes) -> dict:
    """解析 PONG 的 5 字节载荷。

    Raises:
        ValueError: 长度不符。
    """
    if len(data) != PAYLOAD_LEN_PONG:
        raise ValueError(f"PONG 载荷应为 {PAYLOAD_LEN_PONG} 字节, 收到 {len(data)}")
    major, minor, patch, board, chassis = data
    return {
        "firmware": f"v{major}.{minor}.{patch}",
        "board_type": board,
        "board": BOARD_NAMES.get(board, f"unknown(0x{board:02X})"),
        "chassis_type": chassis,
        "chassis": CHASSIS_NAMES.get(chassis, f"unknown(0x{chassis:02X})"),
    }


# ===========================================================================
# 自测 (无硬件)
# ===========================================================================

#: 黄金帧。与 src/firmware/mspm0_diff/test/test_protocol.c 的
#: test_pong_golden_frame 同源 —— 那边逐字节断言固件发出的字节,
#: 这边逐字节断言 Pi 端认得同样的字节。
GOLDEN_PING = bytes.fromhex("a5 04 03 93 d1 5a".replace(" ", ""))
GOLDEN_PONG_MSPM0 = bytes.fromhex("a5 09 13 00 01 00 02 02 e0 ea 5a".replace(" ", ""))
GOLDEN_PONG_STM32 = bytes.fromhex("a5 09 13 00 01 00 01 01 d0 8f 5a".replace(" ", ""))


def self_test() -> int:
    """不碰硬件的协议自测。

    Returns:
        0 全过 / 1 有失败。
    """
    passed = 0
    failed = 0

    def check(name: str, ok: bool, detail: str = "") -> None:
        nonlocal passed, failed
        mark = "✓" if ok else "✗"
        print(f"  {mark} {name}" + (f"  ({detail})" if detail else ""))
        if ok:
            passed += 1
        else:
            failed += 1

    print("=== 串口协议自测 (无硬件) ===")

    # 1. CRC 标准向量 —— 三份实现的共同锚点
    actual = crc16_ccitt(b"123456789")
    check("CRC-16/CCITT-FALSE 标准向量", actual == CRC16_CHECK_VALUE,
          f"0x{actual:04X}, 期望 0x{CRC16_CHECK_VALUE:04X}")

    # 2. 黄金帧: 打包
    built = encode_frame(CMD_PING)
    check("PING 黄金帧逐字节一致", built == GOLDEN_PING,
          f"{built.hex(' ')} vs {GOLDEN_PING.hex(' ')}")

    # 3. 黄金帧: 解包 (两块板)
    for name, golden, board, chassis in (
        ("MSPM0", GOLDEN_PONG_MSPM0, 0x02, 0x02),
        ("STM32", GOLDEN_PONG_STM32, 0x01, 0x01),
    ):
        parser = FrameParser()
        frames = parser.push(golden)
        ok = len(frames) == 1 and frames[0][0] == CMD_PONG
        if ok:
            info = decode_pong(frames[0][1])
            ok = (info["board_type"] == board and info["chassis_type"] == chassis
                  and info["firmware"] == "v0.1.0")
            check(f"{name} PONG 黄金帧解析", ok,
                  f"{info['firmware']} {info['board']} {info['chassis']}")
        else:
            check(f"{name} PONG 黄金帧解析", False, f"解出 {len(frames)} 帧")

    # 4. LEN 字段 = DATA + 4
    frame = encode_frame(CMD_PONG, bytes(PAYLOAD_LEN_PONG))
    check("LEN = DATA + 4", frame[1] == PAYLOAD_LEN_PONG + FRAME_LEN_OVERHEAD,
          f"LEN=0x{frame[1]:02X}")

    # 5. 载荷里出现 SOF/EOF 也要能正确拆出来 (协议不做字节填充)
    tricky = bytes([FRAME_SOF, FRAME_EOF, FRAME_SOF, 0x00])
    parser = FrameParser()
    frames = parser.push(encode_frame(CMD_TELEMETRY, tricky))
    check("载荷含 0xA5/0x5A 仍能拆出", len(frames) == 1 and frames[0][1] == tricky,
          f"解出 {len(frames)} 帧")

    # 6. 一次收到多帧 (固件在 PONG 之外还会周期上报 TELEMETRY)
    parser = FrameParser()
    frames = parser.push(encode_frame(CMD_TELEMETRY, bytes(18))
                         + GOLDEN_PONG_MSPM0
                         + encode_frame(CMD_ACK, bytes([CMD_PING])))
    commands = [cmd for cmd, _ in frames]
    check("一次缓冲里的三帧全部拆出",
          commands == [CMD_TELEMETRY, CMD_PONG, CMD_ACK],
          f"{[hex(c) for c in commands]}")

    # 7. CRC 错必须被拒 —— 这条比"对的能过"重要
    corrupted = bytearray(GOLDEN_PONG_MSPM0)
    corrupted[4] ^= 0xFF
    parser = FrameParser()
    frames = parser.push(bytes(corrupted))
    check("CRC 错的帧被拒", not frames and parser.err_crc == 1,
          f"解出 {len(frames)} 帧, err_crc={parser.err_crc}")

    # 8. EOF 错必须被拒
    corrupted = bytearray(GOLDEN_PONG_MSPM0)
    corrupted[-1] = 0x00
    parser = FrameParser()
    frames = parser.push(bytes(corrupted))
    check("EOF 错的帧被拒", not frames and parser.err_eof == 1,
          f"err_eof={parser.err_eof}")

    # 9. 前导垃圾字节之后仍能同步上
    parser = FrameParser()
    frames = parser.push(b"\x00\xff\x5a\x12" + GOLDEN_PONG_MSPM0)
    check("前导垃圾后能重新同步", len(frames) == 1 and frames[0][0] == CMD_PONG,
          f"解出 {len(frames)} 帧")

    # 10. 杂散 SOF 紧接真 SOF —— protocol_frame.h @warning 描述的失同步窗口。
    #
    #     真 SOF (0xA5 = 165) 会被当成 LEN 字段读掉, 而 165 是合法 LEN,
    #     于是状态机空等 165 字节。@115200 约 14 ms, 期间到达的正常帧
    #     **全部被吞掉** —— 两帧 PONG 一共 22 字节, 一帧都出不来。
    #
    #     这不是本实现的 bug, 是任何不带转义的定长头协议都有的性质,
    #     固件侧一字不差地是同一个行为。所以这里断言的是"确实被吞",
    #     而不是"能恢复" —— 写成"能恢复"会是一条永远绿的假断言。
    parser = FrameParser()
    frames = parser.push(b"\xa5" + GOLDEN_PONG_MSPM0 + GOLDEN_PONG_MSPM0)
    check("杂散 SOF 吞掉随后的帧 (与固件同行为)", frames == [],
          f"22 字节里解出 {len(frames)} 帧 —— 见 protocol_frame.h @warning")

    # 11. 空闲重同步是解药 —— 对应固件的 frame_parser_reset() /
    #     protocol_notify_line_idle()。上位机总是把一帧连续发完, 所以
    #     "收了半截 + 线路空闲"必然是失同步, 立刻放弃比空等 165 字节安全。
    #     run_loopback() 在每次 read() 读空时调 reset(), 就是这一条。
    parser = FrameParser()
    parser.push(b"\xa5" + GOLDEN_PONG_MSPM0[:5])    # 半截帧
    parser.reset()                                   # 线路空闲
    frames = parser.push(GOLDEN_PONG_MSPM0)
    check("空闲重同步后立刻恢复", len(frames) == 1 and frames[0][0] == CMD_PONG,
          f"解出 {len(frames)} 帧")

    # 12. 非法 LEN 被计数
    parser = FrameParser()
    parser.push(bytes([FRAME_SOF, 0x02]))       # LEN=2 < 4
    check("非法 LEN 被拒并计数", parser.err_len == 1, f"err_len={parser.err_len}")

    print(f"\n通过 {passed} · 失败 {failed}")
    return 0 if failed == 0 else 1


# ===========================================================================
# 实机回路
# ===========================================================================

def run_loopback(port: str, baudrate: int, timeout: float,
                 expected_chassis: Optional[str]) -> int:
    """在实机上跑一次 PING → PONG。

    Returns:
        0 通过 / 1 失败 / 2 没装 pyserial。
    """
    try:
        import serial
    except ImportError:
        print("SKIP: 没装 pyserial —— 实机回路测试跑不了。"
              "树莓派上: pip3 install pyserial", file=sys.stderr)
        return 2

    print("=== 串口回路测试 ===")
    print(f"端口 {port} · {baudrate} 8N1 · 超时 {timeout}s")

    try:
        link = serial.Serial(port, baudrate, timeout=timeout)
    except serial.SerialException as error:
        print(f"✗ 打不开串口: {error}", file=sys.stderr)
        print("  查: 线接了吗 / 波特率对吗 / 当前用户在 dialout 组里吗 / "
              "树莓派的串口控制台关了吗 (raspi-config)", file=sys.stderr)
        return 1

    parser = FrameParser()
    with link:
        # 先清掉可能积压的遥测, 免得把上一轮的旧帧当成本次应答
        link.reset_input_buffer()

        ping = encode_frame(CMD_PING)
        print(f"TX PING: {ping.hex(' ')}")
        link.write(ping)

        pong = None
        others: List[int] = []
        deadline_reads = max(1, int(timeout / 0.1))
        for _ in range(deadline_reads):
            chunk = link.read(256)
            if not chunk:
                # 读空 = 线路空闲。这是 Python 侧的"空闲重同步", 对应固件
                # uart.c 的 IDLE 中断 → protocol_notify_line_idle()。
                # 没有这一步的话, 线上一个杂散 0xA5 就能让状态机空等 165 字节,
                # 期间到达的 PONG 全被吞掉, 表现成"固件不应答" (自测第 10 条)。
                parser.reset()
                continue
            for cmd, data in parser.push(chunk):
                if cmd == CMD_PONG:
                    pong = data
                    break
                others.append(cmd)
            if pong is not None:
                break

    if pong is None:
        print("✗ 没收到 PONG", file=sys.stderr)
        if others:
            print(f"  收到了别的帧: {[hex(c) for c in others]} —— "
                  "链路是通的, 但固件没应答 PING", file=sys.stderr)
        else:
            print("  一帧都没收到。查 TX/RX 是不是接反了、地线有没有共。",
                  file=sys.stderr)
        print(f"  帧统计: ok={parser.frames_ok} len错={parser.err_len} "
              f"crc错={parser.err_crc} eof错={parser.err_eof}", file=sys.stderr)
        if parser.err_crc:
            print("  有 CRC 错 —— 通常是波特率不匹配或线太长/干扰大。", file=sys.stderr)
        return 1

    try:
        info = decode_pong(pong)
    except ValueError as error:
        print(f"✗ PONG 载荷不合法: {error}", file=sys.stderr)
        return 1

    print(f"✓ 收到 PONG: 固件 {info['firmware']} · "
          f"板卡 {info['board']} · 底盘 {info['chassis']}")
    print(f"  帧统计: ok={parser.frames_ok} len错={parser.err_len} "
          f"crc错={parser.err_crc} eof错={parser.err_eof}")

    if info["board_type"] not in BOARD_NAMES:
        print(f"✗ 板卡类型 0x{info['board_type']:02X} 不在 ADR-0003 的编码表里",
              file=sys.stderr)
        return 1

    if expected_chassis:
        if info["chassis"] != expected_chassis:
            # 判失败而不是告警。底盘类型不符意味着运动学解算会用错模型 ——
            # 车会动, 但走的方向不对, 而这在原地测试时很难看出来。
            print(f"✗ 底盘类型不符: 环境变量说 {expected_chassis}, "
                  f"板子自报 {info['chassis']}", file=sys.stderr)
            print("  要么烧错了固件, 要么 CHASSIS 环境变量设错了。"
                  "两种都会让运动学用错模型。", file=sys.stderr)
            return 1
        print(f"✓ 底盘类型与 CHASSIS={expected_chassis} 一致")

    print("\n串口通信正常")
    return 0


def main(argv: Optional[List[str]] = None) -> int:
    """命令行入口。"""
    parser = argparse.ArgumentParser(description="树莓派 ↔ STM32/MSPM0 串口回路测试")
    parser.add_argument("--self-test", action="store_true",
                        help="只跑协议自测, 不碰硬件 (CI 用)")
    parser.add_argument("--port", default="/dev/ttyAMA0", help="串口设备")
    parser.add_argument("--baudrate", type=int, default=DEFAULT_BAUDRATE, help="波特率")
    parser.add_argument("--timeout", type=float, default=5.0, help="等待 PONG 的超时 (s)")
    parser.add_argument("--expect-chassis", default=os.environ.get("CHASSIS"),
                        choices=(None, "mecanum", "differential", "diff"),
                        help="期望的底盘类型, 默认取环境变量 CHASSIS")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()

    expected = args.expect_chassis
    # compose / entrypoint 里用的是 diff, ADR-0003 的编码表里叫 differential。
    # 两个名字都得认, 否则实机上会因为一个别名判失败。
    if expected == "diff":
        expected = "differential"
    return run_loopback(args.port, args.baudrate, args.timeout, expected)


if __name__ == "__main__":
    sys.exit(main())
