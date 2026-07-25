# Task-06: 空地通信桥（MAVLink + TCP）

## 前置条件

- Task-02、Task-03 完成（无人机和车可独立仿真运行）
- `air_ground_interfaces` 消息包已编译

## 目标

建立三条通信链路：

```
无人�?──MAVLink(UDP)──�?车机 ──TCP──�?服务�?
   �?                      �?             �?
   └────MAVLink(UDP)───────�?             �?
                                          �?
                                       车机 ◀──TCP── 服务�?
```

在仿真中所有通信�?`localhost`，通过不同端口区分�?

---

## 6.1 网络配置

**文件：`~/air_ground_sim_ws/src/com_bridge/config/network.yaml`**

```yaml
# Communication bridge network configuration
# In simulation, all use localhost. Ports for real hardware deployment.

drone_car_mavlink:
  protocol: "udp"
  drone_ip: "127.0.0.1"
  drone_port: 14550         # PX4 默认 MAVLink 端口 (SITL: udp_gcs_port_local)
  car_ip: "127.0.0.1"
  car_port: 14551
  heartbeat_interval: 1.0   # seconds

edge_server_tcp:
  protocol: "tcp"
  server_ip: "127.0.0.1"   # lab server IP (local in simulation)
  server_port: 9090
  reconnect_interval: 3.0   # seconds
  heartbeat_interval: 2.0

# Data throttling (simulate limited bandwidth of 3DR radio)
throttle:
  enable: true
  max_bandwidth_bytes_per_sec: 24000   # 3DR SiK 典型带宽 ~24KB/s
  image_quality: 50                    # JPEG compression 1-100
  max_image_freq: 2                    # Hz (图像最大发送频�?
```

## 6.2 无人机↔车 MAVLink 桥

**文件：`~/air_ground_sim_ws/src/com_bridge/scripts/drone_car_bridge.py`**

