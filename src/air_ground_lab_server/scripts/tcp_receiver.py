#!/usr/bin/env python3
"""实验室服务器 TCP 接收节点。

接收 Task-06 的四字节网络序长度帧 JSON，严格校验后重建 ICD
Observation 与 RobotState。节点不依赖 MAVLink、Gazebo 或原始传感器类型。
"""

import base64
import binascii
import ipaddress
import json
import math
import socket
import struct
import threading
from pathlib import Path
from typing import Dict, Optional, Tuple

import rospy
import yaml
from air_ground_interfaces.msg import Observation, RobotState


ROBOT_IDS = ("car", "drone")
MAX_LIDAR_RANGES = 4096


def load_yaml_mapping(path_value: str, label: str) -> Dict:
    """读取一个 YAML 映射，并拒绝空文档或非映射根节点。"""
    path = Path(path_value).expanduser().resolve()
    with path.open(encoding="utf-8") as stream:
        value = yaml.safe_load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"{label} must contain a mapping")
    return value


def positive_number(value, label: str) -> float:
    """校验一个有限正数。"""
    number = float(value)
    if not math.isfinite(number) or number <= 0.0:
        raise ValueError(f"{label} must be a finite positive number")
    return number


def positive_integer(value, label: str) -> int:
    """校验一个正整数。"""
    number = int(value)
    if number <= 0:
        raise ValueError(f"{label} must be positive")
    return number


def load_server_config(
    network_path: str, server_path: str
) -> Dict:
    """合并并校验 Task-06 网络配置与 Task-07 服务端配置。"""
    network_root = load_yaml_mapping(network_path, "network config")
    server_root = load_yaml_mapping(server_path, "server config")
    network = network_root.get("edge_server_tcp")
    tcp = server_root.get("tcp")
    if not isinstance(network, dict) or not isinstance(tcp, dict):
        raise ValueError("edge_server_tcp and tcp must be mappings")
    # server_ip 是边缘节点要连接的通告地址；多网卡服务器通常监听 0.0.0.0，
    # 二者不能复用一个字段。旧仿真配置未写时回退，保持 Phase 0 行为。
    host = str(network.get("server_bind_ip", network["server_ip"]))
    ipaddress.ip_address(host)
    port = int(network["server_port"])
    if not 1 <= port <= 65535:
        raise ValueError("server_port must be in [1, 65535]")
    max_payload = positive_integer(
        network.get("max_payload_bytes", 10 * 1024 * 1024),
        "max_payload_bytes",
    )
    if max_payload > 64 * 1024 * 1024:
        raise ValueError("max_payload_bytes cannot exceed 64 MiB")
    return {
        "host": host,
        "port": port,
        "socket_timeout": positive_number(
            network.get("socket_timeout", 1.0), "socket_timeout"
        ),
        "max_payload_bytes": max_payload,
        "listen_backlog": positive_integer(
            tcp.get("listen_backlog", 4), "listen_backlog"
        ),
        "max_clients": positive_integer(
            tcp.get("max_clients", 8), "max_clients"
        ),
    }


def finite_number(value, label: str, default: float = 0.0) -> float:
    """读取有限浮点字段，缺失值由调用方传入默认值。"""
    if value is None:
        return default
    try:
        number = float(value)
    except (TypeError, ValueError) as error:
        raise ValueError(f"{label} must be numeric") from error
    if not math.isfinite(number):
        raise ValueError(f"{label} must be finite")
    return number


def require_mapping(value, label: str) -> Dict:
    """校验 JSON 子字段为对象。"""
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be an object")
    return value


def bounded_sensor_range(
    value, label: str, minimum: float, maximum: float
) -> float:
    """把有效距离限制到量程，无效非正值解释为无障碍最大量程。"""
    number = finite_number(value, label, maximum)
    if number <= 0.0:
        return maximum
    return max(minimum, min(maximum, number))


