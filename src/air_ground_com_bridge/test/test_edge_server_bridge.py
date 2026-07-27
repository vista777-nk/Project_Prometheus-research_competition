#!/usr/bin/env python3
"""Unit tests for Task-06 TCP framing and command validation."""

import json
import math
import struct
import sys
import threading
from pathlib import Path
from unittest import TestCase

import rospy
from air_ground_interfaces.msg import Observation, RobotState


SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from edge_server_bridge import (  # noqa: E402
    EdgeServerBridge,
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

    def test_drone_icd_snapshot_serializes_for_task07(self):
        observation = Observation()
        observation.header.stamp = rospy.Time.from_sec(10.0)
        observation.modalities = ["rgb", "imu"]
        observation.rgb.data = b"\xff\xd8test"
        observation.angular_velocity.z = 0.2
        observation.linear_acceleration.z = 9.81

        state = RobotState()
        state.header.stamp = rospy.Time.from_sec(11.0)
        state.robot_id = "drone"
        state.pose.position.z = 3.0
        state.pose.orientation.w = 1.0
        state.mode = "OFFBOARD"
        state.chassis_type = "none"
        state.is_armed = True
        state.is_connected = True

        bridge = EdgeServerBridge.__new__(EdgeServerBridge)
        bridge.latest_lock = threading.Lock()
        bridge.latest = {
            "ultrasonic": {},
            "drone_observation": observation,
            "drone_state": state,
        }
        bridge.throttle_config = {"max_image_freq": 2.0}
        bridge.last_drone_image_time = 0.0

        payload = bridge.serialize_drone()
        self.assertEqual(payload["source"], "drone")
        self.assertEqual(payload["timestamp"], 11.0)
        self.assertEqual(payload["mode"], "OFFBOARD")
        self.assertAlmostEqual(payload["pose"]["z"], 3.0)
        self.assertIn("image_jpeg_b64", payload)
