#!/usr/bin/env python3
"""Unit tests for Task-05 gimbal command validation."""

import math
import sys
from pathlib import Path
from unittest import TestCase


SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from gimbal_controller import clamp_command, validate_limits  # noqa: E402


class GimbalCommandTest(TestCase):
    def test_command_inside_limits_is_unchanged(self):
        self.assertEqual(clamp_command(0.25, -1.0, 1.0), 0.25)

    def test_command_is_clamped_at_both_limits(self):
        self.assertEqual(clamp_command(2.0, -1.0, 1.0), 1.0)
        self.assertEqual(clamp_command(-2.0, -1.0, 1.0), -1.0)

    def test_limit_boundaries_are_accepted(self):
        self.assertEqual(clamp_command(-1.0, -1.0, 1.0), -1.0)
        self.assertEqual(clamp_command(1.0, -1.0, 1.0), 1.0)

    def test_non_finite_command_is_rejected(self):
        for value in (math.nan, math.inf, -math.inf):
            with self.assertRaises(ValueError):
                clamp_command(value, -1.0, 1.0)

    def test_invalid_limits_are_rejected(self):
        invalid_limits = (
            (1.0, 1.0),
            (2.0, 1.0),
            (math.nan, 1.0),
            (-1.0, math.inf),
        )
        for lower, upper in invalid_limits:
            with self.assertRaises(ValueError):
                validate_limits(lower, upper)
