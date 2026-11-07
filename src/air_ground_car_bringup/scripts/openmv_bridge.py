#!/usr/bin/env python3
"""OpenMV 云台相机桥 —— ROS/协议层与 Linux UART 访问层已实现。

发布: /car/openmv/detections (std_msgs/String, JSON)
配置: config/real_sensors.yaml §openmv

协议 (与电赛队伍约定，评审建议 4)::

    {"objects":[{"label":"red_ball","x":120,"y":80,"w":30,"h":30,"conf":0.95}]}\\n

坐标系: 图像左上角为原点，x 向右，y 向下，单位像素。
帧分隔符是 '\\n'，一行一帧。

本节点**只做协议翻译，不做过滤/融合**：字段不合法的目标直接丢弃并告警，
合法的原样转发。语义判断归 Layer 3/4 (RESEARCH_PHILOSOPHY §三)。
"""

import json
import os
import sys
import time
from typing import Any, Dict, List, Optional

import rospy
from std_msgs.msg import String

# 见 rplidar_driver.py 同处注释：避开 catkin devel relay 的同名自导入。
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from hardware_interface import HardwareError, UARTInterface, create_uart  # noqa: E402
from sensor_config import (  # noqa: E402
    load_section,
    positive_float,
    require_keys,
    resolve_backend,
)

SECTION = "openmv"
REQUIRED_KEYS = (
    "port", "baudrate", "topic", "read_chunk",
    "max_line_bytes", "reconnect_interval", "data_timeout",
)

REQUIRED_OBJECT_KEYS = ("label", "x", "y", "w", "h", "conf")
FRAME_DELIMITER = b"\n"


def sanitize_object(item: Any) -> Optional[Dict[str, Any]]:
    """校验并规整单个检测目标，不合法返回 None。

    Args:
        item: JSON 解出的一个目标对象。

    Returns:
        规整后的 dict（x/y/w/h 为 int，conf 为 [0,1] 的 float），或 None。
    """
    if not isinstance(item, dict):
        return None
    if any(key not in item for key in REQUIRED_OBJECT_KEYS):
        return None
    if not isinstance(item["label"], str) or not item["label"]:
        return None
    try:
        box = {key: int(item[key]) for key in ("x", "y", "w", "h")}
        confidence = float(item["conf"])
    except (TypeError, ValueError):
        return None
    if box["w"] <= 0 or box["h"] <= 0:
        return None
    if not 0.0 <= confidence <= 1.0:
        return None
    result = {"label": item["label"], "conf": confidence}
    result.update(box)
    return result


def parse_detection_line(line: bytes) -> Optional[Dict[str, List[Dict[str, Any]]]]:
    """解析一行 OpenMV JSON，返回 {"objects": [...]}。

    Args:
        line: 不含分隔符的一行原始字节。

    Returns:
        规整后的检测结果；整行不可解析时返回 None。
        单个目标不合法只会被剔除，不影响同一行里其余目标。
    """
    try:
        payload = json.loads(line.decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError):
        return None
    if not isinstance(payload, dict) or not isinstance(payload.get("objects"), list):
        return None
    objects = [
        sanitized
        for sanitized in (sanitize_object(item) for item in payload["objects"])
        if sanitized is not None
    ]
    return {"objects": objects}


