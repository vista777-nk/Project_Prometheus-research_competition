#!/usr/bin/env python3
"""ICM42688-P 驱动骨架单元测试 —— 无 ROS、无 roscore、无硬件。"""

import math
import unittest

from fixtures import configure
from icm42688_driver import (
    REG_ACCEL_CONFIG0,
    REG_ACCEL_DATA_X1,
    REG_GYRO_CONFIG0,
    REG_WHO_AM_I,
    WHO_AM_I_VALUE,
    ICM42688Driver,
    decode_imu_frame,
    parse_axis_mapping,
    remap_vector,
    to_int16,
)
from mock_hardware import MockI2C

GRAVITY = 9.80665


def pack_int16(value: int) -> bytes:
    """按芯片的大端顺序打包一个有符号 16 位量。"""
    return bytes(((value >> 8) & 0xFF, value & 0xFF))


def seed_i2c(accel_counts, gyro_counts) -> MockI2C:
    """构造一个装着指定原始计数的假 I2C 设备。"""
    registers = {REG_WHO_AM_I: WHO_AM_I_VALUE}
    payload = b"".join(pack_int16(v) for v in list(accel_counts) + list(gyro_counts))
    for offset, byte in enumerate(payload):
        registers[REG_ACCEL_DATA_X1 + offset] = byte
    return MockI2C(registers)


class ToInt16Test(unittest.TestCase):
    """补码转换写错的话，静止时的重力会变成 -9.8。"""

    def test_positive(self):
        self.assertEqual(to_int16(0x20, 0x00), 8192)

    def test_negative(self):
        self.assertEqual(to_int16(0xFF, 0xFF), -1)
        self.assertEqual(to_int16(0x80, 0x00), -32768)


class DecodeImuFrameTest(unittest.TestCase):
    """量纲换算：LSB → m/s² 与 LSB → rad/s。"""

    def test_converts_to_si_units(self):
        # ±4g 量程 = 8192 LSB/g；±500dps 量程 = 65.5 LSB/dps
        raw = b"".join(
            pack_int16(v) for v in (8192, 0, -8192, 6550, 0, -655)
        )
        accel, gyro = decode_imu_frame(raw, 8192.0, 65.5)
        self.assertAlmostEqual(accel[0], GRAVITY, places=4)
        self.assertAlmostEqual(accel[1], 0.0, places=6)
        self.assertAlmostEqual(accel[2], -GRAVITY, places=4)
        self.assertAlmostEqual(gyro[0], math.radians(100.0), places=5)
        self.assertAlmostEqual(gyro[2], math.radians(-10.0), places=5)

    def test_rejects_short_frame(self):
        with self.assertRaises(ValueError):
            decode_imu_frame(b"\x00" * 11, 8192.0, 65.5)

    def test_mount_axis_mapping_obeys_rep103(self):
        mapping = parse_axis_mapping(["y", "x", "-z"])
        self.assertEqual(remap_vector((1.0, 2.0, 3.0), mapping), (2.0, 1.0, -3.0))

    def test_axis_mapping_rejects_duplicate_source_axis(self):
        with self.assertRaises(ValueError):
            parse_axis_mapping(["x", "x", "-z"])


class ICM42688DriverTest(unittest.TestCase):
    """驱动整体：参数取自 real_sensors.yaml，寄存器取自 MockI2C。"""

    def setUp(self):
        self.rospy = configure()

    def test_writes_range_and_odr_registers(self):
        """量程写错的话，读数会整体差一个倍数 —— 而且看上去很正常。"""
        i2c = seed_i2c((0, 0, 0), (0, 0, 0))
        driver = ICM42688Driver(i2c)
        self.assertTrue(driver.connect())
        # ±4g → ACCEL_FS_SEL=2, 100Hz → ODR=0x08
        self.assertEqual(i2c.registers[REG_ACCEL_CONFIG0], (2 << 5) | 0x08)
        # ±500dps → GYRO_FS_SEL=2
        self.assertEqual(i2c.registers[REG_GYRO_CONFIG0], (2 << 5) | 0x08)

    def test_step_publishes_si_units(self):
        # 板上 Z 轴朝下；静止时原始 Z=-1g，映射后 base_link Z=+1g。
        i2c = seed_i2c((0, 0, -8192), (0, 0, 0))
        driver = ICM42688Driver(i2c)
        message = driver.step()
        self.assertIsNotNone(message)
        self.assertEqual(message.header.frame_id, "imu_link")
        self.assertAlmostEqual(message.linear_acceleration.z, GRAVITY, places=4)
        self.assertAlmostEqual(message.angular_velocity.x, 0.0, places=6)

    def test_orientation_marked_unavailable(self):
        """6 轴没有姿态。不写 -1 的话，单位四元数会被当成'姿态就是零旋转'。"""
        driver = ICM42688Driver(seed_i2c((0, 0, 0), (0, 0, 0)))
        message = driver.step()
        self.assertEqual(message.orientation_covariance[0], -1.0)

    def test_covariance_from_configured_noise(self):
        driver = ICM42688Driver(seed_i2c((0, 0, 0), (0, 0, 0)))
        message = driver.step()
        self.assertAlmostEqual(
            message.angular_velocity_covariance[0], 0.0005 ** 2
        )
        self.assertAlmostEqual(
            message.linear_acceleration_covariance[8], 0.001 ** 2
        )

    def test_wrong_who_am_i_fails_closed_before_writes(self):
        i2c = seed_i2c((0, 0, 0), (0, 0, 0))
        i2c.registers[REG_WHO_AM_I] = 0x12
        driver = ICM42688Driver(i2c)
        self.assertFalse(driver.connect())
        self.assertFalse(i2c.is_open)
        self.assertEqual(i2c.writes, [])
        self.assertTrue(
            any("WHO_AM_I" in message for _level, message in self.rospy.logs)
        )

    def test_read_failure_triggers_reconnect(self):
        """I2C 挂死时必须断开重连，而不是一直抛异常或静默发零。"""

        class HangingI2C(MockI2C):
            def read_register(self, reg, length):
                if reg == REG_ACCEL_DATA_X1:
                    raise OSError(121, "Remote I/O error")
                return super().read_register(reg, length)

        i2c = HangingI2C({REG_WHO_AM_I: WHO_AM_I_VALUE})
        driver = ICM42688Driver(i2c)
        self.assertIsNone(driver.step())
        self.assertFalse(i2c.is_open)
        self.assertTrue(
            any("重连" in message for _level, message in self.rospy.logs)
        )

    def test_rejects_unsupported_range(self):
        configure({"icm42688/accel_range_g": 3})
        with self.assertRaises(ValueError):
            ICM42688Driver(MockI2C())

    def test_rejects_invalid_axis_mapping(self):
        configure({"icm42688/axis_mapping": ["x", "y"]})
        with self.assertRaises(ValueError):
            ICM42688Driver(MockI2C())


if __name__ == "__main__":
    unittest.main()
