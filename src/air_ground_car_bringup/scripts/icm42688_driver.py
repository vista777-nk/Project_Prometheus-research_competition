#!/usr/bin/env python3
"""ICM42688-P 6 轴 IMU 驱动 —— ROS/量纲层与 Linux I2C 访问层已实现。

发布: /car/imu/data (sensor_msgs/Imu)   ← 与 Gazebo 仿真同一话题
配置: config/real_sensors.yaml §icm42688

话题名是 `/car/imu/data` 而不是 `/car/imu`: 仿真侧
`libgazebo_ros_imu_sensor.so` 发的是前者，`car_preprocessor.py` 订的也是前者。
差一个 `/data` 就是实机上 Observation 永远没有 imu 模态，且没有任何报错。
"""

import math
import os
import sys
import time
from typing import Tuple

import rospy
from sensor_msgs.msg import Imu

# 见 rplidar_driver.py 同处注释：避开 catkin devel relay 的同名自导入。
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from hardware_interface import I2CInterface, create_i2c  # noqa: E402
from sensor_config import (  # noqa: E402
    load_section,
    positive_float,
    require_keys,
    resolve_backend,
)

SECTION = "icm42688"
REQUIRED_KEYS = (
    "bus", "address", "topic", "frame_id", "update_rate",
    "accel_range_g", "gyro_range_dps", "reconnect_interval",
    "gyro_noise_stddev", "accel_noise_stddev",
)

# --- 寄存器 (User Bank 0, 见 ICM-42688-P Datasheet §14) ---
REG_PWR_MGMT0 = 0x4E
REG_GYRO_CONFIG0 = 0x4F
REG_ACCEL_CONFIG0 = 0x50
REG_ACCEL_DATA_X1 = 0x1F   # 0x1F..0x2A 连续 12 字节 = 加速度 3 轴 + 陀螺 3 轴
REG_WHO_AM_I = 0x75
WHO_AM_I_VALUE = 0x47
BURST_LENGTH = 12

# 加速度计 / 陀螺仪同时进入 Low Noise 模式
PWR_MGMT0_LN = 0x0F

# 量程 → (FS_SEL 编码, 灵敏度 LSB)
ACCEL_RANGE_TABLE = {16: (0, 2048.0), 8: (1, 4096.0), 4: (2, 8192.0), 2: (3, 16384.0)}
GYRO_RANGE_TABLE = {
    2000: (0, 16.4), 1000: (1, 32.8), 500: (2, 65.5), 250: (3, 131.0),
    125: (4, 262.0),
}
# ODR 编码 (GYRO_CONFIG0/ACCEL_CONFIG0 低 4 位)
ODR_TABLE = {1000: 0x06, 200: 0x07, 100: 0x08, 50: 0x09, 25: 0x0A}

GRAVITY = 9.80665
DEG_TO_RAD = math.pi / 180.0


def to_int16(high: int, low: int) -> int:
    """两个字节（高位在前）拼成有符号 16 位整数。"""
    value = ((high & 0xFF) << 8) | (low & 0xFF)
    return value - 0x10000 if value & 0x8000 else value


def decode_imu_frame(
    raw: bytes,
    accel_lsb_per_g: float,
    gyro_lsb_per_dps: float,
) -> Tuple[Tuple[float, float, float], Tuple[float, float, float]]:
    """把 12 字节 burst 解成 SI 单位。

    Args:
        raw: 从 ACCEL_DATA_X1 起连续 12 字节。
        accel_lsb_per_g: 当前量程下加速度灵敏度。
        gyro_lsb_per_dps: 当前量程下角速度灵敏度。

    Returns:
        ((ax, ay, az) m/s², (gx, gy, gz) rad/s)。

    Raises:
        ValueError: 字节数不足。
    """
    if len(raw) != BURST_LENGTH:
        raise ValueError(f"expected {BURST_LENGTH} bytes, got {len(raw)}")
    words = [to_int16(raw[i], raw[i + 1]) for i in range(0, BURST_LENGTH, 2)]
    accel = tuple(w / accel_lsb_per_g * GRAVITY for w in words[0:3])
    gyro = tuple(w / gyro_lsb_per_dps * DEG_TO_RAD for w in words[3:6])
    return accel, gyro


