#!/usr/bin/env python3
"""3DR SiK 数传电台参数配置 / 读取。

与 task-12 §12.4 的差异
----------------------
原文给的是 ``setup-3dr-radio.sh``，里面的 Python 片段只建立了一个 mavutil
连接就打印 "3DR Radio configured."，**实际上一个参数也没设**。一个宣称
配置成功却什么都没做的脚本，比没有脚本更糟——它会让人以为这一步已经完成。

这里换成真实的 SiK AT 命令实现，并且：

* **默认只读**。不加 ``--apply`` 就只把当前配置打印出来，随便运行不会改坏电台。
* 用 pyserial 而不是 bash：``+++`` 进命令模式要求前后各有 1 秒静默期，
  shell 里做不到可靠的收发时序。

SiK 用户可设寄存器（``ATI5`` 可查看全部）::

    S0  FORMAT        固件格式，只读
    S1  SERIAL_SPEED  串口波特率（两端**可以**不同）
    S2  AIR_SPEED     空中速率      ← 两端必须一致
    S3  NETID         网络编号      ← 两端必须一致
    S4  TXPOWER       发射功率 dBm
    S5  ECC           纠错开关      ← 两端必须一致
    S6  MAVLINK       MAVLink 分帧
    S8  MIN_FREQ      频段下限      ← 两端必须一致
    S9  MAX_FREQ      频段上限      ← 两端必须一致
    S10 NUM_CHANNELS  跳频信道数    ← 两端必须一致
    S11 DUTY_CYCLE    占空比上限

⚠ **改参数的顺序很重要。** 标注"两端必须一致"的项一旦只改了一端，链路立刻
断开，另一端就再也够不着了（只能拆下来用 USB 直连改回去）。因此：

* 要么把两个电台都拔下来，各自 USB 直连改（本脚本的默认用法，最省心）；
* 要么先用 ``--remote`` 改远端、确认生效后再改本地（``RT`` 系列命令）。

用法::

    # 只看当前配置，不改任何东西
    ./setup-3dr-radio.py --port /dev/ttyUSB0

    # 按 ground 端角色写入参数（会二次确认）
    ./setup-3dr-radio.py --port /dev/ttyUSB0 --role ground --apply

    # 改远端电台（先改远端再改本地）
    ./setup-3dr-radio.py --port /dev/ttyUSB0 --role air --remote --apply

⚠ 尚未在真实电台上验证（Phase 1 无硬件）。AT 命令集依据 SiK 固件公开文档，
  首次上机请先用默认的只读模式确认能进命令模式，再考虑 ``--apply``。
"""

from __future__ import annotations

import argparse
import sys
import time

# 两端必须一致的寄存器。写错一端就断链，因此单独列出来做提示。
LINK_CRITICAL = {2, 3, 5, 8, 9, 10}

REGISTER_NAMES = {
    0: "FORMAT",
    1: "SERIAL_SPEED",
    2: "AIR_SPEED",
    3: "NETID",
    4: "TXPOWER",
    5: "ECC",
    6: "MAVLINK",
    7: "OPPRESEND",
    8: "MIN_FREQ",
    9: "MAX_FREQ",
    10: "NUM_CHANNELS",
    11: "DUTY_CYCLE",
    12: "LBT_RSSI",
    13: "MANCHESTER",
    14: "RTSCTS",
    15: "MAX_WINDOW",
}

# 空地链路的目标配置。air 与 ground 唯一的差别是串口速率——
# 无人机侧接 Pi 的硬件串口，车机侧同样，因此这里其实相同；
# 保留按角色分开的结构是为了将来需要非对称配置时有地方改。
PROFILES = {
    "ground": {
        1: 57,     # SERIAL_SPEED 57600 (SiK 用 "速率/1000" 表示)
        2: 64,     # AIR_SPEED 64kbps
        3: 42,     # NETID —— 同一场地有多套设备时必须各不相同
        4: 20,     # TXPOWER 20dBm
        5: 1,      # ECC 开
        6: 1,      # MAVLINK 分帧开（让电台按 MAVLink 边界打包，降低延迟抖动）
        11: 100,   # DUTY_CYCLE
    },
    "air": {
        1: 57,
        2: 64,
        3: 42,
        4: 20,
        5: 1,
        6: 1,
        11: 100,
    },
}


class RadioError(RuntimeError):
    """与电台交互失败。"""


