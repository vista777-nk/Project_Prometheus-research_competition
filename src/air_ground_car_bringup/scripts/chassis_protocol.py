#!/usr/bin/env python3
"""车载树莓派与两种底盘 MCU 共用的串口协议。

本模块是 ADR-0003 / ADR-0018 在树莓派侧的唯一实现。它不依赖 ROS，也不访问
串口，因此生产桥、台架回路测试和 Host 单元测试可以共用同一份帧解析代码。

线上多字节标量均为小端。超声波帧固定按 ``front, rear, left, right`` 排列；
``0xFFFF`` 表示本周期无有效回波，不能当成 65.535 m 的真实距离。
"""

import math
import struct
from typing import Dict, List, NamedTuple, Optional, Tuple

FRAME_SOF = 0xA5
FRAME_EOF = 0x5A
FRAME_LEN_OVERHEAD = 4
FRAME_MAX_DATA_LEN = 251
FRAME_MIN_LEN_FIELD = FRAME_LEN_OVERHEAD
FRAME_MAX_LEN_FIELD = 255

CMD_SET_VELOCITY = 0x01
CMD_EMERGENCY_STOP = 0x02
CMD_PING = 0x03
CMD_TELEMETRY = 0x11
CMD_ACK = 0x12
CMD_PONG = 0x13
CMD_ULTRASONIC = 0x14
CMD_ERROR = 0xFF

PAYLOAD_LEN_PONG = 5
PAYLOAD_LEN_ULTRASONIC = 8
ULTRASONIC_UNAVAILABLE_MM = 0xFFFF
ULTRASONIC_DIRECTIONS = ("front", "rear", "left", "right")

BOARD_STM32F407 = 0x01
BOARD_MSPM0G3507 = 0x02
CHASSIS_MECANUM = 0x01
CHASSIS_DIFFERENTIAL = 0x02

BOARD_NAMES = {
    BOARD_STM32F407: "STM32F407",
    BOARD_MSPM0G3507: "MSPM0G3507",
}
CHASSIS_NAMES = {
    CHASSIS_MECANUM: "mecanum",
    CHASSIS_DIFFERENTIAL: "differential",
}

CHASSIS_IDENTITIES = {
    "mecanum": (BOARD_STM32F407, CHASSIS_MECANUM),
    "diff": (BOARD_MSPM0G3507, CHASSIS_DIFFERENTIAL),
    "differential": (BOARD_MSPM0G3507, CHASSIS_DIFFERENTIAL),
}

CRC16_CHECK_VALUE = 0x29B1
DEFAULT_BAUDRATE = 115200


class PongInfo(NamedTuple):
    """PONG 中的固件身份。"""

    firmware: str
    version: Tuple[int, int, int]
    board_type: int
    board: str
    chassis_type: int
    chassis: str


class Telemetry(NamedTuple):
    """轮速、可选电流与故障位图。电流不可用时为 NaN。"""

    rpm: Tuple[float, ...]
    current_a: Tuple[float, ...]
    fault: int


