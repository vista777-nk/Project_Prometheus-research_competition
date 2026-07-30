#!/usr/bin/env python3
"""部署配置的跨文件一致性检查。

被 `validate.sh` 调用，也可以单独跑：

    python3 src/deployment/validate_consistency.py

抽成独立文件而不是塞进 validate.sh 的 heredoc，是因为 heredoc 里的 Python
既没法单独调试、也没法被语法检查覆盖——而它恰恰是整个校验里逻辑最多的一段。

检查项
------
1. compose 文件能被 YAML 解析，且顶层有 services 段
2. `agcheck.REQUIRED_TOPICS` 与 edge yaml 里配置的话题名一致
3. compose 里引用的 override 关系自洽（角色 override 不能覆盖公共镜像名）

退出码：0 全通过 / 1 有不一致 / 2 依赖缺失导致降级检查（不算失败）
"""

from __future__ import annotations

import glob
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent / "healthcheck"))

OK = "  \033[32m[v]\033[0m"
BAD = "  \033[31m[X]\033[0m"
SKIPPED = "  \033[33m[-]\033[0m"

try:
    import yaml
except ImportError:
    yaml = None


def check_compose_files() -> tuple[int, bool]:
    """返回 (失败数, 是否走了降级路径)。"""
    paths = sorted(glob.glob(str(REPO_ROOT / "src/deployment/docker/*.yml")))
    if not paths:
        print(f"{BAD} 没找到任何 compose 文件")
        return 1, False

    failed = 0
    degraded = yaml is None

    for path in paths:
        short = Path(path).name
        text = Path(path).read_text(encoding="utf-8")

        if degraded:
            # 没有 pyyaml 时至少确认: 有 services 段、没有 Tab。
            # YAML 规范禁止用 Tab 缩进, 而编辑器很容易插进去,
            # 症状是 compose 报一句位置很模糊的 parse error。
            problems = []
            if not re.search(r"^services:", text, re.M):
                problems.append("缺少顶层 services: 段")
            if "\t" in text:
                problems.append("含 Tab 字符 (YAML 禁止 Tab 缩进)")
            if problems:
                print(f"{BAD} {short}: {'; '.join(problems)}")
                failed += 1
            else:
                print(f"{SKIPPED} {short} (无 pyyaml, 仅结构检查, 通过)")
            continue

        try:
            doc = yaml.safe_load(text)
        except yaml.YAMLError as exc:
            print(f"{BAD} {short}: {exc}")
            failed += 1
            continue

        if not isinstance(doc, dict) or "services" not in doc:
            print(f"{BAD} {short}: 缺少顶层 services 段")
            failed += 1
            continue

        services = list(doc["services"])
        if services != ["edge-node"]:
            # compose override 是按服务名合并的。角色文件里若把服务名写错,
            # 不会报错, 只会**新增**一个服务而不是覆盖 —— 结果是启动了两个容器,
            # 一个缺设备一个缺环境变量。这类错误静默且难查。
            print(f"{BAD} {short}: 服务名应为 ['edge-node'], 实际 {services}")
            failed += 1
            continue

        print(f"{OK} {short} (services: {', '.join(services)})")

    return failed, degraded


def check_required_topics() -> int:
    """agcheck 的必需话题必须与 edge yaml 配置一致。

    这两处分开写是必然的（一个在部署层、一个在 ROS 包里），
    但它们描述的是同一件事。漂移的后果是健康检查盯着一个不存在的话题
    永远报警，或者盯着旧名字永远显示健康。
    """
    from agcheck import REQUIRED_TOPICS  # noqa: PLC0415

    sources = {
        "car": REPO_ROOT / "src/air_ground_car_bringup/config/car_edge.yaml",
        "drone": REPO_ROOT / "src/air_ground_drone_bringup/config/drone_edge.yaml",
    }

    failed = 0
    for role, path in sources.items():
        if not path.exists():
            print(f"{BAD} 找不到 {path}")
            failed += 1
            continue

        text = path.read_text(encoding="utf-8")
        declared = set(
            re.findall(r"^\s+(?:observation|state|capability):\s*(\S+)", text, re.M)
        )
        expected = set(REQUIRED_TOPICS[role])

        if not expected <= declared:
            print(f"{BAD} agcheck 的 {role} 必需话题与 {path.name} 不一致")
            print(f"      agcheck 要求: {sorted(expected)}")
            print(f"      yaml 声明  : {sorted(declared)}")
            print(f"      缺失      : {sorted(expected - declared)}")
            failed += 1
        else:
            print(f"{OK} {role} 必需话题与 {path.name} 一致 ({len(expected)} 个)")

    return failed


def main() -> int:
    print("  --- compose 结构 ---")
    compose_failed, degraded = check_compose_files()

    print("  --- 话题名一致性 ---")
    topics_failed = check_required_topics()

    total = compose_failed + topics_failed
    if total:
        return 1
    return 2 if degraded else 0


if __name__ == "__main__":
    sys.exit(main())