def decode_telemetry(
    payload: Dict,
) -> Tuple[str, Observation, RobotState]:
    """把一个 Task-06 JSON 帧转换为 ICD 消息。

    Args:
        payload: 已完成 JSON 解码的对象。

    Returns:
        `(robot_id, observation, state)`。

    Raises:
        ValueError: 协议、来源、字段类型或数值不合法。
    """
    if not isinstance(payload, dict):
        raise ValueError("TCP payload must be a JSON object")
    try:
        protocol_version = int(payload.get("protocol_version", -1))
    except (TypeError, ValueError) as error:
        raise ValueError("protocol_version must be an integer") from error
    if protocol_version != 1:
        raise ValueError("unsupported protocol_version")
    robot_id = str(payload.get("source", ""))
    if robot_id not in ROBOT_IDS:
        raise ValueError("source must be car or drone")
    kind = str(payload.get("kind", ""))
    if kind not in {"heartbeat", "telemetry"}:
        raise ValueError("kind must be heartbeat or telemetry")
    timestamp = finite_number(
        payload.get("timestamp"), "timestamp", 0.0
    )
    if timestamp < 0.0:
        raise ValueError("timestamp cannot be negative")
    stamp = rospy.Time.from_sec(timestamp)

    observation = Observation()
    observation.header.stamp = stamp
    observation.header.frame_id = f"{robot_id}/base_link"
    observation.robot_id = robot_id
    modalities = []

    image_data = payload.get("image_jpeg_b64")
    if image_data is not None:
        if not isinstance(image_data, str):
            raise ValueError("image_jpeg_b64 must be a string")
        try:
            decoded = base64.b64decode(image_data, validate=True)
        except (binascii.Error, ValueError, TypeError) as error:
            raise ValueError("image_jpeg_b64 is invalid") from error
        if not decoded.startswith(b"\xff\xd8"):
            raise ValueError("image_jpeg_b64 is not a JPEG image")
        observation.rgb.header.stamp = stamp
        observation.rgb.header.frame_id = observation.header.frame_id
        observation.rgb.format = "jpeg"
        observation.rgb.data = decoded
        modalities.append("rgb")

    imu_value = payload.get("imu")
    if imu_value is not None:
        imu = require_mapping(imu_value, "imu")
        observation.angular_velocity.x = finite_number(
            imu.get("wx"), "imu.wx"
        )
        observation.angular_velocity.y = finite_number(
            imu.get("wy"), "imu.wy"
        )
        observation.angular_velocity.z = finite_number(
            imu.get("wz"), "imu.wz"
        )
        observation.linear_acceleration.x = finite_number(
            imu.get("ax"), "imu.ax"
        )
        observation.linear_acceleration.y = finite_number(
            imu.get("ay"), "imu.ay"
        )
        observation.linear_acceleration.z = finite_number(
            imu.get("az"), "imu.az"
        )
        modalities.append("imu")

    scan_value = payload.get("scan")
    if scan_value is not None:
        scan = require_mapping(scan_value, "scan")
        ranges = scan.get("ranges", [])
        if not isinstance(ranges, list):
            raise ValueError("scan.ranges must be a list")
        if len(ranges) > MAX_LIDAR_RANGES:
            raise ValueError("scan.ranges exceeds safety limit")
        observation.lidar_ranges = [
            value
            if (value := finite_number(item, "scan.ranges[]")) > 0.0
            else -1.0
            for item in ranges
        ]
        observation.lidar_angle_min = finite_number(
            scan.get("angle_min"), "scan.angle_min"
        )
        observation.lidar_angle_increment = finite_number(
            scan.get("angle_increment"), "scan.angle_increment"
        )
        modalities.append("lidar_2d")

    ultrasonic_value = payload.get("ultrasonic")
    if ultrasonic_value is not None:
        ultrasonic = require_mapping(ultrasonic_value, "ultrasonic")
        observation.ultrasonic_ranges = [
            bounded_sensor_range(
                ultrasonic.get(direction),
                f"ultrasonic.{direction}",
                0.02,
                4.0,
            )
            for direction in ("front", "rear", "left", "right")
        ]
        modalities.append("ultrasonic")
    observation.modalities = modalities

    state = RobotState()
    state.header.stamp = stamp
    state.header.frame_id = "map"
    state.robot_id = robot_id
    # heartbeat 可以不带 pose；geometry_msgs 的全零默认值不是合法四元数。
    # 无姿态数据时使用单位四元数，避免 TF/下游归一化得到 NaN。
    state.pose.orientation.w = 1.0
    pose_value = payload.get("pose")
    if pose_value is not None:
        pose = require_mapping(pose_value, "pose")
        state.pose.position.x = finite_number(pose.get("x"), "pose.x")
        state.pose.position.y = finite_number(pose.get("y"), "pose.y")
        state.pose.position.z = finite_number(pose.get("z"), "pose.z")
        state.pose.orientation.x = finite_number(
            pose.get("qx"), "pose.qx"
        )
        state.pose.orientation.y = finite_number(
            pose.get("qy"), "pose.qy"
        )
        state.pose.orientation.z = finite_number(
            pose.get("qz"), "pose.qz"
        )
        state.pose.orientation.w = finite_number(
            pose.get("qw"), "pose.qw", 1.0
        )
    twist_value = payload.get("twist")
    if twist_value is not None:
        twist = require_mapping(twist_value, "twist")
        state.velocity.linear.x = finite_number(
            twist.get("vx"), "twist.vx"
        )
        state.velocity.linear.y = finite_number(
            twist.get("vy"), "twist.vy"
        )
        state.velocity.linear.z = finite_number(
            twist.get("vz_linear"), "twist.vz_linear"
        )
        state.velocity.angular.z = finite_number(
            twist.get("vz"), "twist.vz"
        )
    state.mode = str(payload.get("mode", "idle"))
    state.chassis_type = str(
        payload.get(
            "chassis_type", "none" if robot_id == "drone" else "unknown"
        )
    )
    state.battery_voltage = finite_number(
        payload.get("battery_voltage"), "battery_voltage", 11.1
    )
    state.is_armed = bool(payload.get("is_armed", False))
    state.is_connected = bool(payload.get("is_connected", True))
    return robot_id, observation, state


