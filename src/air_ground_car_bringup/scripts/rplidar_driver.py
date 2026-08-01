#!/usr/bin/env python3
"""RPLIDAR A1 驱动 —— ROS/协议层与 Linux UART 访问层已实现。

发布: /car/scan (sensor_msgs/LaserScan)   ← 与 Gazebo 仿真同一话题同一类型
配置: config/real_sensors.yaml §rplidar

分层（ADR-0009）:
    接口层    ROS 话题 / 参数 / 重连退避   —— 完整实现
    数据处理层 RPLIDAR 二进制协议解析      —— 完整实现，纯函数，可单测
    硬件访问层 UART 读写                  —— mock / pyserial real 可替换
"""

import math
import os
import sys
import time
from typing import List, NamedTuple, Optional

import rospy
from sensor_msgs.msg import LaserScan

# catkin_install_python 在 devel 空间生成 relay；relay 目录里还有同名的
# hardware_interface.py。若不优先源码目录，Python 会把 relay 自己当成模块导入。
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from hardware_interface import HardwareError, UARTInterface, create_uart  # noqa: E402
from sensor_config import (  # noqa: E402
    finite_float,
    load_section,
    positive_float,
    require_keys,
    resolve_backend,
)

SECTION = "rplidar"
REQUIRED_KEYS = (
    "port", "baudrate", "topic", "frame_id", "samples", "scan_rate",
    "min_range", "max_range", "angle_min", "angle_max",
    "reconnect_interval", "data_timeout",
)

# --- RPLIDAR 协议常量 (A1/A2 通用, 见 Slamtec 通信协议手册 §5) ---
SYNC_BYTE = 0xA5
CMD_STOP = 0x25
CMD_SCAN = 0x20
DESCRIPTOR_LEN = 7
# 标准扫描模式的应答描述符: A5 5A 05 00 00 40 81
SCAN_DESCRIPTOR = bytes((0xA5, 0x5A, 0x05, 0x00, 0x00, 0x40, 0x81))
NODE_SIZE = 5


class ScanNode(NamedTuple):
    """一个采样点。角度为雷达自身坐标 (度, 顺时针)，距离为米。"""

    start: bool
    quality: int
    angle_deg: float
    distance_m: float


def parse_scan_node(raw: bytes) -> Optional[ScanNode]:
    """解析 5 字节标准扫描节点，校验不过返回 None。

    字节布局::

        byte0: [quality(6) | ~S(1) | S(1)]
        byte1: [angle_q6_low(7) | C(1)]      C 必须为 1
        byte2: [angle_q6_high(8)]
        byte3: [distance_q2_low(8)]
        byte4: [distance_q2_high(8)]

    Args:
        raw: 恰好 5 字节。

    Returns:
        解析出的 ScanNode；同步位/校验位不对时返回 None（调用方应滑动一字节重试）。
    """
    if len(raw) != NODE_SIZE:
        return None
    b0, b1, b2, b3, b4 = raw[0], raw[1], raw[2], raw[3], raw[4]
    start = bool(b0 & 0x01)
    inverted_start = bool((b0 >> 1) & 0x01)
    # S 与 ~S 必须互斥；两位相同说明字节流没有对齐到节点边界
    if start == inverted_start:
        return None
    # byte1 的最低位是固定的 check bit
    if not b1 & 0x01:
        return None
    quality = b0 >> 2
    angle_q6 = ((b1 >> 1) & 0x7F) | (b2 << 7)
    distance_q2 = b3 | (b4 << 8)
    return ScanNode(
        start=start,
        quality=quality,
        angle_deg=angle_q6 / 64.0,
        distance_m=distance_q2 / 4000.0,
    )


def angle_to_index(
    angle_deg: float,
    samples: int,
    angle_min: float,
    angle_increment: float,
) -> int:
    """把雷达角 (度, 顺时针) 映射为 LaserScan 数组下标。

    RPLIDAR 的角度**顺时针**增大，ROS LaserScan 规定**逆时针**为正
    (REP-103)。少这一个负号，实机上整张地图会沿前后轴镜像 ——
    而仿真里一切正常，因为 Gazebo 那边本来就是逆时针。

    Args:
        angle_deg: 雷达上报角度 (度)。
        samples: 一圈的采样点数。
        angle_min: 数组第 0 格对应的角度 (rad)。
        angle_increment: 相邻两格的角度差 (rad)。

    Returns:
        [0, samples) 内的下标。
    """
    theta = -math.radians(angle_deg)
    # 折回 [angle_min, angle_min + 2π)
    theta = angle_min + math.fmod(theta - angle_min, 2.0 * math.pi)
    if theta < angle_min:
        theta += 2.0 * math.pi
    index = int(round((theta - angle_min) / angle_increment))
    return min(max(index, 0), samples - 1)


