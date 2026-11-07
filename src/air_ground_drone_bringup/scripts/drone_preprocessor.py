#!/usr/bin/env python3
"""无人机边缘预处理节点。

聚合深度相机、RGB、IMU、ENU 位姿与 MAVROS 状态，发布 ICD
Observation、RobotState 和 Capability。
"""

import math
import threading
import time
from typing import Dict, Tuple

import cv2
import rospy
from air_ground_interfaces.msg import Capability, Observation, RobotState
from cv_bridge import CvBridge, CvBridgeError
from geometry_msgs.msg import PoseStamped
from mavros_msgs.msg import State
from sensor_msgs.msg import CompressedImage, Image, Imu


def positive_float(value, label: str) -> float:
    """校验一个有限正浮点配置值。"""
    number = float(value)
    if not math.isfinite(number) or number <= 0.0:
        raise ValueError(f"{label} must be a finite positive number")
    return number


def resize_to_width(image, maximum_width: int):
    """按最大宽度等比例缩小图像，不放大小图。"""
    if maximum_width <= 0:
        raise ValueError("maximum_width must be positive")
    if image.shape[1] <= maximum_width:
        return image
    scale = maximum_width / float(image.shape[1])
    height = max(1, int(round(image.shape[0] * scale)))
    return cv2.resize(
        image,
        (maximum_width, height),
        interpolation=cv2.INTER_AREA,
    )