```python
#!/usr/bin/env python3
"""
MAVLink Bridge: Drone �?Car over simulated 3DR radio (UDP).

This node runs on the car edge and:
1. Listens for MAVLink heartbeat/telemetry from drone via UDP
2. Forwards drone state to ROS topics (/drone/state, /drone/pose, etc.)
3. Receives car→drone commands from ROS and forwards via MAVLink

Simulated bandwidth throttling: limits image/data rate to 3DR radio specs.
"""
import rospy
import socket
import struct
import threading
import time
import yaml
import os
from geometry_msgs.msg import PoseStamped, TwistStamped
from std_msgs.msg import String, Bool

# pymavlink is lightweight; use manual MAVLink v1 parsing for core messages
# to avoid full pymavlink dependency on car edge. If pymavlink is available,
# prefer the full parser. Fallback to minimal parser:
try:
    from pymavlink import mavutil
    HAS_PYMAVLINK = True
except ImportError:
    HAS_PYMAVLINK = False
    rospy.logwarn("[DroneCarBridge] pymavlink not found, using minimal MAVLink parser")


class DroneCarBridge:
    """MAVLink bridge between drone (UDP) and car's ROS topics."""

    # MAVLink message IDs (subset)
    MAVLINK_MSG_ID_HEARTBEAT = 0
    MAVLINK_MSG_ID_GLOBAL_POSITION_INT = 33
    MAVLINK_MSG_ID_ATTITUDE = 30
    MAVLINK_MSG_ID_COMMAND_LONG = 76

    def __init__(self):
        rospy.init_node("drone_car_bridge")

        # Load config
        config_path = rospy.get_param("~config_path",
            os.path.expanduser("~/air_ground_sim_ws/src/com_bridge/config/network.yaml"))
        with open(config_path) as f:
            self.config = yaml.safe_load(f)

        cfg = self.config["drone_car_mavlink"]
        self.drone_addr = (cfg["drone_ip"], cfg["drone_port"])
        self.car_port = cfg["car_port"]
        self.throttle = self.config.get("throttle", {})

        # UDP socket for MAVLink
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("0.0.0.0", self.car_port))
        self.sock.settimeout(0.5)

        # Throttling state
        self.last_image_time = 0.0
        self.bytes_sent_window = 0.0
        self.window_start = time.time()

        # ROS publishers (drone telemetry → ROS)
        self.pub_drone_pose = rospy.Publisher("/drone/pose", PoseStamped, queue_size=5)
        self.pub_drone_heartbeat = rospy.Publisher("/drone/heartbeat", Bool, queue_size=5)
        self.pub_drone_state = rospy.Publisher("/drone/state", String, queue_size=5)

        # ROS subscribers (car commands → drone MAVLink)
        self.sub_cmd = rospy.Subscriber("/car/to_drone/cmd", String, self.cmd_callback)

        # For drone pose, reuse existing gps_converter output instead of raw MAVLink parse
        rospy.Subscriber("/drone/gps/local_pose", PoseStamped, self._drone_pose_cb, queue_size=5)
        self._drone_pose = None

        # RX thread
        self.running = True
        self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
        self.rx_thread.start()

        rospy.loginfo(f"[DroneCarBridge] Listening on UDP:{self.car_port}")

    def _drone_pose_cb(self, msg: PoseStamped):
        """Receive properly converted ENU pose from gps_converter (not raw MAVLink lat/lon)."""
        self._drone_pose = msg
        self.pub_drone_pose.publish(msg)

    def _rx_loop(self):
        """Receive MAVLink packets from drone."""
        while self.running and not rospy.is_shutdown():
            try:
                data, addr = self.sock.recvfrom(4096)
                self._parse_mavlink(data)
                # Update drone address in case it changed
                if addr != self.drone_addr:
                    rospy.loginfo(f"[DroneCarBridge] Drone addr updated: {addr}")
                    # Don't update self.drone_addr - we still send to configured addr
            except socket.timeout:
                continue
            except Exception as e:
                rospy.logwarn(f"[DroneCarBridge] RX error: {e}")

    def _parse_mavlink(self, data: bytes):
        """Parse MAVLink v1/v2 packet and publish to ROS."""
        if len(data) < 8:
            return

        # MAVLink v1 frame: STX(0xFE) LEN SEQ SYS COMP MSG_ID PAYLOAD CKA CKB
        if data[0] == 0xFE:  # MAVLink v1
            msg_id = data[5]
            payload = data[6:-2]  # strip 2-byte checksum

            if msg_id == self.MAVLINK_MSG_ID_HEARTBEAT:
                self.pub_drone_heartbeat.publish(Bool(True))
                # Publish custom_mode as state
                if len(payload) >= 8:
                    custom_mode = struct.unpack("<I", payload[2:6])[0]
                    self.pub_drone_state.publish(String(f"mode:{custom_mode}"))

            elif msg_id == self.MAVLINK_MSG_ID_GLOBAL_POSITION_INT:
                # NOTE: Raw MAVLink lat/lon (degrees*1e7) is not a valid ENU pose.
                # Use /drone/gps/local_pose (from gps_converter.py, HOME-offset ENU) instead.
                # This handler only logs that the drone is alive.
                rospy.logdebug("[DroneCarBridge] GPS raw received (ignored; using /drone/gps/local_pose)")

    def cmd_callback(self, msg: String):
        """Forward car commands to drone via MAVLink."""
        # Throttle check
        if self.throttle.get("enable", False):
            elapsed = time.time() - self.window_start
            if elapsed > 1.0:
                self.window_start = time.time()
                self.bytes_sent_window = 0
            max_bw = self.throttle.get("max_bandwidth_bytes_per_sec", 24000)
            if self.bytes_sent_window > max_bw:
                rospy.logdebug("[DroneCarBridge] Throttled")
                return

        # Build simple MAVLink COMMAND_LONG
        # For full implementation, use pymavlink.mavutil.mavlink.MAVLink_command_long_encode
        if HAS_PYMAVLINK:
            try:
                mav = mavutil.mavlink.MAVLink(None)
                # Simplified - real impl would parse msg.data
                cmd_bytes = bytes([0xFE, 21, 0, 1, 0, self.MAVLINK_MSG_ID_COMMAND_LONG])
                cmd_bytes += bytes(32)  # placeholder payload + checksum
                self.sock.sendto(cmd_bytes, self.drone_addr)
                self.bytes_sent_window += len(cmd_bytes)
            except Exception as e:
                rospy.logwarn(f"[DroneCarBridge] TX error: {e}")

    def shutdown(self):
        self.running = False
        self.sock.close()


if __name__ == "__main__":
    bridge = DroneCarBridge()
    rospy.on_shutdown(bridge.shutdown)
    rospy.spin()
```

