#!/usr/bin/env python3
"""HC-SR04 驱动骨架单元测试 —— 无 ROS、无 roscore、无硬件。"""

import unittest

from fixtures import configure
from hcsr04_driver import HCSR04Driver, echo_to_distance
from mock_hardware import MockGPIO

# real_sensors.yaml 里 front 路的 echo 引脚
FRONT_ECHO_PIN = 27
FRONT_TRIG_PIN = 17


class EchoToDistanceTest(unittest.TestCase):
    """回波宽度 → 距离。少除以 2 的话所有距离都会翻倍。"""

    def test_round_trip_is_halved(self):
        # 5820μs 往返 ≈ 1m
        self.assertAlmostEqual(echo_to_distance(5820.0, 343.0), 0.998, places=3)

    def test_timeout_becomes_infinity(self):
        self.assertEqual(echo_to_distance(-1.0, 343.0), float("inf"))

    def test_speed_of_sound_is_a_parameter(self):
        """声速随温度变化约 0.6 m/s/摄氏度，不能写死在代码里。"""
        warm = echo_to_distance(5820.0, 349.0)
        cold = echo_to_distance(5820.0, 343.0)
        self.assertGreater(warm, cold)


class HCSR04DriverTest(unittest.TestCase):
    """驱动整体：参数取自 real_sensors.yaml，回波取自 MockGPIO。"""

    def setUp(self):
        self.rospy = configure()

    def test_sets_up_all_pins(self):
        gpio = MockGPIO()
        driver = HCSR04Driver(gpio)
        driver.setup_pins()
        self.assertEqual(len(gpio.directions), 8)
        self.assertEqual(gpio.directions[FRONT_TRIG_PIN], "out")
        self.assertEqual(gpio.directions[FRONT_ECHO_PIN], "in")

    def test_measure_converts_echo_to_metres(self):
        driver = HCSR04Driver(MockGPIO({FRONT_ECHO_PIN: 5820.0}))
        self.assertAlmostEqual(driver.measure("front"), 0.998, places=3)

    def test_timeout_becomes_infinity(self):
        """没有回波 = inf。夹成 max_range 会被下游读成'4 米处有墙'。"""
        driver = HCSR04Driver(MockGPIO())
        self.assertEqual(driver.measure("front"), float("inf"))

    def test_below_min_range_becomes_infinity(self):
        # 50μs 往返 ≈ 8.6mm, 小于 20mm 量程下限
        driver = HCSR04Driver(MockGPIO({FRONT_ECHO_PIN: 50.0}))
        self.assertEqual(driver.measure("front"), float("inf"))

    def test_trigger_pulse_width_comes_from_config(self):
        """10μs 触发脉冲由 HAL 负责，驱动只把宽度传下去 (ADR-0009)。"""
        gpio = MockGPIO({FRONT_ECHO_PIN: 5820.0})
        driver = HCSR04Driver(gpio)
        driver.measure("front")
        trig, echo, pulse_us, timeout_us = gpio.triggers[0]
        self.assertEqual((trig, echo), (FRONT_TRIG_PIN, FRONT_ECHO_PIN))
        self.assertEqual(pulse_us, 10.0)
        self.assertEqual(timeout_us, 30000.0)

    def test_scan_message_matches_single_beam_convention(self):
        """与仿真侧单束射线一致：一个 range，张角为零。"""
        driver = HCSR04Driver(MockGPIO({FRONT_ECHO_PIN: 5820.0}))
        _name, scan = driver.step()
        self.assertEqual(len(scan.ranges), 1)
        self.assertEqual(scan.angle_min, 0.0)
        self.assertEqual(scan.angle_max, 0.0)
        self.assertEqual(scan.header.frame_id, "ultrasonic_front_link")
        self.assertEqual(scan.range_min, 0.02)
        self.assertEqual(scan.range_max, 4.0)

    def test_directions_are_triggered_round_robin(self):
        """四路同时发射会互相听到对方的回波，必须轮流。"""
        gpio = MockGPIO()
        driver = HCSR04Driver(gpio)
        fired = [driver.step()[0] for _ in range(8)]
        self.assertEqual(fired[:4], ["front", "rear", "left", "right"])
        self.assertEqual(fired[4:], ["front", "rear", "left", "right"])
        # 每次 step 只触发一路
        self.assertEqual(len(gpio.triggers), 8)

    def test_publisher_topic_per_direction(self):
        driver = HCSR04Driver(MockGPIO())
        self.assertEqual(
            sorted(publisher.name for publisher in driver.publishers.values()),
            [
                "/car/ultrasonic/front",
                "/car/ultrasonic/left",
                "/car/ultrasonic/rear",
                "/car/ultrasonic/right",
            ],
        )

    def test_rejects_missing_pin_definition(self):
        stub = configure()
        del stub.params["hcsr04"]["pins"]["left"]
        with self.assertRaises(ValueError):
            HCSR04Driver(MockGPIO())


if __name__ == "__main__":
    unittest.main()
