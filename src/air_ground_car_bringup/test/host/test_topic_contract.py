#!/usr/bin/env python3
"""实机驱动与仿真/预处理器之间的契约测试。

这组断言防的是一类**不会报错的失败**：ROS 的订阅端遇到话题名不符或消息
类型不符时既不连接也不抱怨，节点日志一切正常，只是 Observation 里
永远少一个模态。等到实机联调那天才发现，而那时最贵。

task-14 落地时这里抓到过三处真实不一致（详见 ADR-0009 §背景）：
    /car/imu        vs 预处理器订的 /car/imu/data
    Range           vs 仿真与预处理器用的 LaserScan
    角度 0..2π      vs 仿真的 -π..π

同类做法见 src/deployment/validate.sh §7「交叉引用」。
"""

import ast
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

from fixtures import CAR_EDGE_YAML, PACKAGE_ROOT, REAL_SENSORS_YAML, load_yaml

CAR_SENSORS_YAML = PACKAGE_ROOT / "config" / "car_sensors.yaml"
PREPROCESSOR = PACKAGE_ROOT / "scripts" / "car_preprocessor.py"
SENSOR_URDF = PACKAGE_ROOT / "urdf" / "car_sensors.urdf.xacro"
REAL_LAUNCH = PACKAGE_ROOT / "launch" / "car_edge_real.launch"

# ICD §2.1: ultrasonic_ranges 的语义是 [front, rear, left, right]
ICD_ULTRASONIC_ORDER = ["front", "rear", "left", "right"]


def read_module_constant(path: Path, name: str):
    """不 import 就取出模块里的一个字面量常量。

    car_preprocessor.py 依赖 cv2 / cv_bridge / air_ground_interfaces，
    在纯 Python 环境里 import 不进来；但 AST 解析不需要这些依赖。
    """
    tree = ast.parse(path.read_text(encoding="utf-8"))
    for node in tree.body:
        targets = []
        if isinstance(node, ast.Assign):
            targets = node.targets
        elif isinstance(node, ast.AnnAssign):
            targets = [node.target]
        for target in targets:
            if isinstance(target, ast.Name) and target.id == name:
                return ast.literal_eval(node.value)
    raise AssertionError(
        f"{path.name} 里找不到常量 {name} —— 它被改名或删掉了，"
        "本契约测试需要同步更新，而不是放行。"
    )


class TopicContractTest(unittest.TestCase):
    """驱动发布的话题必须与 car_preprocessor 订阅的完全一致。"""

    @classmethod
    def setUpClass(cls):
        cls.real = load_yaml(REAL_SENSORS_YAML)
        cls.edge = load_yaml(CAR_EDGE_YAML)
        cls.sim = load_yaml(CAR_SENSORS_YAML)

    def test_lidar_topic_matches_preprocessor(self):
        self.assertEqual(self.real["rplidar"]["topic"], self.edge["topics"]["scan"])

    def test_imu_topic_matches_preprocessor(self):
        """`/car/imu` 与 `/car/imu/data` 差一个后缀，症状是 imu 模态永远缺失。"""
        self.assertEqual(self.real["icm42688"]["topic"], self.edge["topics"]["imu"])

    def test_ultrasonic_prefix_matches_preprocessor(self):
        self.assertEqual(
            self.real["chassis_bridge"]["topic_prefix"].rstrip("/"),
            self.edge["topics"]["ultrasonic_prefix"].rstrip("/"),
        )

    def test_ultrasonic_directions_match_icd_order(self):
        directions = list(self.real["chassis_bridge"]["directions"])
        self.assertEqual(directions, ICD_ULTRASONIC_ORDER)
        self.assertEqual(
            directions, list(read_module_constant(PREPROCESSOR, "DIRECTIONS"))
        )

    def test_ultrasonic_range_matches_preprocessor_clamp(self):
        """预处理器按自己的量程夹取，两边不一致会把有效读数夹掉。"""
        self.assertEqual(
            self.real["chassis_bridge"]["min_range"],
            self.edge["ultrasonic_min_range"],
        )
        self.assertEqual(
            self.real["chassis_bridge"]["max_range"],
            self.edge["ultrasonic_max_range"],
        )


class SimulationParityTest(unittest.TestCase):
    """实机传感器参数必须与仿真那套对得上，否则 sim-to-real 没有可比性。"""

    @classmethod
    def setUpClass(cls):
        cls.real = load_yaml(REAL_SENSORS_YAML)
        cls.sim = load_yaml(CAR_SENSORS_YAML)

    def test_lidar_geometry_matches_simulation(self):
        self.assertEqual(self.real["rplidar"]["samples"], self.sim["lidar"]["samples"])
        self.assertEqual(
            self.real["rplidar"]["min_range"], self.sim["lidar"]["min_range"]
        )
        self.assertEqual(
            self.real["rplidar"]["max_range"], self.sim["lidar"]["max_range"]
        )
        self.assertEqual(self.real["rplidar"]["topic"], self.sim["lidar"]["topic"])

    def test_imu_topic_matches_simulation(self):
        self.assertEqual(self.real["icm42688"]["topic"], self.sim["imu"]["topic"])

    def test_ultrasonic_matches_simulation(self):
        self.assertEqual(
            list(self.real["chassis_bridge"]["directions"]),
            list(self.sim["ultrasonic"]["directions"]),
        )
        self.assertEqual(
            self.real["chassis_bridge"]["min_range"],
            self.sim["ultrasonic"]["min_range"],
        )
        self.assertEqual(
            self.real["chassis_bridge"]["max_range"],
            self.sim["ultrasonic"]["max_range"],
        )


class FrameIdContractTest(unittest.TestCase):
    """frame_id 必须是 URDF 里真实存在的 link，否则 TF 查询会静默失败。"""

    @classmethod
    def setUpClass(cls):
        cls.real = load_yaml(REAL_SENSORS_YAML)
        cls.urdf = SENSOR_URDF.read_text(encoding="utf-8")

    def test_lidar_frame_exists_in_urdf(self):
        self.assertIn(
            f'<link name="{self.real["rplidar"]["frame_id"]}">', self.urdf
        )

    def test_imu_frame_exists_in_urdf(self):
        self.assertIn(
            f'<link name="{self.real["icm42688"]["frame_id"]}">', self.urdf
        )

    def test_ultrasonic_frame_template_matches_urdf_macro(self):
        """URDF 里这四个 link 由 xacro 宏生成，只能比对模板本身。"""
        template = self.real["chassis_bridge"]["frame_id_template"]
        self.assertIn("{name}", template)
        self.assertIn(
            f'<link name="{template.replace("{name}", "${name}")}">', self.urdf
        )


class RealLaunchSafetyContractTest(unittest.TestCase):
    """实机节点退出必须终止 launch，不能留下预处理器制造假健康。"""

    def test_every_real_launch_node_is_required(self):
        root = ET.parse(REAL_LAUNCH).getroot()
        nodes = root.findall("node")
        self.assertTrue(nodes)
        self.assertEqual(
            [
                node.attrib.get("name")
                for node in nodes
                if node.attrib.get("required") != "true"
            ],
            [],
        )


if __name__ == "__main__":
    unittest.main()
