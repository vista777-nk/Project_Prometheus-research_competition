#!/usr/bin/env python3
"""车载边缘预处理节点。

将 OpenMV、LiDAR、超声波、IMU 与里程计聚合为 ICD Observation、
RobotState 和 Capability。所有原始硬件话题都在本节点终止。
"""

import math
import threading
import time
from typing import Dict, Iterable, List, Optional, Tuple

import cv2
import rospy
from air_ground_interfaces.msg import Capability, Observation, RobotState
from cv_bridge import CvBridge, CvBridgeError
from nav_msgs.msg import Odometry
from sensor_msgs.msg import CompressedImage, Image, Imu, LaserScan


DIRECTIONS: Tuple[str, ...] = ("front", "rear", "left", "right")


def positive_float(value, label: str) -> float:
    """校验一个有限正浮点配置值。"""
    number = float(value)
    if not math.isfinite(number) or number <= 0.0:
        raise ValueError(f"{label} must be a finite positive number")
    return number


def sanitize_lidar_ranges(
    ranges: Iterable[float], downsample: int
) -> List[float]:
    """降采样 LiDAR，并用 -1.0 表示无效或无穷距离。"""
    if downsample <= 0:
        raise ValueError("downsample must be positive")
    result = []
    for value in list(ranges)[::downsample]:
        number = float(value)
        result.append(number if math.isfinite(number) and number > 0.0 else -1.0)
    return result


def sanitize_ultrasonic(
    message: Optional[LaserScan],
    minimum: float,
    maximum: float,
) -> float:
    """把单波束超声波读数限制到有效量程，缺失时返回最大量程。"""
    if minimum <= 0.0 or maximum <= minimum:
        raise ValueError("invalid ultrasonic range")
    if message is None or not message.ranges:
        return maximum
    value = float(message.ranges[0])
    if not math.isfinite(value):
        return maximum
    return max(minimum, min(maximum, value))