class OpenMVBridge:
    """OpenMV UART → ROS 话题桥。通过依赖注入 UARTInterface 与硬件解耦。"""

    def __init__(self, uart: UARTInterface) -> None:
        """读取 `~openmv` 配置段、建立发布器。"""
        config = load_section(SECTION)
        require_keys(config, REQUIRED_KEYS, "~" + SECTION)

        self.uart = uart
        self.port = str(config["port"])
        self.baudrate = int(config["baudrate"])
        self.read_chunk = int(config["read_chunk"])
        self.max_line_bytes = int(config["max_line_bytes"])
        if self.max_line_bytes < self.read_chunk:
            raise ValueError("~openmv/max_line_bytes must be >= read_chunk")
        self.reconnect_interval = positive_float(
            config["reconnect_interval"], "openmv/reconnect_interval"
        )
        self.data_timeout = positive_float(
            config["data_timeout"], "openmv/data_timeout"
        )

        self.publisher = rospy.Publisher(str(config["topic"]), String, queue_size=10)
        self._buffer = b""
        self._connected = False
        self._last_data_at = time.monotonic()
        self._next_retry_at = 0.0

    # --- 连接管理 -----------------------------------------------------------
    def connect(self) -> bool:
        """打开串口。失败返回 False，不抛异常。"""
        try:
            opened = self.uart.open(self.port, self.baudrate)
        except (HardwareError, OSError, RuntimeError) as error:
            rospy.logwarn_throttle(
                5.0, "[openmv_bridge] 打开 %s 异常: %s", self.port, error
            )
            return False
        if not opened:
            rospy.logwarn_throttle(
                5.0, "[openmv_bridge] 打开 %s 失败，%.1fs 后重试",
                self.port, self.reconnect_interval,
            )
            return False
        self._buffer = b""
        self._connected = True
        self._last_data_at = time.monotonic()
        rospy.loginfo("[openmv_bridge] 已连接 %s @ %d", self.port, self.baudrate)
        return True

    def disconnect(self) -> None:
        """关闭串口并清空半行缓冲。"""
        self.uart.close()
        self._connected = False
        self._buffer = b""

    # --- 主循环 -------------------------------------------------------------
    def step(self) -> List[String]:
        """推进一次：必要时重连，读一批字节，返回本次解析出的消息。"""
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
                5.0, "[openmv_bridge] 读取失败，%.1fs 后重连: %s",
                self.reconnect_interval, error,
            )
            self.disconnect()
            self._next_retry_at = now + self.reconnect_interval
            return []
        if not data:
            if now - self._last_data_at > self.data_timeout:
                rospy.logwarn_throttle(
                    5.0, "[openmv_bridge] %.1fs 未收到数据，重连 %s",
                    now - self._last_data_at, self.port,
                )
                self.disconnect()
                self._next_retry_at = now + self.reconnect_interval
            return []
        self._last_data_at = now
        return self._consume(data)

    def _consume(self, data: bytes) -> List[String]:
        """按 '\\n' 切帧并逐行解析。"""
        self._buffer += data
        messages = []
        while FRAME_DELIMITER in self._buffer:
            line, self._buffer = self._buffer.split(FRAME_DELIMITER, 1)
            payload = parse_detection_line(line.strip())
            if payload is None:
                rospy.logwarn_throttle(
                    10.0, "[openmv_bridge] 丢弃无法解析的一行: %r", line[:60]
                )
                continue
            messages.append(String(data=json.dumps(payload, separators=(",", ":"))))
        # 一直收不到分隔符说明对端在乱发或波特率不对。不设上限的话，
        # 这个缓冲会在树莓派上一直长到把内存吃光 —— 而节点看起来一切正常。
        if len(self._buffer) > self.max_line_bytes:
            rospy.logwarn_throttle(
                10.0,
                "[openmv_bridge] 单行超过 %d 字节仍无分隔符，丢弃缓冲",
                self.max_line_bytes,
            )
            self._buffer = b""
        return messages

    def run(self) -> None:
        """阻塞式主循环，直到 ROS 关闭。"""
        rate = rospy.Rate(50.0)
        while not rospy.is_shutdown():
            for message in self.step():
                self.publisher.publish(message)
            rate.sleep()
        self.disconnect()


def main() -> None:
    """启动 OpenMV 桥接节点。"""
    rospy.init_node("openmv_bridge")
    backend = resolve_backend()
    bridge = OpenMVBridge(create_uart(backend))
    if backend == "real" and not bridge.connect():
        raise RuntimeError("OpenMV real 后端首次连接失败，拒绝空转启动")
    bridge.run()


if __name__ == "__main__":
    main()
