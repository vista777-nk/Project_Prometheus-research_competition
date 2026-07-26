#!/usr/bin/env python3
"""Unit tests for Task-04 mecanum kinematics."""

import math
import sys
from pathlib import Path
from unittest import TestCase


SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from mecanum_controller import (  # noqa: E402
    forward_kinematics,
    inverse_kinematics,
)


class MecanumKinematicsTest(TestCase):
    WHEEL_BASE = 0.20
    TRACK_WIDTH = 0.18
    WHEEL_RADIUS = 0.033
    MAX_SPEED = 100.0

    def inverse(self, linear_x, linear_y, angular_z, max_speed=None):
        return inverse_kinematics(
            linear_x,
            linear_y,
            angular_z,
            self.WHEEL_BASE,
            self.TRACK_WIDTH,
            self.WHEEL_RADIUS,
            self.MAX_SPEED if max_speed is None else max_speed,
        )

    def forward(self, wheel_speeds):
        return forward_kinematics(
            wheel_speeds,
            self.WHEEL_BASE,
            self.TRACK_WIDTH,
            self.WHEEL_RADIUS,
        )

    def test_forward_pattern(self):
        wheel_speeds = self.inverse(0.4, 0.0, 0.0)
        self.assertTrue(all(speed > 0.0 for speed in wheel_speeds))
        self.assertAlmostEqual(max(wheel_speeds), min(wheel_speeds))

    def test_lateral_pattern(self):
        front_left, front_right, rear_left, rear_right = self.inverse(
            0.0, 0.3, 0.0
        )
        self.assertLess(front_left, 0.0)
        self.assertGreater(front_right, 0.0)
        self.assertGreater(rear_left, 0.0)
        self.assertLess(rear_right, 0.0)

    def test_rotation_pattern(self):
        front_left, front_right, rear_left, rear_right = self.inverse(
            0.0, 0.0, 0.8
        )
        self.assertLess(front_left, 0.0)
        self.assertGreater(front_right, 0.0)
        self.assertLess(rear_left, 0.0)
        self.assertGreater(rear_right, 0.0)

    def test_forward_inverse_round_trip(self):
        expected = (0.25, -0.12, 0.7)
        actual = self.forward(self.inverse(*expected))
        for actual_value, expected_value in zip(actual, expected):
            self.assertAlmostEqual(actual_value, expected_value, places=9)

    def test_saturation_preserves_ratios(self):
        unconstrained = self.inverse(1.0, 0.5, 2.0)
        constrained = self.inverse(1.0, 0.5, 2.0, max_speed=5.0)
        self.assertAlmostEqual(max(abs(value) for value in constrained), 5.0)
        ratios = [
            limited / raw
            for raw, limited in zip(unconstrained, constrained)
            if not math.isclose(raw, 0.0)
        ]
        for ratio in ratios[1:]:
            self.assertAlmostEqual(ratio, ratios[0], places=9)

    def test_invalid_geometry_is_rejected(self):
        with self.assertRaises(ValueError):
            inverse_kinematics(
                0.0,
                0.0,
                0.0,
                self.WHEEL_BASE,
                self.TRACK_WIDTH,
                0.0,
                self.MAX_SPEED,
            )

    def test_non_finite_command_is_rejected(self):
        with self.assertRaises(ValueError):
            self.inverse(math.nan, 0.0, 0.0)
