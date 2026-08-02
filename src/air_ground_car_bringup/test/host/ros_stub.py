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


class _TimeBase(_Message):
    """Time / Duration 的公共部分。

    真 rospy 的 `Time(secs, nsecs)` 与 `Duration(secs, nsecs)` 都接受
    **位置参数**, 而且 secs 可以是浮点 (`rospy.Duration(1.0 / 10.0)` 是
    最常见的写法, car_preprocessor.py 就是这么写的)。`_Message` 只收关键字,
    所以这里必须自己实现构造 —— 这个差异是 task-15 让真节点跑在替身上时
    才暴露出来的, 在那之前没有任何测试构造过 Duration。
    """

    __slots__ = ()

    def __init__(self, secs=0, nsecs=0, **kwargs) -> None:
        secs = kwargs.pop("secs", secs)
        nsecs = kwargs.pop("nsecs", nsecs)
        if kwargs:
            raise TypeError(f"未知字段: {sorted(kwargs)}")
        # 真 rospy 会把浮点秒规整成整数 secs + nsecs
        whole = int(secs)
        nsecs = int(nsecs) + int(round((float(secs) - whole) * 1e9))
        whole += nsecs // 1_000_000_000
        nsecs %= 1_000_000_000
        self.secs = whole
        self.nsecs = nsecs

    def to_sec(self) -> float:
        return self.secs + self.nsecs * 1e-9

    def __repr__(self) -> str:
        return f"{type(self).__name__}({self.secs}, {self.nsecs})"


class Time(_TimeBase):
    """rospy.Time 替身。

    比较与相减是真 rospy.Time 的核心用法 (world_model.py 靠它判新鲜度),
    替身不实现的话, `stamp > last_update` 会走 object 的默认比较并当场
    TypeError —— 好在这一种是响的。真正危险的是只实现 `__gt__` 不实现
    `__eq__`: 那样 `a >= b` 会时对时错。所以这里一次给全。
    """

    __slots__ = ("secs", "nsecs")

    @staticmethod
    def now() -> "Time":
        return Time(0, 0)

    @staticmethod
    def from_sec(seconds: float) -> "Time":
        return Time(seconds)

    def __eq__(self, other) -> bool:
        return isinstance(other, _TimeBase) and self.to_sec() == other.to_sec()

    def __lt__(self, other) -> bool:
        return self.to_sec() < other.to_sec()

    def __le__(self, other) -> bool:
        return self.to_sec() <= other.to_sec()

    def __gt__(self, other) -> bool:
        return self.to_sec() > other.to_sec()

    def __ge__(self, other) -> bool:
        return self.to_sec() >= other.to_sec()

    def __hash__(self) -> int:
        return hash(self.to_sec())

    def __sub__(self, other) -> "Duration":
        return Duration(self.to_sec() - other.to_sec())

    def __add__(self, other) -> "Time":
        return Time(self.to_sec() + other.to_sec())


class Duration(_TimeBase):
    """rospy.Duration 替身。"""

    __slots__ = ("secs", "nsecs")

    @staticmethod
    def from_sec(seconds: float) -> "Duration":
        return Duration(seconds)


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


# --- 以下几个类是 task-15 为「传感器→Observation 数据流验证」加的 --------
#
# 目的是让**真的** car_preprocessor.py 在没有 ROS 的机器上跑起来，
# 而不是另写一份"模拟预处理器核心逻辑"的函数来测。后者测的是那份模拟件：
# 预处理器里的新鲜度窗口、LiDAR 降采样、-1.0 无效标记、modalities 的
# 生成规则一旦改了，模拟件照样全绿。
#
# 字段表同样逐条抄自真实 .msg，由 test_ros_stub_fidelity.py 在 ROS 容器里比对。


class Image(_Message):
    """sensor_msgs/Image。"""

    __slots__ = ("header", "height", "width", "encoding", "is_bigendian", "step", "data")
    _defaults = {"header": Header, "height": 0, "width": 0, "encoding": "",
                 "is_bigendian": 0, "step": 0, "data": b""}


class CompressedImage(_Message):
    """sensor_msgs/CompressedImage。"""

    __slots__ = ("header", "format", "data")
    _defaults = {"header": Header, "format": "", "data": b""}


class Point(_Message):
    """geometry_msgs/Point。"""

    __slots__ = ("x", "y", "z")


class Pose(_Message):
    """geometry_msgs/Pose。"""

    __slots__ = ("position", "orientation")
    _defaults = {"position": Point, "orientation": Quaternion}


class Twist(_Message):
    """geometry_msgs/Twist。"""

    __slots__ = ("linear", "angular")
    _defaults = {"linear": Vector3, "angular": Vector3}


class PoseWithCovariance(_Message):
    """geometry_msgs/PoseWithCovariance。"""

    __slots__ = ("pose", "covariance")
    _defaults = {"pose": Pose, "covariance": lambda: [0.0] * 36}


class TwistWithCovariance(_Message):
    """geometry_msgs/TwistWithCovariance。"""

    __slots__ = ("twist", "covariance")
    _defaults = {"twist": Twist, "covariance": lambda: [0.0] * 36}


