# Task-01: 环境搭建 + ROS 工作空间脚手架

## 前置条件

- Ubuntu 20.04.6 (x86_64)
- 至少 20GB 自由磁盘空间
- 已连接互联网
- 用户有 sudo 权限

## 可执行步骤

### 1.1 安装 ROS Noetic

```bash
# 添加 ROS 源
sudo sh -c 'echo "deb http://packages.ros.org/ros/ubuntu focal main" > /etc/apt/sources.list.d/ros-latest.list'
sudo apt install curl -y
curl -s https://raw.githubusercontent.com/ros/rosdistro/master/ros.asc | sudo apt-key add -
sudo apt update

# 安装 ROS Noetic Desktop-Full（含 Gazebo 11）
sudo apt install ros-noetic-desktop-full -y

# 安装常用工具
sudo apt install python3-rosdep python3-rosinstall python3-rosinstall-generator python3-wstool build-essential python3-catkin-tools -y

# 初始化 rosdep
sudo rosdep init
rosdep update

# 写入 bashrc
echo "source /opt/ros/noetic/setup.bash" >> ~/.bashrc
source ~/.bashrc
```

### 1.2 安装 MAVROS 和相关依赖

```bash
sudo apt install ros-noetic-mavros ros-noetic-mavros-extras ros-noetic-mavros-msgs -y
sudo apt install ros-noetic-ros-control ros-noetic-ros-controllers ros-noetic-gazebo-ros-control -y
sudo apt install ros-noetic-teleop-twist-keyboard ros-noetic-joy -y
sudo apt install ros-noetic-robot-localization ros-noetic-gmapping -y
sudo apt install python3-pip python3-yaml python3-numpy -y
pip3 install pymavlink pyserial
```

### 1.3 创建工作空间和目录结构

```bash
# 在 Git 仓库根目录执行：源码保存在仓库内，工作空间只保存构建产物
cd /path/to/research_compitition
mkdir -p src
cd src

# 创建包目录（后续 task 填充内容）
mkdir -p air_ground_interfaces/msg air_ground_interfaces/srv air_ground_interfaces/action
mkdir -p air_ground_drone_bringup/{launch,config,scripts,worlds}
mkdir -p air_ground_car_bringup/{launch,config,scripts,urdf,worlds}
mkdir -p air_ground_com_bridge/launch air_ground_com_bridge/config air_ground_com_bridge/scripts
mkdir -p air_ground_lab_server/launch air_ground_lab_server/config air_ground_lab_server/scripts

# 将外部 Catkin 工作空间映射到 Git 仓库源码
mkdir -p ~/air_ground_sim_ws
ln -s "$(pwd)" ~/air_ground_sim_ws/src
```

若 `~/air_ground_sim_ws/src` 已存在，必须先确认其指向当前仓库的 `src/`；不要覆盖未知目录。

### 1.4 创建 `air_ground_interfaces` — 自定义消息包（需要先编译）

**文件：`air_ground_interfaces/package.xml`**
```xml
<?xml version="1.0"?>
<package format="2">
  <name>air_ground_interfaces</name>
  <version>0.1.0</version>
  <description>Custom messages and services for air-ground EQA system</description>
  <maintainer email="user@example.com">user</maintainer>
  <license>MIT</license>
  <buildtool_depend>catkin</buildtool_depend>
  <build_depend>message_generation</build_depend>
  <build_depend>actionlib_msgs</build_depend>
  <build_depend>std_msgs</build_depend>
  <build_depend>geometry_msgs</build_depend>
  <build_depend>sensor_msgs</build_depend>
  <build_depend>nav_msgs</build_depend>
  <exec_depend>message_runtime</exec_depend>
  <exec_depend>actionlib_msgs</exec_depend>
  <exec_depend>std_msgs</exec_depend>
  <exec_depend>geometry_msgs</exec_depend>
  <exec_depend>sensor_msgs</exec_depend>
  <exec_depend>nav_msgs</exec_depend>
</package>
```

