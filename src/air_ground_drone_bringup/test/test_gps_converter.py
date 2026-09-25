#!/usr/bin/env python3
"""Unit tests for the Task-02 local ENU approximation."""

import math
import sys
from pathlib import Path
from unittest import TestCase
from unittest.mock import Mock, patch

from sensor_msgs.msg import NavSatFix, NavSatStatus


SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

from gps_converter import (  # noqa: E402
    DEFAULT_HOME_ALT,
    DEFAULT_HOME_LAT,
    DEFAULT_HOME_LON,
    GpsConverter,
    compute_meters_per_degree,
    geodetic_to_local_enu,
)


class GpsConverterTest(TestCase):
    def test_home_maps_to_origin(self):
        self.assertEqual(
            geodetic_to_local_enu(
                DEFAULT_HOME_LAT,
                DEFAULT_HOME_LON,
                DEFAULT_HOME_ALT,
                DEFAULT_HOME_LAT,
                DEFAULT_HOME_LON,
                DEFAULT_HOME_ALT,
            ),
            (0.0, 0.0, 0.0),
        )

    def test_axes_are_east_north_up(self):
        meters_per_latitude, meters_per_longitude = compute_meters_per_degree(
            DEFAULT_HOME_LAT
        )
        east, north, up = geodetic_to_local_enu(
            DEFAULT_HOME_LAT + 2.0 / meters_per_latitude,
            DEFAULT_HOME_LON + 1.0 / meters_per_longitude,
            DEFAULT_HOME_ALT + 3.0,
            DEFAULT_HOME_LAT,
            DEFAULT_HOME_LON,
            DEFAULT_HOME_ALT,
        )

        self.assertAlmostEqual(east, 1.0, places=6)
        self.assertAlmostEqual(north, 2.0, places=6)
        self.assertAlmostEqual(up, 3.0, places=6)

    def test_default_scale_is_physical(self):
        meters_per_latitude, meters_per_longitude = compute_meters_per_degree(
            DEFAULT_HOME_LAT
        )
        self.assertGreater(meters_per_latitude, 110_000.0)
        self.assertLess(meters_per_latitude, 112_000.0)
        self.assertGreater(meters_per_longitude, 74_000.0)
        self.assertLess(meters_per_longitude, 76_000.0)

    def test_non_finite_fix_is_rejected(self):
        with self.assertRaises(ValueError):
            geodetic_to_local_enu(
                math.nan,
                DEFAULT_HOME_LON,
                DEFAULT_HOME_ALT,
                DEFAULT_HOME_LAT,
                DEFAULT_HOME_LON,
                DEFAULT_HOME_ALT,
            )

    def test_callback_preserves_input_header(self):
        converter = self._converter_without_ros_init()
        message = NavSatFix()
        message.header.seq = 7
        message.header.frame_id = "gps_link"
        message.status.status = NavSatStatus.STATUS_FIX
        message.latitude = DEFAULT_HOME_LAT
        message.longitude = DEFAULT_HOME_LON
        message.altitude = DEFAULT_HOME_ALT

        converter.callback(message)

        converter.publisher.publish.assert_called_once()
        pose = converter.publisher.publish.call_args.args[0]
        self.assertEqual(pose.header.seq, 7)
        self.assertEqual(pose.header.frame_id, "map")
        self.assertEqual(message.header.frame_id, "gps_link")

    def test_callback_rejects_missing_fix(self):
        converter = self._converter_without_ros_init()
        message = NavSatFix()
        message.status.status = NavSatStatus.STATUS_NO_FIX

        with patch("gps_converter.rospy.logwarn_throttle") as logwarn:
            converter.callback(message)

        converter.publisher.publish.assert_not_called()
        logwarn.assert_called_once()

    @staticmethod
    def _converter_without_ros_init():
        converter = GpsConverter.__new__(GpsConverter)
        converter.home_lat = DEFAULT_HOME_LAT
        converter.home_lon = DEFAULT_HOME_LON
        converter.home_alt = DEFAULT_HOME_ALT
        converter.frame_id = "map"
        converter.publisher = Mock()
        return converter
