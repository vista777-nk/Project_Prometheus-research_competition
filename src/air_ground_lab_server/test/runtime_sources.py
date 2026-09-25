#!/usr/bin/env python3
"""Deterministic ROS sensor sources for Task-07 runtime acceptance."""

import math
import struct

import rospy
from geometry_msgs.msg import PoseStamped
from nav_msgs.msg import Odometry
from sensor_msgs.msg import Image, Imu, LaserScan


def make_image(width, height, frame_id):
    """Build a deterministic RGB image."""
    message = Image()
    message.header.frame_id = frame_id
    message.height = height
    message.width = width
    message.encoding = "rgb8"
    message.is_bigendian = False
    message.step = width * 3
    message.data = bytes(width * height * 3)
    return message


def make_depth(width, height):
    """Build a small deterministic 32-bit floating depth image."""
    message = Image()
    message.header.frame_id = "drone/camera_depth_optical_frame"
    message.height = height
    message.width = width
    message.encoding = "32FC1"
    message.is_bigendian = False
    message.step = width * 4
    message.data = struct.pack(f"<{width * height}f", *([2.0] * (width * height)))
    return message


def make_scan():
    """Build a 360-sample finite LiDAR scan."""
    message = LaserScan()
    message.header.frame_id = "car/lidar_link"
    message.angle_min = -math.pi
    message.angle_max = math.pi
    message.angle_increment = 2.0 * math.pi / 360.0
    message.range_min = 0.15
    message.range_max = 12.0
    message.ranges = [1.25] * 360
    return message


def main():
    """Publish all Task-07 edge inputs until shutdown."""
    rospy.init_node(
        "task07_runtime_sources",
        anonymous=True,
        disable_signals=True,
    )
    rospy.set_param("/car/current_chassis", "diff")
    publishers = {
        "car_image": rospy.Publisher(
            "/car/openmv/image_raw", Image, queue_size=2
        ),
        "car_scan": rospy.Publisher(
            "/car/scan", LaserScan, queue_size=2
        ),
        "car_imu": rospy.Publisher(
            "/car/imu/data", Imu, queue_size=2
        ),
        "car_odom": rospy.Publisher(
            "/car/odom", Odometry, queue_size=2
        ),
        "drone_rgb": rospy.Publisher(
            "/drone/camera/rgb/image_raw", Image, queue_size=2
        ),
        "drone_depth": rospy.Publisher(
            "/drone/camera/depth/image_raw", Image, queue_size=2
        ),
        "drone_imu": rospy.Publisher(
            "/mavros/imu/data", Imu, queue_size=2
        ),
        "drone_pose": rospy.Publisher(
            "/drone/gps/local_pose", PoseStamped, queue_size=2
        ),
    }
    ultrasonic_publishers = {
        direction: rospy.Publisher(
            f"/car/ultrasonic/{direction}",
            LaserScan,
            queue_size=2,
        )
        for direction in ("front", "rear", "left", "right")
    }

    car_image = make_image(320, 240, "car/openmv_optical_frame")
    drone_rgb = make_image(160, 90, "drone/camera_rgb_optical_frame")
    drone_depth = make_depth(64, 48)
    scan = make_scan()

    car_imu = Imu()
    car_imu.header.frame_id = "car/imu_link"
    car_imu.orientation.w = 1.0
    car_imu.angular_velocity.z = 0.1
    car_imu.linear_acceleration.z = 9.81

    drone_imu = Imu()
    drone_imu.header.frame_id = "drone/base_link"
    drone_imu.orientation.w = 1.0
    drone_imu.angular_velocity.z = 0.2
    drone_imu.linear_acceleration.z = 9.81

    odometry = Odometry()
    odometry.header.frame_id = "map"
    odometry.child_frame_id = "car/base_link"
    odometry.pose.pose.position.x = 1.5
    odometry.pose.pose.orientation.w = 1.0
    odometry.twist.twist.linear.x = 0.2

    pose = PoseStamped()
    pose.header.frame_id = "map"
    pose.pose.position.x = 2.5
    pose.pose.position.y = -1.0
    pose.pose.position.z = 3.0
    pose.pose.orientation.w = 1.0

    ultrasonic = {}
    for index, direction in enumerate(
        ("front", "rear", "left", "right")
    ):
        message = LaserScan()
        message.header.frame_id = f"car/ultrasonic_{direction}_link"
        message.range_min = 0.02
        message.range_max = 4.0
        message.ranges = [0.6 + index * 0.1]
        ultrasonic[direction] = message

    rate = rospy.Rate(10)
    while not rospy.is_shutdown():
        stamp = rospy.Time.now()
        for message in (
            car_image,
            scan,
            car_imu,
            odometry,
            drone_rgb,
            drone_depth,
            drone_imu,
            pose,
        ):
            message.header.stamp = stamp
        publishers["car_image"].publish(car_image)
        publishers["car_scan"].publish(scan)
        publishers["car_imu"].publish(car_imu)
        publishers["car_odom"].publish(odometry)
        publishers["drone_rgb"].publish(drone_rgb)
        publishers["drone_depth"].publish(drone_depth)
        publishers["drone_imu"].publish(drone_imu)
        publishers["drone_pose"].publish(pose)
        for direction, publisher in ultrasonic_publishers.items():
            ultrasonic[direction].header.stamp = stamp
            publisher.publish(ultrasonic[direction])
        rate.sleep()


if __name__ == "__main__":
    main()
