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

import rospy
from air_ground_interfaces.msg import (
    Capability,
    Observation,
    RobotState,
    SemanticLandmark,
    WorldState,
)
from geometry_msgs.msg import (
    Point,
    Pose,
    PoseWithCovariance,
    Quaternion,
    Twist,
    TwistWithCovariance,
    Vector3,
)
from nav_msgs.msg import MapMetaData, OccupancyGrid, Odometry
from sensor_msgs.msg import CompressedImage, Image, Imu, LaserScan
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

    # --- task-15 新增的替身 (Observation 数据流验证要用真的预处理器) ---------

    def test_image(self):
        self.assert_same_slots(ros_stub.Image, Image)

    def test_compressed_image(self):
        self.assert_same_slots(ros_stub.CompressedImage, CompressedImage)

    def test_point(self):
        self.assert_same_slots(ros_stub.Point, Point)

    def test_pose(self):
        self.assert_same_slots(ros_stub.Pose, Pose)

    def test_twist(self):
        self.assert_same_slots(ros_stub.Twist, Twist)

    def test_pose_with_covariance(self):
        self.assert_same_slots(ros_stub.PoseWithCovariance, PoseWithCovariance)

    def test_twist_with_covariance(self):
        self.assert_same_slots(ros_stub.TwistWithCovariance, TwistWithCovariance)

    def test_odometry(self):
        self.assert_same_slots(ros_stub.Odometry, Odometry)

    def test_map_meta_data(self):
        self.assert_same_slots(ros_stub.MapMetaData, MapMetaData)

    def test_occupancy_grid(self):
        self.assert_same_slots(ros_stub.OccupancyGrid, OccupancyGrid)

    def test_observation(self):
        self.assert_same_slots(ros_stub.Observation, Observation)

    def test_robot_state(self):
        self.assert_same_slots(ros_stub.RobotState, RobotState)

    def test_capability(self):
        self.assert_same_slots(ros_stub.Capability, Capability)

    def test_world_state(self):
        self.assert_same_slots(ros_stub.WorldState, WorldState)

    def test_semantic_landmark(self):
        self.assert_same_slots(ros_stub.SemanticLandmark, SemanticLandmark)

    def test_time_and_duration_behave_like_rospy(self):
        """Time/Duration 的**行为**要一致，不只是字段名一致。

        字段名比对抓不到这一类差异：真 rospy 的 `Duration(1.0/10.0)` 收
        位置参数、且接受浮点秒。替身早先只收关键字，于是
        `rospy.Duration(1.0 / publish_rate)` —— car_preprocessor.py 的原话 ——
        在替身上直接 TypeError。这条就是从那次失败里长出来的。
        """
        for stub_class, real_class in ((ros_stub.Duration, rospy.Duration),
                                       (ros_stub.Time, rospy.Time)):
            stub_value = stub_class(0.1)
            real_value = real_class(0.1)
            self.assertAlmostEqual(stub_value.to_sec(), real_value.to_sec(), places=9,
                                   msg=f"{stub_class.__name__}(0.1) 与真值不一致")
            self.assertEqual(stub_value.secs, real_value.secs)
            self.assertEqual(stub_value.nsecs, real_value.nsecs)

        # 比较与相减 —— world_model.py 判新鲜度靠这两个
        self.assertTrue(ros_stub.Time(2.0) > ros_stub.Time(1.0))
        self.assertTrue(rospy.Time(2.0) > rospy.Time(1.0))
        self.assertAlmostEqual((ros_stub.Time(5.0) - ros_stub.Time(2.0)).to_sec(), 3.0)
        self.assertAlmostEqual((rospy.Time(5.0) - rospy.Time(2.0)).to_sec(), 3.0)

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

    def test_pose_covariance_arrays_have_thirty_six_entries(self):
        """Odometry 的位姿/速度协方差是 6×6 展平。"""
        for stub_class, real_class in ((ros_stub.PoseWithCovariance, PoseWithCovariance),
                                       (ros_stub.TwistWithCovariance, TwistWithCovariance)):
            self.assertEqual(len(stub_class().covariance), 36)
            self.assertEqual(len(real_class().covariance), 36)


if __name__ == "__main__":
    unittest.main()
