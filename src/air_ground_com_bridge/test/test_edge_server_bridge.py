#!/usr/bin/env python3
"""Unit tests for Task-06 TCP framing and command validation."""

import json
import math
import struct
import sys
from pathlib import Path
from unittest import TestCase


SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from edge_server_bridge import (  # noqa: E402
    TokenBucket,
    decode_server_command,
    encode_json_frame,
    load_network_config,
)


class EdgeProtocolTest(TestCase):
    def test_repository_config_is_valid(self):
        config_path = (
            Path(__file__).resolve().parents[1]
            / "config"
            / "network.yaml"
        )
        config = load_network_config(str(config_path))
        self.assertEqual(
            config["edge_server_tcp"]["server_port"], 9090
        )
        self.assertEqual(
            config["throttle"]["max_bandwidth_bytes_per_sec"],
            24000,
        )

    def test_json_frame_has_network_length_header(self):
        payload = {"source": "car", "value": 1.25}
        frame = encode_json_frame(payload, 1024)
        length = struct.unpack("!I", frame[:4])[0]
        self.assertEqual(length, len(frame) - 4)
        self.assertEqual(json.loads(frame[4:]), payload)

    def test_json_frame_rejects_nan_and_oversize(self):
        with self.assertRaises(ValueError):
            encode_json_frame({"value": math.nan}, 1024)
        with self.assertRaises(ValueError):
            encode_json_frame({"value": "x" * 100}, 16)

    def test_server_command_defaults_are_valid(self):
        command = decode_server_command({})
        self.assertEqual(command["target_id"], "car")
        self.assertEqual(command["target_pose"]["qw"], 1.0)
        self.assertEqual(command["target_velocity"], 0.0)

    def test_server_command_fields_are_preserved(self):
        command = decode_server_command(
            {
                "target_id": "car",
                "command_type": "navigate",
                "query_text": "bridge-test",
                "target_pose": {"x": 1, "y": 2, "z": 3},
                "target_velocity": 0.4,
            }
        )
        self.assertEqual(command["command_type"], "navigate")
        self.assertEqual(command["target_pose"]["x"], 1.0)
        self.assertAlmostEqual(command["target_velocity"], 0.4)

    def test_server_command_rejects_non_finite_values(self):
        invalid = (
            {"target_velocity": math.inf},
            {"target_pose": {"x": math.nan}},
            {"target_pose": []},
        )
        for payload in invalid:
            with self.subTest(payload=payload):
                with self.assertRaises(ValueError):
                    decode_server_command(payload)

    def test_token_bucket_enforces_capacity(self):
        bucket = TokenBucket(64)
        self.assertTrue(bucket.consume(32))
        self.assertTrue(bucket.consume(32))
        self.assertFalse(bucket.consume(1))
