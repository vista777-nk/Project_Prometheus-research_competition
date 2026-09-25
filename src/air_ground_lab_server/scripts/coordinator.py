#!/usr/bin/env python3
"""空地协同调度占位节点。

协调器只消费 WorldState 与 Mission，并把合法 Mission 分发到稳定的
`/<robot_id>/mission`。它不导入或发布 MAVROS、Gazebo、控制器话题。
"""

from typing import Dict, Set

import rospy
from air_ground_interfaces.msg import Mission, WorldState


def validate_coordinator_config(config: Dict) -> Dict[str, Set[str]]:
    """校验允许的 agent 和任务类型集合。"""
    robots = {str(value) for value in config.get("allowed_robots", [])}
    mission_types = {
        str(value)
        for value in config.get("allowed_mission_types", [])
    }
    if not robots or not robots.issubset({"car", "drone"}):
        raise ValueError("coordinator.allowed_robots is invalid")
    if not mission_types or "" in mission_types:
        raise ValueError("coordinator.allowed_mission_types is invalid")
    return {"robots": robots, "mission_types": mission_types}


def mission_is_valid(
    mission: Mission, allowed: Dict[str, Set[str]]
) -> bool:
    """判断 Mission 是否具备可分发的稳定字段。"""
    return bool(
        mission.mission_id
        and mission.robot_id in allowed["robots"]
        and mission.type in allowed["mission_types"]
        and 0 <= mission.priority <= 255
    )


class Coordinator:
    """根据抽象全局状态把高层任务分发到目标 agent。"""

    def __init__(self) -> None:
        """加载白名单并建立 WorldState、Mission 与分发话题。"""
        self.allowed = validate_coordinator_config(
            dict(rospy.get_param("~coordinator"))
        )
        self.world_state = None
        self.dispatched_ids: Set[str] = set()
        self.publishers = {
            robot_id: rospy.Publisher(
                f"/{robot_id}/mission",
                Mission,
                queue_size=5,
            )
            for robot_id in self.allowed["robots"]
        }
        self.world_subscriber = rospy.Subscriber(
            "/server/world_state",
            WorldState,
            self.world_state_callback,
            queue_size=5,
        )
        self.mission_subscriber = rospy.Subscriber(
            "/server/eqa/mission",
            Mission,
            self.mission_callback,
            queue_size=5,
        )
        rospy.loginfo(
            "[coordinator] abstraction-only dispatcher ready"
        )

    def world_state_callback(self, message: WorldState) -> None:
        """缓存最近一次 World Model ASK 结果。"""
        self.world_state = message

    def mission_callback(self, message: Mission) -> None:
        """校验、去重并分发高层 Mission。"""
        if not mission_is_valid(message, self.allowed):
            rospy.logwarn(
                "[coordinator] invalid mission rejected: %s",
                message.mission_id,
            )
            return
        if message.mission_id in self.dispatched_ids:
            rospy.logwarn(
                "[coordinator] duplicate mission ignored: %s",
                message.mission_id,
            )
            return
        self.dispatched_ids.add(message.mission_id)
        self.publishers[message.robot_id].publish(message)
        rospy.loginfo(
            "[coordinator] mission %s dispatched to %s",
            message.mission_id,
            message.robot_id,
        )


def main() -> None:
    """启动空地协同调度 ROS 节点。"""
    rospy.init_node("coordinator")
    Coordinator()
    rospy.spin()


if __name__ == "__main__":
    main()
