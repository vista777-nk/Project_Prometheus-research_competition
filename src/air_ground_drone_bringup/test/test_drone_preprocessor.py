#!/usr/bin/env python3
"""Unit tests for Task-07 drone edge preprocessing helpers."""

import math
import sys
import unittest
from pathlib import Path

import numpy


SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from drone_preprocessor import (  # noqa: E402
    positive_float,
    resize_to_width,
)


class DronePreprocessorHelpersTest(unittest.TestCase):
    """Verify drone image sizing and numeric configuration validation."""

    def test_resize_preserves_aspect_ratio(self):
        image = numpy.zeros((480, 848, 3), dtype=numpy.uint8)
        resized = resize_to_width(image, 424)
        self.assertEqual(resized.shape, (240, 424, 3))

    def test_resize_does_not_enlarge(self):
        image = numpy.zeros((48, 64, 3), dtype=numpy.uint8)
        self.assertIs(resize_to_width(image, 424), image)

    def test_invalid_width_and_rate_are_rejected(self):
        image = numpy.zeros((48, 64, 3), dtype=numpy.uint8)
        with self.assertRaises(ValueError):
            resize_to_width(image, 0)
        for value in (0.0, -1.0, math.inf, math.nan):
            with self.assertRaises(ValueError):
                positive_float(value, "value")


if __name__ == "__main__":
    unittest.main()
