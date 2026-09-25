#!/usr/bin/env python3
"""World Model 系统认知中心。

接收服务器侧 Observation、RobotState 与研究模块更新，维护带新鲜度约束的
WorldState，并通过锁存话题和 QueryWorldState 服务提供统一 ASK 接口。
"""

import math
import threading
from typing import Dict, List, Tuple

import rospy
from air_ground_interfaces.msg import Observation, RobotState, WorldState
from air_ground_interfaces.srv import (
    QueryWorldState,
    QueryWorldStateResponse,
)
from nav_msgs.msg import OccupancyGrid


def positive_float(value, label: str) -> float:
    """校验一个有限正浮点配置值。"""
    number = float(value)
    if not math.isfinite(number) or number <= 0.0:
        raise ValueError(f"{label} must be a finite positive number")
    return number


class WorldModelStore:
    """线程安全地维护服务器全局状态及各数据源更新时间。"""

    def __init__(self, state_timeout: float) -> None:
        """创建一个带状态过期窗口的内存存储。"""
        self.state_timeout = positive_float(
            state_timeout, "state_timeout"
        )
        self.lock = threading.Lock()
        self.states: Dict[str, RobotState] = {}
        self.state_updates: Dict[str, rospy.Time] = {}
        self.observation_updates: Dict[str, rospy.Time] = {}
        self.map_2d = OccupancyGrid()
        self.dynamic_obstacles = []
        self.landmarks = []
        self.last_update_perception = rospy.Time()
        self.last_update_planning = rospy.Time()

    @staticmethod
    def message_time(stamp: rospy.Time, fallback: rospy.Time) -> rospy.Time:
        """将零时间戳替换为接收时间。"""
        return stamp if stamp.to_sec() > 0.0 else fallback

    def update_observation(
        self,
        robot_id: str,
        message: Observation,
        received_at: rospy.Time,
    ) -> None:
        """记录一次 agent 感知更新。"""
        stamp = self.message_time(message.header.stamp, received_at)
        with self.lock:
            self.observation_updates[robot_id] = stamp
            if stamp > self.last_update_perception:
                self.last_update_perception = stamp

    def update_state(
        self,
        robot_id: str,
        message: RobotState,
        received_at: rospy.Time,
    ) -> None:
        """记录一次 agent 状态更新。"""
        stamp = self.message_time(message.header.stamp, received_at)
        with self.lock:
            self.states[robot_id] = message
            self.state_updates[robot_id] = stamp
            if stamp > self.last_update_perception:
                self.last_update_perception = stamp

    def apply_external_update(
        self, message: WorldState, received_at: rospy.Time
    ) -> None:
        """合并 SLAM/EQA 等研究模块提供的非空字段。"""
        with self.lock:
            for state in message.agents:
                if state.robot_id:
                    self.states[state.robot_id] = state
                    self.state_updates[state.robot_id] = received_at
            if message.map_2d.info.width or message.map_2d.data:
                self.map_2d = message.map_2d
            if message.dynamic_obstacles:
                self.dynamic_obstacles = list(
                    message.dynamic_obstacles
                )
            if message.landmarks:
                self.landmarks = list(message.landmarks)
            if message.last_update_perception.to_sec() > 0.0:
                self.last_update_perception = (
                    message.last_update_perception
                )
            if message.last_update_planning.to_sec() > 0.0:
                self.last_update_planning = message.last_update_planning

    def snapshot(self, now: rospy.Time) -> WorldState:
        """生成只包含未过期 agent 的一致 WorldState 快照。"""
        output = WorldState()
        output.header.stamp = now
        output.header.frame_id = "map"
        with self.lock:
            fresh_ids = [
                robot_id
                for robot_id, updated_at in self.state_updates.items()
                if (now - updated_at).to_sec() <= self.state_timeout
            ]
            output.agents = [
                self.states[robot_id] for robot_id in sorted(fresh_ids)
            ]
            output.map_2d = self.map_2d
            output.dynamic_obstacles = list(self.dynamic_obstacles)
            output.landmarks = list(self.landmarks)
            output.last_update_perception = self.last_update_perception
            output.last_update_planning = self.last_update_planning
        if not output.map_2d.header.frame_id:
            output.map_2d.header.frame_id = "map"
        return output

    def query(
        self,
        query_type: str,
        args: List[str],
        now: rospy.Time,
    ) -> Tuple[WorldState, bool]:
        """执行当前阶段支持的 snapshot、agent 与 landmark 查询。"""
        snapshot = self.snapshot(now)
        normalized = query_type.strip().lower()
        if normalized in {"snapshot", "all"}:
            found = bool(
                snapshot.agents
                or snapshot.landmarks
                or snapshot.map_2d.info.width
            )
            return snapshot, found
        if normalized == "agent":
            if not args:
                return snapshot, False
            snapshot.agents = [
                state
                for state in snapshot.agents
                if state.robot_id == args[0]
            ]
            return snapshot, bool(snapshot.agents)
        if normalized == "nearest_landmark":
            if args:
                snapshot.landmarks = [
                    landmark
                    for landmark in snapshot.landmarks
                    if landmark.semantic_label == args[0]
                ]
            if snapshot.landmarks:
                snapshot.landmarks = [
                    max(
                        snapshot.landmarks,
                        key=lambda landmark: landmark.confidence,
                    )
                ]
            return snapshot, bool(snapshot.landmarks)
        return snapshot, False


