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

# 安装 ROS Noetic Desktop-Full（含 Gazebo 9）
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
mkdir -p ~/air_ground_sim_ws/src
cd ~/air_ground_sim_ws/src
catkin_init_workspace

# 创建包目录（后续 task 填充内容）
mkdir -p air_ground_interfaces/msg air_ground_interfaces/srv
mkdir -p drone_bringup/launch drone_bringup/config drone_bringup/scripts
mkdir -p car_bringup/launch car_bringup/config car_bringup/scripts car_bringup/urdf
mkdir -p com_bridge/launch com_bridge/config com_bridge/scripts
mkdir -p lab_server/launch lab_server/config lab_server/scripts

# 创建每个包的 package.xml 和 CMakeLists.txt（Python 包）
for pkg in air_ground_interfaces drone_bringup car_bringup com_bridge lab_server; do
  mkdir -p ~/air_ground_sim_ws/src/$pkg
done
```

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
  <build_depend>std_msgs</build_depend>
  <build_depend>geometry_msgs</build_depend>
  <build_depend>sensor_msgs</build_depend>
  <build_depend>nav_msgs</build_depend>
  <exec_depend>message_runtime</exec_depend>
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
  std_msgs
  geometry_msgs
  sensor_msgs
  nav_msgs
)

add_message_files(
  FILES
  SensorFusion.msg
  ServerCommand.msg
  ChassisState.msg
)

add_service_files(
  FILES
  SwapChassis.srv
)

generate_messages(
  DEPENDENCIES
  std_msgs
  geometry_msgs
  sensor_msgs
  nav_msgs
)

catkin_package(
  CATKIN_DEPENDS
  message_runtime
  std_msgs
  geometry_msgs
  sensor_msgs
  nav_msgs
)
```

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
float32 motor_currents[4]
float32 encoder_ticks[4]
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

**共同模板 — 以 `drone_bringup/package.xml` 为例（其余类似）**：

```xml
<?xml version="1.0"?>
<package format="2">
  <name>drone_bringup</name>
  <version>0.1.0</version>
  <description>Drone simulation bringup: PX4 SITL + sensors + edge preprocessor</description>
  <maintainer email="user@example.com">user</maintainer>
  <license>MIT</license>
  <buildtool_depend>catkin</buildtool_depend>
  <exec_depend>roscpp</exec_depend>
  <exec_depend>rospy</exec_depend>
  <exec_depend>std_msgs</exec_depend>
  <exec_depend>geometry_msgs</exec_depend>
  <exec_depend>sensor_msgs</exec_depend>
  <exec_depend>mavros</exec_depend>
  <exec_depend>air_ground_interfaces</exec_depend>
</package>
```

**共同模板 — 以 `drone_bringup/CMakeLists.txt` 为例**：

```cmake
cmake_minimum_required(VERSION 3.0.2)
project(drone_bringup)
find_package(catkin REQUIRED COMPONENTS
  roscpp rospy std_msgs geometry_msgs sensor_msgs air_ground_interfaces
)
catkin_package()
include_directories(${catkin_INCLUDE_DIRS})
install(DIRECTORY launch config scripts
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION})
```

你需要为 `car_bringup`、`com_bridge`、`lab_server` 创建类似文件，依赖项按需调整：
- `car_bringup`：额外依赖 `gazebo_ros`、`ros_control`、`nav_msgs`
- `com_bridge`：额外依赖 `mavros`
- `lab_server`：额外依赖 `nav_msgs`

### 1.6 初始化编译

```bash
cd ~/air_ground_sim_ws
catkin build
source devel/setup.bash
echo "source ~/air_ground_sim_ws/devel/setup.bash" >> ~/.bashrc

# 验证自定义消息编译成功
rosmsg show air_ground_interfaces/SensorFusion
rossrv show air_ground_interfaces/SwapChassis
```

### 1.7 验证脚本

创建 `~/air_ground_sim_ws/src/test_task01.sh`：

```bash
#!/bin/bash
echo "=== Task-01 Verification ==="

# 1. ROS 安装检查
if [ -d "/opt/ros/noetic" ]; then echo "[PASS] ROS Noetic installed"; else echo "[FAIL] ROS not found"; exit 1; fi

# 2. Gazebo 安装检查
if command -v gzclient &> /dev/null; then echo "[PASS] Gazebo installed"; else echo "[FAIL] Gazebo not found"; fi

# 3. 工作空间编译检查
if [ -d "$HOME/air_ground_sim_ws/devel" ]; then echo "[PASS] Workspace built"; else echo "[FAIL] Workspace not built"; fi

# 4. 自定义消息检查
if rosmsg show air_ground_interfaces/SensorFusion &>/dev/null; then echo "[PASS] Custom messages OK"; else echo "[FAIL] Custom messages broken"; fi

echo "=== Done ==="
```

## 交付产物

完成后 `catkin build` 应无错误，`rosmsg show air_ground_interfaces/*` 能列出三条消息和一条服务，`test_task01.sh` 全部 PASS。

## 注意事项

- 如果 `rosdep init` 失败，检查 `/etc/ros/rosdep/sources.list.d/20-default.list` 是否已存在，若存在则跳过 init
- 华为轻薄本磁盘可能有限，如空间不足，可先不装 `ros-noetic-desktop-full`，改为 `ros-noetic-ros-base` + 手动安装 Gazebo 依赖
- `catkin build` 首次编译需 3-5 分钟，务必等待完成
