#!/usr/bin/env python3
"""端到端数据流验证: 传感器原始消息 → Observation → WorldState。

    python3 test-observation-pipeline.py

退出码: 0 通过 / 1 失败 / 2 缺 numpy/OpenCV (报 SKIP)。

--------------------------------------------------------------------------
跑的是真代码, 不是"模拟核心逻辑"的替代实现
--------------------------------------------------------------------------

task-15 原文的这一项写了一个 `MockObservation` 和一个
`preprocess_to_observation()` —— 后者的注释是"模拟 car_preprocessor.py
的核心逻辑"。那样写的三个用例一定会通过, 因为它们测的是那二十行模拟件,
而不是仓库里那份 400 行的预处理器。

差距具体有多大: 真的 `CarPreprocessor.build_messages()` 里有

  · 新鲜度窗口 —— 超过 sensor_timeout 的传感器要从 modalities 里消失
  · LiDAR 降采样 —— 按 lidar_downsample 抽稀, 并同步放大 angle_increment
  · 无效距离标记 —— inf / NaN / <=0 一律写成 -1.0
  · 超声波限幅与缺失填充 —— 缺失时填 max_range, 不是 0
  · modalities 的生成规则 —— 由数据新鲜度决定, 不是由调用方传进来

这五条模拟件一条都没有, 而它们全是实机上真正会出问题的地方。

做法是复用 task-14 的 ROS 替身 (`test/host/ros_stub.py`): 消息类带
`__slots__`, 写错一个字段名当场 AttributeError; 替身自身的字段忠实度
由 `test_ros_stub_fidelity.py` 在 ROS 容器里拿真消息类比对。
唯一没有替身的是 `cv2` —— 那个用真的, 所以 JPEG 压缩这一段是真跑的。
"""

import os
import sys
import time
import unittest
from typing import Optional

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                         "..", "..", ".."))
CAR_PACKAGE = os.path.join(REPO_ROOT, "src", "air_ground_car_bringup")
SERVER_SCRIPTS = os.path.join(REPO_ROOT, "src", "air_ground_lab_server", "scripts")
HOST_TEST_DIR = os.path.join(CAR_PACKAGE, "test", "host")

# 替身必须在被测模块 import 之前装好 —— car_preprocessor 在 import 期就
# `import rospy`。这与 test/host/conftest.py 的做法一致。
sys.path.insert(0, HOST_TEST_DIR)
sys.path.insert(0, os.path.join(CAR_PACKAGE, "scripts"))
sys.path.insert(0, SERVER_SCRIPTS)

try:
    import numpy
    import cv2  # noqa: F401  car_preprocessor 用它做 JPEG 压缩, 这里用真的
    import yaml
except ImportError as error:  # pragma: no cover
    print(f"SKIP: 数据流验证需要 numpy + OpenCV + PyYAML ({error})", file=sys.stderr)
    raise SystemExit(2)

import ros_stub  # noqa: E402

ros_stub.install()

from car_preprocessor import CarPreprocessor, DIRECTIONS  # noqa: E402
from world_model import WorldModelStore  # noqa: E402

CAR_EDGE_YAML = os.path.join(CAR_PACKAGE, "config", "car_edge.yaml")


def load_car_edge_params() -> dict:
    """读那份**真的** car_edge.yaml。

    刻意不在测试里另抄一份参数字典 —— 抄一份的话, 测试验的是抄件,
    实机上加载的是 YAML, 两者漂移时测试照样全绿 (同 test/host/fixtures.py)。
    """
    with open(CAR_EDGE_YAML, "r", encoding="utf-8") as handle:
        return yaml.safe_load(handle)


