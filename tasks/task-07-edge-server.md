# Task-07: 边缘预处理 + 服务器节点

## 前置条件

- Task-02~06 完成（无人机、车、传感器、通信桥均可运行）

## 目标

1. **边缘端**：无人机和车机上各运行一个 preprocessor 节点，聚合传感器数据为 `SensorFusion` 消息
2. **服务器端**：TCP 接收服务器 + SLAM/EQA/协调器占位节点

---

## 7.1 无人机边缘预处理器

**文件：`~/air_ground_sim_ws/src/drone_bringup/scripts/drone_preprocessor.py`**

```python
#!/usr/bin/env python3
"""
Drone Edge Preprocessor.

Runs on drone's Raspberry Pi 5 (simulated).
Aggregates: depth camera, GPS, IMU → SensorFusion message.
Publishes to /drone/sensor_fusion for the bridge to forward to server.
"""
import rospy
import numpy as np
from air_ground_interfaces.msg import SensorFusion
from sensor_msgs.msg import Image, Imu, NavSatFix, PointCloud2
from geometry_msgs.msg import PoseStamped
import sensor_msgs.point_cloud2 as pc2


class DronePreprocessor:
    def __init__(self):
        rospy.init_node("drone_preprocessor")

        # Latest data cache
        self.latest_depth = None     # depth image
        self.latest_rgb = None       # RGB image
        self.latest_imu = None
        self.latest_gps_pose = None

        # Subscribers
        rospy.Subscriber("/drone/depth_camera/depth/image_raw", Image,
                         self._cb_depth, queue_size=3)
        rospy.Subscriber("/drone/depth_camera/rgb/image_raw", Image,
                         self._cb_rgb, queue_size=3)
        rospy.Subscriber("/mavros/imu/data", Imu, self._cb_imu, queue_size=5)
        rospy.Subscriber("/drone/gps/local_pose", PoseStamped,
                         self._cb_gps, queue_size=5)

        # Publisher
        self.fusion_pub = rospy.Publisher("/drone/sensor_fusion", SensorFusion, queue_size=5)

        # Periodic publish (10Hz)
        rospy.Timer(rospy.Duration(0.1), self._publish_fusion)

        rospy.loginfo("[Drone Preprocessor] Ready (10Hz fusion output)")

    def _cb_depth(self, msg): self.latest_depth = msg
    def _cb_rgb(self, msg):   self.latest_rgb = msg
    def _cb_imu(self, msg):   self.latest_imu = msg
    def _cb_gps(self, msg):   self.latest_gps_pose = msg

    def _publish_fusion(self, event):
        msg = SensorFusion()
        msg.header.stamp = rospy.Time.now()
        msg.header.frame_id = "map"
        msg.source_id = "drone"

        # IMU
        if self.latest_imu:
            msg.imu = self.latest_imu

        # Images (depth + RGB, compressed if available)
        images = []
        if self.latest_depth:
            images.append(self.latest_depth)
        if self.latest_rgb:
            images.append(self.latest_rgb)
        msg.images = images

        # GPS pose
        if self.latest_gps_pose:
            msg.pose = self.latest_gps_pose.pose
            gps_point = type('', (), {})()
            gps_point.x = self.latest_gps_pose.pose.position.x
            gps_point.y = self.latest_gps_pose.pose.position.y
            gps_point.z = self.latest_gps_pose.pose.position.z
            msg.gps = [gps_point]

        self.fusion_pub.publish(msg)


if __name__ == "__main__":
    DronePreprocessor()
    rospy.spin()
```

```bash
chmod +x ~/air_ground_sim_ws/src/drone_bringup/scripts/drone_preprocessor.py
```

## 7.2 车机边缘预处理器

**文件：`~/air_ground_sim_ws/src/car_bringup/scripts/car_preprocessor.py`**

