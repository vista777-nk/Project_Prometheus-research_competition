#!/usr/bin/env python3
"""RPLIDAR A1 驱动骨架单元测试 —— 无 ROS、无 roscore、无硬件。"""

import math
import unittest

from fixtures import configure
from mock_hardware import MockUART
from rplidar_driver import (
    NODE_SIZE,
    SCAN_DESCRIPTOR,
    RPLidarDriver,
    ScanAccumulator,
    angle_to_index,
    parse_scan_node,
)


def make_node(
    angle_deg: float,
    distance_m: float,
    quality: int = 47,
    start: bool = False,
) -> bytes:
    """按 RPLIDAR 标准扫描格式打包一个 5 字节采样点。"""
    angle_q6 = int(round(angle_deg * 64.0))
    distance_q2 = int(round(distance_m * 4000.0))
    b0 = (quality << 2) | (0x01 if start else 0x02)
    b1 = ((angle_q6 & 0x7F) << 1) | 0x01
    b2 = (angle_q6 >> 7) & 0xFF
    return bytes((b0, b1, b2, distance_q2 & 0xFF, (distance_q2 >> 8) & 0xFF))


def make_revolution(samples: int, distance_m: float = 1.0) -> bytes:
    """打包一整圈采样点（首点带 start 标志）。"""
    step = 360.0 / samples
    return b"".join(
        make_node(i * step, distance_m, start=(i == 0)) for i in range(samples)
    )


class ParseScanNodeTest(unittest.TestCase):
    """协议解析是纯函数，先单独把它钉死。"""

    def test_parses_angle_and_distance(self):
        node = parse_scan_node(make_node(90.0, 2.5, quality=47, start=True))
        self.assertIsNotNone(node)
        self.assertTrue(node.start)
        self.assertEqual(node.quality, 47)
        self.assertAlmostEqual(node.angle_deg, 90.0, places=2)
        self.assertAlmostEqual(node.distance_m, 2.5, places=4)

    def test_rejects_unaligned_start_bits(self):
        """S 与 ~S 相同 = 字节流没对齐到节点边界。"""
        raw = bytearray(make_node(10.0, 1.0))
        raw[0] = (raw[0] & 0xFC) | 0x03
        self.assertIsNone(parse_scan_node(bytes(raw)))

    def test_rejects_missing_check_bit(self):
        raw = bytearray(make_node(10.0, 1.0))
        raw[1] &= 0xFE
        self.assertIsNone(parse_scan_node(bytes(raw)))

    def test_rejects_short_frame(self):
        self.assertIsNone(parse_scan_node(b"\x00" * (NODE_SIZE - 1)))


class AngleToIndexTest(unittest.TestCase):
    """顺时针→逆时针那个负号，是实机上最容易漏且最难看出来的一处。"""

    SAMPLES = 360
    ANGLE_MIN = -math.pi
    INCREMENT = 2.0 * math.pi / 360

    def index(self, angle_deg: float) -> int:
        return angle_to_index(
            angle_deg, self.SAMPLES, self.ANGLE_MIN, self.INCREMENT
        )

    def test_front_maps_to_zero_radian_bin(self):
        self.assertEqual(self.index(0.0), 180)

    def test_lidar_clockwise_becomes_ros_counterclockwise(self):
        # 雷达顺时针 90° = ROS 的 -90°(右侧) = 下标 90
        self.assertEqual(self.index(90.0), 90)
        # 雷达顺时针 270° = ROS 的 +90°(左侧) = 下标 270
        self.assertEqual(self.index(270.0), 270)

    def test_wraps_within_bounds(self):
        for angle in (0.0, 0.5, 359.5, 360.0, 719.0):
            self.assertTrue(0 <= self.index(angle) < self.SAMPLES)