class ScanAccumulator:
    """把逐点到达的 ScanNode 攒成一圈，靠 start 标志切圈。"""

    def __init__(
        self,
        samples: int,
        angle_min: float,
        angle_increment: float,
        min_range: float,
        max_range: float,
    ) -> None:
        """初始化累加器并开一个空圈。"""
        self.samples = samples
        self.angle_min = angle_min
        self.angle_increment = angle_increment
        self.min_range = min_range
        self.max_range = max_range
        self.ranges: List[float] = [float("inf")] * samples
        self.intensities: List[float] = [0.0] * samples
        self.point_count = 0

    def reset(self) -> None:
        """丢弃当前这一圈的已收点（重同步后不能把半圈当整圈发出去）。"""
        self.ranges = [float("inf")] * self.samples
        self.intensities = [0.0] * self.samples
        self.point_count = 0

    def push(self, node: ScanNode):
        """加入一个采样点。

        Returns:
            若该点带 start 标志且上一圈非空，返回上一圈的 (ranges, intensities)；
            否则返回 None。
        """
        completed = None
        if node.start and self.point_count > 0:
            completed = (list(self.ranges), list(self.intensities))
            self.reset()
        index = angle_to_index(
            node.angle_deg, self.samples, self.angle_min, self.angle_increment
        )
        # quality=0 或距离越界都记为"该方向无有效回波"。
        # 用 inf 而不是 0.0: LaserScan 规定超量程用 inf，而 0.0 会被下游
        # 当成"贴着雷达有障碍物"—— 那是能直接把车停死的误读。
        distance = node.distance_m
        if node.quality == 0 or not (self.min_range <= distance <= self.max_range):
            distance = float("inf")
        self.ranges[index] = distance
        self.intensities[index] = float(node.quality)
        self.point_count += 1
        return completed