class Odometry(_Message):
    """nav_msgs/Odometry。"""

    __slots__ = ("header", "child_frame_id", "pose", "twist")
    _defaults = {"header": Header, "child_frame_id": "",
                 "pose": PoseWithCovariance, "twist": TwistWithCovariance}


class Observation(_Message):
    """air_ground_interfaces/Observation (ICD §2.1)。"""

    __slots__ = (
        "header", "robot_id", "modalities", "rgb", "depth",
        "lidar_ranges", "lidar_angle_min", "lidar_angle_increment",
        "ultrasonic_ranges", "angular_velocity", "linear_acceleration",
    )
    _defaults = {
        "header": Header, "robot_id": "", "modalities": list,
        "rgb": CompressedImage, "depth": Image,
        "lidar_ranges": list, "ultrasonic_ranges": list,
        "angular_velocity": Vector3, "linear_acceleration": Vector3,
    }


class RobotState(_Message):
    """air_ground_interfaces/RobotState (ICD §2.2)。"""

    __slots__ = ("header", "robot_id", "pose", "velocity", "mode",
                 "chassis_type", "battery_voltage", "is_armed", "is_connected")
    _defaults = {"header": Header, "robot_id": "", "pose": Pose, "velocity": Twist,
                 "mode": "", "chassis_type": "", "battery_voltage": 0.0,
                 "is_armed": False, "is_connected": False}


class Capability(_Message):
    """air_ground_interfaces/Capability (ICD §3.3)。"""

    __slots__ = ("header", "robot_id", "locomotion_type", "max_speed",
                 "max_endurance", "sensor_modalities", "sensor_range",
                 "compute_tier", "max_payload_kg", "has_gripper")
    _defaults = {"header": Header, "robot_id": "", "locomotion_type": "",
                 "max_speed": 0.0, "max_endurance": 0.0,
                 "sensor_modalities": list, "sensor_range": 0.0,
                 "compute_tier": "", "max_payload_kg": 0.0, "has_gripper": False}


class SemanticLandmark(_Message):
    """air_ground_interfaces/SemanticLandmark (ICD §2.4)。"""

    __slots__ = ("landmark_id", "semantic_label", "pose", "confidence", "last_observed")
    _defaults = {"landmark_id": "", "semantic_label": "", "pose": Pose,
                 "confidence": 0.0, "last_observed": Time}


class MapMetaData(_Message):
    """nav_msgs/MapMetaData。"""

    __slots__ = ("map_load_time", "resolution", "width", "height", "origin")
    _defaults = {"map_load_time": Time, "resolution": 0.0,
                 "width": 0, "height": 0, "origin": Pose}


class OccupancyGrid(_Message):
    """nav_msgs/OccupancyGrid。"""

    __slots__ = ("header", "info", "data")
    _defaults = {"header": Header, "info": MapMetaData, "data": list}


class WorldState(_Message):
    """air_ground_interfaces/WorldState (ICD §2.3)。"""

    __slots__ = ("header", "agents", "map_2d", "dynamic_obstacles", "landmarks",
                 "last_update_perception", "last_update_planning")
    _defaults = {"header": Header, "agents": list, "map_2d": OccupancyGrid,
                 "dynamic_obstacles": list, "landmarks": list,
                 "last_update_perception": Time, "last_update_planning": Time}


class QueryWorldStateResponse(_Message):
    """air_ground_interfaces/QueryWorldStateResponse。"""

    __slots__ = ("result", "found")
    _defaults = {"result": WorldState, "found": False}


class QueryWorldState:
    """服务类型占位。world_model.py 在模块层 import 它, 但 Store 用不到。"""

    _response_class = QueryWorldStateResponse


class CvBridgeError(Exception):
    """cv_bridge.CvBridgeError 替身。"""


class CvBridge:
    """cv_bridge.CvBridge 替身 —— 只实现 car_preprocessor 用到的那一个方法。

    刻意只支持 bgr8: 预处理器只用这一种编码。多支持一种就多一份没被
    任何测试覆盖的转换代码，而它错了的时候颜色通道会悄悄反过来。
    """

    def imgmsg_to_cv2(self, message, desired_encoding: str = "passthrough"):
        """把 Image 替身转成 numpy 数组。

        Raises:
            CvBridgeError: 编码不是 bgr8，或 data 长度与 height×step 不符。
        """
        import numpy

        if desired_encoding not in ("bgr8", "passthrough"):
            raise CvBridgeError(f"替身只支持 bgr8，收到 {desired_encoding}")
        if message.encoding != "bgr8":
            raise CvBridgeError(f"替身只支持 bgr8 图像，收到 {message.encoding!r}")
        expected = message.height * message.step
        if len(message.data) != expected:
            raise CvBridgeError(
                f"data 长度 {len(message.data)} 与 height×step={expected} 不符"
            )
        array = numpy.frombuffer(bytes(message.data), dtype=numpy.uint8)
        return array.reshape(message.height, message.width, 3)


