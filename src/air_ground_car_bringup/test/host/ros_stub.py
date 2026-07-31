#!/usr/bin/env python3
"""ROS 替身：让传感器驱动骨架在没有 ROS、没有 roscore 的机器上可测。

为什么不用 `unittest.mock.MagicMock` 假装 rospy：
MagicMock 对**任何**属性名都返回一个新 mock，于是 `scan.ragnes = [...]`
这种拼错字段名的写法在测试里一路绿灯，到实机上才炸。

这里的替身反过来做：消息类用 `__slots__` 精确列出真实 .msg 的字段，
写错一个字母立刻 AttributeError。字段表是从 sensor_msgs/std_msgs 的
消息定义逐条抄下来的，抄错了由 `test_ros_stub_fidelity.py` 在 ROS 容器里
拿真消息类比对 —— 替身本身也要被验证 (Research_Diary 2026-07-31 §4)。
"""

import sys
import types
from typing import Any, Dict, List


class _Message:
    """带 __slots__ 的消息基类：只接受声明过的字段。"""

    __slots__ = ()
    _defaults: Dict[str, Any] = {}

    def __init__(self, **kwargs) -> None:
        for name in self.__slots__:
            setattr(self, name, self._make_default(name))
        for key, value in kwargs.items():
            setattr(self, key, value)

    @classmethod
    def _make_default(cls, name: str) -> Any:
        value = cls._defaults.get(name, 0.0)
        return value() if callable(value) else value

    def __repr__(self) -> str:
        fields = ", ".join(f"{n}={getattr(self, n)!r}" for n in self.__slots__)
        return f"{type(self).__name__}({fields})"


class Time(_Message):
    """rospy.Time 替身。"""

    __slots__ = ("secs", "nsecs")
    _defaults = {"secs": 0, "nsecs": 0}

    @staticmethod
    def now() -> "Time":
        return Time(secs=0, nsecs=0)

    def to_sec(self) -> float:
        return self.secs + self.nsecs * 1e-9


class Duration(_Message):
    """rospy.Duration 替身。"""

    __slots__ = ("secs", "nsecs")
    _defaults = {"secs": 0, "nsecs": 0}


class Header(_Message):
    """std_msgs/Header。"""

    __slots__ = ("seq", "stamp", "frame_id")
    _defaults = {"seq": 0, "stamp": Time, "frame_id": ""}


class Vector3(_Message):
    """geometry_msgs/Vector3。"""

    __slots__ = ("x", "y", "z")


class Quaternion(_Message):
    """geometry_msgs/Quaternion。"""

    __slots__ = ("x", "y", "z", "w")


class LaserScan(_Message):
    """sensor_msgs/LaserScan。"""

    __slots__ = (
        "header", "angle_min", "angle_max", "angle_increment",
        "time_increment", "scan_time", "range_min", "range_max",
        "ranges", "intensities",
    )
    _defaults = {"header": Header, "ranges": list, "intensities": list}


class Imu(_Message):
    """sensor_msgs/Imu。"""

    __slots__ = (
        "header", "orientation", "orientation_covariance",
        "angular_velocity", "angular_velocity_covariance",
        "linear_acceleration", "linear_acceleration_covariance",
    )
    _defaults = {
        "header": Header,
        "orientation": Quaternion,
        "angular_velocity": Vector3,
        "linear_acceleration": Vector3,
        "orientation_covariance": lambda: [0.0] * 9,
        "angular_velocity_covariance": lambda: [0.0] * 9,
        "linear_acceleration_covariance": lambda: [0.0] * 9,
    }


class String(_Message):
    """std_msgs/String。"""

    __slots__ = ("data",)
    _defaults = {"data": ""}


class FakePublisher:
    """记录所有发布内容的 rospy.Publisher 替身。"""

    def __init__(self, name: str, data_class, queue_size: int = 0, **_kwargs) -> None:
        self.name = name
        self.data_class = data_class
        self.queue_size = queue_size
        self.published: List[Any] = []

    def publish(self, message) -> None:
        if not isinstance(message, self.data_class):
            raise TypeError(
                f"publisher {self.name} declared {self.data_class.__name__} "
                f"but got {type(message).__name__}"
            )
        self.published.append(message)


