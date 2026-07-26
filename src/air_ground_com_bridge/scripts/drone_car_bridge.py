#!/usr/bin/env python3
"""Bridge MAVLink v1 UDP telemetry and commands to local ROS topics."""

import ipaddress
import json
import math
import socket
import struct
import threading
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import rospy
import yaml
from air_ground_interfaces.msg import RobotState
from geometry_msgs.msg import PoseStamped
from std_msgs.msg import Bool, String

try:
    from pymavlink import mavutil

    HAS_PYMAVLINK = True
except ImportError:
    mavutil = None
    HAS_PYMAVLINK = False


MAVLINK_V1_STX = 0xFE
MAVLINK_V2_STX = 0xFD
MAVLINK_MSG_ID_HEARTBEAT = 0
MAVLINK_MSG_ID_GLOBAL_POSITION_INT = 33
MAVLINK_MSG_ID_COMMAND_LONG = 76
MAVLINK_CRC_EXTRA = {
    MAVLINK_MSG_ID_HEARTBEAT: 50,
    MAVLINK_MSG_ID_GLOBAL_POSITION_INT: 104,
    MAVLINK_MSG_ID_COMMAND_LONG: 152,
}
MAV_MODE_FLAG_SAFETY_ARMED = 0x80


def _finite_number(value, label: str) -> float:
    number = float(value)
    if not math.isfinite(number):
        raise ValueError(f"{label} must be finite")
    return number


def _port(value, label: str) -> int:
    port = int(value)
    if not 1 <= port <= 65535:
        raise ValueError(f"{label} must be in [1, 65535]")
    return port


def _system_id(value, label: str) -> int:
    system_id = int(value)
    if not 1 <= system_id <= 255:
        raise ValueError(f"{label} must be in [1, 255]")
    return system_id


def load_network_config(config_path: str) -> Dict:
    """Load and validate the Task-06 network configuration."""
    path = Path(config_path).expanduser().resolve()
    with path.open(encoding="utf-8") as stream:
        root = yaml.safe_load(stream)
    if not isinstance(root, dict):
        raise ValueError("network config must be a mapping")
    mavlink = root.get("drone_car_mavlink")
    adapter = root.get("simulation_adapter", {})
    throttle = root.get("throttle", {})
    if (
        not isinstance(mavlink, dict)
        or not isinstance(adapter, dict)
        or not isinstance(throttle, dict)
    ):
        raise ValueError(
            "MAVLink, simulation adapter and throttle must be mappings"
        )
    if str(mavlink.get("protocol", "")).lower() != "udp":
        raise ValueError("drone_car_mavlink.protocol must be udp")

    for key in ("drone_ip", "car_ip"):
        ipaddress.ip_address(str(mavlink[key]))
    mavlink["drone_port"] = _port(
        mavlink["drone_port"], "drone_port"
    )
    mavlink["car_port"] = _port(mavlink["car_port"], "car_port")
    if mavlink["drone_port"] == mavlink["car_port"]:
        raise ValueError("drone_port and car_port must be different")
    mavlink["heartbeat_interval"] = _finite_number(
        mavlink.get("heartbeat_interval", 1.0),
        "heartbeat_interval",
    )
    if mavlink["heartbeat_interval"] <= 0.0:
        raise ValueError("heartbeat_interval must be positive")
    for key, default in (
        ("target_system", 1),
        ("target_component", 1),
        ("source_system", 255),
        ("source_component", 190),
    ):
        mavlink[key] = _system_id(mavlink.get(key, default), key)

    adapter["enable"] = bool(adapter.get("enable", False))
    if adapter["enable"]:
        ipaddress.ip_address(str(adapter["px4_ip"]))
        adapter["px4_port"] = _port(
            adapter["px4_port"], "simulation_adapter.px4_port"
        )
        if adapter["px4_port"] in (
            mavlink["drone_port"],
            mavlink["car_port"],
        ):
            raise ValueError(
                "PX4 adapter port must differ from radio ports"
            )

    throttle["enable"] = bool(throttle.get("enable", True))
    throttle["max_bandwidth_bytes_per_sec"] = int(
        throttle.get("max_bandwidth_bytes_per_sec", 24000)
    )
    if throttle["max_bandwidth_bytes_per_sec"] <= 0:
        raise ValueError("max bandwidth must be positive")
    return root


