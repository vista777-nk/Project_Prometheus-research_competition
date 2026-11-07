#!/usr/bin/env python3
"""可插拔车载载荷到 STM32/MSPM0 底盘模块的生产串口桥。

输入：``/car/cmd_vel`` (`geometry_msgs/Twist`)
输出：``/car/ultrasonic/{front,rear,left,right}`` (`sensor_msgs/LaserScan`)

启动时先用 PING/PONG 核对板卡与底盘身份。身份不符、固件版本过旧或规定时间
内没有收到 MCU 采集的四路超声波帧，实机模式都失败关闭。HC-SR04 的微秒级
时序属于底盘 MCU，不再由共享树莓派 GPIO 实现（ADR-0018）。
"""

import math
import os
import sys
import threading
import time
from typing import Optional

import rospy
from geometry_msgs.msg import Twist
from sensor_msgs.msg import LaserScan

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from chassis_protocol import (  # noqa: E402
    CMD_EMERGENCY_STOP,
    CMD_PING,
    CMD_PONG,
    CMD_TELEMETRY,
    CMD_ULTRASONIC,
    FrameParser,
    decode_pong,
    decode_telemetry,
    decode_ultrasonic,
    encode_frame,
    encode_ultrasonic,
    encode_velocity,
    expected_identity,
)
from hardware_interface import HardwareError, UARTInterface, create_uart  # noqa: E402
from sensor_config import (  # noqa: E402
    load_section,
    positive_float,
    require_keys,
    resolve_backend,
)

SECTION = "chassis_bridge"
REQUIRED_KEYS = (
    "port",
    "baudrate",
    "command_topic",
    "topic_prefix",
    "frame_id_template",
    "directions",
    "min_range",
    "max_range",
    "read_chunk",
    "poll_rate",
    "handshake_timeout",
    "ultrasonic_startup_timeout",
    "data_timeout",
    "reconnect_interval",
)
MIN_PROTOCOL_VERSION = (0, 2, 0)


