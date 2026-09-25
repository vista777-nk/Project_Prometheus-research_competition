#!/usr/bin/env python3
"""树莓派 ↔ STM32/MSPM0 串口回路测试。

帧实现直接复用生产 ``chassis_bridge.py`` 使用的 ``chassis_protocol.py``，避免
“测试协议通过、生产解析器却漂移”。``--self-test`` 不访问硬件，供 CI 使用。
"""

import argparse
import os
import sys
import time
from pathlib import Path
from typing import List, Optional

REPO_ROOT = Path(__file__).resolve().parents[3]
CAR_SCRIPTS = REPO_ROOT / "src" / "air_ground_car_bringup" / "scripts"
if str(CAR_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(CAR_SCRIPTS))

from chassis_protocol import (  # noqa: E402
    BOARD_NAMES,
    CHASSIS_NAMES,
    CMD_ACK,
    CMD_PING,
    CMD_PONG,
    CMD_TELEMETRY,
    CMD_ULTRASONIC,
    CRC16_CHECK_VALUE,
    DEFAULT_BAUDRATE,
    FRAME_EOF,
    FRAME_SOF,
    FrameParser,
    crc16_ccitt,
    decode_pong,
    decode_ultrasonic,
    encode_frame,
    encode_ultrasonic,
    expected_identity,
)

MIN_PROTOCOL_VERSION = (0, 2, 0)
GOLDEN_PING = bytes.fromhex("a5 04 03 93 d1 5a")
# 与两块固件 test_protocol.c 的 v0.2.0 PONG 字节逐一对应。
GOLDEN_PONG_MSPM0 = bytes.fromhex("a5 09 13 00 02 00 02 02 3c 71 5a")
GOLDEN_PONG_STM32 = bytes.fromhex("a5 09 13 00 02 00 01 01 0c 14 5a")


def self_test() -> int:
    """运行无硬件协议门禁。"""
    passed = 0
    failed = 0

    def check(name: str, ok: bool, detail: str = "") -> None:
        nonlocal passed, failed
        print("  {} {}{}".format("✓" if ok else "✗", name, "  " + detail if detail else ""))
        if ok:
            passed += 1
        else:
            failed += 1

    print("=== 串口协议自测 (无硬件) ===")
    actual_crc = crc16_ccitt(b"123456789")
    check("CRC-16/CCITT-FALSE 标准向量", actual_crc == CRC16_CHECK_VALUE)
    check("PING 黄金帧逐字节一致", encode_frame(CMD_PING) == GOLDEN_PING)

    for name, golden, board, chassis in (
        ("MSPM0", GOLDEN_PONG_MSPM0, 0x02, 0x02),
        ("STM32", GOLDEN_PONG_STM32, 0x01, 0x01),
    ):
        frames = FrameParser().push(golden)
        ok = len(frames) == 1 and frames[0][0] == CMD_PONG
        if ok:
            info = decode_pong(frames[0][1])
            ok = (
                info.version == MIN_PROTOCOL_VERSION
                and info.board_type == board
                and info.chassis_type == chassis
            )
        check("{} PONG 黄金帧解析".format(name), ok)

    tricky = encode_ultrasonic((FRAME_SOF, FRAME_EOF, 1000, 0xFFFF))
    frames = FrameParser().push(tricky)
    check(
        "超声波载荷含 0xA5/0x5A 仍可拆出",
        len(frames) == 1 and frames[0][0] == CMD_ULTRASONIC,
    )
    if frames:
        readings = decode_ultrasonic(frames[0][1])
        check(
            "超声波无回波哨兵不会伪装成距离",
            readings["right"] is None and readings["left"] == 1.0,
        )

    parser = FrameParser()
    frames = parser.push(
        encode_frame(CMD_TELEMETRY, bytes(18))
        + GOLDEN_PONG_MSPM0
        + encode_frame(CMD_ACK, bytes((CMD_PING,)))
    )
    check(
        "一次缓冲里的三帧全部拆出",
        [cmd for cmd, _data in frames] == [CMD_TELEMETRY, CMD_PONG, CMD_ACK],
    )

    corrupted = bytearray(GOLDEN_PONG_MSPM0)
    corrupted[4] ^= 0xFF
    parser = FrameParser()
    check("CRC 错帧被拒", not parser.push(bytes(corrupted)) and parser.err_crc == 1)

    corrupted = bytearray(GOLDEN_PONG_MSPM0)
    corrupted[-1] = 0
    parser = FrameParser()
    check("EOF 错帧被拒", not parser.push(bytes(corrupted)) and parser.err_eof == 1)

    parser = FrameParser()
    parser.push(b"\xa5" + GOLDEN_PONG_MSPM0[:5])
    parser.reset()
    frames = parser.push(GOLDEN_PONG_MSPM0)
    check("线路空闲复位后恢复同步", len(frames) == 1)

    print("\n通过 {} · 失败 {}".format(passed, failed))
    return 0 if failed == 0 else 1


