#!/usr/bin/env python3
"""树莓派边缘主机预检（不依赖 ROS 或第三方 Python 包）。

换卡后先跑 ``--stage base``；Docker 与硬件接口配置完成后，再按角色跑
``--stage deploy``。脚本只检查已经确认的主机基线，不猜测尚未定稿的传感器
型号、VID/PID 或 UART 映射。
"""

from __future__ import annotations

import argparse
import json
import platform
import shutil
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

GIB = 1024 ** 3
MIN_MEMORY_KIB = 7 * 1024 ** 2
# 标称 64 GB 的卡格式化后容量会小于 64 GiB；54 GiB 足以区分 32 GB 卡，
# 同时为分区表、厂商容量换算和文件系统保留合理余量。
MIN_ROOT_BYTES = 54 * GIB


@dataclass(frozen=True)
class PiFacts:
    arch: str
    model: str
    os_id: str
    version_id: str
    codename: str
    memory_kib: int
    root_bytes: int
    docker_available: bool
    compose_available: bool
    devices: frozenset[str]


@dataclass(frozen=True)
class CheckResult:
    name: str
    ok: bool
    detail: str


def parse_key_values(text: str) -> dict[str, str]:
    """解析 os-release 这类简单 ``KEY=VALUE`` 文件。"""
    values: dict[str, str] = {}
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key] = value.strip().strip('"').strip("'")
    return values


def read_memory_kib(path: Path = Path("/proc/meminfo")) -> int:
    """读取 Linux ``MemTotal``，失败时返回 0 让预检明确失败。"""
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("MemTotal:"):
            return int(line.split()[1])
    return 0


def command_succeeds(command: list[str]) -> bool:
    try:
        return subprocess.run(
            command,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
            timeout=10,
        ).returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


def collect_facts() -> PiFacts:
    os_release = parse_key_values(
        Path("/etc/os-release").read_text(encoding="utf-8", errors="replace")
    )
    model_path = Path("/proc/device-tree/model")
    model = (
        model_path.read_text(encoding="utf-8", errors="replace").rstrip("\x00\n")
        if model_path.exists()
        else ""
    )
    docker_available = shutil.which("docker") is not None
    compose_available = docker_available and command_succeeds(
        ["docker", "compose", "version"]
    )
    known_devices = (
        "/dev/i2c-1",
        "/dev/serial0",
        "/dev/mcu",
        "/dev/telem",
        "/dev/pixhawk",
        "/dev/rplidar",
        "/dev/openmv",
    )
    return PiFacts(
        arch=platform.machine(),
        model=model,
        os_id=os_release.get("ID", ""),
        version_id=os_release.get("VERSION_ID", ""),
        codename=os_release.get("VERSION_CODENAME", ""),
        memory_kib=read_memory_kib(),
        root_bytes=shutil.disk_usage("/").total,
        docker_available=docker_available,
        compose_available=compose_available,
        devices=frozenset(path for path in known_devices if Path(path).exists()),
    )


def evaluate(facts: PiFacts, stage: str, role: str) -> list[CheckResult]:
    results = [
        CheckResult(
            "架构",
            facts.arch == "aarch64",
            f"检测到 {facts.arch or '未知'}，要求 aarch64",
        ),
        CheckResult(
            "主板",
            "Raspberry Pi 5" in facts.model,
            f"检测到 {facts.model or '未知'}，要求 Raspberry Pi 5",
        ),
        CheckResult(
            "宿主系统",
            facts.os_id == "debian"
            and facts.version_id == "13"
            and facts.codename == "trixie",
            f"检测到 {facts.os_id or '?'} {facts.version_id or '?'} "
            f"({facts.codename or '?'})，基线为 Debian 13 trixie",
        ),
        CheckResult(
            "内存",
            facts.memory_kib >= MIN_MEMORY_KIB,
            f"检测到 {facts.memory_kib / 1024 ** 2:.1f} GiB，"
            "基线至少 7 GiB 可用物理内存",
        ),
        CheckResult(
            "系统盘",
            facts.root_bytes >= MIN_ROOT_BYTES,
            f"根文件系统 {facts.root_bytes / GIB:.1f} GiB；"
            "项目标准为标称 64 GB 卡（验收下限 54 GiB）",
        ),
    ]

    if stage == "deploy":
        results.extend(
            [
                CheckResult(
                    "Docker Engine",
                    facts.docker_available,
                    "要求宿主机可执行 docker",
                ),
                CheckResult(
                    "Docker Compose",
                    facts.compose_available,
                    "要求 docker compose version 成功",
                ),
            ]
        )
        if role == "car":
            results.append(
                CheckResult(
                    "车机 I²C",
                    "/dev/i2c-1" in facts.devices,
                    "要求 /dev/i2c-1；启用 dtparam=i2c_arm=on 后重启",
                )
            )

    return results


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Raspberry Pi 5 边缘主机基线预检")
    parser.add_argument("--stage", choices=("base", "deploy"), default="base")
    parser.add_argument("--role", choices=("car", "drone"), default="car")
    parser.add_argument("--json", action="store_true", help="输出机器可读 JSON")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    facts = collect_facts()
    results = evaluate(facts, args.stage, args.role)
    ok = all(result.ok for result in results)

    if args.json:
        serializable_facts = {**asdict(facts), "devices": sorted(facts.devices)}
        print(
            json.dumps(
                {
                    "ok": ok,
                    "stage": args.stage,
                    "role": args.role,
                    "facts": serializable_facts,
                    "checks": [asdict(result) for result in results],
                },
                ensure_ascii=False,
                sort_keys=True,
            )
        )
    else:
        for result in results:
            mark = "✓" if result.ok else "✗"
            stream = sys.stdout if result.ok else sys.stderr
            print(f"{mark} {result.name}: {result.detail}", file=stream)
        print("预检通过" if ok else "预检失败：按上面的失败项处理后重跑")

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