class ChassisBridge:
    """一份桥接逻辑同时服务麦轮 STM32 和差速 MSPM0。"""

    def __init__(self, uart: UARTInterface, chassis: str) -> None:
        config = load_section(SECTION)
        require_keys(config, REQUIRED_KEYS, "~" + SECTION)

        self.uart = uart
        self.chassis = str(chassis)
        self.expected_board, self.expected_chassis = expected_identity(self.chassis)
        self.wheel_count = 4 if self.chassis == "mecanum" else 2
        self.port = str(config["port"])
        self.baudrate = int(config["baudrate"])
        self.read_chunk = int(config["read_chunk"])
        if self.read_chunk <= 0:
            raise ValueError("chassis_bridge/read_chunk must be positive")
        self.poll_rate = positive_float(config["poll_rate"], "chassis_bridge/poll_rate")
        self.handshake_timeout = positive_float(
            config["handshake_timeout"], "chassis_bridge/handshake_timeout"
        )
        self.ultrasonic_startup_timeout = positive_float(
            config["ultrasonic_startup_timeout"],
            "chassis_bridge/ultrasonic_startup_timeout",
        )
        self.data_timeout = positive_float(
            config["data_timeout"], "chassis_bridge/data_timeout"
        )
        self.reconnect_interval = positive_float(
            config["reconnect_interval"], "chassis_bridge/reconnect_interval"
        )
        self.min_range = positive_float(
            config["min_range"], "chassis_bridge/min_range"
        )
        self.max_range = positive_float(
            config["max_range"], "chassis_bridge/max_range"
        )
        if self.max_range <= self.min_range:
            raise ValueError("chassis_bridge/max_range must exceed min_range")

        self.directions = tuple(str(value) for value in config["directions"])
        if self.directions != ("front", "rear", "left", "right"):
            raise ValueError(
                "chassis_bridge/directions 必须按 front,rear,left,right 排列"
            )
        self.frame_id_template = str(config["frame_id_template"])
        prefix = str(config["topic_prefix"]).rstrip("/")
        self.publishers = {
            name: rospy.Publisher("{}/{}".format(prefix, name), LaserScan, queue_size=5)
            for name in self.directions
        }
        self.subscriber = rospy.Subscriber(
            str(config["command_topic"]), Twist, self.on_command, queue_size=1
        )

        self.parser = FrameParser()
        self._write_lock = threading.Lock()
        self._connected = False
        self._validated = False
        self._seen_ultrasonic = False
        self._last_data_at = 0.0
        self._next_retry_at = 0.0
        self._identity_error: Optional[str] = None
        self.last_telemetry = None

    def _write(self, frame: bytes) -> None:
        with self._write_lock:
            written = self.uart.write(frame)
        if written != len(frame):
            raise HardwareError(
                "MCU 串口短写: {}/{} 字节".format(written, len(frame))
            )

    def _validate_pong(self, data: bytes) -> None:
        info = decode_pong(data)
        actual = (info.board_type, info.chassis_type)
        expected = (self.expected_board, self.expected_chassis)
        if actual != expected:
            self._identity_error = (
                "配置 chassis={} 期望板卡/底盘 {}/{}, 对端却自报 {}/{}".format(
                    self.chassis,
                    expected[0],
                    expected[1],
                    info.board_type,
                    info.chassis_type,
                )
            )
            raise RuntimeError(self._identity_error)
        if info.version < MIN_PROTOCOL_VERSION:
            self._identity_error = (
                "固件 {} 过旧；HC-SR04 下沉 MCU 需要至少 v{}.{}.{}".format(
                    info.firmware, *MIN_PROTOCOL_VERSION
                )
            )
            raise RuntimeError(self._identity_error)
        self._validated = True
        rospy.loginfo(
            "[chassis_bridge] 身份核对通过: %s · %s · %s",
            info.firmware,
            info.board,
            info.chassis,
        )

    def _consume(self, chunk: bytes) -> None:
        if not chunk:
            return
        self._last_data_at = time.monotonic()
        for cmd, data in self.parser.push(chunk):
            if cmd == CMD_PONG:
                self._validate_pong(data)
            elif cmd == CMD_ULTRASONIC:
                if not self._validated:
                    continue
                readings = decode_ultrasonic(data)
                self._seen_ultrasonic = True
                for name, distance in readings.items():
                    self.publishers[name].publish(self.build_scan(name, distance))
            elif cmd == CMD_TELEMETRY:
                if self._validated:
                    self.last_telemetry = decode_telemetry(data, self.wheel_count)

    def connect(self) -> bool:
        """打开串口、核对身份并等待首帧超声波，任一缺失都不进入 ready。"""
        self.disconnect()
        try:
            if not self.uart.open(self.port, self.baudrate):
                return False
            self._connected = True
            self._last_data_at = time.monotonic()
            self._write(encode_frame(CMD_PING))
            deadline = time.monotonic() + self.handshake_timeout
            while time.monotonic() < deadline and not rospy.is_shutdown():
                self._consume(self.uart.read(self.read_chunk, timeout_ms=50.0))
                if self._validated:
                    break
            if not self._validated:
                rospy.logerr("[chassis_bridge] %.1fs 内没有合法 PONG", self.handshake_timeout)
                self.disconnect()
                return False

            sensor_deadline = time.monotonic() + self.ultrasonic_startup_timeout
            while time.monotonic() < sensor_deadline and not rospy.is_shutdown():
                self._consume(self.uart.read(self.read_chunk, timeout_ms=50.0))
                if self._seen_ultrasonic:
                    rospy.loginfo(
                        "[chassis_bridge] MCU 四路超声波就绪: %s",
                        ", ".join(self.directions),
                    )
                    return True
            rospy.logerr(
                "[chassis_bridge] 身份已通过，但 %.1fs 内没有 CMD_ULTRASONIC；"
                "检查 MCU 的 HC-SR04 引脚/捕获定时器",
                self.ultrasonic_startup_timeout,
            )
            self.disconnect()
            return False
        except (HardwareError, OSError, RuntimeError, ValueError) as error:
            self.disconnect()
            if self._identity_error is not None:
                raise
            rospy.logwarn("[chassis_bridge] 连接 %s 失败: %s", self.port, error)
            return False

    def disconnect(self) -> None:
        self.uart.close()
        self._connected = False
        self._validated = False
        self._seen_ultrasonic = False
        self.parser.reset()

    def on_command(self, command: Twist) -> None:
        """ROS 指令回调；链路未完成身份核对时不发送任何运动命令。"""
        if not self._connected or not self._validated or not self._seen_ultrasonic:
            rospy.logwarn_throttle(
                5.0, "[chassis_bridge] MCU 尚未 ready，拒绝 /car/cmd_vel"
            )
            return
        try:
            frame = encode_velocity(
                self.chassis,
                command.linear.x,
                command.linear.y,
                command.angular.z,
            )
            self._write(frame)
        except (HardwareError, OSError, ValueError) as error:
            rospy.logerr("[chassis_bridge] 速度指令发送失败: %s", error)
            self.disconnect()
            self._next_retry_at = time.monotonic() + self.reconnect_interval

    def build_scan(self, name: str, distance: Optional[float]) -> LaserScan:
        """把 MCU 毫米读数恢复为与仿真一致的单束 LaserScan。"""
        value = float("inf") if distance is None else float(distance)
        if not math.isfinite(value) or not self.min_range <= value <= self.max_range:
            value = float("inf")
        scan = LaserScan()
        scan.header.stamp = rospy.Time.now()
        scan.header.frame_id = self.frame_id_template.format(name=name)
        scan.angle_min = 0.0
        scan.angle_max = 0.0
        scan.angle_increment = 0.0
        scan.time_increment = 0.0
        scan.scan_time = 0.0
        scan.range_min = self.min_range
        scan.range_max = self.max_range
        scan.ranges = [value]
        scan.intensities = []
        return scan

    def step(self) -> None:
        """推进连接/读取状态机一次。"""
        now = time.monotonic()
        if not self._connected:
            if now < self._next_retry_at:
                return
            if not self.connect():
                self._next_retry_at = now + self.reconnect_interval
            return
        try:
            self._consume(self.uart.read(self.read_chunk, timeout_ms=20.0))
        except (HardwareError, OSError, RuntimeError, ValueError) as error:
            rospy.logerr("[chassis_bridge] 串口读取/解码失败: %s", error)
            self.disconnect()
            self._next_retry_at = now + self.reconnect_interval
            return
        if now - self._last_data_at > self.data_timeout:
            rospy.logerr(
                "[chassis_bridge] %.1fs 无 MCU 数据，断开重连",
                now - self._last_data_at,
            )
            self.disconnect()
            self._next_retry_at = now + self.reconnect_interval

    def shutdown(self) -> None:
        """尽力发送软件急停；MCU 自身看门狗仍是最终保障。"""
        if self._connected and self._validated:
            try:
                self._write(encode_frame(CMD_EMERGENCY_STOP))
            except (HardwareError, OSError):
                pass
        self.disconnect()

    def run(self) -> None:
        """阻塞运行。"""
        rospy.on_shutdown(self.shutdown)
        rate = rospy.Rate(self.poll_rate)
        while not rospy.is_shutdown():
            self.step()
            rate.sleep()


def _create_backend(backend: str, chassis: str) -> UARTInterface:
    """mock 显式提供合法身份/超声波帧；real 始终访问真串口。"""
    uart = create_uart(backend)
    if backend == "mock":
        board, chassis_type = expected_identity(chassis)
        pong = encode_frame(CMD_PONG, bytes((0, 2, 0, board, chassis_type)))
        sensors = encode_ultrasonic((1000, 1100, 1200, 1300))
        uart.feed(pong + sensors)
    return uart


def main() -> None:
    rospy.init_node("chassis_bridge")
    chassis = str(rospy.get_param("~chassis", "diff"))
    backend = resolve_backend()
    bridge = ChassisBridge(_create_backend(backend, chassis), chassis)
    bridge.run()


if __name__ == "__main__":
    main()
