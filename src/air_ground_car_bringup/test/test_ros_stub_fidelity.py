#!/usr/bin/env python3
"""验证 ROS 替身与真实消息定义一致 —— 本测试只在有 ROS 的环境里跑。

`test/host/` 下那 69 个用例全部跑在 `ros_stub.py` 的假消息类上。
那些用例能证明驱动逻辑对，但证明不了**替身本身**对：
如果替身把 `angle_increment` 抄成了 `angle_step`，驱动跟着写 `angle_step`，
两边一致，测试全绿，实机上 LaserScan 的角分辨率永远是 0。

所以替身也要被验证。这一条由 catkin test 在 ROS 容器里跑，
拿真 `sensor_msgs` 的 `__slots__` 与替身逐字段比对。

（同一个检查不实现两处 —— 见 ADR-0008 §决策-3。驱动逻辑只在
lint-scripts job 里跑一次，本文件只管替身的字段忠实度。）
"""

import sys
import unittest
from pathlib import Path

from geometry_msgs.msg import Quaternion, Vector3
from sensor_msgs.msg import Imu, LaserScan
from std_msgs.msg import Header, String

HOST_TEST_DIRECTORY = Path(__file__).resolve().parent / "host"
sys.path.insert(0, str(HOST_TEST_DIRECTORY))

import ros_stub  # noqa: E402


class RosStubFidelityTest(unittest.TestCase):
    """替身消息类的字段集必须与真实消息定义完全相同。"""

    def assert_same_slots(self, stub_class, real_class):
        self.assertEqual(
            set(stub_class.__slots__),
            set(real_class.__slots__),
            f"{stub_class.__name__} 的替身字段与 {real_class._type} 不一致；"
            "改的是替身而不是驱动 —— 先对齐字段表再说",
        )

    def test_laser_scan(self):
        self.assert_same_slots(ros_stub.LaserScan, LaserScan)

    def test_imu(self):
        self.assert_same_slots(ros_stub.Imu, Imu)

    def test_header(self):
        self.assert_same_slots(ros_stub.Header, Header)

    def test_string(self):
        self.assert_same_slots(ros_stub.String, String)

    def test_vector3(self):
        self.assert_same_slots(ros_stub.Vector3, Vector3)

    def test_quaternion(self):
        self.assert_same_slots(ros_stub.Quaternion, Quaternion)

    def test_covariance_arrays_have_nine_entries(self):
        """协方差是 3×3 展平。替身给错长度的话，驱动填的下标会越界或漏填。"""
        stub_imu = ros_stub.Imu()
        real_imu = Imu()
        for field in (
            "orientation_covariance",
            "angular_velocity_covariance",
            "linear_acceleration_covariance",
        ):
            self.assertEqual(len(getattr(stub_imu, field)), 9)
            self.assertEqual(len(getattr(real_imu, field)), 9)


if __name__ == "__main__":
    unittest.main()