**文件：`air_ground_interfaces/CMakeLists.txt`**
```cmake
cmake_minimum_required(VERSION 3.0.2)
project(air_ground_interfaces)

find_package(catkin REQUIRED COMPONENTS
  message_generation
  actionlib_msgs
  std_msgs
  geometry_msgs
  sensor_msgs
  nav_msgs
)

# ── ICD 稳定接口 ──
add_message_files(
  FILES
  Observation.msg
  RobotState.msg
  WorldState.msg
  SemanticLandmark.msg
  Mission.msg
  MissionStatus.msg
  Capability.msg
  # 旧别名（过渡兼容，下个版本删除）
  SensorFusion.msg
  ServerCommand.msg
  ChassisState.msg
)

add_service_files(
  FILES
  SwapChassis.srv
  QueryWorldState.srv
)

add_action_files(
  FILES
  Navigate.action
)

generate_messages(
  DEPENDENCIES
  actionlib_msgs
  std_msgs
  geometry_msgs
  sensor_msgs
  nav_msgs
)

catkin_package(
  CATKIN_DEPENDS
  message_runtime
  actionlib_msgs
  std_msgs
  geometry_msgs
  sensor_msgs
  nav_msgs
)
```

---

### ICD 新消息定义（task-07 依赖）

**文件：`air_ground_interfaces/msg/Observation.msg`**
```
# Edge → Server: multimodal observation snapshot (ICD §二.1)
Header header
string robot_id            # "drone" | "car"
string[] modalities        # present modalities: ["rgb","depth","lidar_2d","ultrasonic","imu"]

# Images (optional — only when modality listed)
sensor_msgs/CompressedImage rgb
sensor_msgs/Image depth

# LiDAR (optional, downsampled)
float32[] lidar_ranges
float32 lidar_angle_min
float32 lidar_angle_increment

# Ultrasonic (optional)
float32[] ultrasonic_ranges

# IMU (optional)
geometry_msgs/Vector3 angular_velocity
geometry_msgs/Vector3 linear_acceleration
```

**文件：`air_ground_interfaces/msg/RobotState.msg`**
```
# Edge → Server: robot self-state snapshot (ICD §二.2)
Header header
string robot_id

geometry_msgs/Pose pose
geometry_msgs/Twist velocity

string mode                 # "idle" | "navigating" | "exploring" | "emergency"
string chassis_type         # "diff" | "mecanum" | "none"

float32 battery_voltage
bool is_armed
bool is_connected
```

**文件：`air_ground_interfaces/msg/WorldState.msg`**
```
# Server internal: global world state maintained by WorldModel (ICD §二.3)
Header header

RobotState[] agents
nav_msgs/OccupancyGrid map_2d
geometry_msgs/Pose[] dynamic_obstacles
SemanticLandmark[] landmarks

time last_update_perception
time last_update_planning
```

**文件：`air_ground_interfaces/msg/SemanticLandmark.msg`**
```
# Semantic landmark for EQA queries (ICD §二.4)
string landmark_id
string semantic_label        # "red_ball", "door", "table", "charging_station"
geometry_msgs/Pose pose
float32 confidence           # 0.0 ~ 1.0
time last_observed
```

**文件：`air_ground_interfaces/msg/Mission.msg`**
```
# Server → Edge: high-level task (WHAT, not HOW) (ICD §三.1)
Header header
string mission_id            # UUID
string robot_id

string type                  # "navigate" | "search" | "inspect" | "return_home" | "follow" | "explore" | "takeoff" | "land"
geometry_msgs/Pose target_pose
string target_landmark_id
float32 search_radius
int32 priority               # 0=lowest, 255=highest

string query_text            # EQA query (if applicable)
string query_id
```

**文件：`air_ground_interfaces/msg/MissionStatus.msg`**
```
# Edge → Server: mission execution status (ICD §三.2)
Header header
string mission_id
string status                # "accepted" | "executing" | "completed" | "failed" | "aborted"
string failure_reason
float32 progress             # 0.0 ~ 1.0
```

**文件：`air_ground_interfaces/msg/Capability.msg`**
```
# Edge → Server: robot capability self-description (ICD §三.3)
Header header
string robot_id

string locomotion_type       # "aerial" | "ground_wheeled" | "ground_tracked"
float32 max_speed
float32 max_endurance

string[] sensor_modalities
float32 sensor_range

string compute_tier          # "edge_low" | "edge_mid" | "edge_high" | "server"
float32 max_payload_kg
bool has_gripper
```