```python
#!/usr/bin/env python3
"""
Car Edge Preprocessor.

Runs on car's Raspberry Pi 5 (simulated).
Aggregates: OpenMV camera, 2D LiDAR, 4× ultrasonic, IMU, odometry
→ SensorFusion message. Publishes to /car/sensor_fusion.
"""
import rospy
import numpy as np
from air_ground_interfaces.msg import SensorFusion, ChassisState
from sensor_msgs.msg import Image, LaserScan, Imu, JointState
from nav_msgs.msg import Odometry
from geometry_msgs.msg import Pose


class CarPreprocessor:
    def __init__(self):
        rospy.init_node("car_preprocessor")

        # Data cache
        self.latest = {
            "image": None,    # OpenMV RGB
            "scan": None,     # 2D LiDAR
            "imu": None,
            "odom": None,
            "ultrasonic": {},
        }
        self.chassis_type = "unknown"

        # Subscribers
        rospy.Subscriber("/car/openmv/image_raw", Image, self._cb("image"), queue_size=3)
        rospy.Subscriber("/car/scan", LaserScan, self._cb("scan"), queue_size=5)
        rospy.Subscriber("/car/imu/data", Imu, self._cb("imu"), queue_size=5)
        rospy.Subscriber("/car/odom", Odometry, self._cb("odom"), queue_size=5)
        for d in ["front", "rear", "left", "right"]:
            rospy.Subscriber(f"/car/ultrasonic/{d}", LaserScan,
                             self._cb_ultrasonic(d), queue_size=5)

        # Publisher
        self.fusion_pub = rospy.Publisher("/car/sensor_fusion", SensorFusion, queue_size=5)
        self.chassis_pub = rospy.Publisher("/car/chassis_state", ChassisState, queue_size=5)

        # Detect chassis type
        self._detect_chassis()

        # Periodic publish (10Hz)
        rospy.Timer(rospy.Duration(0.1), self._publish_fusion)

        rospy.loginfo(f"[Car Preprocessor] Ready (chassis={self.chassis_type})")

    def _detect_chassis(self):
        """Detect current chassis by checking which wheel joints exist."""
        import subprocess
        try:
            topics = subprocess.check_output(["rostopic", "list"], text=True)
            if "/car/front_left_wheel_controller/command" in topics:
                self.chassis_type = "mecanum"
            elif "/car/left_wheel_joint" in topics:
                self.chassis_type = "diff"
        except:
            self.chassis_type = "unknown"

    def _cb(self, key):
        return lambda msg: self.latest.__setitem__(key, msg)

    def _cb_ultrasonic(self, direction):
        return lambda msg: self.latest["ultrasonic"].__setitem__(direction, msg)

    def _publish_fusion(self, event):
        msg = SensorFusion()
        msg.header.stamp = rospy.Time.now()
        msg.header.frame_id = "base_link"
        msg.source_id = "car"

        # Images (OpenMV)
        if self.latest["image"]:
            msg.images = [self.latest["image"]]

        # LiDAR scan
        if self.latest["scan"]:
            msg.laser = self.latest["scan"]

        # IMU
        if self.latest["imu"]:
            msg.imu = self.latest["imu"]

        # Odometry → pose
        if self.latest["odom"]:
            msg.pose = self.latest["odom"].pose.pose

        # Ultrasonic as float array [front, rear, left, right]
        ultrasonic_distances = [0.0, 0.0, 0.0, 0.0]
        for i, d in enumerate(["front", "rear", "left", "right"]):
            if d in self.latest["ultrasonic"] and self.latest["ultrasonic"][d]:
                s = self.latest["ultrasonic"][d]
                ultrasonic_distances[i] = s.ranges[0] if s.ranges else 4.0
        msg.ultrasonic = ultrasonic_distances

        self.fusion_pub.publish(msg)

        # Also publish chassis state
        cs = ChassisState()
        cs.header.stamp = rospy.Time.now()
        cs.chassis_type = self.chassis_type
        cs.is_connected = True
        cs.battery_voltage = 11.1  # simulated 3S
        cs.motor_currents = [0.0, 0.0, 0.0, 0.0]
        cs.encoder_ticks = [0.0, 0.0, 0.0, 0.0]
        self.chassis_pub.publish(cs)


if __name__ == "__main__":
    CarPreprocessor()
    rospy.spin()
```

