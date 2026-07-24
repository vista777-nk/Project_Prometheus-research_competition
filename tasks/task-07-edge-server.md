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

## 7.4 World Model 节点（系统认知中心）← 新增

> 根据 ChatGPT 审阅建议 #3: 从 Day 1 就将 World Model 定义为系统概念中心。  
> 一切节点要么 TELL、要么 ASK World Model。

**文件：`~/air_ground_sim_ws/src/lab_server/scripts/world_model.py`**

```python
#!/usr/bin/env python3
"""
World Model — 系统认知中心 (Core of the Platform).

架构角色: Layer 4 的核心. 所有节点的信息交换都通过 World Model.
- 接收 TELL: Observation, RobotState, SLAM 位姿, EQA 回答
- 提供 ASK: WorldState (统一状态视图), QueryWorldState 服务
- 维护: 全局一致的世界状态 (map, agents, landmarks, dynamic obstacles)

设计原则 (ICD §二.3):
  WorldState 是 World Model 的**输出**.
  决策节点只 ASK WorldState, 不直接读传感器.

当前阶段: 占位. 聚合 Observation + RobotState → 发布 WorldState.
"""
import rospy
from air_ground_interfaces.msg import (
    Observation, RobotState, WorldState, SemanticLandmark
)
from air_ground_interfaces.srv import QueryWorldState, QueryWorldStateResponse
import threading


class WorldModel:
    """维护全局一致的世界认知."""

    def __init__(self):
        rospy.init_node("world_model")

        # ── TELL 入口 ──
        # 各 edge node 告诉 WorldModel 它们的 Observation 和 State
        rospy.Subscriber("/server/drone/observation", Observation,
                         self._tell_obs("drone"), queue_size=5)
        rospy.Subscriber("/server/car/observation", Observation,
                         self._tell_obs("car"), queue_size=5)
        rospy.Subscriber("/server/drone/state", RobotState,
                         self._tell_state("drone"), queue_size=5)
        rospy.Subscriber("/server/car/state", RobotState,
                         self._tell_state("car"), queue_size=5)

        # SLAM 结果 → TELL WorldModel
        # (在 task-07 占位阶段, SLAM node 也通过这个话题更新)
        rospy.Subscriber("/server/world_state/update", WorldState,
                         self._on_external_update, queue_size=5)

        # ── ASK 出口 ──
        # 发布统一的 WorldState
        self.world_state_pub = rospy.Publisher("/server/world_state", WorldState, queue_size=5, latch=True)

        # QueryWorldState 服务 (同步查询)
        self.query_srv = rospy.Service("/server/query_world_state",
                                       QueryWorldState, self._handle_query)

        # ── 内部状态 ──
        self._lock = threading.Lock()
        self._latest_obs = {}      # robot_id → Observation
        self._latest_states = {}   # robot_id → RobotState
        self._landmarks = []       # SemanticLandmark[]

        # 定时发布 (5Hz)
        rospy.Timer(rospy.Duration(0.2), self._publish_world_state)
        rospy.loginfo("[WorldModel] Cognitive center ready. Awaiting TELLs from agents.")

    def _tell_obs(self, robot_id: str):
        """TELL: agent 上报一次 Observation."""
        def callback(msg: Observation):
            with self._lock:
                self._latest_obs[robot_id] = msg
        return callback

    def _tell_state(self, robot_id: str):
        """TELL: agent 上报一次 RobotState."""
        def callback(msg: RobotState):
            with self._lock:
                self._latest_states[robot_id] = msg
        return callback

    def _on_external_update(self, msg: WorldState):
        """外部节点 (SLAM/EQA) TELL WorldModel 更新."""
        with self._lock:
            if msg.agents:
                for agent in msg.agents:
                    self._latest_states[agent.robot_id] = agent
            if msg.landmarks:
                self._landmarks = list(msg.landmarks)

    def _publish_world_state(self, event):
        """定期发布聚合后的 WorldState."""
        ws = WorldState()
        ws.header.stamp = rospy.Time.now()
        ws.header.frame_id = "map"

        with self._lock:
            # 聚合所有 agent 状态
            ws.agents = list(self._latest_states.values())
            ws.landmarks = list(self._landmarks)

        self.world_state_pub.publish(ws)

    def _handle_query(self, req):
        """ASK: 同步查询 WorldState."""
        resp = QueryWorldStateResponse()
        # Placeholder: just return latest world state
        resp.found = True
        # In real impl: filter by query_type (e.g., "nearest_landmark")
        return resp


if __name__ == "__main__":
    WorldModel()
    rospy.spin()
```