class ICM42688Driver:
    """ICM42688-P 驱动。通过依赖注入 I2CInterface 与硬件解耦。"""

    def __init__(self, i2c: I2CInterface) -> None:
        """读取 `~icm42688` 配置段、建立发布器。"""
        config = load_section(SECTION)
        require_keys(config, REQUIRED_KEYS, "~" + SECTION)

        self.i2c = i2c
        self.bus = int(config["bus"])
        self.address = int(config["address"])
        self.frame_id = str(config["frame_id"])
        self.update_rate = positive_float(
            config["update_rate"], "icm42688/update_rate"
        )
        self.reconnect_interval = positive_float(
            config["reconnect_interval"], "icm42688/reconnect_interval"
        )
        accel_range = int(config["accel_range_g"])
        gyro_range = int(config["gyro_range_dps"])
        if accel_range not in ACCEL_RANGE_TABLE:
            raise ValueError(
                f"icm42688/accel_range_g must be one of "
                f"{sorted(ACCEL_RANGE_TABLE)}, got {accel_range}"
            )
        if gyro_range not in GYRO_RANGE_TABLE:
            raise ValueError(
                f"icm42688/gyro_range_dps must be one of "
                f"{sorted(GYRO_RANGE_TABLE)}, got {gyro_range}"
            )
        self.accel_fs_sel, self.accel_lsb_per_g = ACCEL_RANGE_TABLE[accel_range]
        self.gyro_fs_sel, self.gyro_lsb_per_dps = GYRO_RANGE_TABLE[gyro_range]
        self.accel_range_g = accel_range
        self.gyro_range_dps = gyro_range
        self.odr_code = ODR_TABLE.get(int(round(self.update_rate)), ODR_TABLE[100])
        gyro_stddev = positive_float(
            config["gyro_noise_stddev"], "icm42688/gyro_noise_stddev"
        )
        self.accel_stddev = positive_float(
            config["accel_noise_stddev"], "icm42688/accel_noise_stddev"
        )
        self.gyro_stddev = gyro_stddev

        self.publisher = rospy.Publisher(str(config["topic"]), Imu, queue_size=10)
        self._connected = False
        self._next_retry_at = 0.0

    # --- 连接管理 -----------------------------------------------------------
    def connect(self) -> bool:
        """打开 I2C、校验芯片 ID、写入量程与 ODR。失败返回 False。"""
        try:
            if not self.i2c.open(self.bus, self.address):
                rospy.logwarn_throttle(
                    5.0, "[icm42688_driver] 打开 I2C bus %d addr 0x%02X 失败",
                    self.bus, self.address,
                )
                return False
            whoami = self.i2c.read_register(REG_WHO_AM_I, 1)
            if not whoami or whoami[0] != WHO_AM_I_VALUE:
                got = whoami[0] if whoami else -1
                rospy.logwarn(
                    "[icm42688_driver] WHO_AM_I=0x%02X, 期望 0x%02X，拒绝写寄存器",
                    got, WHO_AM_I_VALUE,
                )
                self.i2c.close()
                return False
            self.i2c.write_register(REG_PWR_MGMT0, bytes((PWR_MGMT0_LN,)))
            self.i2c.write_register(
                REG_GYRO_CONFIG0,
                bytes(((self.gyro_fs_sel << 5) | self.odr_code,)),
            )
            self.i2c.write_register(
                REG_ACCEL_CONFIG0,
                bytes(((self.accel_fs_sel << 5) | self.odr_code,)),
            )
        except (OSError, RuntimeError) as error:
            rospy.logwarn_throttle(
                5.0, "[icm42688_driver] 初始化失败: %s", error
            )
            return False
        self._connected = True
        rospy.loginfo(
            "[icm42688_driver] 已连接 bus %d addr 0x%02X (±%dg / ±%ddps @ %.0fHz)",
            self.bus, self.address,
            self.accel_range_g, self.gyro_range_dps, self.update_rate,
        )
        return True

    def disconnect(self) -> None:
        """关闭 I2C 总线。"""
        self.i2c.close()
        self._connected = False

    # --- 主循环 -------------------------------------------------------------
    def step(self):
        """推进一次：必要时重连，读一帧，返回 Imu 消息或 None。"""
        now = time.monotonic()
        if not self._connected:
            if now < self._next_retry_at:
                return None
            if not self.connect():
                self._next_retry_at = now + self.reconnect_interval
                return None
        try:
            raw = self.i2c.read_register(REG_ACCEL_DATA_X1, BURST_LENGTH)
            accel, gyro = decode_imu_frame(
                raw, self.accel_lsb_per_g, self.gyro_lsb_per_dps
            )
        except (OSError, RuntimeError, ValueError) as error:
            # I2C 挂死时 read 会一直报错。断开重连，不静默失败 (评审建议 3)。
            rospy.logwarn_throttle(
                5.0, "[icm42688_driver] 读取失败，%.1fs 后重连: %s",
                self.reconnect_interval, error,
            )
            self.disconnect()
            self._next_retry_at = now + self.reconnect_interval
            return None
        return self.build_imu(accel, gyro)

    def build_imu(
        self,
        accel: Tuple[float, float, float],
        gyro: Tuple[float, float, float],
    ) -> Imu:
        """装配 Imu 消息。"""
        imu = Imu()
        imu.header.stamp = rospy.Time.now()
        imu.header.frame_id = self.frame_id
        imu.linear_acceleration.x = accel[0]
        imu.linear_acceleration.y = accel[1]
        imu.linear_acceleration.z = accel[2]
        imu.angular_velocity.x = gyro[0]
        imu.angular_velocity.y = gyro[1]
        imu.angular_velocity.z = gyro[2]
        # ICM42688 是 6 轴，没有姿态输出。sensor_msgs/Imu 规定此时
        # orientation_covariance[0] = -1，下游据此判断"这条消息没有姿态"。
        # 不写这一位的话，单位四元数会被当成"姿态就是零旋转"。
        imu.orientation.w = 1.0
        imu.orientation_covariance = [-1.0] + [0.0] * 8
        gyro_var = self.gyro_stddev ** 2
        accel_var = self.accel_stddev ** 2
        imu.angular_velocity_covariance = [
            gyro_var, 0.0, 0.0, 0.0, gyro_var, 0.0, 0.0, 0.0, gyro_var,
        ]
        imu.linear_acceleration_covariance = [
            accel_var, 0.0, 0.0, 0.0, accel_var, 0.0, 0.0, 0.0, accel_var,
        ]
        return imu

    def run(self) -> None:
        """阻塞式主循环，直到 ROS 关闭。"""
        rate = rospy.Rate(self.update_rate)
        while not rospy.is_shutdown():
            message = self.step()
            if message is not None:
                self.publisher.publish(message)
            rate.sleep()
        self.disconnect()


def main() -> None:
    """启动 ICM42688 驱动节点。"""
    rospy.init_node("icm42688_driver")
    backend = resolve_backend()
    driver = ICM42688Driver(create_i2c(backend))
    if backend == "real" and not driver.connect():
        raise RuntimeError("ICM42688 real 后端首次连接失败，拒绝空转启动")
    driver.run()


if __name__ == "__main__":
    main()
