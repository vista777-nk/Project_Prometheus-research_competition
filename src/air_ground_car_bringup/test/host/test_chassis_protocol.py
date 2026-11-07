#!/usr/bin/env python3
"""统一底盘串口协议与生产桥 Host 测试。"""

import math
import struct

import pytest
from geometry_msgs.msg import Twist

from chassis_bridge import ChassisBridge
from chassis_protocol import (
    CMD_PING,
    CMD_SET_VELOCITY,
    CMD_ULTRASONIC,
    CRC16_CHECK_VALUE,
    FrameParser,
    crc16_ccitt,
    decode_pong,
    decode_telemetry,
    decode_ultrasonic,
    encode_frame,
    encode_ultrasonic,
    encode_velocity,
)
from fixtures import configure
from mock_hardware import MockUART


def test_crc_and_ping_golden_frame_match_firmware_contract():
    assert crc16_ccitt(b"123456789") == CRC16_CHECK_VALUE
    assert encode_frame(CMD_PING) == bytes.fromhex("a5 04 03 93 d1 5a")


def test_parser_handles_fragmentation_noise_and_reserved_bytes():
    expected = encode_ultrasonic((0x00A5, 0x005A, 1000, 0xFFFF))
    parser = FrameParser()
    assert parser.push(b"\x00\xff" + expected[:3]) == []
    frames = parser.push(expected[3:])
    assert frames == [(CMD_ULTRASONIC, struct.pack("<4H", 0x00A5, 0x005A, 1000, 0xFFFF))]
    assert parser.frames_ok == 1


def test_parser_rejects_crc_and_eof_errors():
    frame = bytearray(encode_ultrasonic((100, 200, 300, 400)))
    frame[4] ^= 0x80
    parser = FrameParser()
    assert parser.push(bytes(frame)) == []
    assert parser.err_crc == 1

    frame = bytearray(encode_ultrasonic((100, 200, 300, 400)))
    frame[-1] = 0
    parser = FrameParser()
    assert parser.push(bytes(frame)) == []
    assert parser.err_eof == 1


def test_pong_identity_and_ultrasonic_sentinel_decode():
    info = decode_pong(bytes((0, 2, 0, 2, 2)))
    assert info.firmware == "v0.2.0"
    assert info.board == "MSPM0G3507"
    assert info.chassis == "differential"

    decoded = decode_ultrasonic(struct.pack("<4H", 1000, 0xFFFF, 25, 4000))
    assert decoded == {
        "front": 1.0,
        "rear": None,
        "left": 0.025,
        "right": 4.0,
    }


def test_velocity_payload_diff_and_mecanum_have_distinct_lengths():
    parser = FrameParser()
    diff = parser.push(encode_velocity("diff", 0.5, 0.0, -0.25))[0]
    assert diff[0] == CMD_SET_VELOCITY
    assert struct.unpack("<ff", diff[1]) == pytest.approx((0.5, -0.25))

    mecanum = parser.push(encode_velocity("mecanum", 0.5, 0.2, -0.25))[0]
    assert mecanum[0] == CMD_SET_VELOCITY
    assert struct.unpack("<fff", mecanum[1]) == pytest.approx((0.5, 0.2, -0.25))


def test_diff_rejects_lateral_or_nonfinite_command():
    with pytest.raises(ValueError, match="lateral"):
        encode_velocity("diff", 0.0, 0.1, 0.0)
    with pytest.raises(ValueError, match="NaN/Inf"):
        encode_velocity("mecanum", math.nan, 0.0, 0.0)


def test_telemetry_keeps_nan_as_explicit_unavailable_current():
    raw = struct.pack("<ffffH", 10.0, -10.0, math.nan, math.nan, 0x0004)
    telemetry = decode_telemetry(raw, 2)
    assert telemetry.rpm == pytest.approx((10.0, -10.0))
    assert all(math.isnan(value) for value in telemetry.current_a)
    assert telemetry.fault == 0x0004


def test_bridge_requires_matching_identity_and_first_ultrasonic_frame():
    rospy = configure()
    rospy._shutdown = False
    uart = MockUART(
        encode_frame(0x13, bytes((0, 2, 0, 2, 2)))
        + encode_ultrasonic((1000, 1100, 1200, 0xFFFF))
    )
    bridge = ChassisBridge(uart, "diff")
    assert bridge.connect()
    assert bridge._validated
    assert bridge._seen_ultrasonic
    assert bridge.publishers["front"].published[-1].ranges == [1.0]
    assert bridge.publishers["right"].published[-1].ranges == [float("inf")]

    command = Twist()
    command.linear.x = 0.4
    command.angular.z = -0.2
    bridge.on_command(command)
    commands = [cmd for cmd, _data in FrameParser().push(uart.written)]
    assert commands == [CMD_PING, CMD_SET_VELOCITY]
    rospy._shutdown = True


def test_bridge_fails_closed_on_wrong_board_or_old_protocol():
    for pong in (
        bytes((0, 2, 0, 1, 1)),
        bytes((0, 1, 0, 2, 2)),
    ):
        rospy = configure()
        rospy._shutdown = False
        bridge = ChassisBridge(MockUART(encode_frame(0x13, pong)), "diff")
        with pytest.raises(RuntimeError):
            bridge.connect()
        rospy._shutdown = True