---

### 新增服务定义

**文件：`air_ground_interfaces/srv/QueryWorldState.srv`**
```
# ASK WorldModel: synchronous query (ICD §四.2)
string query_type            # "nearest_landmark" | "path_to" | "visibility_from"
string[] args
---
WorldState result
bool found
```

---

### 新增 Action 定义

**文件：`air_ground_interfaces/action/Navigate.action`**
```
# Long-running navigation task (ICD §六)
geometry_msgs/Pose target_pose
float32 target_speed
---
float32 progress             # 0.0 ~ 1.0
geometry_msgs/Pose current_pose
---
# (empty — cancellation not needed for placeholder)
```

---

### 旧消息（别名兼容，下个版本删除）

**文件：`air_ground_interfaces/msg/SensorFusion.msg`**
```
# Edge node → Server: aggregated sensor data
Header header
string source_id          # "drone" or "car"
geometry_msgs/Pose pose   # current pose estimate
sensor_msgs/Imu imu       # IMU data
sensor_msgs/Image[] images # camera images (compressed)
sensor_msgs/LaserScan laser # 2D laser scan (car only)
float32[] ultrasonic       # ultrasonic distances (car only)
geometry_msgs/Point[] gps  # GPS coordinates (drone only)
```

**文件：`air_ground_interfaces/msg/ServerCommand.msg`**
```
# Server → Edge node: high-level commands
Header header
string target_id          # "drone" or "car"
string command_type       # "navigate", "takeoff", "land", "explore", "query"
geometry_msgs/Pose target_pose
float32 target_velocity
string query_text         # EQA query text
```

**文件：`air_ground_interfaces/msg/ChassisState.msg`**
```
# Car chassis status feedback
Header header
string chassis_type       # "diff" or "mecanum"
bool is_connected
float32 battery_voltage
float32[4] motor_currents
float32[4] encoder_ticks
```

**文件：`air_ground_interfaces/srv/SwapChassis.srv`**
```
# Request chassis swap
string target_chassis     # "diff" or "mecanum"
---
bool success
string message
```

### 1.5 创建其他包的 `package.xml` 和 `CMakeLists.txt`

**共同模板 — 以 `air_ground_drone_bringup/package.xml` 为例（其余类似）**：

```xml
<?xml version="1.0"?>
<package format="2">
  <name>air_ground_drone_bringup</name>
  <version>0.1.0</version>
  <description>Drone simulation bringup: PX4 SITL + sensors + edge preprocessor</description>
  <maintainer email="user@example.com">user</maintainer>
  <license>MIT</license>
  <buildtool_depend>catkin</buildtool_depend>
  <!-- CMake find_package 中使用的组件必须同时是构建和运行依赖 -->
  <depend>roscpp</depend>
  <depend>rospy</depend>
  <depend>std_msgs</depend>
  <depend>geometry_msgs</depend>
  <depend>sensor_msgs</depend>
  <depend>mavros_msgs</depend>
  <depend>air_ground_interfaces</depend>
  <exec_depend>mavros</exec_depend>
</package>
```

**共同模板 — 以 `air_ground_drone_bringup/CMakeLists.txt` 为例**：

```cmake
cmake_minimum_required(VERSION 3.0.2)
project(air_ground_drone_bringup)
find_package(catkin REQUIRED COMPONENTS
  roscpp rospy std_msgs geometry_msgs sensor_msgs air_ground_interfaces
)
catkin_package(
  CATKIN_DEPENDS
  roscpp rospy std_msgs geometry_msgs sensor_msgs air_ground_interfaces
)
include_directories(${catkin_INCLUDE_DIRS})
install(DIRECTORY launch config scripts
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION})
```