class RPLidarDriver:
    """RPLIDAR A1 驱动。通过依赖注入 UARTInterface 与硬件解耦。"""

    def __init__(self, uart: UARTInterface) -> None:
        """读取 `~rplidar` 配置段、建立发布器。"""
        config = load_section(SECTION)
        require_keys(config, REQUIRED_KEYS, "~" + SECTION)

        self.uart = uart
        self.port = str(config["port"])
        self.baudrate = int(config["baudrate"])
        self.frame_id = str(config["frame_id"])
        self.samples = int(config["samples"])
        if self.samples < 2:
            raise ValueError("~rplidar/samples must be >= 2")
        self.scan_rate = positive_float(config["scan_rate"], "rplidar/scan_rate")
        self.min_range = positive_float(config["min_range"], "rplidar/min_range")
        self.max_range = positive_float(config["max_range"], "rplidar/max_range")
        if self.max_range <= self.min_range:
            raise ValueError("~rplidar/max_range must exceed min_range")
        self.angle_min = finite_float(config["angle_min"], "rplidar/angle_min")
        angle_span_max = finite_float(config["angle_max"], "rplidar/angle_max")
        if angle_span_max <= self.angle_min:
            raise ValueError("~rplidar/angle_max must exceed angle_min")
        # 配置里的 angle_min/angle_max 描述的是**扫描张角**；
        # LaserScan.angle_max 的语义是**最后一束的角度**，两者差一个 increment。
        # 360 点整圈时若直接把 π 填进 angle_max，第 0 格与第 359 格会指向同一方向。
        self.angle_increment = (angle_span_max - self.angle_min) / self.samples
        self.angle_max = self.angle_min + self.angle_increment * (self.samples - 1)
        self.reconnect_interval = positive_float(
            config["reconnect_interval"], "rplidar/reconnect_interval"
        )
        self.data_timeout = positive_float(
            config["data_timeout"], "rplidar/data_timeout"
        )
        self.read_chunk = int(config.get("read_chunk", 512))

        self.publisher = rospy.Publisher(
            str(config["topic"]), LaserScan, queue_size=5
        )
        self.accumulator = ScanAccumulator(
            self.samples,
            self.angle_min,
            self.angle_increment,
            self.min_range,
            self.max_range,
        )
        self._buffer = b""
        self._connected = False
        self._synced = False
        self._last_data_at = time.monotonic()
        self._next_retry_at = 0.0

    # --- 连接管理 -----------------------------------------------------------
    def connect(self) -> bool:
        """打开串口并进入标准扫描模式。失败返回 False，不抛异常。"""
        try:
            opened = self.uart.open(self.port, self.baudrate)
        except (HardwareError, OSError, RuntimeError) as error:
            rospy.logwarn_throttle(
                5.0, "[rplidar_driver] 打开 %s 异常: %s", self.port, error
            )
            return False
        if not opened:
            rospy.logwarn_throttle(
                5.0, "[rplidar_driver] 打开 %s 失败，%.1fs 后重试",
                self.port, self.reconnect_interval,
            )
            return False
        try:
            self.uart.write(bytes((SYNC_BYTE, CMD_STOP)))
            self.uart.write(bytes((SYNC_BYTE, CMD_SCAN)))
            descriptor = self.uart.read(DESCRIPTOR_LEN, timeout_ms=1000.0)
        except (HardwareError, OSError, RuntimeError) as error:
            self.uart.close()
            rospy.logwarn_throttle(
                5.0, "[rplidar_driver] 启动扫描失败: %s", error
            )
            return False
        if len(descriptor) != DESCRIPTOR_LEN:
            self.uart.close()
            rospy.logwarn_throttle(
                5.0,
                "[rplidar_driver] 扫描应答不完整: %d/%d 字节",
                len(descriptor), DESCRIPTOR_LEN,
            )
            return False
        if descriptor and descriptor != SCAN_DESCRIPTOR:
            # 不当作致命错误: 部分固件版本的描述符尾字节不同，但数据帧格式一致。
            rospy.logwarn(
                "[rplidar_driver] 应答描述符异常: %s", descriptor.hex()
            )
        self._buffer = b""
        self._synced = False
        self._connected = True
        self._last_data_at = time.monotonic()
        rospy.loginfo("[rplidar_driver] 已连接 %s @ %d", self.port, self.baudrate)
        return True

    def disconnect(self) -> None:
        """关闭串口并清空半帧缓冲。"""
        self.uart.close()
        self._connected = False
        self._synced = False
        self._buffer = b""

    def _handle_stall(self, now: float) -> None:
        """长时间收不到字节 → 断开重连。

        UART 掉线不会有任何异常，只是 read() 一直返回空。不做这个判断的话，
        节点会一直"运行正常"地不发布任何数据 (评审建议 3)。
        """
        if now - self._last_data_at <= self.data_timeout:
            return
        rospy.logwarn_throttle(
            5.0, "[rplidar_driver] %.1fs 未收到数据，重连 %s",
            now - self._last_data_at, self.port,
        )
        self.disconnect()
        self._next_retry_at = now + self.reconnect_interval

    # --- 主循环 -------------------------------------------------------------
    def step(self) -> List[LaserScan]:
        """推进一次：必要时重连，读一批字节，返回本次攒满的整圈扫描。"""
        now = time.monotonic()
        if not self._connected:
            if now < self._next_retry_at:
                return []
            if not self.connect():
                self._next_retry_at = now + self.reconnect_interval
                return []

        try:
            data = self.uart.read(self.read_chunk, timeout_ms=100.0)
        except (HardwareError, OSError, RuntimeError) as error:
            rospy.logwarn_throttle(
                5.0, "[rplidar_driver] 读取失败，%.1fs 后重连: %s",
                self.reconnect_interval, error,
            )
            self.disconnect()
            self._next_retry_at = now + self.reconnect_interval
            return []
        if data:
            self._last_data_at = now
        else:
            self._handle_stall(now)
            return []
        return self._consume(data)

    def _consume(self, data: bytes) -> List[LaserScan]:
        """把新到字节并入缓冲并逐节点解析，返回攒满的整圈。"""
        self._buffer += data
        scans = []
        while len(self._buffer) >= NODE_SIZE:
            node = parse_scan_node(self._buffer[:NODE_SIZE])
            if node is None:
                # 没对齐: 丢一个字节重新找边界，而不是丢整个 5 字节
                self._lose_sync()
                self._buffer = self._buffer[1:]
                continue
            if not self._synced:
                # 只认带 start 标志的点作为重同步锚点。
                # 错位的字节流照样能凑出一个"两个校验位都对"的假节点
                # （概率 1/4），认了它就会一直错位下去，而且每个点的角度
                # 都是错的 —— 建出来的图看着像样，实际全歪。
                # 一圈只有一个 start 点，所以重同步最多丢一圈 (0.1s)。
                if not node.start:
                    self._buffer = self._buffer[1:]
                    continue
                self._synced = True
            self._buffer = self._buffer[NODE_SIZE:]
            completed = self.accumulator.push(node)
            if completed is not None:
                scans.append(self.build_scan(*completed))
        return scans

    def _lose_sync(self) -> None:
        """标记失步并丢掉半圈数据。"""
        if self._synced:
            rospy.logwarn_throttle(
                5.0, "[rplidar_driver] 数据流失步，重新寻找圈起点"
            )
        self._synced = False
        self.accumulator.reset()

    def build_scan(
        self, ranges: List[float], intensities: List[float]
    ) -> LaserScan:
        """把一圈距离值装配为 LaserScan 消息。"""
        scan = LaserScan()
        scan.header.stamp = rospy.Time.now()
        scan.header.frame_id = self.frame_id
        scan.angle_min = self.angle_min
        scan.angle_max = self.angle_max
        scan.angle_increment = self.angle_increment
        scan.time_increment = 1.0 / (self.scan_rate * self.samples)
        scan.scan_time = 1.0 / self.scan_rate
        scan.range_min = self.min_range
        scan.range_max = self.max_range
        scan.ranges = ranges
        scan.intensities = intensities
        return scan

    def run(self) -> None:
        """阻塞式主循环，直到 ROS 关闭。"""
        rate = rospy.Rate(self.scan_rate * 2.0)
        while not rospy.is_shutdown():
            for scan in self.step():
                self.publisher.publish(scan)
            rate.sleep()
        self.disconnect()


def main() -> None:
    """启动 RPLIDAR 驱动节点。"""
    rospy.init_node("rplidar_driver")
    backend = resolve_backend()
    driver = RPLidarDriver(create_uart(backend))
    if backend == "real" and not driver.connect():
        raise RuntimeError("RPLIDAR real 后端首次连接失败，拒绝空转启动")
    driver.run()


if __name__ == "__main__":
    main()