class FakeRate:
    """rospy.Rate 替身，sleep 立即返回。"""

    def __init__(self, hz: float) -> None:
        self.hz = hz
        self.sleeps = 0

    def sleep(self) -> None:
        self.sleeps += 1


class _RospyStub(types.ModuleType):
    """rospy 替身模块。参数由测试直接写进 `params`。"""

    def __init__(self) -> None:
        super().__init__("rospy")
        self.params: Dict[str, Any] = {}
        self.logs: List[str] = []
        self.Publisher = FakePublisher
        self.Rate = FakeRate
        self.Time = Time
        self.Duration = Duration
        self._shutdown = True

    # --- 参数 ---
    def get_param(self, name, *default):
        """与真 rospy 一致：不给缺省值又查不到时抛 KeyError，不是返回 None。"""
        key = name[1:] if name.startswith("~") else name
        if key in self.params:
            return self.params[key]
        if default:
            return default[0]
        raise KeyError(name)

    def set_param(self, name, value) -> None:
        self.params[name[1:] if name.startswith("~") else name] = value

    def get_name(self) -> str:
        return "/stub_node"

    # --- 生命周期 ---
    def init_node(self, name, **_kwargs) -> None:
        self.logs.append(f"init_node:{name}")

    def is_shutdown(self) -> bool:
        return self._shutdown

    def spin(self) -> None:
        return None

    def sleep(self, _duration) -> None:
        return None

    # --- 日志 ---
    def loginfo(self, fmt, *args) -> None:
        self.logs.append(("info", str(fmt) % args if args else str(fmt)))

    def logwarn(self, fmt, *args) -> None:
        self.logs.append(("warn", str(fmt) % args if args else str(fmt)))

    def logerr(self, fmt, *args) -> None:
        self.logs.append(("error", str(fmt) % args if args else str(fmt)))

    def logwarn_throttle(self, _period, fmt, *args) -> None:
        self.logwarn(fmt, *args)

    def loginfo_throttle(self, _period, fmt, *args) -> None:
        self.loginfo(fmt, *args)


def install() -> "_RospyStub":
    """把替身模块装进 sys.modules，返回 rospy 替身以便测试改参数。

    已经装过就直接返回同一个实例（清空日志），保证同一次 pytest 会话里
    各测试文件拿到的是同一个参数空间。
    """
    existing = sys.modules.get("rospy")
    if isinstance(existing, _RospyStub):
        existing.logs.clear()
        return existing

    rospy_stub = _RospyStub()
    sys.modules["rospy"] = rospy_stub

    sensor_msgs = types.ModuleType("sensor_msgs")
    sensor_msgs_msg = types.ModuleType("sensor_msgs.msg")
    sensor_msgs_msg.LaserScan = LaserScan
    sensor_msgs_msg.Imu = Imu
    sensor_msgs.msg = sensor_msgs_msg
    sys.modules["sensor_msgs"] = sensor_msgs
    sys.modules["sensor_msgs.msg"] = sensor_msgs_msg

    std_msgs = types.ModuleType("std_msgs")
    std_msgs_msg = types.ModuleType("std_msgs.msg")
    std_msgs_msg.String = String
    std_msgs_msg.Header = Header
    std_msgs.msg = std_msgs_msg
    sys.modules["std_msgs"] = std_msgs
    sys.modules["std_msgs.msg"] = std_msgs_msg

    geometry_msgs = types.ModuleType("geometry_msgs")
    geometry_msgs_msg = types.ModuleType("geometry_msgs.msg")
    geometry_msgs_msg.Vector3 = Vector3
    geometry_msgs_msg.Quaternion = Quaternion
    geometry_msgs.msg = geometry_msgs_msg
    sys.modules["geometry_msgs"] = geometry_msgs
    sys.modules["geometry_msgs.msg"] = geometry_msgs_msg

    return rospy_stub