```bash
chmod +x ~/air_ground_sim_ws/src/car_bringup/scripts/car_preprocessor.py
```

## 7.3 实验室服务器 — TCP 接收器

**文件：`~/air_ground_sim_ws/src/lab_server/scripts/tcp_receiver.py`**

```python
#!/usr/bin/env python3
"""
Lab Server TCP Receiver.

Listens for TCP connections from edge nodes (drone/car).
Receives JSON-serialized SensorFusion data → publishes to ROS topics.
Accepts client commands → sends ServerCommand JSON back.
"""
import rospy
import socket
import json
import struct
import threading
import yaml
import os
from air_ground_interfaces.msg import SensorFusion, ServerCommand
from geometry_msgs.msg import Pose
from sensor_msgs.msg import Imu, LaserScan
import numpy as np


class TCPServer:
    def __init__(self):
        rospy.init_node("tcp_server")

        config_path = rospy.get_param("~config_path",
            os.path.expanduser("~/air_ground_sim_ws/src/com_bridge/config/network.yaml"))
        with open(config_path) as f:
            cfg = yaml.safe_load(f)["edge_server_tcp"]

        self.host = "0.0.0.0"
        self.port = cfg["server_port"]

        # Publishers (deserialize edge data → ROS)
        self.pub_drone_fusion = rospy.Publisher("/server/drone/fusion", SensorFusion, queue_size=10)
        self.pub_car_fusion = rospy.Publisher("/server/car/fusion", SensorFusion, queue_size=10)

        # Server socket
        self.server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_sock.bind((self.host, self.port))
        self.server_sock.listen(2)
        self.server_sock.settimeout(1.0)

        self.clients = {}  # addr → socket
        self.running = True

        self.accept_thread = threading.Thread(target=self._accept_loop, daemon=True)
        self.accept_thread.start()

        rospy.loginfo(f"[TCP Server] Listening on {self.host}:{self.port}")

    def _accept_loop(self):
        while self.running and not rospy.is_shutdown():
            try:
                client_sock, addr = self.server_sock.accept()
                rospy.loginfo(f"[TCP Server] Client connected: {addr}")
                self.clients[addr] = client_sock
                t = threading.Thread(target=self._client_handler, args=(client_sock, addr),
                                     daemon=True)
                t.start()
            except socket.timeout:
                continue
            except Exception as e:
                rospy.logwarn(f"[TCP Server] Accept error: {e}")

    def _client_handler(self, sock: socket.socket, addr):
        """Handle one edge client."""
        sock.settimeout(1.0)
        while self.running and not rospy.is_shutdown():
            try:
                # Read 4-byte length header
                header = sock.recv(4)
                if len(header) < 4:
                    break
                length = struct.unpack("!I", header)[0]
                data = b""
                while len(data) < length:
                    chunk = sock.recv(length - len(data))
                    if not chunk:
                        break
                    data += chunk
                if data:
                    self._process_edge_data(json.loads(data), sock, addr)
            except socket.timeout:
                continue
            except Exception as e:
                rospy.logwarn(f"[TCP Server] Client {addr} error: {e}")
                break
        rospy.loginfo(f"[TCP Server] Client disconnected: {addr}")
        self.clients.pop(addr, None)

    def _process_edge_data(self, data: dict, sock: socket.socket, addr):
        """Convert JSON edge data to SensorFusion ROS message."""
        source = data.get("source", "unknown")
        msg = SensorFusion()
        msg.header.stamp = rospy.Time.from_sec(data.get("timestamp", 0))
        msg.header.frame_id = "map"
        msg.source_id = source

        # Pose
        if "pose" in data:
            p = data["pose"]
            msg.pose = Pose()
            msg.pose.position.x = p["x"]; msg.pose.position.y = p["y"]; msg.pose.position.z = p["z"]
            msg.pose.orientation.w = p["qw"]; msg.pose.orientation.x = p["qx"]
            msg.pose.orientation.y = p["qy"]; msg.pose.orientation.z = p["qz"]

        # IMU
        if "imu" in data:
            i = data["imu"]
            msg.imu = Imu()
            msg.imu.angular_velocity.x = i["wx"]
            msg.imu.angular_velocity.y = i["wy"]
            msg.imu.angular_velocity.z = i["wz"]
            msg.imu.linear_acceleration.x = i["ax"]
            msg.imu.linear_acceleration.y = i["ay"]
            msg.imu.linear_acceleration.z = i["az"]

        # Scan
        if "scan" in data:
            s = data["scan"]
            msg.laser = LaserScan()
            msg.laser.angle_min = s["angle_min"]
            msg.laser.angle_increment = s["angle_increment"]
            msg.laser.ranges = [float(r) if r > 0 else float('inf') for r in s["ranges"]]

        # Ultrasonic
        if "ultrasonic" in data:
            u = data["ultrasonic"]
            msg.ultrasonic = [u.get("front", 0), u.get("rear", 0),
                              u.get("left", 0), u.get("right", 0)]

        # Publish to source-specific topic
        if source == "drone":
            self.pub_drone_fusion.publish(msg)
        elif source == "car":
            self.pub_car_fusion.publish(msg)

    def shutdown(self):
        self.running = False
        self.server_sock.close()
        for sock in self.clients.values():
            sock.close()


if __name__ == "__main__":
    server = TCPServer()
    rospy.on_shutdown(server.shutdown)
    rospy.spin()
```