def decode_embedded_drone_state(payload: Dict) -> Optional[RobotState]:
    """从车机兼容帧中的 `drone_pose` 重建最小无人机状态。"""
    if payload.get("source") != "car" or "drone_pose" not in payload:
        return None
    pose = require_mapping(payload["drone_pose"], "drone_pose")
    state = RobotState()
    timestamp = finite_number(
        payload.get("timestamp"), "timestamp", 0.0
    )
    state.header.stamp = rospy.Time.from_sec(timestamp)
    state.header.frame_id = "map"
    state.robot_id = "drone"
    state.pose.position.x = finite_number(pose.get("x"), "drone_pose.x")
    state.pose.position.y = finite_number(pose.get("y"), "drone_pose.y")
    state.pose.position.z = finite_number(pose.get("z"), "drone_pose.z")
    state.pose.orientation.w = 1.0
    state.mode = "unknown"
    state.chassis_type = "none"
    state.battery_voltage = 11.1
    state.is_connected = True
    return state


class TCPReceiver:
    """管理受限 TCP 客户端并把有效帧发布到服务器侧 ROS 话题。"""

    def __init__(self) -> None:
        """读取配置、绑定监听端口并启动接受线程。"""
        self.config = load_server_config(
            str(rospy.get_param("~network_config")),
            str(rospy.get_param("~server_config")),
        )
        self.running = True
        self.client_lock = threading.Lock()
        self.clients: Dict[socket.socket, Tuple[str, int]] = {}
        self.publishers = {
            robot_id: {
                "observation": rospy.Publisher(
                    f"/server/{robot_id}/observation",
                    Observation,
                    queue_size=10,
                ),
                "state": rospy.Publisher(
                    f"/server/{robot_id}/state",
                    RobotState,
                    queue_size=10,
                ),
            }
            for robot_id in ROBOT_IDS
        }

        self.server_socket = socket.socket(
            socket.AF_INET, socket.SOCK_STREAM
        )
        self.server_socket.setsockopt(
            socket.SOL_SOCKET, socket.SO_REUSEADDR, 1
        )
        self.server_socket.bind(
            (self.config["host"], self.config["port"])
        )
        self.server_socket.listen(self.config["listen_backlog"])
        self.server_socket.settimeout(self.config["socket_timeout"])
        self.accept_thread = threading.Thread(
            target=self.accept_loop,
            name="task07-tcp-accept",
            daemon=True,
        )
        self.accept_thread.start()
        rospy.loginfo(
            "[tcp_server] listening on %s:%d",
            self.config["host"],
            self.config["port"],
        )

    def accept_loop(self) -> None:
        """接受有限数量客户端，每个连接使用独立接收线程。"""
        while self.running and not rospy.is_shutdown():
            try:
                connection, address = self.server_socket.accept()
            except socket.timeout:
                continue
            except OSError as error:
                if self.running:
                    rospy.logwarn("[tcp_server] accept failed: %s", error)
                continue
            connection.settimeout(self.config["socket_timeout"])
            with self.client_lock:
                if len(self.clients) >= self.config["max_clients"]:
                    connection.close()
                    rospy.logwarn(
                        "[tcp_server] rejected client: limit reached"
                    )
                    continue
                self.clients[connection] = address
            threading.Thread(
                target=self.client_loop,
                args=(connection,),
                name=f"task07-tcp-client-{address[1]}",
                daemon=True,
            ).start()
            rospy.loginfo(
                "[tcp_server] client connected: %s:%d",
                address[0],
                address[1],
            )

    def receive_exact(
        self, connection: socket.socket, length: int
    ) -> bytes:
        """在允许超时重试的同时完整读取指定长度。"""
        data = bytearray()
        while len(data) < length and self.running:
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

    def client_loop(self, connection: socket.socket) -> None:
        """持续处理单个客户端的长度帧，错误时仅断开该客户端。"""
        try:
            while self.running and not rospy.is_shutdown():
                header = self.receive_exact(connection, 4)
                length = struct.unpack("!I", header)[0]
                if not 1 <= length <= self.config["max_payload_bytes"]:
                    raise ValueError(f"invalid payload length: {length}")
                raw = self.receive_exact(connection, length)
                payload = json.loads(raw.decode("utf-8"))
                self.process_payload(payload)
        except (
            EOFError,
            json.JSONDecodeError,
            OSError,
            UnicodeDecodeError,
            TypeError,
            ValueError,
        ) as error:
            if self.running:
                rospy.logwarn("[tcp_server] client rejected: %s", error)
        finally:
            with self.client_lock:
                address = self.clients.pop(connection, None)
            connection.close()
            if address is not None:
                rospy.loginfo(
                    "[tcp_server] client disconnected: %s:%d",
                    address[0],
                    address[1],
                )

    def process_payload(self, payload: Dict) -> None:
        """校验一个 JSON 对象并发布对应抽象消息。"""
        robot_id, observation, state = decode_telemetry(payload)
        if observation.modalities:
            self.publishers[robot_id]["observation"].publish(observation)
        self.publishers[robot_id]["state"].publish(state)
        embedded = decode_embedded_drone_state(payload)
        if embedded is not None:
            self.publishers["drone"]["state"].publish(embedded)

    def shutdown(self) -> None:
        """关闭监听端口和所有客户端，并等待接受线程退出。"""
        self.running = False
        self.server_socket.close()
        with self.client_lock:
            connections = list(self.clients)
            self.clients.clear()
        for connection in connections:
            try:
                connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            connection.close()
        if self.accept_thread.is_alive():
            self.accept_thread.join(timeout=2.0)


def main() -> None:
    """启动 ROS TCP 接收节点。"""
    rospy.init_node("tcp_server")
    receiver = TCPReceiver()
    rospy.on_shutdown(receiver.shutdown)
    rospy.spin()


if __name__ == "__main__":
    main()