class ScanAccumulatorTest(unittest.TestCase):
    """无效读数必须变成 inf，不能变成 0.0。"""

    def build(self):
        return ScanAccumulator(
            samples=360,
            angle_min=-math.pi,
            angle_increment=2.0 * math.pi / 360,
            min_range=0.15,
            max_range=12.0,
        )

    def test_zero_quality_becomes_infinity(self):
        accumulator = self.build()
        accumulator.push(parse_scan_node(make_node(0.0, 3.0, quality=0)))
        self.assertEqual(accumulator.ranges[180], float("inf"))

    def test_out_of_range_becomes_infinity(self):
        accumulator = self.build()
        accumulator.push(parse_scan_node(make_node(0.0, 15.0)))
        self.assertEqual(accumulator.ranges[180], float("inf"))
        accumulator.push(parse_scan_node(make_node(180.0, 0.01)))
        self.assertEqual(accumulator.ranges[0], float("inf"))

    def test_emits_previous_revolution_on_start_flag(self):
        accumulator = self.build()
        for angle in (0.0, 1.0, 2.0):
            self.assertIsNone(
                accumulator.push(parse_scan_node(make_node(angle, 1.0)))
            )
        completed = accumulator.push(
            parse_scan_node(make_node(3.0, 1.0, start=True))
        )
        self.assertIsNotNone(completed)
        self.assertEqual(len(completed[0]), 360)
        self.assertEqual(accumulator.point_count, 1)


class RPLidarDriverTest(unittest.TestCase):
    """驱动整体：参数取自 real_sensors.yaml，数据取自 MockUART。"""

    def setUp(self):
        self.rospy = configure()

    def test_start_scan_command_is_sent(self):
        uart = MockUART()
        driver = RPLidarDriver(uart)
        self.assertTrue(driver.connect())
        self.assertIn(b"\xa5\x20", uart.written)

    def test_publishes_one_scan_per_revolution(self):
        samples = 360
        # connect() 会先把 7 字节应答描述符读走, mock 流里必须带上它
        stream = (
            SCAN_DESCRIPTOR
            + make_revolution(samples, 1.0)
            + make_node(0.0, 1.0, start=True)
        )
        driver = RPLidarDriver(MockUART(stream))
        driver.read_chunk = len(stream)

        scans = driver.step()
        self.assertEqual(len(scans), 1)
        scan = scans[0]
        self.assertEqual(len(scan.ranges), samples)
        self.assertEqual(scan.header.frame_id, "lidar_link")
        self.assertAlmostEqual(scan.angle_min, -math.pi, places=6)
        self.assertAlmostEqual(scan.angle_increment, 2.0 * math.pi / samples)
        # angle_max 是**最后一束**的角度, 不是张角上界; 差一个 increment
        self.assertAlmostEqual(
            scan.angle_max, -math.pi + scan.angle_increment * (samples - 1)
        )
        self.assertEqual(scan.range_min, 0.15)
        self.assertEqual(scan.range_max, 12.0)
        self.assertAlmostEqual(scan.ranges[180], 1.0, places=3)

    def test_resyncs_after_garbage_bytes(self):
        """数据流开头的半帧不能把整条流带偏。"""
        samples = 360
        stream = (
            SCAN_DESCRIPTOR
            + b"\xff\xfe"
            + make_revolution(samples, 2.0)
            + make_node(0.0, 2.0, start=True)
        )
        driver = RPLidarDriver(MockUART(stream))
        driver.read_chunk = len(stream)
        scans = driver.step()
        self.assertEqual(len(scans), 1)
        self.assertAlmostEqual(scans[0].ranges[180], 2.0, places=3)

    def test_open_failure_does_not_raise(self):
        class FailingUART(MockUART):
            def open(self, port, baudrate):
                self.open_calls += 1
                return False

        uart = FailingUART()
        driver = RPLidarDriver(uart)
        self.assertEqual(driver.step(), [])
        self.assertFalse(uart.is_open)
        self.assertTrue(
            any("打开" in message for _level, message in self.rospy.logs)
        )

    def test_stall_triggers_reconnect(self):
        """读不到字节超过 data_timeout 就得断开重连，而不是静静地不发数据。"""
        uart = MockUART()
        driver = RPLidarDriver(uart)
        self.assertTrue(driver.connect())
        driver._last_data_at -= driver.data_timeout + 1.0
        self.assertEqual(driver.step(), [])
        self.assertFalse(uart.is_open)

    def test_rejects_config_missing_keys(self):
        stub = configure()
        del stub.params["rplidar"]["max_range"]
        with self.assertRaises(ValueError):
            RPLidarDriver(MockUART())

    def test_rejects_inverted_range(self):
        configure({"rplidar/max_range": 0.1})
        with self.assertRaises(ValueError):
            RPLidarDriver(MockUART())


if __name__ == "__main__":
    unittest.main()