```bash
chmod +x ~/air_ground_sim_ws/src/lab_server/scripts/world_model.py
```

---

## 7.5 SLAM 占位节点（→ World Model）

**文件：`~/air_ground_sim_ws/src/lab_server/scripts/slam_node.py`**

```python
#!/usr/bin/env python3
"""
SLAM Node — 定位与建图 (Placeholder → World Model 的数据源之一).

架构角色: 本节点属于 Layer 4 (Research). 它从 World Model 订阅
Observation + RobotState, 计算后 TELL World Model 更新 map / pose.

当前阶段: 占位运行, 仅统计数据流频率.
后续: 接入 RTAB-Map / SLAM Toolbox 等真实 SLAM 算法.
"""
import rospy
from air_ground_interfaces.msg import Observation, RobotState
from nav_msgs.msg import OccupancyGrid, Odometry
from geometry_msgs.msg import PoseWithCovarianceStamped


class SLAMPlaceholder:
    """
    职责: 消费 Observation, 产出全局一致的位姿估计和地图.
    TELL WorldModel: 每当新 scan/odom 到达, 更新 WorldState.map_2d 和 agent pose.
    """

    def __init__(self):
        rospy.init_node("slam_node")

        # 订阅: Observation (由 World Model 或 tcp_receiver 提供)
        rospy.Subscriber("/server/car/observation", Observation, self.car_cb, queue_size=5)
        rospy.Subscriber("/server/drone/observation", Observation, self.drone_cb, queue_size=5)
        # 订阅: RobotState (里程计)
        rospy.Subscriber("/server/car/state", RobotState, self.car_state_cb, queue_size=5)

        # 发布: 更新后的 World State (map + pose)
        self.map_pub = rospy.Publisher("/server/world_state/map", OccupancyGrid, queue_size=1, latch=True)
        self.car_pose_pub = rospy.Publisher("/server/world_state/car_pose",
                                            PoseWithCovarianceStamped, queue_size=5)

        # 统计
        self.car_msg_count = 0
        self.drone_msg_count = 0
        self.start_time = rospy.Time.now()

        rospy.Timer(rospy.Duration(5.0), self._print_stats)
        rospy.loginfo("[SLAM Node] Placeholder ready. TELL WorldModel when data arrives.")

    def car_cb(self, msg: Observation):
        self.car_msg_count += 1
        # TODO: Feed LiDAR + odom → SLAM backend
        # TODO: TELL WorldModel: /server/world_state 的 map 和 agent pose

    def drone_cb(self, msg: Observation):
        self.drone_msg_count += 1
        # TODO: Fuse aerial depth for global map refinement
        # TODO: TELL WorldModel: 无人机俯瞰视角的全局地图更新

    def car_state_cb(self, msg: RobotState):
        pass  # TODO: Use for odometry prior in SLAM

    def _print_stats(self, event):
        elapsed = (rospy.Time.now() - self.start_time).to_sec()
        if elapsed > 0:
            rospy.loginfo(f"[SLAM] Car Obs: {self.car_msg_count/elapsed:.1f} Hz | "
                          f"Drone Obs: {self.drone_msg_count/elapsed:.1f} Hz | "
                          f"→ TELLs WorldModel each frame")


if __name__ == "__main__":
    SLAMPlaceholder()
    rospy.spin()
```

```bash
chmod +x ~/air_ground_sim_ws/src/lab_server/scripts/slam_node.py
```

## 7.6 EQA 引擎占位节点（← World Model → Coordinator）

