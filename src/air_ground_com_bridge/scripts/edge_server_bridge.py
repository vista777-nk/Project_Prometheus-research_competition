#!/usr/bin/env python3
"""Bridge cached ROS telemetry to a framed JSON TCP connection."""

import base64
import ipaddress
import json
import math
import socket
import struct
import threading
import time
from pathlib import Path
from typing import Dict, Optional

import cv2
import rospy
import yaml
from air_ground_interfaces.msg import ServerCommand
from cv_bridge import CvBridge, CvBridgeError
from geometry_msgs.msg import PoseStamped
from nav_msgs.msg import Odometry
from sensor_msgs.msg import Image, Imu, LaserScan
from std_msgs.msg import Bool


def _finite_number(value, label: str) -> float:
    number = float(value)
    if not math.isfinite(number):
        raise ValueError(f"{label} must be finite")
    return number


def _positive_number(value, label: str) -> float:
    number = _finite_number(value, label)
    if number <= 0.0:
        raise ValueError(f"{label} must be positive")
    return number


def _port(value, label: str) -> int:
    port = int(value)
    if not 1 <= port <= 65535:
        raise ValueError(f"{label} must be in [1, 65535]")
    return port


def load_network_config(config_path: str) -> Dict:
    """Load and validate TCP and bandwidth settings."""
    path = Path(config_path).expanduser().resolve()
    with path.open(encoding="utf-8") as stream:
        root = yaml.safe_load(stream)
    if not isinstance(root, dict):
        raise ValueError("network config must be a mapping")
    network = root.get("edge_server_tcp")
    throttle = root.get("throttle", {})
    if not isinstance(network, dict) or not isinstance(throttle, dict):
        raise ValueError("TCP and throttle config must be mappings")
    if str(network.get("protocol", "")).lower() != "tcp":
        raise ValueError("edge_server_tcp.protocol must be tcp")
    ipaddress.ip_address(str(network["server_ip"]))
    network["server_port"] = _port(
        network["server_port"], "server_port"
    )
    for key, default in (
        ("reconnect_interval", 3.0),
        ("connect_timeout", 2.0),
        ("socket_timeout", 1.0),
        ("heartbeat_interval", 2.0),
        ("publish_rate", 10.0),
    ):
        network[key] = _positive_number(
            network.get(key, default), key
        )
    network["max_payload_bytes"] = int(
        network.get("max_payload_bytes", 10 * 1024 * 1024)
    )
    if not 1024 <= network["max_payload_bytes"] <= 64 * 1024 * 1024:
        raise ValueError("max_payload_bytes must be in [1 KiB, 64 MiB]")

    throttle["enable"] = bool(throttle.get("enable", True))
    throttle["max_bandwidth_bytes_per_sec"] = int(
        throttle.get("max_bandwidth_bytes_per_sec", 24000)
    )
    if throttle["max_bandwidth_bytes_per_sec"] <= 0:
        raise ValueError("max bandwidth must be positive")
    throttle["image_quality"] = int(
        throttle.get("image_quality", 50)
    )
    if not 1 <= throttle["image_quality"] <= 100:
        raise ValueError("image_quality must be in [1, 100]")
    throttle["max_image_freq"] = _positive_number(
        throttle.get("max_image_freq", 2.0), "max_image_freq"
    )
    throttle["lidar_downsample"] = int(
        throttle.get("lidar_downsample", 4)
    )
    if throttle["lidar_downsample"] <= 0:
        raise ValueError("lidar_downsample must be positive")
    return root


def encode_json_frame(payload: Dict, max_payload_bytes: int) -> bytes:
    """Encode strict JSON with a network-order four-byte length prefix."""
    data = json.dumps(
        payload,
        allow_nan=False,
        ensure_ascii=False,
        separators=(",", ":"),
    ).encode("utf-8")
    if not data or len(data) > max_payload_bytes:
        raise ValueError("JSON payload length is outside configured bounds")
    return struct.pack("!I", len(data)) + data


def decode_server_command(payload: Dict) -> Dict:
    """Validate the Task-06 compatibility command JSON."""
    if not isinstance(payload, dict):
        raise ValueError("server command must be a JSON object")
    target = payload.get("target_pose", {})
    if not isinstance(target, dict):
        raise ValueError("target_pose must be a JSON object")
    pose = {}
    for key, default in (
        ("x", 0.0),
        ("y", 0.0),
        ("z", 0.0),
        ("qx", 0.0),
        ("qy", 0.0),
        ("qz", 0.0),
        ("qw", 1.0),
    ):
        pose[key] = _finite_number(
            target.get(key, default), f"target_pose.{key}"
        )
    return {
        "target_id": str(payload.get("target_id", "car")),
        "command_type": str(payload.get("command_type", "")),
        "query_text": str(payload.get("query_text", "")),
        "target_pose": pose,
        "target_velocity": _finite_number(
            payload.get("target_velocity", 0.0),
            "target_velocity",
        ),
    }