def x25_crc(data: bytes, crc: int = 0xFFFF) -> int:
    """Calculate MAVLink's X.25 checksum."""
    for byte in data:
        temporary = byte ^ (crc & 0xFF)
        temporary ^= (temporary << 4) & 0xFF
        crc = (
            (crc >> 8)
            ^ (temporary << 8)
            ^ (temporary << 3)
            ^ (temporary >> 4)
        ) & 0xFFFF
    return crc


def build_mavlink_v1_frame(
    message_id: int,
    payload: bytes,
    sequence: int,
    system_id: int,
    component_id: int,
) -> bytes:
    """Build a checksummed MAVLink v1 frame for a supported message."""
    if message_id not in MAVLINK_CRC_EXTRA:
        raise ValueError(f"unsupported MAVLink message id: {message_id}")
    if len(payload) > 255:
        raise ValueError("MAVLink v1 payload exceeds 255 bytes")
    header_and_payload = bytes(
        (
            len(payload),
            sequence & 0xFF,
            system_id & 0xFF,
            component_id & 0xFF,
            message_id & 0xFF,
        )
    ) + payload
    checksum = x25_crc(
        header_and_payload
        + bytes((MAVLINK_CRC_EXTRA[message_id],))
    )
    return (
        bytes((MAVLINK_V1_STX,))
        + header_and_payload
        + struct.pack("<H", checksum)
    )


class MavlinkV1Parser:
    """Incrementally parse and CRC-check the MAVLink v1 subset in use."""

    def __init__(self) -> None:
        self.buffer = bytearray()

    def feed(self, data: bytes) -> List[Dict]:
        self.buffer.extend(data)
        frames = []
        while self.buffer:
            try:
                start = self.buffer.index(MAVLINK_V1_STX)
            except ValueError:
                self.buffer.clear()
                break
            if start:
                del self.buffer[:start]
            if len(self.buffer) < 2:
                break
            frame_length = int(self.buffer[1]) + 8
            if len(self.buffer) < frame_length:
                break
            frame = bytes(self.buffer[:frame_length])
            del self.buffer[:frame_length]
            message_id = frame[5]
            crc_extra = MAVLINK_CRC_EXTRA.get(message_id)
            if crc_extra is None:
                continue
            expected_crc = x25_crc(
                frame[1:-2] + bytes((crc_extra,))
            )
            actual_crc = struct.unpack("<H", frame[-2:])[0]
            if actual_crc != expected_crc:
                continue
            frames.append(
                {
                    "message_id": message_id,
                    "sequence": frame[2],
                    "system_id": frame[3],
                    "component_id": frame[4],
                    "payload": frame[6:-2],
                }
            )
        return frames


def parse_command(command_text: str) -> Dict:
    """Validate a JSON command for MAVLink COMMAND_LONG."""
    try:
        command = json.loads(command_text)
    except json.JSONDecodeError as error:
        raise ValueError("command must be valid JSON") from error
    if not isinstance(command, dict):
        raise ValueError("command must be a JSON object")
    command_id = int(command.get("command", -1))
    if not 0 <= command_id <= 65535:
        raise ValueError("command must be in [0, 65535]")
    params = command.get("params", [])
    if not isinstance(params, list) or len(params) > 7:
        raise ValueError("params must be a list with at most 7 values")
    params = [
        _finite_number(value, f"params[{index}]")
        for index, value in enumerate(params)
    ]
    params.extend([0.0] * (7 - len(params)))
    return {
        "command": command_id,
        "params": params,
        "target_system": _system_id(
            command.get("target_system", 1), "target_system"
        ),
        "target_component": _system_id(
            command.get("target_component", 1), "target_component"
        ),
        "confirmation": int(command.get("confirmation", 0)) & 0xFF,
    }