class SikRadio:
    """SiK 电台的 AT 命令会话。"""

    def __init__(self, port: str, baud: int = 57600, timeout: float = 2.0):
        try:
            import serial  # noqa: PLC0415  — 只在真正要开串口时才需要 pyserial
        except ImportError as exc:  # pragma: no cover - 依赖缺失路径
            raise RadioError(
                "缺少 pyserial。安装: pip3 install pyserial"
            ) from exc

        self._serial = serial.Serial(port, baud, timeout=timeout)
        self._prefix = "AT"

    def close(self) -> None:
        self._serial.close()

    def use_remote(self, remote: bool) -> None:
        """切换后续命令作用于远端(RT)还是本端(AT)。"""
        self._prefix = "RT" if remote else "AT"

    # --- 底层收发 ---------------------------------------------------------

    def _read_all(self, settle: float = 0.5) -> str:
        time.sleep(settle)
        data = self._serial.read(self._serial.in_waiting or 1)
        return data.decode("ascii", errors="replace")

    def enter_command_mode(self) -> str:
        """发送 +++ 进入 AT 命令模式。

        SiK 要求 ``+++`` 前后各有至少 1 秒的静默期，期间不能有任何数据经过，
        否则电台会把 ``+++`` 当成普通数据透传出去。这就是本脚本用 pyserial
        而不是 shell 的原因。
        """
        self._serial.reset_input_buffer()
        time.sleep(1.2)
        self._serial.write(b"+++")
        self._serial.flush()
        time.sleep(1.2)
        response = self._read_all()
        if "OK" not in response:
            raise RadioError(
                "进不了 AT 命令模式（没等到 OK）。逐项排查:\n"
                "  1) 串口设备与波特率对不对（默认 57600）\n"
                "  2) 电台是不是正在透传数据——先把飞控/上位机断开\n"
                "  3) 电台已经在命令模式里了（再发 +++ 不会回 OK，试试直接 ATI）\n"
                f"  实际收到: {response!r}"
            )
        return response

    def command(self, cmd: str) -> str:
        """发送一条 AT/RT 命令并返回响应文本。"""
        full = f"{self._prefix}{cmd}\r\n"
        self._serial.reset_input_buffer()
        self._serial.write(full.encode("ascii"))
        self._serial.flush()
        return self._read_all()

    # --- 高层操作 ---------------------------------------------------------

    def version(self) -> str:
        return self.command("I").strip()

    def dump_parameters(self) -> str:
        return self.command("I5").strip()

    def set_register(self, index: int, value: int) -> None:
        response = self.command(f"S{index}={value}")
        if "OK" not in response:
            name = REGISTER_NAMES.get(index, f"S{index}")
            raise RadioError(f"设置 {name} (S{index}={value}) 失败: {response!r}")

    def save_and_reboot(self) -> None:
        if "OK" not in self.command("&W"):
            raise RadioError("写入 EEPROM (AT&W) 失败——参数不会保留到下次上电")
        self.command("Z")   # 重启后不会再回 OK，不检查


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="读取或配置 3DR SiK 数传电台（默认只读）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "--port", default="/dev/ttyUSB0",
        help="电台串口。PC 端用 USB 转串口时通常是 /dev/ttyUSB0; "
             "树莓派上电台接硬件串口时是 /dev/telem（见 udev 规则）",
    )
    parser.add_argument("--baud", type=int, default=57600, help="当前串口速率，默认 57600")
    parser.add_argument(
        "--role", choices=sorted(PROFILES), default=None,
        help="air=无人机端 / ground=车机端。仅 --apply 时需要",
    )
    parser.add_argument(
        "--remote", action="store_true",
        help="作用于**远端**电台（RT 命令）。先改远端再改本地，"
             "否则改完本地就够不着远端了",
    )
    parser.add_argument(
        "--apply", action="store_true",
        help="真正写入参数。不加此参数时只读取并打印当前配置",
    )
    parser.add_argument(
        "--yes", action="store_true",
        help="跳过二次确认（脚本化场景用）",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)

    if args.apply and args.role is None:
        print("--apply 必须同时指定 --role air 或 --role ground", file=sys.stderr)
        return 2

    try:
        radio = SikRadio(args.port, args.baud)
    except RadioError as exc:
        print(f"打开电台失败: {exc}", file=sys.stderr)
        return 1

    try:
        radio.enter_command_mode()
        radio.use_remote(args.remote)
        target = "远端" if args.remote else "本端"

        print(f"=== {target}电台 ({args.port}) ===")
        print(radio.version())
        print()
        print(radio.dump_parameters())

        if not args.apply:
            print()
            print("只读模式，未修改任何参数。要写入请加 --apply --role air|ground")
            return 0

        profile = PROFILES[args.role]
        print()
        print(f"将把{target}电台配置为 role={args.role}:")
        for index in sorted(profile):
            name = REGISTER_NAMES.get(index, f"S{index}")
            flag = "  ← 两端必须一致" if index in LINK_CRITICAL else ""
            print(f"  S{index:<2} {name:<13} = {profile[index]}{flag}")
        print()
        print("⚠ 标了「两端必须一致」的项只改一端会立刻断链，另一端将无法远程访问。")

        if not args.yes:
            if input("确认写入？输入 yes 继续: ").strip().lower() != "yes":
                print("已取消，未修改任何参数。")
                return 0

        for index in sorted(profile):
            radio.set_register(index, profile[index])
            print(f"  S{index} = {profile[index]}  OK")

        radio.save_and_reboot()
        print()
        print("已写入 EEPROM 并重启电台。")
        print("下一步：对另一端重复本命令（--role 换成对侧），两端 NETID/空速必须相同。")
        return 0

    except RadioError as exc:
        print(f"配置失败: {exc}", file=sys.stderr)
        return 1
    finally:
        radio.close()


if __name__ == "__main__":
    sys.exit(main())
