#!/usr/bin/env python3
"""Unit tests for Task-06 MAVLink framing and command validation."""

import json
import math
import struct
import sys
from pathlib import Path
from unittest import TestCase


SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from drone_car_bridge import (  # noqa: E402
    HAS_PYMAVLINK,
    MAVLINK_MSG_ID_COMMAND_LONG,
    MAVLINK_MSG_ID_HEARTBEAT,
    MavlinkV1Parser,
    TokenBucket,
    build_command_long,
    build_mavlink_v1_frame,
    load_network_config,
    mavutil,
    parse_command,
)


def heartbeat_frame(sequence=7):
    payload = struct.pack("<IBBBBB", 4, 2, 12, 0x80, 4, 3)
    return build_mavlink_v1_frame(
        MAVLINK_MSG_ID_HEARTBEAT,
        payload,
        sequence,
        1,
        1,
    )


class MavlinkProtocolTest(TestCase):
    def test_repository_config_enables_px4_sitl_adapter(self):
        config_path = (
            Path(__file__).resolve().parents[1]
            / "config"
            / "network.yaml"
        )
        config = load_network_config(str(config_path))
        self.assertTrue(config["simulation_adapter"]["enable"])
        self.assertEqual(
            config["simulation_adapter"]["px4_port"], 18570
        )

    def test_fragmented_heartbeat_is_parsed(self):
        frame = heartbeat_frame()
        parser = MavlinkV1Parser()
        self.assertEqual(parser.feed(frame[:5]), [])
        parsed = parser.feed(frame[5:])
        self.assertEqual(len(parsed), 1)
        self.assertEqual(
            parsed[0]["message_id"], MAVLINK_MSG_ID_HEARTBEAT
        )
        self.assertEqual(parsed[0]["system_id"], 1)

    def test_noise_and_multiple_frames_are_parsed(self):
        parser = MavlinkV1Parser()
        parsed = parser.feed(
            b"\x00\x01noise" + heartbeat_frame(1) + heartbeat_frame(2)
        )
        self.assertEqual([item["sequence"] for item in parsed], [1, 2])

    def test_corrupt_checksum_is_rejected(self):
        frame = bytearray(heartbeat_frame())
        frame[-1] ^= 0xFF
        self.assertEqual(MavlinkV1Parser().feed(bytes(frame)), [])

    def test_frame_matches_pymavlink_when_available(self):
        if not HAS_PYMAVLINK:
            self.skipTest("optional pymavlink is not installed")
        parser = mavutil.mavlink.MAVLink(None)
        message = None
        for byte in heartbeat_frame():
            candidate = parser.parse_char(bytes((byte,)))
            if candidate is not None:
                message = candidate
        self.assertIsNotNone(message)
        self.assertEqual(message.get_type(), "HEARTBEAT")
        self.assertEqual(message.custom_mode, 4)

    def test_command_long_has_valid_payload(self):
        command = parse_command(
            json.dumps(
                {
                    "command": 400,
                    "params": [1.0],
                    "target_system": 2,
                    "target_component": 3,
                }
            )
        )
        frame = build_command_long(command, 9, 255, 190)
        parsed = MavlinkV1Parser().feed(frame)
        self.assertEqual(
            parsed[0]["message_id"], MAVLINK_MSG_ID_COMMAND_LONG
        )
        values = struct.unpack("<7fHBBB", parsed[0]["payload"])
        self.assertEqual(values[7:], (400, 2, 3, 0))
        self.assertAlmostEqual(values[0], 1.0)

    def test_invalid_commands_are_rejected(self):
        invalid_commands = (
            "not-json",
            "[]",
            '{"command": -1}',
            '{"command": 70000}',
            '{"command": 400, "params": [NaN]}',
            '{"command": 400, "params": [1,2,3,4,5,6,7,8]}',
        )
        for command in invalid_commands:
            with self.subTest(command=command):
                with self.assertRaises(ValueError):
                    parse_command(command)

    def test_token_bucket_rejects_over_capacity(self):
        bucket = TokenBucket(100)
        self.assertTrue(bucket.consume(100))
        self.assertFalse(bucket.consume(1))
        with self.assertRaises(ValueError):
            bucket.consume(-1)

    def test_non_finite_python_value_is_rejected(self):
        command = json.dumps(
            {"command": 400, "params": [math.inf]}
        )
        with self.assertRaises(ValueError):
            parse_command(command)
