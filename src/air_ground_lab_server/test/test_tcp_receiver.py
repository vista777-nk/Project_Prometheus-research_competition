#!/usr/bin/env python3
"""Unit tests for strict Task-07 TCP telemetry reconstruction."""

import base64
import math
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from tcp_receiver import (  # noqa: E402
    decode_embedded_drone_state,
    decode_telemetry,
    load_server_config,
)


PACKAGE_DIRECTORY = Path(__file__).resolve().parents[1]
WORKSPACE_SOURCE = PACKAGE_DIRECTORY.parent
NETWORK_CONFIG = (
    WORKSPACE_SOURCE / "air_ground_com_bridge" / "config" / "network.yaml"
)
SERVER_CONFIG = PACKAGE_DIRECTORY / "config" / "server_params.yaml"


class TCPReceiverTest(unittest.TestCase):
    """Verify configuration, protocol bounds and ICD message conversion."""

    def valid_payload(self):
        """Return a complete valid car payload."""
        return {
            "protocol_version": 1,
            "kind": "telemetry",
            "timestamp": 10.0,
            "source": "car",
            "image_jpeg_b64": base64.b64encode(b"\xff\xd8test").decode(),
            "imu": {
                "wx": 0.1,
                "wy": 0.2,
                "wz": 0.3,
                "ax": 1.0,
                "ay": 2.0,
                "az": 9.8,
            },
            "scan": {
                "angle_min": -1.0,
                "angle_increment": 0.1,
                "ranges": [1.0, -1.0, 2.0],
            },
            "ultrasonic": {"front": 0.5},
            "pose": {"x": 1.0, "y": 2.0, "qw": 1.0},
            "twist": {"vx": 0.2, "vz": 0.1},
            "chassis_type": "diff",
            "drone_pose": {"x": 3.0, "y": 4.0, "z": 5.0},
        }

    def test_config_matches_task06_network(self):
        config = load_server_config(
            str(NETWORK_CONFIG), str(SERVER_CONFIG)
        )
        self.assertEqual(config["port"], 9090)
        self.assertEqual(config["max_payload_bytes"], 10 * 1024 * 1024)

    def test_server_bind_ip_is_distinct_from_advertised_ip(self):
        with tempfile.TemporaryDirectory() as directory:
            network = Path(directory) / "network.yaml"
            network.write_text(
                "edge_server_tcp:\n"
                "  server_ip: 192.168.1.100\n"
                "  server_bind_ip: 0.0.0.0\n"
                "  server_port: 9090\n",
                encoding="utf-8",
            )
            config = load_server_config(str(network), str(SERVER_CONFIG))
        self.assertEqual(config["host"], "0.0.0.0")

    def test_heartbeat_without_pose_uses_identity_quaternion(self):
        payload = {
            "protocol_version": 1,
            "kind": "heartbeat",
            "source": "car",
            "timestamp": 10.0,
        }
        _, _, state = decode_telemetry(payload)
        self.assertEqual(state.pose.orientation.x, 0.0)
        self.assertEqual(state.pose.orientation.y, 0.0)
        self.assertEqual(state.pose.orientation.z, 0.0)
        self.assertEqual(state.pose.orientation.w, 1.0)

    def test_complete_payload_reconstructs_icd_messages(self):
        robot_id, observation, state = decode_telemetry(
            self.valid_payload()
        )
        self.assertEqual(robot_id, "car")
        self.assertEqual(
            set(observation.modalities),
            {"rgb", "imu", "lidar_2d", "ultrasonic"},
        )
        self.assertEqual(list(observation.lidar_ranges), [1.0, -1.0, 2.0])
        self.assertEqual(
            list(observation.ultrasonic_ranges),
            [0.5, 4.0, 4.0, 4.0],
        )
        self.assertEqual(state.chassis_type, "diff")
        self.assertAlmostEqual(state.pose.position.x, 1.0)

    def test_ultrasonic_invalid_and_out_of_range_values_are_bounded(self):
        payload = self.valid_payload()
        payload["ultrasonic"] = {
            "front": -1.0,
            "rear": 0.001,
            "left": 10.0,
        }
        _, observation, _ = decode_telemetry(payload)
        self.assertEqual(
            list(observation.ultrasonic_ranges),
            [4.0, 0.02, 4.0, 4.0],
        )

    def test_embedded_drone_pose_is_reconstructed(self):
        state = decode_embedded_drone_state(self.valid_payload())
        self.assertIsNotNone(state)
        self.assertEqual(state.robot_id, "drone")
        self.assertAlmostEqual(state.pose.position.z, 5.0)

    def test_invalid_protocol_source_and_non_finite_are_rejected(self):
        cases = []
        for key, value in (
            ("protocol_version", 2),
            ("protocol_version", []),
            ("source", "unknown"),
            ("timestamp", math.nan),
        ):
            payload = self.valid_payload()
            payload[key] = value
            cases.append(payload)
        for payload in cases:
            with self.assertRaises(ValueError):
                decode_telemetry(payload)

    def test_oversized_scan_is_rejected(self):
        payload = self.valid_payload()
        payload["scan"]["ranges"] = [1.0] * 4097
        with self.assertRaises(ValueError):
            decode_telemetry(payload)


if __name__ == "__main__":
    unittest.main()