你需要为 `air_ground_car_bringup`、`air_ground_com_bridge`、`air_ground_lab_server` 创建类似文件，依赖项按需调整：
- `air_ground_car_bringup`：额外依赖 `gazebo_ros`、`gazebo_ros_control`、`controller_manager`、`hardware_interface`、`nav_msgs`
- `air_ground_com_bridge`：额外依赖 `mavros`、`mavros_msgs`、`nav_msgs`、`cv_bridge`
- `air_ground_lab_server`：依赖 `air_ground_interfaces`、`geometry_msgs`、`nav_msgs`、`rospy`、`std_msgs`；不得依赖 `mavros`、`gazebo_msgs` 或 `sensor_msgs`

> `ros_control` 是技术栈名称，不是可供 `find_package` 使用的 ROS package；应声明上面列出的实际 package。

### 1.6 初始化编译

```bash
source /opt/ros/noetic/setup.bash
cd ~/air_ground_sim_ws
catkin init
catkin config --extend /opt/ros/noetic \
  --cmake-args -DPYTHON_EXECUTABLE=/usr/bin/python3
catkin build
source devel/setup.bash

# 验证自定义消息编译成功
rosmsg show air_ground_interfaces/Observation
rosmsg show air_ground_interfaces/WorldState
rossrv show air_ground_interfaces/SwapChassis
rosmsg show air_ground_interfaces/NavigateAction
```

显式指定 `/usr/bin/python3`，避免已自动激活的 Conda Python 与 ROS Noetic 的系统 Python 3.8 冲突。确认环境稳定后，可按需将 ROS 与工作空间的 `setup.bash` 加入用户 Shell 配置；本任务不自动修改 `~/.bashrc`。

### 1.7 验证脚本

创建 `~/air_ground_sim_ws/src/test_task01.sh`：

```bash
#!/usr/bin/env bash

set -o pipefail

WORKSPACE_ROOT="${AIR_GROUND_WS:-${HOME}/air_ground_sim_ws}"
FAILURES=0

pass() {
  printf '[PASS] %s\n' "$1"
}

fail() {
  printf '[FAIL] %s\n' "$1"
  FAILURES=$((FAILURES + 1))
}

check_ros_interface() {
  local command_name="$1"
  local interface_name="$2"

  if "${command_name}" show "${interface_name}" >/dev/null 2>&1; then
    pass "${interface_name}"
  else
    fail "${interface_name}"
  fi
}

echo "=== Task-01 Verification ==="

if [ -f /opt/ros/noetic/setup.bash ]; then
  source /opt/ros/noetic/setup.bash
  pass "ROS Noetic installed"
else
  fail "ROS Noetic setup not found"
fi

if command -v gzserver >/dev/null 2>&1; then
  pass "Gazebo server installed"
else
  fail "Gazebo server not found"
fi

if [ -f "${WORKSPACE_ROOT}/devel/setup.bash" ]; then
  source "${WORKSPACE_ROOT}/devel/setup.bash"
  pass "Catkin workspace built"
else
  fail "Workspace setup missing"
fi

for message_name in Capability ChassisState Mission MissionStatus Observation \
  RobotState SemanticLandmark SensorFusion ServerCommand WorldState; do
  check_ros_interface rosmsg "air_ground_interfaces/${message_name}"
done

for service_name in QueryWorldState SwapChassis; do
  check_ros_interface rossrv "air_ground_interfaces/${service_name}"
done

for action_message in NavigateAction NavigateGoal NavigateResult NavigateFeedback; do
  check_ros_interface rosmsg "air_ground_interfaces/${action_message}"
done

if [ "${FAILURES}" -eq 0 ]; then
  echo "=== Task-01 PASS ==="
  exit 0
fi

echo "=== Task-01 FAIL: ${FAILURES} check(s) failed ==="
exit 1
```

## 交付产物

完成后 `catkin build` 应无错误；`air_ground_interfaces` 应生成 10 个声明消息、2 个服务和 `Navigate` Action 的派生消息；`test_task01.sh` 必须全部 PASS 并返回 0。

## 注意事项

- 如果 `rosdep init` 失败，检查 `/etc/ros/rosdep/sources.list.d/20-default.list` 是否已存在，若存在则跳过 init
- 华为轻薄本磁盘可能有限，如空间不足，可先不装 `ros-noetic-desktop-full`，改为 `ros-noetic-ros-base` + 手动安装 Gazebo 依赖
- `catkin build` 首次编译需 3-5 分钟，务必等待完成