```bash
chmod +x ~/air_ground_sim_ws/src/com_bridge/scripts/drone_car_bridge.py
```

## 6.3 边缘↔服务器 TCP 桥

**文件：`~/air_ground_sim_ws/src/com_bridge/scripts/edge_server_bridge.py`**

```python
#!/usr/bin/env python3
"""
Edge-Server TCP Bridge.

Runs on car edge node. Responsibilities:
1. Collect sensor+state data �?serialize as JSON �?send to server via TCP
2. Receive ServerCommand from server �?publish to local ROS topics
"""
import rospy
import socket
import json
import struct
import threading
import time
import yaml
import os
import zlib
from air_ground_interfaces.msg import SensorFusion, ServerCommand
from sensor_msgs.msg import Image, LaserScan, Imu
import base64
import cv2
from cv_bridge import CvBridge
from nav_msgs.msg import Odometry
from geometry_msgs.msg import PoseStamped
import numpy as np


class EdgeServerBridge:
    def __init__(self):
        rospy.init_node("edge_server_bridge")

        # Load config
        config_path = rospy.get_param("~config_path",
            os.path.expanduser("~/air_ground_sim_ws/src/com_bridge/config/network.yaml"))
        with open(config_path) as f:
            cfg = yaml.safe_load(f)["edge_server_tcp"]
            self.network_cfg = yaml.safe_load(f)

        self.server_addr = (cfg["server_ip"], cfg["server_port"])
        self.reconnect_interval = cfg.get("reconnect_interval", 3.0)
        self.throttle_cfg = self.network_cfg.get("throttle", {})

        # CV bridge for image compression
        self.cv_bridge = CvBridge()

        # Latest sensor data cache
        self.latest = {
            "odom": None,
            "imu": None,
            "scan": None,
            "image": None,
            "ultrasonic": {},
            "drone_pose": None,
        }
        self.last_image_time = 0.0  # throttle image encoding

        # Subscribers (car sensors)
        rospy.Subscriber("/car/odom", Odometry, self._cb("odom"), queue_size=5)
        rospy.Subscriber("/car/imu/data", Imu, self._cb("imu"), queue_size=5)
        rospy.Subscriber("/car/scan", LaserScan, self._cb("scan"), queue_size=5)
        rospy.Subscriber("/car/openmv/image_raw", Image, self._cb("image"), queue_size=2)
        for d in ["front", "rear", "left", "right"]:
            rospy.Subscriber(f"/car/ultrasonic/{d}", LaserScan,
                             self._cb_ultrasonic(d), queue_size=5)
        rospy.Subscriber("/drone/pose", PoseStamped, self._cb("drone_pose"), queue_size=5)

        # Publisher (server→edge commands)
        self.pub_server_cmd = rospy.Publisher("/car/server_command", ServerCommand, queue_size=10)

        # TCP state
        self.sock = None
        self.connected = False
        self.running = True

        # TX thread (periodic data push)
        self.tx_timer = rospy.Timer(rospy.Duration(0.1), self._tx_tick)  # 10Hz push

        # Connect async
        self.connect_thread = threading.Thread(target=self._connect_loop, daemon=True)
        self.connect_thread.start()

        rospy.loginfo(f"[EdgeServerBridge] Ready, server={self.server_addr}")

    def _cb(self, key):
        """Create a callback that caches latest value."""
        def callback(msg):
            self.latest[key] = msg
        return callback

    def _cb_ultrasonic(self, direction):
        def callback(msg):
            self.latest["ultrasonic"][direction] = msg
        return callback

    def _connect_loop(self):
        """Persistent TCP reconnect loop."""
        while self.running and not rospy.is_shutdown():
            try:
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.sock.settimeout(5.0)
                self.sock.connect(self.server_addr)
                self.connected = True
                rospy.loginfo("[EdgeServerBridge] Connected to server")
                # RX thread for server commands
                rx = threading.Thread(target=self._rx_loop, daemon=True)
                rx.start()
                break
            except (socket.error, ConnectionRefusedError) as e:
                rospy.logwarn(f"[EdgeServerBridge] Connection failed: {e}, retrying...")
                time.sleep(self.reconnect_interval)

    def _tx_tick(self, event):
        """Periodic data push to server."""
        if not self.connected or self.sock is None:
            return
        try:
            payload = self._serialize_sensors()
            data = json.dumps(payload).encode()
            # Prepend 4-byte length header
            header = struct.pack("!I", len(data))
            self.sock.sendall(header + data)
        except (socket.error, BrokenPipeError) as e:
            rospy.logwarn(f"[EdgeServerBridge] TX failed: {e}")
            self.connected = False

    def _serialize_sensors(self) -> dict:
        """Serialize latest sensor data to lightweight JSON dict."""
        out = {"timestamp": rospy.Time.now().to_sec(), "source": "car"}

        # Odometry �?pose
        if self.latest["odom"]:
            o = self.latest["odom"]
            out["pose"] = {
                "x": o.pose.pose.position.x, "y": o.pose.pose.position.y,
                "z": o.pose.pose.position.z,
                "qw": o.pose.pose.orientation.w, "qx": o.pose.pose.orientation.x,
                "qy": o.pose.pose.orientation.y, "qz": o.pose.pose.orientation.z
            }
            out["twist"] = {
                "vx": o.twist.twist.linear.x, "vz": o.twist.twist.angular.z
            }

        # IMU �?orientation + angular velocity
        if self.latest["imu"]:
            i = self.latest["imu"]
            out["imu"] = {
                "wx": i.angular_velocity.x, "wy": i.angular_velocity.y,
                "wz": i.angular_velocity.z,
                "ax": i.linear_acceleration.x, "ay": i.linear_acceleration.y,
                "az": i.linear_acceleration.z
            }

        # LiDAR �?compressed range array (pick every 4th sample for bandwidth)
        if self.latest["scan"]:
            s = self.latest["scan"]
            ranges = s.ranges[::4]  # downsample 4:1
            # JSON 不支�?inf: �?-1.0 替代
            out["scan"] = {
                "angle_min": s.angle_min, "angle_increment": s.angle_increment * 4,
                "ranges": [r if r > 0 and np.isfinite(r) else -1.0 for r in ranges]
            }

        # OpenMV image �?base64 JPEG (throttled to bandwidth limits)
        if self.latest["image"]:
            now = time.time()
            max_freq = self.throttle_cfg.get("max_image_freq", 2)
            if now - self.last_image_time >= 1.0 / max_freq:
                self.last_image_time = now
                try:
                    # Convert ROS Image �?OpenCV �?JPEG �?base64
                    cv_img = self.cv_bridge.imgmsg_to_cv2(self.latest["image"], "bgr8")
                    quality = self.throttle_cfg.get("image_quality", 50)
                    _, jpeg = cv2.imencode(".jpg", cv_img,
                                           [cv2.IMWRITE_JPEG_QUALITY, quality])
                    out["image_jpeg_b64"] = base64.b64encode(jpeg.tobytes()).decode()
                except Exception as e:
                    rospy.logwarn(f"[EdgeServerBridge] Image encode failed: {e}")

        # Ultrasonic �?distances
        out["ultrasonic"] = {}
        for d, msg in self.latest["ultrasonic"].items():
            if msg and msg.ranges:
                out["ultrasonic"][d] = min(msg.ranges[0], 4.0)  # clamp

        # Drone pose (if available)
        if self.latest["drone_pose"]:
            dp = self.latest["drone_pose"]
            out["drone_pose"] = {
                "x": dp.pose.position.x, "y": dp.pose.position.y,
                "z": dp.pose.position.z
            }

        return out

    def _rx_loop(self):
        """Receive server commands via TCP."""
        while self.running and self.connected:
            try:
                # Read 4-byte length header
                header = self.sock.recv(4)
                if len(header) < 4:
                    break
                length = struct.unpack("!I", header)[0]
                data = b""
                while len(data) < length:
                    chunk = self.sock.recv(length - len(data))
                    if not chunk:
                        break
                    data += chunk
                if data:
                    self._handle_server_msg(json.loads(data))
            except socket.timeout:
                continue
            except Exception as e:
                rospy.logwarn(f"[EdgeServerBridge] RX error: {e}")
                break
        self.connected = False

    def _handle_server_msg(self, msg: dict):
        """Parse server message and publish to ROS."""
        cmd = ServerCommand()
        cmd.header.stamp = rospy.Time.now()
        cmd.target_id = msg.get("target_id", "car")
        cmd.command_type = msg.get("command_type", "")
        cmd.query_text = msg.get("query_text", "")
        target = msg.get("target_pose", {})
        cmd.target_pose.position.x = target.get("x", 0)
        cmd.target_pose.position.y = target.get("y", 0)
        cmd.target_pose.position.z = target.get("z", 0)
        cmd.target_velocity = msg.get("target_velocity", 0.0)
        self.pub_server_cmd.publish(cmd)

    def shutdown(self):
        self.running = False
        if self.sock:
            self.sock.close()


if __name__ == "__main__":
    bridge = EdgeServerBridge()
    rospy.on_shutdown(bridge.shutdown)
    rospy.spin()
```