class TokenBucket:
    """Bound output bandwidth while allowing one second of burst."""

    def __init__(self, rate: int) -> None:
        if rate <= 0:
            raise ValueError("token rate must be positive")
        self.rate = float(rate)
        self.capacity = float(rate)
        self.tokens = self.capacity
        self.updated_at = time.monotonic()
        self.lock = threading.Lock()

    def consume(self, amount: int) -> bool:
        if amount < 0:
            raise ValueError("token amount cannot be negative")
        with self.lock:
            now = time.monotonic()
            self.tokens = min(
                self.capacity,
                self.tokens + (now - self.updated_at) * self.rate,
            )
            self.updated_at = now
            if amount > self.tokens:
                return False
            self.tokens -= amount
            return True


class EdgeServerBridge:
    """Serialize local car data and reconnect to the laboratory server."""

    def __init__(self) -> None:
        config_path = str(rospy.get_param("~config_path"))
        config = load_network_config(config_path)
        network = config["edge_server_tcp"]
        self.throttle_config = config["throttle"]

        self.server_address = (
            str(network["server_ip"]),
            int(network["server_port"]),
        )
        self.reconnect_interval = float(
            network["reconnect_interval"]
        )
        self.connect_timeout = float(network["connect_timeout"])
        self.socket_timeout = float(network["socket_timeout"])
        self.heartbeat_interval = float(
            network["heartbeat_interval"]
        )
        self.max_payload_bytes = int(network["max_payload_bytes"])
        self.last_empty_heartbeat = 0.0
        self.last_image_time = 0.0
        self.bridge = CvBridge()

        self.latest = {
            "odom": None,
            "imu": None,
            "scan": None,
            "image": None,
            "ultrasonic": {},
            "drone_pose": None,
        }
        self.latest_lock = threading.Lock()
        self.socket_lock = threading.Lock()
        self.transmit_lock = threading.Lock()
        self.socket: Optional[socket.socket] = None
        self.running = True
        self.stop_event = threading.Event()
        self.token_bucket = TokenBucket(
            int(
                self.throttle_config[
                    "max_bandwidth_bytes_per_sec"
                ]
            )
        )

        self.command_publisher = rospy.Publisher(
            "/car/server_command",
            ServerCommand,
            queue_size=10,
        )
        self.connected_publisher = rospy.Publisher(
            "/car/server_connected",
            Bool,
            queue_size=2,
            latch=True,
        )
        self.connected_publisher.publish(Bool(False))

        self.subscribers = [
            rospy.Subscriber(
                "/car/odom",
                Odometry,
                self.cache_callback("odom"),
                queue_size=5,
            ),
            rospy.Subscriber(
                "/car/imu/data",
                Imu,
                self.cache_callback("imu"),
                queue_size=5,
            ),
            rospy.Subscriber(
                "/car/scan",
                LaserScan,
                self.cache_callback("scan"),
                queue_size=5,
            ),
            rospy.Subscriber(
                "/car/openmv/image_raw",
                Image,
                self.cache_callback("image"),
                queue_size=2,
            ),
            rospy.Subscriber(
                "/drone/pose",
                PoseStamped,
                self.cache_callback("drone_pose"),
                queue_size=5,
            ),
        ]
        for direction in ("front", "rear", "left", "right"):
            self.subscribers.append(
                rospy.Subscriber(
                    f"/car/ultrasonic/{direction}",
                    LaserScan,
                    self.ultrasonic_callback(direction),
                    queue_size=5,
                )
            )

        self.transmit_timer = rospy.Timer(
            rospy.Duration(1.0 / float(network["publish_rate"])),
            self.transmit_callback,
        )
        self.connect_thread = threading.Thread(
            target=self.connect_loop,
            name="tcp-connect",
            daemon=True,
        )
        self.connect_thread.start()
        rospy.loginfo(
            "[edge_server_bridge] server=%s:%d, limit=%d B/s",
            self.server_address[0],
            self.server_address[1],
            self.token_bucket.rate,
        )

    def cache_callback(self, key: str):
        def callback(message) -> None:
            with self.latest_lock:
                self.latest[key] = message

        return callback

    def ultrasonic_callback(self, direction: str):
        def callback(message: LaserScan) -> None:
            with self.latest_lock:
                self.latest["ultrasonic"][direction] = message

        return callback

    def current_socket(self) -> Optional[socket.socket]:
        with self.socket_lock:
            return self.socket

    def connect_loop(self) -> None:
        while self.running and not rospy.is_shutdown():
            if self.current_socket() is not None:
                self.stop_event.wait(0.2)
                continue
            candidate = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            candidate.settimeout(self.connect_timeout)
            try:
                candidate.connect(self.server_address)
                candidate.settimeout(self.socket_timeout)
            except OSError as error:
                candidate.close()
                rospy.logwarn_throttle(
                    5.0,
                    "[edge_server_bridge] connection failed: %s; "
                    "retrying",
                    error,
                )
                self.stop_event.wait(self.reconnect_interval)
                continue
            with self.socket_lock:
                if not self.running or self.socket is not None:
                    candidate.close()
                    continue
                self.socket = candidate
            self.connected_publisher.publish(Bool(True))
            rospy.loginfo("[edge_server_bridge] connected")
            threading.Thread(
                target=self.receive_loop,
                args=(candidate,),
                name="tcp-rx",
                daemon=True,
            ).start()

    def mark_disconnected(self, connection: socket.socket) -> None:
        should_close = False
        with self.socket_lock:
            if self.socket is connection:
                self.socket = None
                should_close = True
        if not should_close:
            return
        try:
            connection.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        connection.close()
        self.connected_publisher.publish(Bool(False))
        rospy.loginfo("[edge_server_bridge] disconnected; reconnecting")

    def receive_exact(
        self, connection: socket.socket, length: int
    ) -> bytes:
        data = bytearray()
        while (
            len(data) < length
            and self.running
            and self.current_socket() is connection
        ):
            try:
                chunk = connection.recv(length - len(data))
            except socket.timeout:
                continue
            if not chunk:
                raise EOFError("TCP peer closed the connection")
            data.extend(chunk)
        if len(data) != length:
            raise EOFError("TCP receive interrupted")
        return bytes(data)

    def receive_loop(self, connection: socket.socket) -> None:
        try:
            while (
                self.running
                and self.current_socket() is connection
                and not rospy.is_shutdown()
            ):
                header = self.receive_exact(connection, 4)
                length = struct.unpack("!I", header)[0]
                if not 1 <= length <= self.max_payload_bytes:
                    raise ValueError(
                        f"invalid TCP payload length: {length}"
                    )
                payload = json.loads(
                    self.receive_exact(connection, length).decode("utf-8")
                )
                self.handle_server_message(payload)
        except (
            EOFError,
            json.JSONDecodeError,
            OSError,
            UnicodeDecodeError,
            ValueError,
        ) as error:
            if self.running:
                rospy.logwarn(
                    "[edge_server_bridge] receive failed: %s", error
                )
        finally:
            self.mark_disconnected(connection)

    @staticmethod
    def safe_number(value, default: float = 0.0) -> float:
        try:
            number = float(value)
        except (TypeError, ValueError):
            return default
        return number if math.isfinite(number) else default

    def snapshot(self) -> Dict:
        with self.latest_lock:
            result = dict(self.latest)
            result["ultrasonic"] = dict(self.latest["ultrasonic"])
        return result

    def serialize_sensors(self) -> Dict:
        """Serialize a thread-safe snapshot to the Task-07 JSON schema."""
        latest = self.snapshot()
        output = {
            "protocol_version": 1,
            "timestamp": rospy.Time.now().to_sec(),
            "source": "car",
        }

        odometry = latest["odom"]
        if odometry is not None:
            position = odometry.pose.pose.position
            orientation = odometry.pose.pose.orientation
            twist = odometry.twist.twist
            output["pose"] = {
                "x": self.safe_number(position.x),
                "y": self.safe_number(position.y),
                "z": self.safe_number(position.z),
                "qw": self.safe_number(orientation.w, 1.0),
                "qx": self.safe_number(orientation.x),
                "qy": self.safe_number(orientation.y),
                "qz": self.safe_number(orientation.z),
            }
            output["twist"] = {
                "vx": self.safe_number(twist.linear.x),
                "vy": self.safe_number(twist.linear.y),
                "vz": self.safe_number(twist.angular.z),
            }

        imu = latest["imu"]
        if imu is not None:
            output["imu"] = {
                "wx": self.safe_number(imu.angular_velocity.x),
                "wy": self.safe_number(imu.angular_velocity.y),
                "wz": self.safe_number(imu.angular_velocity.z),
                "ax": self.safe_number(imu.linear_acceleration.x),
                "ay": self.safe_number(imu.linear_acceleration.y),
                "az": self.safe_number(imu.linear_acceleration.z),
            }

        scan = latest["scan"]
        if scan is not None:
            step = int(self.throttle_config["lidar_downsample"])
            output["scan"] = {
                "angle_min": self.safe_number(scan.angle_min),
                "angle_increment": self.safe_number(
                    scan.angle_increment * step
                ),
                "ranges": [
                    self.safe_number(value, -1.0)
                    if value > 0.0
                    else -1.0
                    for value in scan.ranges[::step]
                ],
            }

        image = latest["image"]
        now = time.monotonic()
        image_period = 1.0 / float(
            self.throttle_config["max_image_freq"]
        )
        if image is not None and now - self.last_image_time >= image_period:
            try:
                cv_image = self.bridge.imgmsg_to_cv2(image, "bgr8")
                success, jpeg = cv2.imencode(
                    ".jpg",
                    cv_image,
                    (
                        cv2.IMWRITE_JPEG_QUALITY,
                        int(self.throttle_config["image_quality"]),
                    ),
                )
                if not success:
                    raise ValueError("OpenCV JPEG encoder returned false")
                output["image_jpeg_b64"] = base64.b64encode(
                    jpeg.tobytes()
                ).decode("ascii")
                self.last_image_time = now
            except (
                CvBridgeError,
                TypeError,
                ValueError,
                cv2.error,
            ) as error:
                rospy.logwarn_throttle(
                    5.0,
                    "[edge_server_bridge] image encode failed: %s",
                    error,
                )

        output["ultrasonic"] = {}
        for direction, message in latest["ultrasonic"].items():
            if message is None or not message.ranges:
                continue
            value = self.safe_number(message.ranges[0], 4.0)
            output["ultrasonic"][direction] = max(
                0.02, min(4.0, value)
            )

        drone_pose = latest["drone_pose"]
        if drone_pose is not None:
            output["drone_pose"] = {
                "x": self.safe_number(drone_pose.pose.position.x),
                "y": self.safe_number(drone_pose.pose.position.y),
                "z": self.safe_number(drone_pose.pose.position.z),
            }

        has_telemetry = any(
            key in output
            for key in ("pose", "imu", "scan", "image_jpeg_b64")
        )
        if has_telemetry:
            output["kind"] = "telemetry"
        else:
            output["kind"] = "heartbeat"
        return output

    def transmit_callback(self, _event: rospy.timer.TimerEvent) -> None:
        connection = self.current_socket()
        if connection is None:
            return
        if not self.transmit_lock.acquire(blocking=False):
            return
        try:
            payload = self.serialize_sensors()
            is_heartbeat = payload["kind"] == "heartbeat"
            if (
                is_heartbeat
                and time.monotonic() - self.last_empty_heartbeat
                < self.heartbeat_interval
            ):
                return
            frame = encode_json_frame(
                payload, self.max_payload_bytes
            )
            if (
                self.throttle_config["enable"]
                and not self.token_bucket.consume(len(frame))
            ):
                rospy.logwarn_throttle(
                    5.0,
                    "[edge_server_bridge] telemetry throttled",
                )
                return
            connection.sendall(frame)
            if is_heartbeat:
                self.last_empty_heartbeat = time.monotonic()
        except (OSError, TypeError, ValueError) as error:
            rospy.logwarn(
                "[edge_server_bridge] transmit failed: %s", error
            )
            self.mark_disconnected(connection)
        finally:
            self.transmit_lock.release()

    def handle_server_message(self, payload: Dict) -> None:
        command_data = decode_server_command(payload)
        command = ServerCommand()
        command.header.stamp = rospy.Time.now()
        command.target_id = command_data["target_id"]
        command.command_type = command_data["command_type"]
        command.query_text = command_data["query_text"]
        pose = command_data["target_pose"]
        command.target_pose.position.x = pose["x"]
        command.target_pose.position.y = pose["y"]
        command.target_pose.position.z = pose["z"]
        command.target_pose.orientation.x = pose["qx"]
        command.target_pose.orientation.y = pose["qy"]
        command.target_pose.orientation.z = pose["qz"]
        command.target_pose.orientation.w = pose["qw"]
        command.target_velocity = command_data["target_velocity"]
        self.command_publisher.publish(command)

    def shutdown(self) -> None:
        self.running = False
        self.stop_event.set()
        self.transmit_timer.shutdown()
        connection = self.current_socket()
        if connection is not None:
            self.mark_disconnected(connection)
        if self.connect_thread.is_alive():
            self.connect_thread.join(
                timeout=self.connect_timeout + 1.0
            )


def main() -> None:
    rospy.init_node("edge_server_bridge")
    bridge = EdgeServerBridge()
    rospy.on_shutdown(bridge.shutdown)
    rospy.spin()


if __name__ == "__main__":
    main()
