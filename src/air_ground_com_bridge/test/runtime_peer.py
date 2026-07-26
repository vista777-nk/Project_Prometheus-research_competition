#!/usr/bin/env python3
"""Deterministic UDP, TCP and ROS peers for Task-06 runtime acceptance."""

import argparse
import base64
import json
import math
import socket
import struct
import sys
import time
from pathlib import Path


SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))


def receive_exact(connection, length):
    data = bytearray()
    while len(data) < length:
        chunk = connection.recv(length - len(data))
        if not chunk:
            raise EOFError("peer disconnected")
        data.extend(chunk)
    return bytes(data)


def receive_json(connection):
    length = struct.unpack("!I", receive_exact(connection, 4))[0]
    if not 1 <= length <= 10 * 1024 * 1024:
        raise ValueError("invalid frame length")
    return json.loads(receive_exact(connection, length).decode("utf-8"))


def encode_json(payload):
    data = json.dumps(
        payload, allow_nan=False, separators=(",", ":")
    ).encode("utf-8")
    return struct.pack("!I", len(data)) + data


def telemetry_is_complete(payload):
    try:
        image = base64.b64decode(
            payload["image_jpeg_b64"], validate=True
        )
        ranges = payload["scan"]["ranges"]
        ultrasonic = payload["ultrasonic"]
        return (
            payload["kind"] == "telemetry"
            and payload["source"] == "car"
            and payload["protocol_version"] == 1
            and len(ranges) == 90
            and all(math.isfinite(float(value)) for value in ranges)
            and set(ultrasonic)
            == {"front", "rear", "left", "right"}
            and image.startswith(b"\xff\xd8")
            and "pose" in payload
            and "imu" in payload
            and "drone_pose" in payload
        )
    except (KeyError, TypeError, ValueError):
        return False


