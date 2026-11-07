#!/usr/bin/env python3
"""测试公共夹具：参数直接来自仓库里那份 real_sensors.yaml。

刻意**不**在测试里另抄一份参数字典。抄一份的话，测试验的是那份抄件，
而实机上加载的是 YAML —— 两者漂移时测试照样全绿。
现在少一个键、写错一个话题名，单元测试会当场红。
"""

import copy
from pathlib import Path
from typing import Any, Dict, Optional

import yaml

import ros_stub

PACKAGE_ROOT = Path(__file__).resolve().parents[2]
REAL_SENSORS_YAML = PACKAGE_ROOT / "config" / "real_sensors.yaml"
CAR_EDGE_YAML = PACKAGE_ROOT / "config" / "car_edge.yaml"


def load_yaml(path: Path) -> Dict[str, Any]:
    """读取一份 YAML 配置。"""
    with path.open("r", encoding="utf-8") as handle:
        return yaml.safe_load(handle)


def configure(overrides: Optional[Dict[str, Any]] = None):
    """装好 ROS 替身并把 real_sensors.yaml 灌进参数空间。

    Args:
        overrides: 覆盖项，按 "section/key" 指定，如 {"rplidar/samples": 8}。

    Returns:
        rospy 替身，供测试断言日志或改参数。
    """
    stub = ros_stub.install()
    stub.params.clear()
    stub.logs.clear()
    stub.params.update(copy.deepcopy(load_yaml(REAL_SENSORS_YAML)))
    for path, value in (overrides or {}).items():
        section, _, key = path.partition("/")
        if key:
            stub.params[section][key] = value
        else:
            stub.params[section] = value
    return stub
