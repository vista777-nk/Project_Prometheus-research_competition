#!/usr/bin/env python3
"""传感器驱动的公共配置读取与校验。

四个驱动骨架共用一份 `config/real_sensors.yaml`，每个节点只读自己那一段。
参数不硬编码在类常量里 —— 换一块 RPLIDAR S2 或把 IMU 量程从 ±2000dps
改成 ±500dps，应该只改 YAML，不改代码 (CONVENTIONS §4.3)。

校验语义与 `car_preprocessor.py` 保持一致：配置错了在节点启动时就抛，
不要带着一个 0 Hz 的发布频率跑起来再慢慢排查。
"""

import math
from typing import Any, Dict, Iterable

import rospy

BACKENDS = ("mock", "real")


def positive_float(value: Any, label: str) -> float:
    """校验一个有限正浮点配置值。"""
    number = float(value)
    if not math.isfinite(number) or number <= 0.0:
        raise ValueError(f"{label} must be a finite positive number")
    return number


def finite_float(value: Any, label: str) -> float:
    """校验一个有限浮点配置值（允许 0 与负数，如角度下界）。"""
    number = float(value)
    if not math.isfinite(number):
        raise ValueError(f"{label} must be a finite number")
    return number


def require_keys(config: Dict[str, Any], keys: Iterable[str], label: str) -> None:
    """确认配置段包含全部必需键，缺哪个就报哪个。"""
    missing = sorted(set(keys) - set(config))
    if missing:
        raise ValueError(f"{label} is missing keys: {', '.join(missing)}")


def load_section(section: str) -> Dict[str, Any]:
    """从私有参数空间读取一段传感器配置。

    Args:
        section: 段名，如 "rplidar"。对应 `~rplidar`。

    Returns:
        该段的配置字典。

    Raises:
        ValueError: 参数不存在或不是字典（多半是漏 load real_sensors.yaml）。
    """
    value = rospy.get_param("~" + section, None)
    if not isinstance(value, dict):
        raise ValueError(
            f"parameter ~{section} is missing or not a dict; "
            "did the launch file load config/real_sensors.yaml?"
        )
    return dict(value)


def resolve_backend() -> str:
    """读取并校验 `~backend`，缺省 real（失败关闭）。

    mock 模式下会打一条**醒目**的告警：话题上出现的是假数据，
    任何人接手一份 rosbag 时必须能从日志里看出来。
    """
    backend = str(rospy.get_param("~backend", "real")).lower()
    if backend not in BACKENDS:
        raise ValueError(f"~backend must be one of {BACKENDS}, got '{backend}'")
    if backend == "mock":
        rospy.logwarn(
            "[%s] backend=mock —— 本节点发布的是假数据，不是传感器读数",
            rospy.get_name(),
        )
    return backend
