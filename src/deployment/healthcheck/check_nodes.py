#!/usr/bin/env python3
"""边缘节点健康检查 —— systemd timer 的入口。

只依赖 Python 3 标准库：不 import rospy，因此可以在**容器外**跑，
也可以在开发机上对着实验室的 Master 跑。

退出码（`air-ground-healthcheck.service` 与 `alert.sh` 都依赖这组值）::

    0  健康
    1  ROS Master 不可达
    2  Master 正常但必需话题缺发布者
    3  用法/配置错误

用法::

    ./check_nodes.py                          # 从环境变量取配置
    ./check_nodes.py --master http://1.2.3.4:11311 --role drone
    ./check_nodes.py --quiet                  # 只给退出码, 不打印

环境变量（与 /opt/air-ground/.env 一致）::

    ROS_MASTER_URI    默认 http://192.168.1.100:11311
    AIR_GROUND_ROLE   默认 car
"""

from __future__ import annotations

import argparse
import os
import socket
import sys
import xmlrpc.client
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from agcheck import (  # noqa: E402  — 必须先补 sys.path
    EXIT_USAGE,
    evaluate,
    extract_published_topics,
    format_report,
    parse_master_uri,
)

#: 调用方标识。ROS Master 的 API 要求带一个 caller_id，
#: 用一个可辨识的名字，方便在 Master 日志里认出健康检查的流量。
CALLER_ID = "/air_ground_healthcheck"

DEFAULT_MASTER = "http://192.168.1.100:11311"
DEFAULT_TIMEOUT = 5.0


def probe_master(uri: str, timeout: float) -> tuple[bool, str, list]:
    """探测 Master 并取回系统状态。

    :return: (可达, 错误说明, getSystemState 结果)

    超时必须显式设置：``ServerProxy`` 默认用 socket 全局超时，
    而全局超时默认是 None（永远等）。健康检查卡死会让 systemd timer
    堆积起来，比检查失败更麻烦。
    """
    try:
        host, port = parse_master_uri(uri)
    except ValueError as exc:
        return False, str(exc), []

    original_timeout = socket.getdefaulttimeout()
    socket.setdefaulttimeout(timeout)
    try:
        proxy = xmlrpc.client.ServerProxy(f"http://{host}:{port}")

        # getPid 只验证 Master 进程在应答
        code, status, _ = proxy.getPid(CALLER_ID)
        if code != 1:
            return False, f"Master 拒绝了 getPid: code={code} {status}", []

        # getSystemState 拿全量话题拓扑
        code, status, state = proxy.getSystemState(CALLER_ID)
        if code != 1:
            return True, f"getSystemState 返回 code={code} {status}", []

        return True, "", state

    except socket.timeout:
        return False, f"连接超时 (>{timeout}s): {uri}", []
    except (OSError, xmlrpc.client.Error) as exc:
        return False, f"{type(exc).__name__}: {exc}", []
    finally:
        socket.setdefaulttimeout(original_timeout)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="空地边缘节点健康检查（不依赖 ROS 环境）",
    )
    parser.add_argument(
        "--master",
        default=os.environ.get("ROS_MASTER_URI", DEFAULT_MASTER),
        help="ROS Master URI，默认取环境变量 ROS_MASTER_URI",
    )
    parser.add_argument(
        "--role",
        default=os.environ.get("AIR_GROUND_ROLE", "car"),
        help="car | drone，默认取环境变量 AIR_GROUND_ROLE",
    )
    parser.add_argument(
        "--timeout", type=float, default=DEFAULT_TIMEOUT,
        help=f"单次 XML-RPC 超时秒数，默认 {DEFAULT_TIMEOUT}",
    )
    parser.add_argument(
        "--quiet", action="store_true",
        help="不打印，只用退出码表达结果",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)

    reachable, error, state = probe_master(args.master, args.timeout)
    published = extract_published_topics(state) if reachable else set()

    report = evaluate(
        master_reachable=reachable,
        master_error=error,
        role=args.role,
        published=published,
    )

    if not args.quiet:
        text = format_report(report, datetime.now().isoformat(timespec="seconds"))
        stream = sys.stdout if report.healthy else sys.stderr
        print(text, file=stream)

    return report.exit_code


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(EXIT_USAGE)