def crc16_ccitt(data: bytes) -> int:
    """CRC-16/CCITT-FALSE，与 ``common/crc16.c`` 逐位等价。"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def encode_frame(cmd: int, data: bytes = b"") -> bytes:
    """编码 ``SOF|LEN|CMD|DATA|CRC16|EOF``。"""
    if not 0 <= int(cmd) <= 0xFF:
        raise ValueError("CMD must fit in one byte")
    payload_data = bytes(data)
    if len(payload_data) > FRAME_MAX_DATA_LEN:
        raise ValueError(
            "DATA 最长 {} 字节, 收到 {}".format(
                FRAME_MAX_DATA_LEN, len(payload_data)
            )
        )
    payload = bytes((int(cmd),)) + payload_data
    crc = crc16_ccitt(payload)
    return (
        bytes((FRAME_SOF, len(payload_data) + FRAME_LEN_OVERHEAD))
        + payload
        + struct.pack("<H", crc)
        + bytes((FRAME_EOF,))
    )


class FrameParser:
    """可增量喂入的拆帧状态机；载荷里的 0xA5/0x5A 不作分隔符。"""

    def __init__(self) -> None:
        self.reset(clear_stats=True)

    def reset(self, clear_stats: bool = False) -> None:
        """复位半帧；默认保留链路错误计数。"""
        self._state = "SOF"
        self._len_field = 0
        self._buffer = bytearray()
        if clear_stats:
            self.frames_ok = 0
            self.err_len = 0
            self.err_crc = 0
            self.err_eof = 0

    def push(self, chunk: bytes) -> List[Tuple[int, bytes]]:
        """喂入字节并返回本次完成的所有 ``(cmd, data)``。"""
        frames: List[Tuple[int, bytes]] = []
        for byte in bytes(chunk):
            frame = self._push_byte(byte)
            if frame is not None:
                frames.append(frame)
        return frames

    def _push_byte(self, byte: int) -> Optional[Tuple[int, bytes]]:
        if self._state == "SOF":
            if byte == FRAME_SOF:
                self._state = "LEN"
            return None

        if self._state == "LEN":
            if not FRAME_MIN_LEN_FIELD <= byte <= FRAME_MAX_LEN_FIELD:
                self.err_len += 1
                self._state = "LEN" if byte == FRAME_SOF else "SOF"
                return None
            self._len_field = byte
            self._buffer = bytearray()
            self._state = "PAYLOAD"
            return None

        self._buffer.append(byte)
        if len(self._buffer) < self._len_field:
            return None

        payload = bytes(self._buffer[:self._len_field - 3])
        crc_received = struct.unpack(
            "<H", bytes(self._buffer[self._len_field - 3:self._len_field - 1])
        )[0]
        eof = self._buffer[self._len_field - 1]
        self.reset()

        if crc_received != crc16_ccitt(payload):
            self.err_crc += 1
            return None
        if eof != FRAME_EOF:
            self.err_eof += 1
            return None
        self.frames_ok += 1
        return payload[0], payload[1:]


def decode_pong(data: bytes) -> PongInfo:
    """解析 PONG 并保留未知枚举值，供调用方失败关闭。"""
    if len(data) != PAYLOAD_LEN_PONG:
        raise ValueError(
            "PONG 载荷应为 {} 字节, 收到 {}".format(PAYLOAD_LEN_PONG, len(data))
        )
    major, minor, patch, board, chassis = data
    return PongInfo(
        firmware="v{}.{}.{}".format(major, minor, patch),
        version=(major, minor, patch),
        board_type=board,
        board=BOARD_NAMES.get(board, "unknown(0x{:02X})".format(board)),
        chassis_type=chassis,
        chassis=CHASSIS_NAMES.get(chassis, "unknown(0x{:02X})".format(chassis)),
    )


def expected_identity(chassis: str) -> Tuple[int, int]:
    """把部署名映射为协议身份；拒绝拼写错误。"""
    try:
        return CHASSIS_IDENTITIES[str(chassis)]
    except KeyError as exc:
        raise ValueError("chassis 只接受 diff/differential/mecanum") from exc


def encode_velocity(
    chassis: str,
    linear_x: float,
    linear_y: float,
    angular_z: float,
) -> bytes:
    """把 ROS Twist 三自由度压成对应底盘的 SET_VELOCITY 帧。"""
    values = (float(linear_x), float(linear_y), float(angular_z))
    if not all(math.isfinite(value) for value in values):
        raise ValueError("速度指令不得包含 NaN/Inf")
    identity = expected_identity(chassis)
    if identity[1] == CHASSIS_MECANUM:
        payload = struct.pack("<fff", *values)
    else:
        if abs(values[1]) > 1e-9:
            raise ValueError("差速底盘不接受 lateral linear_y 指令")
        payload = struct.pack("<ff", values[0], values[2])
    return encode_frame(CMD_SET_VELOCITY, payload)


def encode_ultrasonic(ranges_mm: Tuple[int, int, int, int]) -> bytes:
    """测试/固件黄金向量使用的超声波帧编码器。"""
    if len(ranges_mm) != len(ULTRASONIC_DIRECTIONS):
        raise ValueError("超声波必须恰好四路")
    checked = []
    for value in ranges_mm:
        if not 0 <= int(value) <= 0xFFFF:
            raise ValueError("超声波毫米值必须在 uint16 范围内")
        checked.append(int(value))
    return encode_frame(CMD_ULTRASONIC, struct.pack("<4H", *checked))


def decode_ultrasonic(data: bytes) -> Dict[str, Optional[float]]:
    """解码为米；无回波哨兵转换为 ``None``。"""
    if len(data) != PAYLOAD_LEN_ULTRASONIC:
        raise ValueError(
            "ULTRASONIC 载荷应为 {} 字节, 收到 {}".format(
                PAYLOAD_LEN_ULTRASONIC, len(data)
            )
        )
    values = struct.unpack("<4H", data)
    return {
        name: None if value == ULTRASONIC_UNAVAILABLE_MM else value / 1000.0
        for name, value in zip(ULTRASONIC_DIRECTIONS, values)
    }


def decode_telemetry(data: bytes, wheel_count: int) -> Telemetry:
    """按 PONG 已确认的轮数解析 TELEMETRY。"""
    if wheel_count not in (2, 4):
        raise ValueError("wheel_count 只接受 2 或 4")
    expected = wheel_count * 8 + 2
    if len(data) != expected:
        raise ValueError(
            "{} 轮 TELEMETRY 应为 {} 字节, 收到 {}".format(
                wheel_count, expected, len(data)
            )
        )
    rpm = struct.unpack("<{}f".format(wheel_count), data[:wheel_count * 4])
    start = wheel_count * 4
    current = struct.unpack(
        "<{}f".format(wheel_count), data[start:start + wheel_count * 4]
    )
    fault = struct.unpack("<H", data[-2:])[0]
    return Telemetry(rpm=tuple(rpm), current_a=tuple(current), fault=fault)
