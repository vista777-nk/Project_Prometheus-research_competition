#!/usr/bin/env python3
"""Convert MAVROS global GPS fixes into a local ENU PoseStamped."""

import math
from typing import Tuple

import rospy
from geometry_msgs.msg import PoseStamped
from sensor_msgs.msg import NavSatFix, NavSatStatus


DEFAULT_HOME_LAT = 47.397742
DEFAULT_HOME_LON = 8.545594
DEFAULT_HOME_ALT = 488.0


def compute_meters_per_degree(latitude_deg: float) -> Tuple[float, float]:
    """Return WGS84 metres per degree of latitude and longitude."""
    latitude_rad = math.radians(latitude_deg)
    meters_per_degree_lat = (
        111132.92
        - 559.82 * math.cos(2.0 * latitude_rad)
        + 1.175 * math.cos(4.0 * latitude_rad)
        - 0.0023 * math.cos(6.0 * latitude_rad)
    )
    meters_per_degree_lon = (
        111412.84 * math.cos(latitude_rad)
        - 93.5 * math.cos(3.0 * latitude_rad)
        + 0.118 * math.cos(5.0 * latitude_rad)
    )
    return meters_per_degree_lat, meters_per_degree_lon


def geodetic_to_local_enu(
    latitude: float,
    longitude: float,
    altitude: float,
    home_latitude: float,
    home_longitude: float,
    home_altitude: float,
) -> Tuple[float, float, float]:
    """Approximate a nearby WGS84 fix as local east, north and up metres."""
    values = (
        latitude,
        longitude,
        altitude,
        home_latitude,
        home_longitude,
        home_altitude,
    )
    if not all(math.isfinite(value) for value in values):
        raise ValueError("GPS coordinates and home position must be finite")

    meters_per_degree_lat, meters_per_degree_lon = compute_meters_per_degree(
        home_latitude
    )
    east = (longitude - home_longitude) * meters_per_degree_lon
    north = (latitude - home_latitude) * meters_per_degree_lat
    up = altitude - home_altitude
    return east, north, up


class GpsConverter:
    """ROS adapter from sensor_msgs/NavSatFix to local geometry_msgs/PoseStamped."""

    def __init__(self) -> None:
        gps_config = rospy.get_param("~gps", {})
        self.home_lat = float(
            rospy.get_param(
                "~home_lat", gps_config.get("home_lat", DEFAULT_HOME_LAT)
            )
        )
        self.home_lon = float(
            rospy.get_param(
                "~home_lon", gps_config.get("home_lon", DEFAULT_HOME_LON)
            )
        )
        self.home_alt = float(
            rospy.get_param(
                "~home_alt", gps_config.get("home_alt", DEFAULT_HOME_ALT)
            )
        )
        self.frame_id = str(
            rospy.get_param(
                "~frame_id", gps_config.get("local_frame_id", "map")
            )
        )
        input_topic = str(
            rospy.get_param(
                "~input_topic",
                gps_config.get(
                    "source_topic", "/mavros/global_position/global"
                ),
            )
        )
        output_topic = str(
            rospy.get_param(
                "~output_topic",
                gps_config.get(
                    "local_pose_topic", "/drone/gps/local_pose"
                ),
            )
        )

        geodetic_to_local_enu(
            self.home_lat,
            self.home_lon,
            self.home_alt,
            self.home_lat,
            self.home_lon,
            self.home_alt,
        )

        self.publisher = rospy.Publisher(
            output_topic, PoseStamped, queue_size=10
        )
        self.subscriber = rospy.Subscriber(
            input_topic, NavSatFix, self.callback, queue_size=10
        )
        rospy.loginfo(
            "[gps_converter] home=(%.7f, %.7f, %.3f), input=%s, output=%s",
            self.home_lat,
            self.home_lon,
            self.home_alt,
            input_topic,
            output_topic,
        )

    def callback(self, message: NavSatFix) -> None:
        if message.status.status < NavSatStatus.STATUS_FIX:
            rospy.logwarn_throttle(
                5.0, "[gps_converter] ignoring NavSatFix without a fix"
            )
            return

        try:
            east, north, up = geodetic_to_local_enu(
                message.latitude,
                message.longitude,
                message.altitude,
                self.home_lat,
                self.home_lon,
                self.home_alt,
            )
        except ValueError as error:
            rospy.logwarn_throttle(5.0, "[gps_converter] %s", error)
            return

        pose = PoseStamped()
        pose.header.seq = message.header.seq
        pose.header.stamp = message.header.stamp
        pose.header.frame_id = self.frame_id
        pose.pose.position.x = east
        pose.pose.position.y = north
        pose.pose.position.z = up
        pose.pose.orientation.w = 1.0
        self.publisher.publish(pose)


def main() -> None:
    rospy.init_node("gps_converter")
    GpsConverter()
    rospy.spin()


if __name__ == "__main__":
    main()