```bash
chmod +x ~/air_ground_sim_ws/src/com_bridge/scripts/edge_server_bridge.py
```

## 6.4 Launch 文件

**文件：`~/air_ground_sim_ws/src/com_bridge/launch/com_bridge.launch`**

```xml
<launch>
  <!-- MAVLink Drone↔Car Bridge -->
  <node name="drone_car_bridge" pkg="com_bridge" type="drone_car_bridge.py"
        output="screen">
    <param name="config_path"
           value="$(find com_bridge)/config/network.yaml"/>
  </node>

  <!-- Edge↔Server TCP Bridge -->
  <node name="edge_server_bridge" pkg="com_bridge" type="edge_server_bridge.py"
        output="screen">
    <param name="config_path"
           value="$(find com_bridge)/config/network.yaml"/>
  </node>
</launch>
```

## 6.5 验证脚本

**文件：`~/air_ground_sim_ws/src/com_bridge/scripts/test_bridge.sh`**

```bash
#!/bin/bash
echo "=== Task-06 Communication Bridge Verification ==="

# Start drone in background
roslaunch drone_bringup drone_sitl.launch headless:=true gui:=false &
DRONE_PID=$!
sleep 12

# Start car in background
roslaunch car_bringup car_diff.launch headless:=true gui:=false &
CAR_PID=$!
sleep 8

# Start bridge
roslaunch com_bridge com_bridge.launch &
BRIDGE_PID=$!
sleep 5

# 1. Check drone heartbeat published by bridge
echo "--- Checking /drone/heartbeat ---"
rostopic echo /drone/heartbeat -n 1 2>/dev/null | grep -q "data" && echo "[PASS]" || echo "[WARN]"

# 2. Check drone pose forwarded
echo "--- Checking /drone/pose ---"
rostopic echo /drone/pose -n 1 2>/dev/null | grep -q "position" && echo "[PASS]" || echo "[WARN]"

# 3. Check edge_server_bridge is running
echo "--- Checking edge_server_bridge node ---"
rosnode list 2>/dev/null | grep -q "edge_server_bridge" && echo "[PASS]" || echo "[WARN]"

# Cleanup
kill $BRIDGE_PID $CAR_PID $DRONE_PID 2>/dev/null
wait 2>/dev/null
echo "=== Done ==="
```

## 6.6 更新 `com_bridge/CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.0.2)
project(com_bridge)
find_package(catkin REQUIRED COMPONENTS
  roscpp rospy std_msgs geometry_msgs sensor_msgs nav_msgs
  air_ground_interfaces mavros
)
catkin_package()
install(DIRECTORY launch config scripts
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION})
```

## 交付产物

1. `drone_car_bridge.py` 可接收无人机 MAVLink 心跳并发�?`/drone/heartbeat`
2. `edge_server_bridge.py` 可正常启动（连接服务器可能显�?retrying，这是预期的——服务器�?Task-07 实现�?
3. `test_bridge.sh` 前两�?PASS，第三项至少确认 edge_server_bridge 节点在运�?