class CarPreprocessor:
    """聚合车载原始传感器并发布稳定抽象接口。"""

    def __init__(self) -> None:
        """读取私有参数、建立订阅发布器并启动周期快照。"""
        self.publish_rate = positive_float(
            rospy.get_param("~publish_rate"), "publish_rate"
        )
        self.sensor_timeout = positive_float(
            rospy.get_param("~sensor_timeout"), "sensor_timeout"
        )
        self.battery_voltage = positive_float(
            rospy.get_param("~battery_voltage"), "battery_voltage"
        )
        self.frame_id = str(rospy.get_param("~frame_id"))
        self.state_frame_id = str(rospy.get_param("~state_frame_id"))
        self.default_chassis = str(rospy.get_param("~default_chassis"))
        self.lidar_downsample = int(rospy.get_param("~lidar_downsample"))
        self.ultrasonic_minimum = positive_float(
            rospy.get_param("~ultrasonic_min_range"),
            "ultrasonic_min_range",
        )
        self.ultrasonic_maximum = positive_float(
            rospy.get_param("~ultrasonic_max_range"),
            "ultrasonic_max_range",
        )
        self.jpeg_quality = int(rospy.get_param("~jpeg_quality"))
        self.topics = dict(rospy.get_param("~topics"))
        self.capability_config = dict(rospy.get_param("~capability"))
        self._validate_config()

        self.bridge = CvBridge()
        self.lock = threading.Lock()
        self.latest: Dict[str, object] = {
            "image": None,
            "scan": None,
            "imu": None,
            "odometry": None,
            "ultrasonic": {},
        }
        self.updated_at: Dict[str, float] = {}

        self.observation_publisher = rospy.Publisher(
            self.topics["observation"],
            Observation,
            queue_size=5,
        )
        self.state_publisher = rospy.Publisher(
            self.topics["state"],
            RobotState,
            queue_size=5,
        )
        self.capability_publisher = rospy.Publisher(
            self.topics["capability"],
            Capability,
            queue_size=1,
            latch=True,
        )
        self.subscribers = [
            rospy.Subscriber(
                self.topics["image"],
                Image,
                self.image_callback,
                queue_size=2,
            ),
            rospy.Subscriber(
                self.topics["scan"],
                LaserScan,
                self.cache_callback("scan"),
                queue_size=5,
            ),
            rospy.Subscriber(
                self.topics["imu"],
                Imu,
                self.cache_callback("imu"),
                queue_size=5,
            ),
            rospy.Subscriber(
                self.topics["odometry"],
                Odometry,
                self.cache_callback("odometry"),
                queue_size=5,
            ),
        ]
        prefix = str(self.topics["ultrasonic_prefix"]).rstrip("/")
        for direction in DIRECTIONS:
            self.subscribers.append(
                rospy.Subscriber(
                    f"{prefix}/{direction}",
                    LaserScan,
                    self.ultrasonic_callback(direction),
                    queue_size=5,
                )
            )

        self.publish_capability()
        self.timer = rospy.Timer(
            rospy.Duration(1.0 / self.publish_rate),
            self.publish_snapshot,
        )
        rospy.loginfo(
            "[car_preprocessor] ready at %.1f Hz -> %s",
            self.publish_rate,
            self.topics["observation"],
        )

    def _validate_config(self) -> None:
        """校验话题、量程、压缩与能力配置。"""
        required_topics = {
            "image",
            "scan",
            "imu",
            "odometry",
            "ultrasonic_prefix",
            "observation",
            "state",
            "capability",
        }
        if not required_topics.issubset(self.topics):
            raise ValueError("car edge topics are incomplete")
        if any(not str(self.topics[key]) for key in required_topics):
            raise ValueError("car edge topics must be non-empty")
        if self.default_chassis not in {"diff", "mecanum"}:
            raise ValueError("default_chassis must be diff or mecanum")
        if self.lidar_downsample <= 0:
            raise ValueError("lidar_downsample must be positive")
        if self.ultrasonic_maximum <= self.ultrasonic_minimum:
            raise ValueError("ultrasonic range is invalid")
        if not 1 <= self.jpeg_quality <= 100:
            raise ValueError("jpeg_quality must be in [1, 100]")

    def cache_callback(self, key: str):
        """创建带单调时钟时间戳的通用缓存回调。"""

        def callback(message) -> None:
            with self.lock:
                self.latest[key] = message
                self.updated_at[key] = time.monotonic()

        return callback

    def ultrasonic_callback(self, direction: str):
        """创建指定方向的超声波缓存回调。"""

        def callback(message: LaserScan) -> None:
            with self.lock:
                ultrasonic = self.latest["ultrasonic"]
                ultrasonic[direction] = message
                self.updated_at[f"ultrasonic/{direction}"] = time.monotonic()

        return callback

    def image_callback(self, message: Image) -> None:
        """在边缘端把 OpenMV 原始图像压缩为 JPEG。"""
        try:
            image = self.bridge.imgmsg_to_cv2(message, "bgr8")
            success, encoded = cv2.imencode(
                ".jpg",
                image,
                (cv2.IMWRITE_JPEG_QUALITY, self.jpeg_quality),
            )
            if not success:
                raise ValueError("OpenCV JPEG encoder returned false")
            compressed = CompressedImage()
            compressed.header = message.header
            compressed.format = "jpeg"
            compressed.data = encoded.tobytes()
        except (CvBridgeError, TypeError, ValueError, cv2.error) as error:
            rospy.logwarn_throttle(
                5.0,
                "[car_preprocessor] image compression failed: %s",
                error,
            )
            return
        with self.lock:
            self.latest["image"] = compressed
            self.updated_at["image"] = time.monotonic()

    def snapshot(self) -> Tuple[Dict[str, object], Dict[str, float]]:
        """复制当前缓存，避免在构造消息时持有回调锁。"""
        with self.lock:
            latest = dict(self.latest)
            latest["ultrasonic"] = dict(self.latest["ultrasonic"])
            return latest, dict(self.updated_at)

    def is_fresh(
        self,
        updated_at: Dict[str, float],
        key: str,
        now: float,
    ) -> bool:
        """判断一个缓存项是否仍在配置的新鲜度窗口内。"""
        return (
            key in updated_at
            and now - updated_at[key] <= self.sensor_timeout
        )

    def current_chassis(self) -> str:
        """读取 Task-04 事务切换器维护的当前底盘参数。"""
        value = str(
            rospy.get_param("/car/current_chassis", self.default_chassis)
        ).lower()
        if value not in {"diff", "mecanum"}:
            rospy.logwarn_throttle(
                5.0,
                "[car_preprocessor] invalid chassis '%s'; using %s",
                value,
                self.default_chassis,
            )
            return self.default_chassis
        return value

    def build_messages(
        self, stamp: rospy.Time
    ) -> Tuple[Observation, RobotState]:
        """从一次线程安全快照构造 Observation 与 RobotState。"""
        latest, updated_at = self.snapshot()
        now = time.monotonic()

        observation = Observation()
        observation.header.stamp = stamp
        observation.header.frame_id = self.frame_id
        observation.robot_id = "car"
        modalities = []

        if self.is_fresh(updated_at, "image", now):
            observation.rgb = latest["image"]
            modalities.append("rgb")

        scan = latest["scan"]
        if scan is not None and self.is_fresh(updated_at, "scan", now):
            observation.lidar_ranges = sanitize_lidar_ranges(
                scan.ranges, self.lidar_downsample
            )
            observation.lidar_angle_min = scan.angle_min
            observation.lidar_angle_increment = (
                scan.angle_increment * self.lidar_downsample
            )
            modalities.append("lidar_2d")

        imu = latest["imu"]
        if imu is not None and self.is_fresh(updated_at, "imu", now):
            observation.angular_velocity = imu.angular_velocity
            observation.linear_acceleration = imu.linear_acceleration
            modalities.append("imu")

        ultrasonic = []
        has_ultrasonic = False
        for direction in DIRECTIONS:
            key = f"ultrasonic/{direction}"
            message = latest["ultrasonic"].get(direction)
            if not self.is_fresh(updated_at, key, now):
                message = None
            else:
                has_ultrasonic = True
            ultrasonic.append(
                sanitize_ultrasonic(
                    message,
                    self.ultrasonic_minimum,
                    self.ultrasonic_maximum,
                )
            )
        if has_ultrasonic:
            observation.ultrasonic_ranges = ultrasonic
            modalities.append("ultrasonic")
        observation.modalities = modalities

        state = RobotState()
        state.header.stamp = stamp
        state.header.frame_id = self.state_frame_id
        state.robot_id = "car"
        odometry = latest["odometry"]
        odometry_fresh = (
            odometry is not None
            and self.is_fresh(updated_at, "odometry", now)
        )
        if odometry_fresh:
            state.pose = odometry.pose.pose
            state.velocity = odometry.twist.twist
        state.mode = "idle"
        state.chassis_type = self.current_chassis()
        state.battery_voltage = self.battery_voltage
        state.is_armed = False
        state.is_connected = bool(odometry_fresh or modalities)
        return observation, state

    def publish_snapshot(self, _event: rospy.timer.TimerEvent) -> None:
        """按配置频率发布一致的观测和机器人状态快照。"""
        observation, state = self.build_messages(rospy.Time.now())
        self.observation_publisher.publish(observation)
        self.state_publisher.publish(state)

    def publish_capability(self) -> None:
        """发布并锁存车体能力声明。"""
        config = self.capability_config
        capability = Capability()
        capability.header.stamp = rospy.Time.now()
        capability.robot_id = "car"
        capability.locomotion_type = str(config["locomotion_type"])
        capability.max_speed = positive_float(
            config["max_speed"], "capability.max_speed"
        )
        capability.max_endurance = positive_float(
            config["max_endurance"], "capability.max_endurance"
        )
        capability.sensor_modalities = [
            str(value) for value in config["sensor_modalities"]
        ]
        capability.sensor_range = positive_float(
            config["sensor_range"], "capability.sensor_range"
        )
        capability.compute_tier = str(config["compute_tier"])
        capability.max_payload_kg = float(config["max_payload_kg"])
        capability.has_gripper = bool(config["has_gripper"])
        self.capability_publisher.publish(capability)


def main() -> None:
    """启动 ROS 车载边缘预处理节点。"""
    rospy.init_node("car_preprocessor")
    CarPreprocessor()
    rospy.spin()


if __name__ == "__main__":
    main()