**文件：`~/air_ground_sim_ws/src/lab_server/scripts/eqa_engine.py`**

```python
#!/usr/bin/env python3
"""
Embodied Question Answering (EQA) Engine (Placeholder → research core).

架构角色: 本节点属于 Layer 4 (Research).
1. ASK WorldModel: 当前有什么 landmark? 地图状态如何?
2. 调用 VLM (LLaVA / InternVL) 进行多模态推理.
3. TELL Coordinator: 下发探索/回答计划 (Mission).

约束 (ICD §三): EQA Engine 只消费 Observation/RobotState/WorldState 抽象接口,
   永远不 import mavros / sensor_msgs/LaserScan / gazebo_msgs.

当前阶段: 占位运行. 收到查询 → 日志记录 → 下发 explore 占位 Mission.
"""
import rospy
from std_msgs.msg import String
from air_ground_interfaces.msg import Mission


class EQAEngine:
    """
    生命周期:
        query → ASK WorldModel → reason (VLM) → TELL Coordinator (Mission)
    """

    def __init__(self):
        rospy.init_node("eqa_engine")

        # 查询输入 (外部触发)
        self.query_sub = rospy.Subscriber("/server/eqa/query", String, self.query_cb, queue_size=5)

        # 输出: Mission → Coordinator
        self.mission_pub = rospy.Publisher("/server/eqa/mission", Mission, queue_size=5)

        rospy.loginfo("[EQA Engine] Placeholder ready. VLM + WorldModel integration: TBD")

    def query_cb(self, msg: String):
        rospy.loginfo(f"[EQA] Received query: '{msg.data}'")
        # TODO Step 1: ASK WorldModel ('QueryWorldState' service)
        #   → 获取当前已知 landmark、agent pose、地图

        # TODO Step 2: VLM reasoning
        #   → 输入: query_text + WorldState + RGB images
        #   → 输出: 回答 或 下一探索 Mission

        # TODO Step 3: TELL Coordinator
        #   → 如果回答已确定: publish answer
        #   → 如果需继续探索: publish Mission(type="search"/"inspect")

        # 占位: 下发一个 explore Mission
        mission = Mission()
        mission.header.stamp = rospy.Time.now()
        mission.mission_id = f"eqa_{rospy.Time.now().to_nsec()}"
        mission.robot_id = "car"
        mission.type = "explore"
        mission.query_text = msg.data
        mission.priority = 128
        self.mission_pub.publish(mission)
        rospy.loginfo("[EQA] → TELL Coordinator: explore mission issued")


if __name__ == "__main__":
    EQAEngine()
    rospy.spin()
```

```bash
chmod +x ~/air_ground_sim_ws/src/lab_server/scripts/eqa_engine.py
```

## 7.7 空地协同策略占位节点（← EQA/Planner → Edge）

**文件：`~/air_ground_sim_ws/src/lab_server/scripts/coordinator.py`**

