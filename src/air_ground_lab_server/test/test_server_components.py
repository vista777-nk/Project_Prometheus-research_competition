#!/usr/bin/env python3
"""Unit tests for EQA and coordinator stable-interface helpers."""

import sys
import unittest
from pathlib import Path

import rospy
from air_ground_interfaces.msg import Mission


SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from coordinator import (  # noqa: E402
    mission_is_valid,
    validate_coordinator_config,
)
from eqa_engine import build_mission, validate_eqa_config  # noqa: E402


class ServerComponentsTest(unittest.TestCase):
    """Verify Mission generation and dispatch validation."""

    def setUp(self):
        self.eqa_config = validate_eqa_config(
            {
                "target_robot": "car",
                "mission_type": "explore",
                "priority": 128,
                "max_query_length": 100,
            }
        )
        self.coordinator_config = validate_coordinator_config(
            {
                "allowed_robots": ["car", "drone"],
                "allowed_mission_types": ["explore", "takeoff"],
            }
        )

    def test_query_builds_stable_mission(self):
        mission = build_mission(
            " Where is the red ball? ",
            self.eqa_config,
            rospy.Time.from_sec(1.0),
        )
        self.assertEqual(mission.robot_id, "car")
        self.assertEqual(mission.type, "explore")
        self.assertEqual(mission.query_text, "Where is the red ball?")
        self.assertTrue(mission_is_valid(mission, self.coordinator_config))

    def test_empty_and_oversized_queries_are_rejected(self):
        for query in ("", "x" * 101):
            with self.assertRaises(ValueError):
                build_mission(
                    query,
                    self.eqa_config,
                    rospy.Time.from_sec(1.0),
                )

    def test_invalid_mission_is_rejected(self):
        mission = Mission()
        mission.mission_id = "id"
        mission.robot_id = "car"
        mission.type = "land"
        mission.priority = 1
        self.assertFalse(
            mission_is_valid(mission, self.coordinator_config)
        )

    def test_invalid_component_configs_are_rejected(self):
        with self.assertRaises(ValueError):
            validate_eqa_config({})
        with self.assertRaises(ValueError):
            validate_coordinator_config(
                {
                    "allowed_robots": ["truck"],
                    "allowed_mission_types": ["explore"],
                }
            )


if __name__ == "__main__":
    unittest.main()
