#!/usr/bin/env python3
"""Unit tests for Task-07 World Model state and query semantics."""

import sys
import unittest
from pathlib import Path

import rospy
from air_ground_interfaces.msg import RobotState, SemanticLandmark


SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from world_model import WorldModelStore  # noqa: E402


class WorldModelStoreTest(unittest.TestCase):
    """Verify freshness filtering and supported ASK query types."""

    @staticmethod
    def state(robot_id, stamp):
        """Build a minimal timestamped RobotState."""
        message = RobotState()
        message.header.stamp = stamp
        message.robot_id = robot_id
        message.is_connected = True
        return message

    def test_snapshot_sorts_agents_and_expires_old_state(self):
        store = WorldModelStore(3.0)
        store.update_state(
            "drone",
            self.state("drone", rospy.Time.from_sec(9.0)),
            rospy.Time.from_sec(9.0),
        )
        store.update_state(
            "car",
            self.state("car", rospy.Time.from_sec(5.0)),
            rospy.Time.from_sec(5.0),
        )
        snapshot = store.snapshot(rospy.Time.from_sec(10.0))
        self.assertEqual(
            [state.robot_id for state in snapshot.agents],
            ["drone"],
        )

    def test_agent_query_filters_snapshot(self):
        store = WorldModelStore(3.0)
        now = rospy.Time.from_sec(10.0)
        for robot_id in ("drone", "car"):
            store.update_state(
                robot_id, self.state(robot_id, now), now
            )
        result, found = store.query("agent", ["car"], now)
        self.assertTrue(found)
        self.assertEqual(
            [state.robot_id for state in result.agents], ["car"]
        )

    def test_landmark_query_selects_highest_confidence(self):
        store = WorldModelStore(3.0)
        first = SemanticLandmark()
        first.semantic_label = "red_ball"
        first.confidence = 0.5
        second = SemanticLandmark()
        second.semantic_label = "red_ball"
        second.confidence = 0.9
        with store.lock:
            store.landmarks = [first, second]
        result, found = store.query(
            "nearest_landmark",
            ["red_ball"],
            rospy.Time.from_sec(10.0),
        )
        self.assertTrue(found)
        self.assertEqual(len(result.landmarks), 1)
        self.assertAlmostEqual(result.landmarks[0].confidence, 0.9)

    def test_unknown_query_reports_not_found(self):
        store = WorldModelStore(3.0)
        _, found = store.query(
            "unsupported", [], rospy.Time.from_sec(10.0)
        )
        self.assertFalse(found)


if __name__ == "__main__":
    unittest.main()