```bash
chmod +x ~/air_ground_sim_ws/src/lab_server/scripts/tcp_receiver.py
```

## 7.4 SLAM 占位节点

**文件：`~/air_ground_sim_ws/src/lab_server/scripts/slam_node.py`**

```python
#!/usr/bin/env python3
"""
SLAM Node (Placeholder).

Receives car SensorFusion (odom + LiDAR) and drone SensorFusion (depth + GPS)
→ placeholder: just logs data rate. Real SLAM implementation TBD.
"""
import rospy
from air_ground_interfaces.msg import SensorFusion
from nav_msgs.msg import OccupancyGrid, Odometry
from geometry_msgs.msg import PoseWithCovarianceStamped


class SLAMPlaceholder:
    def __init__(self):
        rospy.init_node("slam_node")

        # Subscribers
        rospy.Subscriber("/server/car/fusion", SensorFusion, self.car_cb, queue_size=5)
        rospy.Subscriber("/server/drone/fusion", SensorFusion, self.drone_cb, queue_size=5)

        # Publishers (placeholder)
        self.map_pub = rospy.Publisher("/server/map", OccupancyGrid, queue_size=1, latch=True)
        self.car_pose_pub = rospy.Publisher("/server/car/pose_estimate",
                                            PoseWithCovarianceStamped, queue_size=5)

        # Stats
        self.car_msg_count = 0
        self.drone_msg_count = 0
        self.start_time = rospy.Time.now()

        rospy.Timer(rospy.Duration(5.0), self._print_stats)
        rospy.loginfo("[SLAM Node] Placeholder ready. Real SLAM: TBD")

    def car_cb(self, msg: SensorFusion):
        self.car_msg_count += 1
        # TODO: Feed odom + LiDAR to SLAM (e.g., gmapping, slam_toolbox, Cartographer)

    def drone_cb(self, msg: SensorFusion):
        self.drone_msg_count += 1
        # TODO: Fuse aerial depth + GPS for global map updates

    def _print_stats(self, event):
        elapsed = (rospy.Time.now() - self.start_time).to_sec()
        if elapsed > 0:
            rospy.loginfo(f"[SLAM] Car: {self.car_msg_count/elapsed:.1f} Hz | "
                          f"Drone: {self.drone_msg_count/elapsed:.1f} Hz")


if __name__ == "__main__":
    SLAMPlaceholder()
    rospy.spin()
```

```bash
chmod +x ~/air_ground_sim_ws/src/lab_server/scripts/slam_node.py
```

## 7.5 EQA 引擎占位节点

**文件：`~/air_ground_sim_ws/src/lab_server/scripts/eqa_engine.py`**

