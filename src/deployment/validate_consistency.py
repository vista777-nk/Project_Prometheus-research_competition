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
4. 实机传感器配置的设备路径与 compose 容器路径、udev 稳定名一致

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


def check_calibration_topics() -> int:
    """record-calib-bag.sh 里写死的话题名必须在仓库的配置里真实存在。

    这条检查针对的是标定采集特有的失败方式：`rosbag record` 订阅一个
    没有发布者的话题**不报错**，它就那么等着，录出一个 0 条消息的 bag。
    人已经举着标定板站了两分钟、或者 IMU 已经静置了两小时，回到桌前才发现。

    task-15 原文的采集脚本里五个话题名全是仓库里不存在的
    （/drone/rgb/image_raw、/drone/imu/data_raw、/car/imu/data_raw …），
    所以这条不是假想的风险。同 validate.sh §7「交叉引用」的用意。
    """
    script = REPO_ROOT / "src/deployment/calibration/record-calib-bag.sh"
    if not script.exists():
        print(f"{BAD} 找不到 {script}")
        return 1

    declared = set(
        re.findall(r'^[A-Z_]+_TOPIC="(/[^"]+)"', script.read_text(encoding="utf-8"), re.M)
    )
    if not declared:
        print(f"{BAD} {script.name} 里没解析出任何 *_TOPIC 赋值 —— 变量命名被改过？")
        return 1

    sources = [
        REPO_ROOT / "src/air_ground_car_bringup/config/car_edge.yaml",
        REPO_ROOT / "src/air_ground_drone_bringup/config/drone_edge.yaml",
        REPO_ROOT / "src/air_ground_car_bringup/config/real_sensors.yaml",
    ]
    known: set[str] = set()
    for path in sources:
        if not path.exists():
            print(f"{BAD} 找不到 {path}")
            return 1
        known |= set(re.findall(r"(/[A-Za-z0-9_/]+)", path.read_text(encoding="utf-8")))

    unknown = sorted(declared - known)
    if unknown:
        print(f"{BAD} {script.name} 录的话题在任何配置里都找不到: {unknown}")
        print("      rosbag record 不会为此报错, 只会录出空 bag。")
        print(f"      配置来源: {', '.join(p.name for p in sources)}")
        return 1

    print(f"{OK} 标定采集话题均在配置中存在 ({len(declared)} 个)")
    return 0


def _container_devices(path: Path) -> set[str]:
    """取 compose ``devices`` 列表里的容器侧路径。"""
    document = yaml.safe_load(path.read_text(encoding="utf-8"))
    devices = document["services"]["edge-node"].get("devices", [])
    targets: set[str] = set()
    for item in devices:
        if isinstance(item, str):
            parts = item.split(":")
            targets.add(parts[1] if len(parts) >= 2 else parts[0])
        elif isinstance(item, dict) and item.get("target"):
            targets.add(str(item["target"]))
    return targets


def check_sensor_devices() -> int:
    """YAML、compose 与 udev 必须对同一个稳定设备名达成一致。"""
    if yaml is None:
        print(f"{SKIPPED} 无 pyyaml，跳过传感器设备路径交叉检查")
        return 0

    config_path = (
        REPO_ROOT / "src/air_ground_car_bringup/config/real_sensors.yaml"
    )
    car_compose = REPO_ROOT / "src/deployment/docker/docker-compose.car.yml"
    sensor_compose = (
        REPO_ROOT / "src/deployment/docker/docker-compose.car-sensors.yml"
    )
    udev_path = REPO_ROOT / "src/deployment/network/99-air-ground-devices.rules"
    paths = (config_path, car_compose, sensor_compose, udev_path)
    missing = [str(path) for path in paths if not path.exists()]
    if missing:
        print(f"{BAD} 传感器设备契约缺文件: {missing}")
        return 1

    config = yaml.safe_load(config_path.read_text(encoding="utf-8"))
    sensor_targets = _container_devices(sensor_compose)
    car_targets = _container_devices(car_compose)
    expected_uart = {
        str(config["rplidar"]["port"]),
        str(config["openmv"]["port"]),
    }
    expected_i2c = f'/dev/i2c-{int(config["icm42688"]["bus"])}'

    problems = []
    absent_uart = sorted(expected_uart - sensor_targets)
    if absent_uart:
        problems.append(f"传感器 compose 缺容器路径 {absent_uart}")
    if expected_i2c not in car_targets:
        problems.append(f"车机 compose 缺容器路径 {expected_i2c}")

    udev_text = udev_path.read_text(encoding="utf-8")
    for device in sorted(expected_uart):
        stable_name = Path(device).name
        if f'SYMLINK+="{stable_name}"' not in udev_text:
            problems.append(f"udev 未声明稳定名 {device}")

    if problems:
        print(f"{BAD} 实机传感器设备路径不一致")
        for problem in problems:
            print(f"      {problem}")
        return 1

    all_targets = sorted(expected_uart | {expected_i2c})
    print(f"{OK} 传感器 YAML / compose / udev 设备路径一致: {all_targets}")
    return 0


def main() -> int:
    print("  --- compose 结构 ---")
    compose_failed, degraded = check_compose_files()

    print("  --- 话题名一致性 ---")
    topics_failed = check_required_topics()
    topics_failed += check_calibration_topics()

    print("  --- 实机设备路径一致性 ---")
    devices_failed = check_sensor_devices()

    total = compose_failed + topics_failed + devices_failed
    if total:
        return 1
    return 2 if degraded else 0


if __name__ == "__main__":
    sys.exit(main())
