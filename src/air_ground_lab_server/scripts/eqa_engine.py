#!/usr/bin/env python3
"""EQA 研究占位节点。

收到自然语言查询后先 ASK World Model，再产生稳定 Mission 交给协调器。
当前不包含 VLM 推理，保留明确的研究模块替换边界。
"""

import uuid
from typing import Dict

import rospy
from air_ground_interfaces.msg import Mission
from air_ground_interfaces.srv import QueryWorldState
from std_msgs.msg import String


def validate_eqa_config(config: Dict) -> Dict:
    """校验 EQA 占位策略配置。"""
    target_robot = str(config.get("target_robot", ""))
    if target_robot not in {"car", "drone"}:
        raise ValueError("eqa.target_robot must be car or drone")
    mission_type = str(config.get("mission_type", ""))
    if not mission_type:
        raise ValueError("eqa.mission_type must be non-empty")
    priority = int(config.get("priority", -1))
    if not 0 <= priority <= 255:
        raise ValueError("eqa.priority must be in [0, 255]")
    max_query_length = int(config.get("max_query_length", 0))
    if max_query_length <= 0:
        raise ValueError("eqa.max_query_length must be positive")
    return {
        "target_robot": target_robot,
        "mission_type": mission_type,
        "priority": priority,
        "max_query_length": max_query_length,
    }


def build_mission(query: str, config: Dict, stamp: rospy.Time) -> Mission:
    """为当前占位策略构造高层探索 Mission。"""
    text = query.strip()
    if not text:
        raise ValueError("EQA query cannot be empty")
    if len(text) > config["max_query_length"]:
        raise ValueError("EQA query exceeds configured length")
    mission = Mission()
    mission.header.stamp = stamp
    mission.header.frame_id = "map"
    mission.mission_id = f"eqa-{uuid.uuid4()}"
    mission.robot_id = config["target_robot"]
    mission.type = config["mission_type"]
    mission.priority = config["priority"]
    mission.query_text = text
    mission.query_id = mission.mission_id
    return mission


class EQAEngine:
    """把自然语言查询转换为可调度 Mission 的研究占位实现。"""

    def __init__(self) -> None:
        """加载策略、连接 World Model 并建立查询链。"""
        self.config = validate_eqa_config(
            dict(rospy.get_param("~eqa"))
        )
        self.world_query = rospy.ServiceProxy(
            "/server/query_world_state", QueryWorldState
        )
        self.mission_publisher = rospy.Publisher(
            "/server/eqa/mission",
            Mission,
            queue_size=5,
        )
        self.query_subscriber = rospy.Subscriber(
            "/server/eqa/query",
            String,
            self.query_callback,
            queue_size=5,
        )
        rospy.loginfo("[eqa_engine] placeholder ready")

    def query_callback(self, message: String) -> None:
        """ASK World Model 后向 Coordinator 发布一条 Mission。"""
        try:
            mission = build_mission(
                message.data, self.config, rospy.Time.now()
            )
            rospy.wait_for_service(
                "/server/query_world_state", timeout=2.0
            )
            self.world_query("snapshot", [])
        except (ValueError, rospy.ROSException, rospy.ServiceException) as error:
            rospy.logwarn("[eqa_engine] query rejected: %s", error)
            return
        self.mission_publisher.publish(mission)
        rospy.loginfo(
            "[eqa_engine] mission %s -> %s",
            mission.mission_id,
            mission.robot_id,
        )


def main() -> None:
    """启动 EQA 占位 ROS 节点。"""
    rospy.init_node("eqa_engine")
    EQAEngine()
    rospy.spin()


if __name__ == "__main__":
    main()