```python
#!/usr/bin/env python3
"""
Embodied Question Answering (EQA) Engine (Placeholder).

Receives natural language query → plans air-ground exploration → answers.
Current: echo server that logs queries.
"""
import rospy
from std_msgs.msg import String
from air_ground_interfaces.msg import ServerCommand


class EQAEngine:
    def __init__(self):
        rospy.init_node("eqa_engine")

        # Query input (simulated)
        self.query_sub = rospy.Subscriber("/server/eqa/query", String, self.query_cb, queue_size=5)

        # Command output → coordinator
        self.cmd_pub = rospy.Publisher("/server/eqa/command", ServerCommand, queue_size=5)

        rospy.loginfo("[EQA Engine] Placeholder ready. Real VLM: TBD")

    def query_cb(self, msg: String):
        rospy.loginfo(f"[EQA] Received query: '{msg.data}'")
        # TODO: Call VLM (LLaVA / InternVL) on server GPU
        # TODO: Convert VLM output → exploration plan → ServerCommand sequence
        rospy.loginfo("[EQA] Response: placeholder - will explore and answer")

        # Echo a dummy explore command
        cmd = ServerCommand()
        cmd.header.stamp = rospy.Time.now()
        cmd.target_id = "car"
        cmd.command_type = "explore"
        cmd.query_text = msg.data
        self.cmd_pub.publish(cmd)


if __name__ == "__main__":
    EQAEngine()
    rospy.spin()
```

```bash
chmod +x ~/air_ground_sim_ws/src/lab_server/scripts/eqa_engine.py
```

## 7.6 空地协同策略占位节点

**文件：`~/air_ground_sim_ws/src/lab_server/scripts/coordinator.py`**

```python
#!/usr/bin/env python3
"""
Air-Ground Coordinator (Placeholder).

Orchestrates drone and car for joint tasks:
- Drone: overhead survey, global path planning from aerial view
- Car: ground-level navigation, close-up sensor inspection

Receives commands from EQA engine → dispatches to drone/car.
"""
import rospy
from air_ground_interfaces.msg import ServerCommand, SensorFusion
from geometry_msgs.msg import PoseStamped, Twist


class Coordinator:
    def __init__(self):
        rospy.init_node("coordinator")

        # Sensor fusion input
        rospy.Subscriber("/server/drone/fusion", SensorFusion, self.drone_fusion_cb, queue_size=5)
        rospy.Subscriber("/server/car/fusion", SensorFusion, self.car_fusion_cb, queue_size=5)

        # EQA commands
        rospy.Subscriber("/server/eqa/command", ServerCommand, self.eqa_cmd_cb, queue_size=5)

        # Output commands → edge devices
        self.drone_setpoint_pub = rospy.Publisher("/drone/setpoint", PoseStamped, queue_size=5)
        self.car_cmd_pub = rospy.Publisher("/car/cmd_vel", Twist, queue_size=5)

        # State
        self.drone_pose = None
        self.car_pose = None

        rospy.loginfo("[Coordinator] Placeholder ready. Real strategies: TBD")

    def drone_fusion_cb(self, msg: SensorFusion):
        self.drone_pose = msg.pose

    def car_fusion_cb(self, msg: SensorFusion):
        self.car_pose = msg.pose

    def eqa_cmd_cb(self, msg: ServerCommand):
        rospy.loginfo(f"[Coordinator] Dispatching: target={msg.target_id}, "
                      f"cmd={msg.command_type}, query='{msg.query_text}'")

        if msg.command_type == "explore" and msg.target_id == "car":
            # Placeholder: send car forward slowly
            twist = Twist()
            twist.linear.x = 0.2
            self.car_cmd_pub.publish(twist)
            rospy.loginfo("[Coordinator] Car exploring...")
        elif msg.command_type == "takeoff" and msg.target_id == "drone":
            # Placeholder: drone takeoff (via MAVROS setpoint)
            sp = PoseStamped()
            sp.header.stamp = rospy.Time.now()
            sp.header.frame_id = "map"
            sp.pose.position.z = 5.0  # 5m altitude
            sp.pose.orientation.w = 1.0
            self.drone_setpoint_pub.publish(sp)
            rospy.loginfo("[Coordinator] Drone taking off...")


if __name__ == "__main__":
    Coordinator()
    rospy.spin()
```

