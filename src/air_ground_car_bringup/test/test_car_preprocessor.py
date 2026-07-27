#!/usr/bin/env python3
"""Unit tests for Task-07 car edge preprocessing helpers."""

import math
import sys
import unittest
from pathlib import Path

from sensor_msgs.msg import LaserScan


SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from car_preprocessor import (  # noqa: E402
    positive_float,
    sanitize_lidar_ranges,
    sanitize_ultrasonic,
)


class CarPreprocessorHelpersTest(unittest.TestCase):
    """Verify bounded and finite car sensor preprocessing."""

    def test_lidar_downsamples_and_replaces_non_finite_values(self):
        values = [1.0, 2.0, math.inf, 4.0, math.nan, 6.0, -2.0, 8.0]
        self.assertEqual(
            sanitize_lidar_ranges(values, 2),
            [1.0, -1.0, -1.0, -1.0],
        )

    def test_lidar_rejects_invalid_downsample(self):
        with self.assertRaises(ValueError):
            sanitize_lidar_ranges([1.0], 0)

    def test_ultrasonic_defaults_and_clamps(self):
        self.assertEqual(sanitize_ultrasonic(None, 0.02, 4.0), 4.0)
        message = LaserScan()
        message.ranges = [10.0]
        self.assertEqual(
            sanitize_ultrasonic(message, 0.02, 4.0), 4.0
        )
        message.ranges = [0.001]
        self.assertEqual(
            sanitize_ultrasonic(message, 0.02, 4.0), 0.02
        )

    def test_positive_float_rejects_non_finite_values(self):
        for value in (0.0, -1.0, math.inf, math.nan):
            with self.assertRaises(ValueError):
                positive_float(value, "value")


if __name__ == "__main__":
    unittest.main()