def build_command_long(
    command: Dict,
    sequence: int,
    source_system: int,
    source_component: int,
) -> bytes:
    """Encode a validated COMMAND_LONG message."""
    payload = struct.pack(
        "<7fHBBB",
        *command["params"],
        command["command"],
        command["target_system"],
        command["target_component"],
        command["confirmation"],
    )
    return build_mavlink_v1_frame(
        MAVLINK_MSG_ID_COMMAND_LONG,
        payload,
        sequence,
        source_system,
        source_component,
    )


class TokenBucket:
    """Bound bytes sent per second while allowing one second of burst."""

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
            elapsed = now - self.updated_at
            self.updated_at = now
            self.tokens = min(
                self.capacity, self.tokens + elapsed * self.rate
            )
            if amount > self.tokens:
                return False
            self.tokens -= amount
            return True


class DroneCarBridge:
    """Translate drone MAVLink UDP data to ROS and ROS commands to MAVLink."""

    def __init__(self) -> None:
        config_path = str(rospy.get_param("~config_path"))
        config = load_network_config(config_path)
        network = config["drone_car_mavlink"]
        adapter = config.get("simulation_adapter", {})
        throttle = config["throttle"]

        self.drone_address = (
            str(network["drone_ip"]),
            int(network["drone_port"]),
        )
        self.car_address = (
            str(network["car_ip"]),
            int(network["car_port"]),
        )
        self.heartbeat_interval = float(
            network["heartbeat_interval"]
        )
        self.heartbeat_timeout = max(
            1.0, 3.0 * self.heartbeat_interval
        )
        self.source_system = int(network["source_system"])
        self.source_component = int(network["source_component"])
        self.default_target_system = int(network["target_system"])
        self.default_target_component = int(
            network["target_component"]
        )
        self.adapter_enabled = bool(adapter.get("enable", False))
        self.px4_address: Optional[Tuple[str, int]] = None

        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.socket.bind(self.car_address)
        self.socket.settimeout(0.5)
        self.adapter_socket: Optional[socket.socket] = None
        self.adapter_thread: Optional[threading.Thread] = None
        if self.adapter_enabled:
            self.px4_address = (
                str(adapter["px4_ip"]),
                int(adapter["px4_port"]),
            )
            self.adapter_socket = socket.socket(
                socket.AF_INET, socket.SOCK_DGRAM
            )
            self.adapter_socket.setsockopt(
                socket.SOL_SOCKET, socket.SO_REUSEADDR, 1
            )
            self.adapter_socket.bind(self.drone_address)
            self.adapter_socket.settimeout(0.5)
        self.parser = MavlinkV1Parser()
        self.pymavlink_parser = (
            mavutil.mavlink.MAVLink(None) if HAS_PYMAVLINK else None
        )
        self.token_bucket = TokenBucket(
            int(throttle["max_bandwidth_bytes_per_sec"])
        )
        self.throttle_enabled = bool(throttle["enable"])

        self.running = True
        self.sequence = 0
        self.send_lock = threading.Lock()
        self.last_heartbeat: Optional[float] = None
        self.last_connected_state = False
        self.latest_pose: Optional[PoseStamped] = None
        self.state_lock = threading.Lock()

        self.heartbeat_publisher = rospy.Publisher(
            "/drone/heartbeat", Bool, queue_size=5, latch=True
        )
        self.pose_publisher = rospy.Publisher(
            "/drone/pose", PoseStamped, queue_size=5
        )
        self.state_publisher = rospy.Publisher(
            "/drone/state", RobotState, queue_size=5
        )
        self.pose_subscriber = rospy.Subscriber(
            "/drone/gps/local_pose",
            PoseStamped,
            self.pose_callback,
            queue_size=5,
        )
        self.command_subscriber = rospy.Subscriber(
            "/car/to_drone/cmd",
            String,
            self.command_callback,
            queue_size=10,
        )
        self.heartbeat_publisher.publish(Bool(False))
        self.watchdog_timer = rospy.Timer(
            rospy.Duration(self.heartbeat_interval),
            self.watchdog_callback,
        )
        self.receive_thread = threading.Thread(
            target=self.receive_loop,
            name="mavlink-rx",
            daemon=True,
        )
        self.receive_thread.start()
        if self.adapter_socket is not None:
            self.adapter_thread = threading.Thread(
                target=self.adapter_loop,
                name="px4-sitl-adapter",
                daemon=True,
            )
            self.adapter_thread.start()
        self.gcs_heartbeat_timer = rospy.Timer(
            rospy.Duration(self.heartbeat_interval),
            self.gcs_heartbeat_callback,
        )
        rospy.loginfo(
            "[drone_car_bridge] UDP %s:%d -> %s:%d, "
            "PX4 adapter=%s, pymavlink=%s",
            self.drone_address[0],
            self.drone_address[1],
            self.car_address[0],
            self.car_address[1],
            self.px4_address,
            HAS_PYMAVLINK,
        )

    def pose_callback(self, message: PoseStamped) -> None:
        with self.state_lock:
            self.latest_pose = message
        self.pose_publisher.publish(message)

    def receive_loop(self) -> None:
        while self.running and not rospy.is_shutdown():
            try:
                data, address = self.socket.recvfrom(4096)
                if address != self.drone_address:
                    rospy.logwarn_throttle(
                        5.0,
                        "[drone_car_bridge] ignored UDP source %s:%d",
                        address[0],
                        address[1],
                    )
                    continue
                self.parse_datagram(data)
            except socket.timeout:
                continue
            except OSError as error:
                if self.running:
                    rospy.logwarn(
                        "[drone_car_bridge] UDP receive failed: %s",
                        error,
                    )

    def adapter_loop(self) -> None:
        """Relay logical radio traffic to PX4 SITL's actual GCS port."""
        if self.adapter_socket is None or self.px4_address is None:
            return
        while self.running and not rospy.is_shutdown():
            try:
                data, address = self.adapter_socket.recvfrom(4096)
                if address == self.car_address:
                    self.adapter_socket.sendto(data, self.px4_address)
                elif address == self.px4_address:
                    self.adapter_socket.sendto(data, self.car_address)
                else:
                    rospy.logwarn_throttle(
                        5.0,
                        "[drone_car_bridge] adapter ignored %s:%d",
                        address[0],
                        address[1],
                    )
            except socket.timeout:
                continue
            except OSError as error:
                if self.running:
                    rospy.logwarn(
                        "[drone_car_bridge] adapter failed: %s", error
                    )

    def parse_datagram(self, data: bytes) -> None:
        frames = self.parser.feed(data)
        for frame in frames:
            self.handle_message(
                frame["message_id"],
                frame["payload"],
                frame["system_id"],
            )
        if not frames and MAVLINK_V2_STX in data:
            self.parse_with_pymavlink(data)

    def parse_with_pymavlink(self, data: bytes) -> None:
        if self.pymavlink_parser is None:
            rospy.logwarn_throttle(
                10.0,
                "[drone_car_bridge] MAVLink v2 needs optional pymavlink",
            )
            return
        try:
            for byte in data:
                message = self.pymavlink_parser.parse_char(bytes((byte,)))
                if message is None:
                    continue
                if message.get_type() == "HEARTBEAT":
                    payload = struct.pack(
                        "<IBBBBB",
                        int(message.custom_mode),
                        int(message.type),
                        int(message.autopilot),
                        int(message.base_mode),
                        int(message.system_status),
                        int(message.mavlink_version),
                    )
                    self.handle_message(
                        MAVLINK_MSG_ID_HEARTBEAT,
                        payload,
                        int(message.get_srcSystem()),
                    )
        except Exception as error:  # pymavlink raises dialect-specific errors
            rospy.logwarn_throttle(
                5.0,
                "[drone_car_bridge] MAVLink v2 parse failed: %s",
                error,
            )

    def handle_message(
        self, message_id: int, payload: bytes, system_id: int
    ) -> None:
        if message_id == MAVLINK_MSG_ID_HEARTBEAT:
            if len(payload) < 9:
                return
            custom_mode, _, _, base_mode, _, _ = struct.unpack(
                "<IBBBBB", payload[:9]
            )
            now = time.monotonic()
            with self.state_lock:
                self.last_heartbeat = now
                pose = self.latest_pose
            self.heartbeat_publisher.publish(Bool(True))
            self.last_connected_state = True
            state = RobotState()
            state.header.stamp = rospy.Time.now()
            state.header.frame_id = (
                pose.header.frame_id if pose else "map"
            )
            state.robot_id = "drone"
            if pose:
                state.pose = pose.pose
            state.mode = f"custom_mode:{custom_mode}"
            state.is_armed = bool(
                base_mode & MAV_MODE_FLAG_SAFETY_ARMED
            )
            state.is_connected = True
            self.state_publisher.publish(state)
            rospy.logdebug(
                "[drone_car_bridge] heartbeat sys=%d mode=%d",
                system_id,
                custom_mode,
            )

    def watchdog_callback(self, _event: rospy.timer.TimerEvent) -> None:
        with self.state_lock:
            last_heartbeat = self.last_heartbeat
        connected = (
            last_heartbeat is not None
            and time.monotonic() - last_heartbeat
            <= self.heartbeat_timeout
        )
        if connected != self.last_connected_state:
            self.heartbeat_publisher.publish(Bool(connected))
            self.last_connected_state = connected

    def send_message(self, message_id: int, payload: bytes) -> bool:
        """Build, throttle and send one MAVLink v1 message."""
        with self.send_lock:
            frame = build_mavlink_v1_frame(
                message_id,
                payload,
                self.sequence,
                self.source_system,
                self.source_component,
            )
            if (
                self.throttle_enabled
                and not self.token_bucket.consume(len(frame))
            ):
                return False
            try:
                self.socket.sendto(frame, self.drone_address)
            except OSError as error:
                rospy.logwarn(
                    "[drone_car_bridge] UDP send failed: %s", error
                )
                return False
            self.sequence = (self.sequence + 1) & 0xFF
            return True

    def gcs_heartbeat_callback(
        self, _event: rospy.timer.TimerEvent
    ) -> None:
        payload = struct.pack(
            "<IBBBBB",
            0,
            6,
            8,
            0,
            0,
            3,
        )
        if not self.send_message(MAVLINK_MSG_ID_HEARTBEAT, payload):
            rospy.logwarn_throttle(
                5.0,
                "[drone_car_bridge] GCS heartbeat throttled",
            )

    def command_callback(self, message: String) -> None:
        try:
            command = parse_command(message.data)
            if "target_system" not in json.loads(message.data):
                command["target_system"] = self.default_target_system
            if "target_component" not in json.loads(message.data):
                command[
                    "target_component"
                ] = self.default_target_component
            payload = struct.pack(
                "<7fHBBB",
                *command["params"],
                command["command"],
                command["target_system"],
                command["target_component"],
                command["confirmation"],
            )
        except (TypeError, ValueError, json.JSONDecodeError) as error:
            rospy.logwarn_throttle(
                5.0,
                "[drone_car_bridge] rejected command: %s",
                error,
            )
            return
        if not self.send_message(MAVLINK_MSG_ID_COMMAND_LONG, payload):
            rospy.logwarn_throttle(
                5.0,
                "[drone_car_bridge] command throttled",
            )

    def shutdown(self) -> None:
        self.running = False
        self.watchdog_timer.shutdown()
        self.gcs_heartbeat_timer.shutdown()
        self.socket.close()
        if self.adapter_socket is not None:
            self.adapter_socket.close()
        if self.receive_thread.is_alive():
            self.receive_thread.join(timeout=2.0)
        if (
            self.adapter_thread is not None
            and self.adapter_thread.is_alive()
        ):
            self.adapter_thread.join(timeout=2.0)


def main() -> None:
    rospy.init_node("drone_car_bridge")
    bridge = DroneCarBridge()
    rospy.on_shutdown(bridge.shutdown)
    rospy.spin()


if __name__ == "__main__":
    main()
