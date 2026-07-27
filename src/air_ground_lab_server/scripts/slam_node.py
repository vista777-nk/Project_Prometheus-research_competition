#!/usr/bin/env python3
"""SLAM 研究占位节点。

当前仅消费 ICD Observation/RobotState、统计数据流并通过
`/server/world_state/update` 告知 World Model 感知更新时间。
"""

import math
import threading

import rospy
from air_ground_interfaces.msg import Observation, RobotState, WorldState


def positive_float(value, label: str) -> float:
    """校验一个有限正浮点配置值。"""
    number = float(value)
    if not math.isfinite(number) or number <= 0.0:
        raise ValueError(f"{label} must be a finite positive number")
    return number


class SLAMPlaceholder:
    """提供不依赖硬件消息的 SLAM 算法接入边界。"""

    def __init__(self) -> None:
        """建立抽象接口订阅、更新时间发布器和统计定时器。"""
        interval = positive_float(
            rospy.get_param("~slam/statistics_interval"),
            "slam.statistics_interval",
        )
        self.lock = threading.Lock()
        self.counts = {"car": 0, "drone": 0}
        self.latest_car_state = None
        self.update_publisher = rospy.Publisher(
            "/server/world_state/update",
            WorldState,
            queue_size=5,
        )
        self.subscribers = [
            rospy.Subscriber(
                "/server/car/observation",
                Observation,
                self.observation_callback("car"),
                queue_size=5,
            ),
            rospy.Subscriber(
                "/server/drone/observation",
                Observation,
                self.observation_callback("drone"),
                queue_size=5,
            ),
            rospy.Subscriber(
                "/server/car/state",
                RobotState,
                self.car_state_callback,
                queue_size=5,
            ),
        ]
        self.started_at = rospy.Time.now()
        self.timer = rospy.Timer(
            rospy.Duration(interval), self.log_statistics
        )
        rospy.loginfo(
            "[slam_node] abstraction-only placeholder ready"
        )

    def observation_callback(self, robot_id: str):
        """创建指定 agent 的抽象观测回调。"""

        def callback(_message: Observation) -> None:
            with self.lock:
                self.counts[robot_id] += 1
            update = WorldState()
            update.header.stamp = rospy.Time.now()
            update.header.frame_id = "map"
            update.last_update_perception = update.header.stamp
            self.update_publisher.publish(update)

        return callback

    def car_state_callback(self, message: RobotState) -> None:
        """缓存未来 SLAM 后端可使用的车体里程计先验。"""
        with self.lock:
            self.latest_car_state = message

    def log_statistics(self, _event: rospy.timer.TimerEvent) -> None:
        """周期记录抽象观测到达率。"""
        elapsed = max(
            1e-6, (rospy.Time.now() - self.started_at).to_sec()
        )
        with self.lock:
            car_rate = self.counts["car"] / elapsed
            drone_rate = self.counts["drone"] / elapsed
        rospy.loginfo(
            "[slam_node] car=%.1f Hz drone=%.1f Hz",
            car_rate,
            drone_rate,
        )


def main() -> None:
    """启动 SLAM 占位 ROS 节点。"""
    rospy.init_node("slam_node")
    SLAMPlaceholder()
    rospy.spin()


if __name__ == "__main__":
    main()