def wait_for_telemetry(connection, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        payload = receive_json(connection)
        if telemetry_is_complete(payload):
            return payload
    raise TimeoutError("complete telemetry was not received")


def run_server(args):
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((args.host, args.port))
    server.listen(2)
    server.settimeout(args.timeout)
    print("SERVER_READY", flush=True)
    try:
        connection, _ = server.accept()
        connection.settimeout(args.timeout)
        with connection:
            wait_for_telemetry(connection, args.timeout)
            print("SERVER_TELEMETRY_OK", flush=True)
            command = encode_json(
                {
                    "target_id": "car",
                    "command_type": "navigate",
                    "query_text": "bridge-test",
                    "target_pose": {
                        "x": 1.0,
                        "y": -2.0,
                        "z": 0.5,
                        "qw": 1.0,
                    },
                    "target_velocity": 0.4,
                }
            )
            for _ in range(10):
                connection.sendall(command[:2])
                time.sleep(0.02)
                connection.sendall(command[2:])
                time.sleep(0.15)
            print("SERVER_COMMAND_SENT", flush=True)

        connection, _ = server.accept()
        connection.settimeout(args.timeout)
        with connection:
            wait_for_telemetry(connection, args.timeout)
            print("SERVER_RECONNECT_OK", flush=True)
            time.sleep(1.0)
    finally:
        server.close()


def run_drone(args):
    from drone_car_bridge import (
        MAVLINK_MSG_ID_COMMAND_LONG,
        MAVLINK_MSG_ID_HEARTBEAT,
        MavlinkV1Parser,
        build_mavlink_v1_frame,
    )

    peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    peer.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    peer.bind((args.host, args.source_port))
    peer.settimeout(0.1)
    heartbeat_payload = struct.pack(
        "<IBBBBB", 4, 2, 12, 0x80, 4, 3
    )
    parser = MavlinkV1Parser()
    sequence = 0
    deadline = time.monotonic() + args.timeout
    next_heartbeat = 0.0
    command_seen = False
    try:
        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_heartbeat:
                frame = build_mavlink_v1_frame(
                    MAVLINK_MSG_ID_HEARTBEAT,
                    heartbeat_payload,
                    sequence,
                    1,
                    1,
                )
                peer.sendto(
                    frame, (args.host, args.destination_port)
                )
                sequence = (sequence + 1) & 0xFF
                next_heartbeat = now + 0.2
            try:
                data, address = peer.recvfrom(4096)
            except socket.timeout:
                continue
            if address != (args.host, args.destination_port):
                continue
            for message in parser.feed(data):
                if (
                    message["message_id"]
                    != MAVLINK_MSG_ID_COMMAND_LONG
                ):
                    continue
                values = struct.unpack(
                    "<7fHBBB", message["payload"]
                )
                if (
                    values[7] == 400
                    and values[8] == 1
                    and values[9] == 1
                    and abs(values[0] - 1.0) < 1e-6
                ):
                    command_seen = True
                    print("DRONE_COMMAND_OK", flush=True)
            if command_seen:
                next_heartbeat = min(
                    next_heartbeat, time.monotonic() + 0.2
                )
        if not command_seen:
            raise RuntimeError("valid COMMAND_LONG was not received")
    finally:
        peer.close()


def run_publishers(args):
    import rospy
    from geometry_msgs.msg import PoseStamped
    from nav_msgs.msg import Odometry
    from sensor_msgs.msg import Image, Imu, LaserScan

    rospy.init_node(
        "task06_runtime_publishers",
        anonymous=True,
        disable_signals=True,
    )
    pose_publisher = rospy.Publisher(
        "/drone/gps/local_pose", PoseStamped, queue_size=2
    )
    odometry_publisher = rospy.Publisher(
        "/car/odom", Odometry, queue_size=2
    )
    imu_publisher = rospy.Publisher(
        "/car/imu/data", Imu, queue_size=2
    )
    scan_publisher = rospy.Publisher(
        "/car/scan", LaserScan, queue_size=2
    )
    image_publisher = rospy.Publisher(
        "/car/openmv/image_raw", Image, queue_size=2
    )
    ultrasonic_publishers = {
        direction: rospy.Publisher(
            f"/car/ultrasonic/{direction}",
            LaserScan,
            queue_size=2,
        )
        for direction in ("front", "rear", "left", "right")
    }

    pose = PoseStamped()
    pose.header.frame_id = "map"
    pose.pose.position.x = 1.25
    pose.pose.position.y = -0.5
    pose.pose.position.z = 2.0
    pose.pose.orientation.w = 1.0

    odometry = Odometry()
    odometry.header.frame_id = "odom"
    odometry.child_frame_id = "base_link"
    odometry.pose.pose.position.x = 0.4
    odometry.pose.pose.orientation.w = 1.0
    odometry.twist.twist.linear.x = 0.1
    odometry.twist.twist.angular.z = 0.2

    imu = Imu()
    imu.header.frame_id = "imu_link"
    imu.orientation.w = 1.0
    imu.angular_velocity.z = 0.1
    imu.linear_acceleration.z = 9.81

    scan = LaserScan()
    scan.header.frame_id = "lidar_link"
    scan.angle_min = -math.pi
    scan.angle_max = math.pi
    scan.angle_increment = 2.0 * math.pi / 360.0
    scan.range_min = 0.15
    scan.range_max = 12.0
    scan.ranges = [0.8] * 360

    image = Image()
    image.header.frame_id = "openmv_camera_optical_link"
    image.height = 240
    image.width = 320
    image.encoding = "rgb8"
    image.is_bigendian = False
    image.step = 960
    image.data = bytes(320 * 240 * 3)

    ultrasonic = {}
    for index, direction in enumerate(
        ("front", "rear", "left", "right")
    ):
        message = LaserScan()
        message.header.frame_id = f"ultrasonic_{direction}_link"
        message.range_min = 0.02
        message.range_max = 4.0
        message.ranges = [0.5 + index * 0.1]
        ultrasonic[direction] = message

    rate = rospy.Rate(10)
    deadline = time.monotonic() + args.timeout
    while not rospy.is_shutdown() and time.monotonic() < deadline:
        stamp = rospy.Time.now()
        for message in (pose, odometry, imu, scan, image):
            message.header.stamp = stamp
        pose_publisher.publish(pose)
        odometry_publisher.publish(odometry)
        imu_publisher.publish(imu)
        scan_publisher.publish(scan)
        image_publisher.publish(image)
        for direction, publisher in ultrasonic_publishers.items():
            ultrasonic[direction].header.stamp = stamp
            publisher.publish(ultrasonic[direction])
        rate.sleep()


def main():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="mode", required=True)

    server = subparsers.add_parser("server")
    server.add_argument("--host", default="127.0.0.1")
    server.add_argument("--port", type=int, default=9090)
    server.add_argument("--timeout", type=float, default=45.0)

    drone = subparsers.add_parser("drone")
    drone.add_argument("--host", default="127.0.0.1")
    drone.add_argument("--source-port", type=int, default=18570)
    drone.add_argument("--destination-port", type=int, default=14550)
    drone.add_argument("--timeout", type=float, default=45.0)

    publishers = subparsers.add_parser("publishers")
    publishers.add_argument("--timeout", type=float, default=45.0)

    arguments = parser.parse_args()
    if arguments.mode == "server":
        run_server(arguments)
    elif arguments.mode == "drone":
        run_drone(arguments)
    else:
        run_publishers(arguments)


if __name__ == "__main__":
    main()