class ObservationPipelineTest(unittest.TestCase):
    """驱动真的 CarPreprocessor 与真的 WorldModelStore。"""

    def setUp(self) -> None:
        """每个用例一份干净的参数空间与预处理器。"""
        self.stub = ros_stub.install()
        self.stub.params.clear()
        self.stub.params.update(load_car_edge_params())
        self.preprocessor = CarPreprocessor()
        self.subscribers = {sub.name: sub for sub in self.preprocessor.subscribers}

    # --- 构造输入 -----------------------------------------------------------

    def make_image(self, width: int = 32, height: int = 24):
        """构造一条 bgr8 的 sensor_msgs/Image。"""
        message = ros_stub.Image()
        message.height, message.width = height, width
        message.encoding = "bgr8"
        message.step = width * 3
        pixels = numpy.zeros((height, width, 3), dtype=numpy.uint8)
        pixels[:, : width // 2] = (0, 0, 255)      # 左半红 (BGR)
        message.data = pixels.tobytes()
        return message

    def make_scan(self, ranges, angle_min: float = -3.14159265,
                  angle_increment: float = 0.0174533):
        """构造一条 sensor_msgs/LaserScan。"""
        message = ros_stub.LaserScan()
        message.ranges = list(ranges)
        message.angle_min = angle_min
        message.angle_increment = angle_increment
        message.range_min, message.range_max = 0.15, 12.0
        return message

    def make_imu(self, gyro=(0.01, 0.02, 0.03), accel=(0.1, 0.2, 9.8)):
        """构造一条 sensor_msgs/Imu。"""
        message = ros_stub.Imu()
        message.angular_velocity = ros_stub.Vector3(x=gyro[0], y=gyro[1], z=gyro[2])
        message.linear_acceleration = ros_stub.Vector3(x=accel[0], y=accel[1], z=accel[2])
        return message

    def make_ultrasonic(self, distance: float):
        """超声波在本项目里用 LaserScan 单波束表达 (task-14 ADR-0009)。"""
        message = ros_stub.LaserScan()
        message.ranges = [distance]
        return message

    def make_odometry(self, x: float = 1.5, vx: float = 0.4):
        """构造一条 nav_msgs/Odometry。"""
        message = ros_stub.Odometry()
        message.pose.pose.position.x = x
        message.twist.twist.linear.x = vx
        return message

    def feed(self, topic: str, message) -> None:
        """把消息喂给预处理器**真正订阅的**那个回调。

        走 subscribers 而不是直接调回调, 顺带验证了话题名 —— 话题名写错时
        这里 KeyError, 而不是悄悄测了个不存在的通路。
        """
        self.subscribers[topic].feed(message)

    def feed_all_modalities(self) -> None:
        """喂满四个模态 + 里程计。"""
        topics = self.preprocessor.topics
        self.feed(topics["image"], self.make_image())
        self.feed(topics["scan"], self.make_scan([1.0] * 360))
        self.feed(topics["imu"], self.make_imu())
        self.feed(topics["odometry"], self.make_odometry())
        prefix = str(topics["ultrasonic_prefix"]).rstrip("/")
        for index, direction in enumerate(DIRECTIONS):
            self.feed(f"{prefix}/{direction}", self.make_ultrasonic(0.3 + 0.1 * index))

    def build(self):
        """跑一次真的 build_messages()。"""
        return self.preprocessor.build_messages(ros_stub.Time.from_sec(100.0))

    # --- 用例 ---------------------------------------------------------------

    def test_01_full_multimodal_observation(self):
        """四个模态齐全时, Observation 的每个字段都要对。"""
        self.feed_all_modalities()
        observation, state = self.build()

        self.assertEqual(observation.robot_id, "car")
        # 顺序是 build_messages 里的填充顺序, 不是集合 —— 顺序变了下游
        # 按下标取模态的代码会错位, 所以钉死
        self.assertEqual(observation.modalities, ["rgb", "lidar_2d", "imu", "ultrasonic"])
        self.assertEqual(observation.header.frame_id, self.preprocessor.frame_id)

        # LiDAR: 360 点按 lidar_downsample=4 抽稀 → 90 点
        downsample = self.preprocessor.lidar_downsample
        self.assertEqual(len(observation.lidar_ranges), 360 // downsample)
        # angle_increment 必须同步放大, 否则下游按点序号还原角度会错 4 倍
        self.assertAlmostEqual(observation.lidar_angle_increment,
                               0.0174533 * downsample, places=9)

        # 超声波: ICD §2.1 规定顺序是 front, rear, left, right
        self.assertEqual(len(observation.ultrasonic_ranges), len(DIRECTIONS))
        self.assertAlmostEqual(observation.ultrasonic_ranges[0], 0.3, places=6)
        self.assertAlmostEqual(observation.ultrasonic_ranges[3], 0.6, places=6)

        # IMU 直接透传
        self.assertAlmostEqual(observation.angular_velocity.z, 0.03, places=6)
        self.assertAlmostEqual(observation.linear_acceleration.z, 9.8, places=6)

        # RGB 是真的被 OpenCV 压成 JPEG 了 (cv2 没有替身)
        self.assertEqual(observation.rgb.format, "jpeg")
        self.assertTrue(observation.rgb.data.startswith(b"\xff\xd8"),
                        "JPEG 必须以 SOI 标记 FFD8 开头")
        decoded = cv2.imdecode(numpy.frombuffer(observation.rgb.data, numpy.uint8),
                               cv2.IMREAD_COLOR)
        self.assertEqual(decoded.shape, (24, 32, 3))

        self.assertEqual(state.robot_id, "car")
        self.assertTrue(state.is_connected)
        self.assertAlmostEqual(state.pose.position.x, 1.5, places=6)
        self.assertAlmostEqual(state.velocity.linear.x, 0.4, places=6)

    def test_02_stale_sensor_drops_out_of_modalities(self):
        """超过 sensor_timeout 的传感器必须从 modalities 里消失。

        这是实机上最常见的失败方式: 雷达线松了, 节点不报错, Observation
        里 lidar_ranges 保持着几秒前的旧值 —— 下游拿它当当前障碍物。
        新鲜度窗口就是为了让"没数据"表现成"没这个模态"而不是"旧数据"。
        """
        self.feed_all_modalities()
        # 把雷达的更新时间往前推到超时窗口之外
        stale = time.monotonic() - self.preprocessor.sensor_timeout - 1.0
        self.preprocessor.updated_at["scan"] = stale

        observation, _ = self.build()
        self.assertNotIn("lidar_2d", observation.modalities)
        self.assertEqual(observation.lidar_ranges, [])
        self.assertIn("imu", observation.modalities)

    def test_03_invalid_lidar_ranges_become_minus_one(self):
        """inf / NaN / <=0 一律写成 -1.0, 不能原样透传。

        inf 透传下去, 下游算最近障碍物时会得到 inf, 于是"前方无障碍";
        0 透传下去则是"贴脸有障碍"。两个方向的错都很贵。
        """
        ranges = [1.0] * 360
        ranges[0] = float("inf")
        ranges[4] = float("nan")
        ranges[8] = 0.0
        ranges[12] = -2.0
        self.feed(self.preprocessor.topics["scan"], self.make_scan(ranges))
        observation, _ = self.build()

        # downsample=4, 所以原始下标 0/4/8/12 对应输出下标 0/1/2/3
        self.assertEqual(observation.lidar_ranges[:4], [-1.0, -1.0, -1.0, -1.0])
        self.assertAlmostEqual(observation.lidar_ranges[4], 1.0, places=6)

    def test_04_ultrasonic_clamped_and_filled(self):
        """超声波要限幅到 [min, max]; 缺的那一路填 max, 不是 0。

        填 0 的话下游会读成"贴着障碍物", 一个没接线的探头能让车永远不敢动。
        """
        prefix = str(self.preprocessor.topics["ultrasonic_prefix"]).rstrip("/")
        self.feed(f"{prefix}/front", self.make_ultrasonic(99.0))    # 超上限
        self.feed(f"{prefix}/rear", self.make_ultrasonic(0.001))    # 低于下限
        # left / right 不喂 —— 模拟没接线
        observation, _ = self.build()

        maximum = self.preprocessor.ultrasonic_maximum
        minimum = self.preprocessor.ultrasonic_minimum
        self.assertAlmostEqual(observation.ultrasonic_ranges[0], maximum, places=6)
        self.assertAlmostEqual(observation.ultrasonic_ranges[1], minimum, places=6)
        self.assertAlmostEqual(observation.ultrasonic_ranges[2], maximum, places=6)
        self.assertAlmostEqual(observation.ultrasonic_ranges[3], maximum, places=6)

    def test_05_empty_observation_is_honest(self):
        """一路传感器都没有时, modalities 为空且 is_connected 为 false。"""
        observation, state = self.build()
        self.assertEqual(observation.modalities, [])
        self.assertEqual(observation.robot_id, "car")
        self.assertFalse(state.is_connected)

    def test_06_publishers_receive_declared_types(self):
        """定时器一响, 两个发布器都要收到声明类型的消息。

        FakePublisher 会做类型检查 —— 发错类型在真 ROS 上是运行期异常,
        在这里是当场 TypeError。
        """
        self.feed_all_modalities()
        self.preprocessor.timer.fire()

        observation_pub = self.preprocessor.observation_publisher
        state_pub = self.preprocessor.state_publisher
        self.assertEqual(len(observation_pub.published), 1)
        self.assertEqual(len(state_pub.published), 1)
        self.assertIsInstance(observation_pub.published[0], ros_stub.Observation)
        self.assertIsInstance(state_pub.published[0], ros_stub.RobotState)

    def test_07_capability_is_latched_on_startup(self):
        """能力声明在构造时就锁存发布一次 —— 服务端靠它做任务分配。"""
        published = self.preprocessor.capability_publisher.published
        self.assertEqual(len(published), 1)
        capability = published[0]
        self.assertEqual(capability.robot_id, "car")
        self.assertEqual(capability.locomotion_type, "ground_wheeled")
        self.assertIn("lidar_2d", capability.sensor_modalities)

    # --- 到 World Model 为止 -------------------------------------------------

    def test_10_observation_reaches_world_model(self):
        """Observation / RobotState 进真的 WorldModelStore, 出 WorldState。"""
        self.feed_all_modalities()
        observation, state = self.build()

        store = WorldModelStore(state_timeout=5.0)
        now = ros_stub.Time.from_sec(100.0)
        store.update_observation("car", observation, now)
        store.update_state("car", state, now)

        snapshot = store.snapshot(now)
        self.assertEqual([agent.robot_id for agent in snapshot.agents], ["car"])
        self.assertEqual(snapshot.header.frame_id, "map")
        self.assertAlmostEqual(snapshot.last_update_perception.to_sec(), 100.0, places=6)

    def test_11_two_agents_are_sorted(self):
        """两台机器人同时在线时, WorldState.agents 按 robot_id 排序。

        排序是稳定输出的前提: 下游若按下标取 agents[0], 顺序不定就会
        时而拿到车、时而拿到无人机, 而且是间歇性的。
        """
        self.feed_all_modalities()
        _, car_state = self.build()

        drone_state = ros_stub.RobotState()
        drone_state.robot_id = "drone"
        drone_state.header.stamp = ros_stub.Time.from_sec(100.0)

        store = WorldModelStore(state_timeout=5.0)
        now = ros_stub.Time.from_sec(100.0)
        store.update_state("car", car_state, now)
        store.update_state("drone", drone_state, now)

        snapshot = store.snapshot(now)
        self.assertEqual([agent.robot_id for agent in snapshot.agents], ["car", "drone"])

    def test_12_stale_agent_disappears_from_world_state(self):
        """超过 state_timeout 的 agent 要从 WorldState 里消失。

        与预处理器那一层的新鲜度窗口是同一个道理, 只是尺度更大: 一台掉线的
        车不该继续以最后一次位姿出现在世界模型里 —— 规划器会绕着一个
        并不在那里的障碍物走。
        """
        self.feed_all_modalities()
        _, state = self.build()

        store = WorldModelStore(state_timeout=5.0)
        store.update_state("car", state, ros_stub.Time.from_sec(100.0))

        self.assertEqual(len(store.snapshot(ros_stub.Time.from_sec(104.0)).agents), 1)
        self.assertEqual(len(store.snapshot(ros_stub.Time.from_sec(106.0)).agents), 0)


def main(argv: Optional[list] = None) -> int:
    """命令行入口。"""
    suite = unittest.TestLoader().loadTestsFromTestCase(ObservationPipelineTest)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