```bash
chmod +x ~/air_ground_sim_ws/src/lab_server/scripts/coordinator.py
```

## 7.7 Launch 文件

**文件：`~/air_ground_sim_ws/src/drone_bringup/launch/drone_edge.launch`**

```xml
<launch>
  <node name="drone_preprocessor" pkg="drone_bringup" type="drone_preprocessor.py"
        output="screen"/>
  <node name="gps_converter" pkg="drone_bringup" type="gps_converter.py"
        output="screen"/>
</launch>
```

**文件：`~/air_ground_sim_ws/src/car_bringup/launch/car_edge.launch`**

```xml
<launch>
  <node name="car_preprocessor" pkg="car_bringup" type="car_preprocessor.py"
        output="screen"/>
  <node name="gimbal_controller" pkg="car_bringup" type="gimbal_controller.py"
        output="screen"/>
  <node name="chassis_swapper" pkg="car_bringup" type="chassis_swapper.py"
        output="screen"/>
</launch>
```

**文件：`~/air_ground_sim_ws/src/lab_server/launch/server.launch`**

```xml
<launch>
  <node name="tcp_server" pkg="lab_server" type="tcp_receiver.py" output="screen"/>
  <node name="slam_node" pkg="lab_server" type="slam_node.py" output="screen"/>
  <node name="eqa_engine" pkg="lab_server" type="eqa_engine.py" output="screen"/>
  <node name="coordinator" pkg="lab_server" type="coordinator.py" output="screen"/>
</launch>
```

## 7.8 验证脚本

**文件：`~/air_ground_sim_ws/src/lab_server/scripts/test_server.sh`**

```bash
#!/bin/bash
echo "=== Task-07 Edge+Server Verification ==="

# Start drone + edge
roslaunch drone_bringup drone_sitl.launch headless:=true gui:=false &
D_PID=$!
sleep 8
roslaunch drone_bringup drone_edge.launch &
DE_PID=$!
sleep 3

# Start car + edge
roslaunch car_bringup car_diff.launch headless:=true gui:=false &
C_PID=$!
sleep 8
roslaunch car_bringup car_edge.launch &
CE_PID=$!
sleep 3

# Start server
roslaunch lab_server server.launch &
S_PID=$!
sleep 3

# 1. Check drone preprocessor
echo "--- Checking /drone/sensor_fusion ---"
rostopic echo /drone/sensor_fusion -n 1 2>/dev/null | grep -q "source_id" && echo "[PASS] Drone fusion" || echo "[WARN]"

# 2. Check car preprocessor
echo "--- Checking /car/sensor_fusion ---"
rostopic echo /car/sensor_fusion -n 1 2>/dev/null | grep -q "source_id" && echo "[PASS] Car fusion" || echo "[WARN]"

# 3. Check server nodes running
echo "--- Checking server nodes ---"
for node in tcp_server slam_node eqa_engine coordinator; do
  rosnode list 2>/dev/null | grep -q $node && echo "  [PASS] $node" || echo "  [WARN] $node"
done

# 4. Test EQA query
echo "--- Testing EQA query ---"
rostopic pub -1 /server/eqa/query std_msgs/String "data: 'Where is the red ball?'" 2>/dev/null
sleep 1
echo "[INFO] Check coordinator was triggered"

# Cleanup
kill $D_PID $DE_PID $C_PID $CE_PID $S_PID 2>/dev/null
wait 2>/dev/null
echo "=== Done ==="
```

## 交付产物

1. 无人机和车机的 `sensor_fusion` 话题均有数据流
2. TCP 服务器正常启动并监听 9090 端口
3. 所有 4 个服务器节点都在运行（`rosnode list` 确认）
4. 发送 EQA query 能触发 coordinator 响应