```python
#!/usr/bin/env python3
"""
Air-Ground Coordinator (Placeholder → 空地协同调度中心).

架构角色: 本节点属于 Layer 4 (Research).
1. ASK WorldModel: 获取最新 WorldState (用于决策).
2. 接收 EQA Engine / Planner 的 Mission 指令.
3. TELL Edge: 将 Mission 翻译为具体执行指令 (位置/航点) 下发给车机或无人机.

约束 (ICD): Coordinator 不关心底层是 PX4 还是 STM32,
   只面对统一的 Mission 接口.

当前阶段: 占位运行. 收到 Mission → 下发简单 cmd_vel / takeoff setpoint.
"""
import rospy
from air_ground_interfaces.msg import Mission, RobotState, WorldState
from geometry_msgs.msg import PoseStamped, Twist


class Coordinator:
    """
    流程: Mission → 拆解为子任务 → 按 agent capability 分发.
    """

    def __init__(self):
        rospy.init_node("coordinator")

        # ASK WorldModel: 订阅 WorldState 获取全局认知
        rospy.Subscriber("/server/world_state", WorldState, self.world_state_cb, queue_size=5)

        # 接收 Mission (from EQA / Planner / external)
        rospy.Subscriber("/server/eqa/mission", Mission, self.mission_cb, queue_size=5)

        # TELL Edge: 下发执行指令
        self.drone_setpoint_pub = rospy.Publisher("/drone/mission_setpoint", PoseStamped, queue_size=5)
        self.car_cmd_pub = rospy.Publisher("/car/cmd_vel", Twist, queue_size=5)

        # 内部状态
        self.world_state = None

        rospy.loginfo("[Coordinator] Placeholder ready. ASK WorldModel → TELL Edge.")

    def world_state_cb(self, msg: WorldState):
        """ASK WorldModel: 维护全局认知."""
        self.world_state = msg

    def mission_cb(self, msg: Mission):
        """
        收到 Mission → 根据 agent capability + WorldState 决策.
        TELL Edge: 下发具体指令.
        """
        rospy.loginfo(f"[Coordinator] Mission received: id={msg.mission_id}, "
                      f"type={msg.type}, robot={msg.robot_id}, query='{msg.query_text}'")

        if msg.type == "explore" and msg.robot_id == "car":
            # Placeholder: 让车前进
            twist = Twist()
            twist.linear.x = 0.2
            self.car_cmd_pub.publish(twist)
            rospy.loginfo("[Coordinator] → TELL car: explore (fwd 0.2m/s)")

        elif msg.type == "takeoff" and msg.robot_id == "drone":
            # Placeholder: 无人机起飞 5m
            sp = PoseStamped()
            sp.header.stamp = rospy.Time.now()
            sp.header.frame_id = "map"
            sp.pose.position.z = 5.0
            sp.pose.orientation.w = 1.0
            self.drone_setpoint_pub.publish(sp)
            rospy.loginfo("[Coordinator] → TELL drone: takeoff to 5m")

        else:
            rospy.logwarn(f"[Coordinator] Unhandled mission type: {msg.type}")


if __name__ == "__main__":
    Coordinator()
    rospy.spin()
```

```bash
chmod +x ~/air_ground_sim_ws/src/lab_server/scripts/coordinator.py
```

## 7.8 Launch 文件

**文件：`~/air_ground_sim_ws/src/drone_bringup/launch/drone_edge.launch`**

```xml
<launch>
  <!-- 边缘预处理器: 传感器 → Observation + RobotState -->
  <node name="drone_preprocessor" pkg="drone_bringup" type="drone_preprocessor.py"
        output="screen"/>
  <node name="gps_converter" pkg="drone_bringup" type="gps_converter.py"
        output="screen"/>
</launch>
```

**文件：`~/air_ground_sim_ws/src/car_bringup/launch/car_edge.launch`**

```xml
<launch>
  <!-- 边缘预处理器: 传感器 → Observation + RobotState -->
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
  <!-- Layer 2→4 桥接: TCP JSON → ROS -->
  <node name="tcp_server" pkg="lab_server" type="tcp_receiver.py" output="screen"/>

  <!-- Layer 4: World Model (认知中心) -->
  <!-- 所有其他节点 ASK/TELL WorldModel，不直接读传感器 -->
  <node name="world_model" pkg="lab_server" type="world_model.py" output="screen"/>

  <!-- Layer 4: 研究模块 (只依赖 WorldState + Mission 接口) -->
  <node name="slam_node" pkg="lab_server" type="slam_node.py" output="screen"/>
  <node name="eqa_engine" pkg="lab_server" type="eqa_engine.py" output="screen"/>
  <node name="coordinator" pkg="lab_server" type="coordinator.py" output="screen"/>
</launch>
```

## 7.9 验证脚本

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
for node in tcp_server world_model slam_node eqa_engine coordinator; do
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
3. **World Model (`world_model`) 节点运行**，发布 `/server/world_state`
4. 所有 5 个服务器节点都在运行（`rosnode list` 确认）：tcp_server, world_model, slam_node, eqa_engine, coordinator
5. 发送 EQA query 能触发 Mission → Coordinator 响应链
6. `test_server.sh` 全部 PASS