class DronePreprocessor:
    """聚合无人机原始传感器并发布稳定抽象接口。"""

    def __init__(self) -> None:
        """读取配置、建立订阅发布器并启动周期快照。"""
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
        self.jpeg_quality = int(rospy.get_param("~jpeg_quality"))
        self.max_rgb_width = int(rospy.get_param("~max_rgb_width"))
        self.topics = dict(rospy.get_param("~topics"))
        self.capability_config = dict(rospy.get_param("~capability"))
        self._validate_config()

        self.bridge = CvBridge()
        self.lock = threading.Lock()
        self.latest: Dict[str, object] = {
            "rgb": None,
            "depth": None,
            "imu": None,
            "pose": None,
            "mavros_state": None,
        }
        self.updated_at: Dict[str, float] = {}

        self.observation_publisher = rospy.Publisher(
            self.topics["observation"],
            Observation,
            queue_size=3,
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
        rospy.set_param("/drone/state_owner", "preprocessor")
        self.subscribers = [
            rospy.Subscriber(
                self.topics["rgb"],
                Image,
                self.rgb_callback,
                queue_size=2,
            ),
            rospy.Subscriber(
                self.topics["depth"],
                Image,
                self.cache_callback("depth"),
                queue_size=2,
            ),
            rospy.Subscriber(
                self.topics["imu"],
                Imu,
                self.cache_callback("imu"),
                queue_size=5,
            ),
            rospy.Subscriber(
                self.topics["pose"],
                PoseStamped,
                self.cache_callback("pose"),
                queue_size=5,
            ),
            rospy.Subscriber(
                self.topics["mavros_state"],
                State,
                self.cache_callback("mavros_state"),
                queue_size=5,
            ),
        ]

        self.publish_capability()
        self.timer = rospy.Timer(
            rospy.Duration(1.0 / self.publish_rate),
            self.publish_snapshot,
        )
        rospy.loginfo(
            "[drone_preprocessor] ready at %.1f Hz -> %s",
            self.publish_rate,
            self.topics["observation"],
        )

    def _validate_config(self) -> None:
        """校验话题与图像预处理配置。"""
        required_topics = {
            "rgb",
            "depth",
            "imu",
            "pose",
            "mavros_state",
            "observation",
            "state",
            "capability",
        }
        if not required_topics.issubset(self.topics):
            raise ValueError("drone edge topics are incomplete")
        if any(not str(self.topics[key]) for key in required_topics):
            raise ValueError("drone edge topics must be non-empty")
        if not 1 <= self.jpeg_quality <= 100:
            raise ValueError("jpeg_quality must be in [1, 100]")
        if self.max_rgb_width <= 0:
            raise ValueError("max_rgb_width must be positive")

    def cache_callback(self, key: str):
        """创建带单调时钟时间戳的通用缓存回调。"""

        def callback(message) -> None:
            with self.lock:
                self.latest[key] = message
                self.updated_at[key] = time.monotonic()

        return callback

    def rgb_callback(self, message: Image) -> None:
        """缩放并压缩 RGB 图像，限制边缘上行负载。"""
        try:
            image = self.bridge.imgmsg_to_cv2(message, "bgr8")
            image = resize_to_width(image, self.max_rgb_width)
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
                "[drone_preprocessor] RGB compression failed: %s",
                error,
            )
            return
        with self.lock:
            self.latest["rgb"] = compressed
            self.updated_at["rgb"] = time.monotonic()

    def snapshot(self) -> Tuple[Dict[str, object], Dict[str, float]]:
        """复制缓存，避免在构造消息时阻塞传感器回调。"""
        with self.lock:
            return dict(self.latest), dict(self.updated_at)

    def is_fresh(
        self,
        updated_at: Dict[str, float],
        key: str,
        now: float,
    ) -> bool:
        """判断缓存项是否位于配置的新鲜度窗口内。"""
        return (
            key in updated_at
            and now - updated_at[key] <= self.sensor_timeout
        )

    def build_messages(
        self, stamp: rospy.Time
    ) -> Tuple[Observation, RobotState]:
        """从同一缓存快照构造无人机观测与状态。"""
        latest, updated_at = self.snapshot()
        now = time.monotonic()

        observation = Observation()
        observation.header.stamp = stamp
        observation.header.frame_id = self.frame_id
        observation.robot_id = "drone"
        modalities = []

        if self.is_fresh(updated_at, "rgb", now):
            observation.rgb = latest["rgb"]
            modalities.append("rgb")
        if self.is_fresh(updated_at, "depth", now):
            observation.depth = latest["depth"]
            modalities.append("depth")
        imu = latest["imu"]
        if imu is not None and self.is_fresh(updated_at, "imu", now):
            observation.angular_velocity = imu.angular_velocity
            observation.linear_acceleration = imu.linear_acceleration
            modalities.append("imu")
        observation.modalities = modalities

        state = RobotState()
        state.header.stamp = stamp
        state.header.frame_id = self.state_frame_id
        state.robot_id = "drone"
        pose = latest["pose"]
        pose_fresh = (
            pose is not None and self.is_fresh(updated_at, "pose", now)
        )
        if pose_fresh:
            state.pose = pose.pose
        mavros_state = latest["mavros_state"]
        mavros_fresh = (
            mavros_state is not None
            and self.is_fresh(updated_at, "mavros_state", now)
        )
        if mavros_fresh:
            state.mode = mavros_state.mode
            state.is_armed = mavros_state.armed
            state.is_connected = mavros_state.connected
        else:
            state.mode = "unknown"
            state.is_armed = False
            state.is_connected = bool(pose_fresh or modalities)
        state.chassis_type = "none"
        state.battery_voltage = self.battery_voltage
        return observation, state

    def publish_snapshot(self, _event: rospy.timer.TimerEvent) -> None:
        """按配置频率发布一致的观测和状态快照。"""
        observation, state = self.build_messages(rospy.Time.now())
        self.observation_publisher.publish(observation)
        self.state_publisher.publish(state)

    def publish_capability(self) -> None:
        """发布并锁存无人机能力声明。"""
        config = self.capability_config
        capability = Capability()
        capability.header.stamp = rospy.Time.now()
        capability.robot_id = "drone"
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

    def shutdown(self) -> None:
        """释放状态话题所有权，允许 Task-06 桥恢复兼容发布。"""
        self.timer.shutdown()
        if (
            rospy.has_param("/drone/state_owner")
            and rospy.get_param("/drone/state_owner") == "preprocessor"
        ):
            rospy.delete_param("/drone/state_owner")


def main() -> None:
    """启动 ROS 无人机边缘预处理节点。"""
    rospy.init_node("drone_preprocessor")
    preprocessor = DronePreprocessor()
    rospy.on_shutdown(preprocessor.shutdown)
    rospy.spin()


if __name__ == "__main__":
    main()