class WorldModelNode:
    """把 WorldModelStore 暴露为 ROS TELL/ASK 接口。"""

    def __init__(self) -> None:
        """建立订阅、发布器、查询服务和周期发布定时器。"""
        publish_rate = positive_float(
            rospy.get_param("~world_model/publish_rate"),
            "world_model.publish_rate",
        )
        state_timeout = positive_float(
            rospy.get_param("~world_model/state_timeout"),
            "world_model.state_timeout",
        )
        self.store = WorldModelStore(state_timeout)
        self.subscribers = []
        for robot_id in ("car", "drone"):
            self.subscribers.extend(
                [
                    rospy.Subscriber(
                        f"/server/{robot_id}/observation",
                        Observation,
                        self.observation_callback(robot_id),
                        queue_size=10,
                    ),
                    rospy.Subscriber(
                        f"/server/{robot_id}/state",
                        RobotState,
                        self.state_callback(robot_id),
                        queue_size=10,
                    ),
                ]
            )
        self.subscribers.append(
            rospy.Subscriber(
                "/server/world_state/update",
                WorldState,
                self.external_update_callback,
                queue_size=5,
            )
        )
        self.publisher = rospy.Publisher(
            "/server/world_state",
            WorldState,
            queue_size=5,
            latch=True,
        )
        self.service = rospy.Service(
            "/server/query_world_state",
            QueryWorldState,
            self.handle_query,
        )
        self.timer = rospy.Timer(
            rospy.Duration(1.0 / publish_rate),
            self.publish_snapshot,
        )
        rospy.loginfo(
            "[world_model] ready at %.1f Hz; timeout=%.1f s",
            publish_rate,
            state_timeout,
        )

    def observation_callback(self, robot_id: str):
        """创建指定 agent 的 Observation TELL 回调。"""

        def callback(message: Observation) -> None:
            self.store.update_observation(
                robot_id, message, rospy.Time.now()
            )

        return callback

    def state_callback(self, robot_id: str):
        """创建指定 agent 的 RobotState TELL 回调。"""

        def callback(message: RobotState) -> None:
            self.store.update_state(
                robot_id, message, rospy.Time.now()
            )

        return callback

    def external_update_callback(self, message: WorldState) -> None:
        """合并研究模块经稳定通道提供的更新。"""
        self.store.apply_external_update(message, rospy.Time.now())

    def publish_snapshot(self, _event: rospy.timer.TimerEvent) -> None:
        """周期发布锁存 WorldState。"""
        self.publisher.publish(self.store.snapshot(rospy.Time.now()))

    def handle_query(self, request):
        """同步执行 ASK 请求。"""
        result, found = self.store.query(
            request.query_type, list(request.args), rospy.Time.now()
        )
        response = QueryWorldStateResponse()
        response.result = result
        response.found = found
        return response


def main() -> None:
    """启动 World Model ROS 节点。"""
    rospy.init_node("world_model")
    WorldModelNode()
    rospy.spin()


if __name__ == "__main__":
    main()