class FakeSubscriber:
    """rospy.Subscriber 替身。记录话题名与回调，供测试直接投喂消息。"""

    def __init__(self, name: str, data_class, callback=None,
                 queue_size: int = 0, **_kwargs) -> None:
        self.name = name
        self.data_class = data_class
        self.callback = callback
        self.queue_size = queue_size

    def feed(self, message) -> None:
        """把一条消息喂给回调，顺带检查类型 —— 类型不符在真 ROS 上是订不上的。"""
        if not isinstance(message, self.data_class):
            raise TypeError(
                f"subscriber {self.name} declared {self.data_class.__name__} "
                f"but got {type(message).__name__}"
            )
        if self.callback is not None:
            self.callback(message)

    def unregister(self) -> None:
        """真 Subscriber 有这个方法，节点关闭时会调。"""
        self.callback = None


class TimerEvent:
    """rospy.timer.TimerEvent 替身。"""

    def __init__(self) -> None:
        self.last_expected = None
        self.last_real = None
        self.current_expected = None
        self.current_real = None
        self.last_duration = 0.0


class FakeTimer:
    """rospy.Timer 替身 —— 不起线程，测试自己决定什么时候触发。

    真 Timer 会起后台线程按周期调回调。测试里那样做会让断言依赖时序，
    是不稳定测试的经典来源；这里改成手动 `fire()`。
    """

    def __init__(self, period, callback, oneshot: bool = False, **_kwargs) -> None:
        self.period = period
        self.callback = callback
        self.oneshot = oneshot
        self.fires = 0

    def fire(self) -> None:
        """手动触发一次回调。"""
        self.fires += 1
        self.callback(TimerEvent())

    def shutdown(self) -> None:
        """真 Timer 有这个方法。"""
        self.callback = None


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
        self.Subscriber = FakeSubscriber
        self.Timer = FakeTimer
        self.Rate = FakeRate
        self.Time = Time
        self.Duration = Duration
        self._shutdown = True
        self._shutdown_callbacks = []
        # rospy.timer.TimerEvent —— car_preprocessor 的类型注解会取到它
        timer_module = types.ModuleType("rospy.timer")
        timer_module.TimerEvent = TimerEvent
        timer_module.Timer = FakeTimer
        self.timer = timer_module

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

    def on_shutdown(self, callback) -> None:
        self._shutdown_callbacks.append(callback)

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
    sys.modules["rospy.timer"] = rospy_stub.timer

    sensor_msgs = types.ModuleType("sensor_msgs")
    sensor_msgs_msg = types.ModuleType("sensor_msgs.msg")
    sensor_msgs_msg.LaserScan = LaserScan
    sensor_msgs_msg.Imu = Imu
    sensor_msgs_msg.Image = Image
    sensor_msgs_msg.CompressedImage = CompressedImage
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
    geometry_msgs_msg.Point = Point
    geometry_msgs_msg.Pose = Pose
    geometry_msgs_msg.Twist = Twist
    geometry_msgs_msg.PoseWithCovariance = PoseWithCovariance
    geometry_msgs_msg.TwistWithCovariance = TwistWithCovariance
    geometry_msgs.msg = geometry_msgs_msg
    sys.modules["geometry_msgs"] = geometry_msgs
    sys.modules["geometry_msgs.msg"] = geometry_msgs_msg

    nav_msgs = types.ModuleType("nav_msgs")
    nav_msgs_msg = types.ModuleType("nav_msgs.msg")
    nav_msgs_msg.Odometry = Odometry
    nav_msgs_msg.OccupancyGrid = OccupancyGrid
    nav_msgs_msg.MapMetaData = MapMetaData
    nav_msgs.msg = nav_msgs_msg
    sys.modules["nav_msgs"] = nav_msgs
    sys.modules["nav_msgs.msg"] = nav_msgs_msg

    interfaces = types.ModuleType("air_ground_interfaces")
    interfaces_msg = types.ModuleType("air_ground_interfaces.msg")
    interfaces_msg.Observation = Observation
    interfaces_msg.RobotState = RobotState
    interfaces_msg.Capability = Capability
    interfaces_msg.WorldState = WorldState
    interfaces_msg.SemanticLandmark = SemanticLandmark
    interfaces_srv = types.ModuleType("air_ground_interfaces.srv")
    interfaces_srv.QueryWorldState = QueryWorldState
    interfaces_srv.QueryWorldStateResponse = QueryWorldStateResponse
    interfaces.msg = interfaces_msg
    interfaces.srv = interfaces_srv
    sys.modules["air_ground_interfaces"] = interfaces
    sys.modules["air_ground_interfaces.msg"] = interfaces_msg
    sys.modules["air_ground_interfaces.srv"] = interfaces_srv

    cv_bridge = types.ModuleType("cv_bridge")
    cv_bridge.CvBridge = CvBridge
    cv_bridge.CvBridgeError = CvBridgeError
    sys.modules["cv_bridge"] = cv_bridge

    return rospy_stub