def run_loopback(
    port: str,
    baudrate: int,
    timeout: float,
    expected_chassis: Optional[str],
) -> int:
    """发送 PING 并核对 PONG 身份/版本。"""
    try:
        import serial
    except ImportError:
        print("SKIP: 缺 pyserial；树莓派上安装 python3-serial", file=sys.stderr)
        return 2

    try:
        link = serial.Serial(port, baudrate, timeout=0.1)
    except serial.SerialException as error:
        print("✗ 打不开串口: {}".format(error), file=sys.stderr)
        return 1

    print("=== 串口回路测试 ===")
    print("端口 {} · {} 8N1 · 超时 {}s".format(port, baudrate, timeout))
    parser = FrameParser()
    pong = None
    saw_ultrasonic = False
    with link:
        link.reset_input_buffer()
        link.write(encode_frame(CMD_PING))
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            chunk = link.read(256)
            if not chunk:
                parser.reset()
                continue
            for cmd, data in parser.push(chunk):
                if cmd == CMD_PONG:
                    pong = data
                elif cmd == CMD_ULTRASONIC:
                    decode_ultrasonic(data)
                    saw_ultrasonic = True
            if pong is not None and saw_ultrasonic:
                break

    if pong is None:
        print("✗ 没收到 PONG；检查 TX/RX、共地、波特率与固件", file=sys.stderr)
        return 1
    info = decode_pong(pong)
    print(
        "✓ 收到 PONG: 固件 {} · 板卡 {} · 底盘 {}".format(
            info.firmware, info.board, info.chassis
        )
    )
    if info.version < MIN_PROTOCOL_VERSION:
        print("✗ 固件版本不支持 MCU 超声波帧", file=sys.stderr)
        return 1
    if info.board_type not in BOARD_NAMES or info.chassis_type not in CHASSIS_NAMES:
        print("✗ PONG 含未知板卡/底盘编码", file=sys.stderr)
        return 1
    if expected_chassis:
        expected = expected_identity(expected_chassis)
        if (info.board_type, info.chassis_type) != expected:
            print("✗ CHASSIS 与固件身份不符", file=sys.stderr)
            return 1
    if not saw_ultrasonic:
        print("✗ 没收到 CMD_ULTRASONIC；检查四路 HC-SR04 MCU 采集", file=sys.stderr)
        return 1
    print("✓ MCU 四路超声波帧在线")
    return 0


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="树莓派 ↔ 底盘 MCU 串口回路测试")
    parser.add_argument(
        "device",
        nargs="?",
        help="串口设备；兼容 test-serial-loopback.sh /dev/ttyUSB0 的既有用法",
    )
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--port", dest="port_override")
    parser.add_argument("--baudrate", type=int, default=DEFAULT_BAUDRATE)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument(
        "--expect-chassis",
        default=os.environ.get("CHASSIS"),
        choices=("mecanum", "differential", "diff"),
    )
    args = parser.parse_args(argv)
    if args.device and args.port_override:
        parser.error("位置设备参数与 --port 只能使用一个")
    if args.self_test:
        return self_test()
    port = args.port_override or args.device or "/dev/mcu"
    return run_loopback(
        port, args.baudrate, args.timeout, args.expect_chassis
    )


if __name__ == "__main__":
    sys.exit(main())
