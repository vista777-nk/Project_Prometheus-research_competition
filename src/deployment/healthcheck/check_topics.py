#!/usr/bin/env python3
"""列出 ROS Master 上的话题拓扑，并标出本角色缺了哪些。

与 `check_nodes.py` 的分工：

* `check_nodes.py` 是**自动化入口**（systemd timer 调用），只给退出码 + 一行结论
* 本脚本是**人用的排查工具**，把 Master 上到底有什么全列出来

⚠ 能力边界（重要，别把它当成它不是的东西）
--------------------------------------------
本脚本检查的是"话题**有没有发布者注册**"，**不是**"话题上真的有消息在流"。
两者的区别在实践中很关键：一个节点卡死在回调里时，它的发布者仍然注册在
Master 上，这里会显示一切正常。

要真正测消息速率必须订阅话题，那需要 rospy 和 ROS 环境，只能在容器内跑::

    docker exec -it air_ground_edge bash -lc \\
        'source /opt/ros/noetic/setup.bash && rostopic hz /car/observation'

task-12 §12.1 的目录清单里把本文件叫作"Topic 心跳检查"。心跳（消息速率）
这层能力不在纯标准库能达到的范围内，因此这里只做发布者存在性检查，
并把真正的心跳检查方式写在上面。不含糊其辞地叫它"心跳"。

用法::

    ./check_topics.py                    # 列出全部 + 标注本角色缺失项
    ./check_topics.py --role drone
    ./check_topics.py --filter /car      # 只看某个前缀
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from agcheck import (  # noqa: E402
    EXIT_MASTER_DOWN,
    EXIT_OK,
    EXIT_TOPICS_MISSING,
    extract_published_topics,
    required_topics,
)
from check_nodes import (  # noqa: E402
    DEFAULT_MASTER,
    DEFAULT_TIMEOUT,
    probe_master,
)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="列出 ROS 话题拓扑并标注缺失项")
    parser.add_argument("--master", default=os.environ.get("ROS_MASTER_URI", DEFAULT_MASTER))
    parser.add_argument("--role", default=os.environ.get("AIR_GROUND_ROLE", "car"))
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    parser.add_argument("--filter", default="", help="只显示以此前缀开头的话题")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)

    reachable, error, state = probe_master(args.master, args.timeout)
    if not reachable:
        print(f"Master 不可达 ({args.master}): {error}", file=sys.stderr)
        return EXIT_MASTER_DOWN

    published = extract_published_topics(state)
    shown = sorted(t for t in published if t.startswith(args.filter))

    print(f"=== {args.master} 上有发布者的话题 ({len(published)} 个) ===")
    if args.filter:
        print(f"    过滤前缀: {args.filter} → {len(shown)} 个")
    for topic in shown:
        print(f"  {topic}")

    expected = required_topics(args.role)
    if not expected:
        print(f"\n未知角色 {args.role!r}, 跳过缺失项检查", file=sys.stderr)
        return EXIT_OK

    missing = [t for t in expected if t not in published]
    print(f"\n=== role={args.role} 必需话题 ===")
    for topic in expected:
        mark = "✓" if topic in published else "✗"
        print(f"  {mark} {topic}")

    if missing:
        print(f"\n缺失 {len(missing)} 个: {', '.join(missing)}", file=sys.stderr)
        return EXIT_TOPICS_MISSING

    print("\n全部就位。注意这只说明发布者已注册, 不代表消息真的在流 —— ")
    print("要验证速率: docker exec -it air_ground_edge bash -lc \\")
    print("  'source /opt/ros/noetic/setup.bash && rostopic hz " + expected[0] + "'")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
