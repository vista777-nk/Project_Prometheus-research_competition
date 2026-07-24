# Task-08: 集成总装 Launch + Makefile

## 前置条件

- Task-02~07 所有子模块可独立运行

## 目标

提供一键启动整个空地联合仿真系统的 Launch 文件和便捷 Makefile。

---

## 8.1 总入口 Launch 文件

**文件：`~/air_ground_sim_ws/src/air_ground_sim.launch`**

（在工作空间 src 根目录下，作为总入口）

```xml
<?xml version="1.0"?>
<launch>
  <!-- ============================================================ -->
  <!-- Air-Ground Joint Simulation - Master Launch                  -->
  <!-- ============================================================ -->
  <arg name="chassis" default="diff" doc="Chassis type: diff or mecanum"/>
  <arg name="gui" default="false" doc="Show Gazebo GUI (heavy!)"/>
  <arg name="headless" default="true" doc="Run Gazebo headless"/>
  <arg name="drone_x" default="0.0"/>
  <arg name="drone_y" default="0.0"/>
  <arg name="car_x" default="2.0"/>
  <arg name="car_y" default="0.0"/>

  <!-- ====== Phase 1: Drone ====== -->
  <include file="$(find drone_bringup)/launch/drone_sitl.launch">
    <arg name="gui" value="$(arg gui)"/>
    <arg name="headless" value="$(arg headless)"/>
  </include>

  <include file="$(find drone_bringup)/launch/drone_edge.launch"/>

  <!-- ====== Phase 2: Car ====== -->
  <include file="$(find car_bringup)/launch/car_$(arg chassis).launch">
    <arg name="gui" value="$(arg gui)"/>
    <arg name="headless" value="$(arg headless)"/>
    <arg name="x" value="$(arg car_x)"/>
    <arg name="y" value="$(arg car_y)"/>
  </include>

  <include file="$(find car_bringup)/launch/car_edge.launch"/>

  <!-- ====== Phase 3: Communication Bridge ====== -->
  <include file="$(find com_bridge)/launch/com_bridge.launch"/>

  <!-- ====== Phase 4: Lab Server ====== -->
  <include file="$(find lab_server)/launch/server.launch"/>

</launch>
```

## 8.2 按需分场景 Launch

**文件：`~/air_ground_sim_ws/src/drone_only.launch`**

```xml
<?xml version="1.0"?>
<launch>
  <arg name="gui" default="false"/>
  <arg name="headless" default="true"/>
  <include file="$(find drone_bringup)/launch/drone_sitl.launch">
    <arg name="gui" value="$(arg gui)"/>
    <arg name="headless" value="$(arg headless)"/>
  </include>
  <include file="$(find drone_bringup)/launch/drone_edge.launch"/>
</launch>
```

**文件：`~/air_ground_sim_ws/src/car_only.launch`**

```xml
<?xml version="1.0"?>
<launch>
  <arg name="chassis" default="diff"/>
  <arg name="gui" default="false"/>
  <arg name="headless" default="true"/>
  <include file="$(find car_bringup)/launch/car_$(arg chassis).launch">
    <arg name="gui" value="$(arg gui)"/>
    <arg name="headless" value="$(arg headless)"/>
  </include>
  <include file="$(find car_bringup)/launch/car_edge.launch"/>
</launch>
```

**文件：`~/air_ground_sim_ws/src/server_only.launch`**

```xml
<?xml version="1.0"?>
<launch>
  <include file="$(find lab_server)/launch/server.launch"/>
  <include file="$(find com_bridge)/launch/com_bridge.launch"/>
</launch>
```

## 8.3 Makefile

**文件：`~/air_ground_sim_ws/Makefile`**

```makefile
# Air-Ground Simulation Makefile
# Usage: make <target>

WS = $(HOME)/air_ground_sim_ws
SOURCE = source $(WS)/devel/setup.bash

.PHONY: build clean test-drone test-car test-bridge test-all launch-drone launch-car launch-full help

# ── Build ────────────────────────────────────────────
build:
	cd $(WS) && catkin build
	@echo "Build complete. Run: source $(WS)/devel/setup.bash"

clean:
	cd $(WS) && catkin clean -y

rebuild: clean build

# ── Test targets ──────────────────────────────────────
test-drone:
	$(SOURCE) && bash $(WS)/src/drone_bringup/scripts/test_drone.sh

test-car:
	$(SOURCE) && bash $(WS)/src/car_bringup/scripts/test_diff.sh

test-mecanum:
	$(SOURCE) && bash $(WS)/src/car_bringup/scripts/test_mecanum.sh

test-sensors:
	$(SOURCE) && bash $(WS)/src/car_bringup/scripts/test_sensors.sh

test-bridge:
	$(SOURCE) && bash $(WS)/src/com_bridge/scripts/test_bridge.sh

test-server:
	$(SOURCE) && bash $(WS)/src/lab_server/scripts/test_server.sh

test-all: test-drone test-car test-sensors test-bridge test-server
	@echo "All tests complete."

# ── Launch targets ────────────────────────────────────
launch-drone:
	$(SOURCE) && roslaunch drone_only.launch gui:=false headless:=true

launch-car:
	$(SOURCE) && roslaunch car_only.launch chassis:=diff gui:=false headless:=true

launch-car-mecanum:
	$(SOURCE) && roslaunch car_only.launch chassis:=mecanum gui:=false headless:=true

launch-full:
	$(SOURCE) && roslaunch air_ground_sim.launch chassis:=diff gui:=false headless:=true

launch-full-mecanum:
	$(SOURCE) && roslaunch air_ground_sim.launch chassis:=mecanum gui:=false headless:=true

# ── Kill all simulation processes ─────────────────────
kill:
	-pkill -f "px4" 2>/dev/null
	-pkill -f "gzserver" 2>/dev/null
	-pkill -f "gzclient" 2>/dev/null
	-pkill -f "roslaunch" 2>/dev/null
	-pkill -f "rosmaster" 2>/dev/null
	@echo "All simulation processes killed."

# ── Status ────────────────────────────────────────────
status:
	@echo "=== ROS Master ==="
	@pgrep -f "rosmaster" >/dev/null && echo "  Running" || echo "  Not running"
	@echo "=== Gazebo ==="
	@pgrep -f "gzserver" >/dev/null && echo "  gzserver: Running" || echo "  gzserver: Not running"
	@echo "=== PX4 SITL ==="
	@pgrep -f "px4" >/dev/null && echo "  PX4: Running" || echo "  PX4: Not running"
	@echo "=== Active ROS Nodes ==="
	@$(SOURCE) && rosnode list 2>/dev/null || echo "  (rosmaster not running)"

# ── Help ──────────────────────────────────────────────
help:
	@echo "Air-Ground Simulation Makefile"
	@echo ""
	@echo "Build:"
	@echo "  make build          Build all packages"
	@echo "  make clean          Clean build artifacts"
	@echo ""
	@echo "Test:"
	@echo "  make test-drone     Test drone SITL"
	@echo "  make test-car       Test diff chassis"
	@echo "  make test-mecanum   Test mecanum chassis"
	@echo "  make test-sensors   Test car sensors"
	@echo "  make test-bridge    Test communication bridge"
	@echo "  make test-server    Test server nodes"
	@echo "  make test-all       Run all tests"
	@echo ""
	@echo "Launch:"
	@echo "  make launch-drone           Drone only"
	@echo "  make launch-car             Diff car only"
	@echo "  make launch-car-mecanum     Mecanum car only"
	@echo "  make launch-full            Full system (diff chassis)"
	@echo "  make launch-full-mecanum    Full system (mecanum chassis)"
	@echo ""
	@echo "Utilities:"
	@echo "  make kill           Kill all simulation processes"
	@echo "  make status         Show simulation status"
```

## 8.4 `.gitignore`

**文件：`~/air_ground_sim_ws/.gitignore`**

```
build/
devel/
logs/
*.pyc
__pycache__/
*.swp
*.swo
*~
.vscode/
.idea/
```

## 8.5 一键安装脚本（汇总所有依赖）

**文件：`~/air_ground_sim_ws/setup_all.sh`**

```bash
#!/bin/bash
# ============================================================
# Air-Ground Simulation - Full Setup Script
# Run this once on a fresh Ubuntu 20.04 to install everything.
# ============================================================
set -e

echo "=== Step 1: ROS Noetic ==="
if ! dpkg -l | grep -q ros-noetic-desktop-full; then
    sudo sh -c 'echo "deb http://packages.ros.org/ros/ubuntu focal main" > /etc/apt/sources.list.d/ros-latest.list'
    curl -s https://raw.githubusercontent.com/ros/rosdistro/master/ros.asc | sudo apt-key add -
    sudo apt update
    sudo apt install ros-noetic-desktop-full -y
fi

echo "=== Step 2: ROS Dependencies ==="
sudo apt install -y python3-rosdep python3-rosinstall python3-catkin-tools
sudo rosdep init || true
rosdep update

echo "=== Step 3: MAVROS & Gazebo Plugins ==="
sudo apt install -y ros-noetic-mavros ros-noetic-mavros-extras ros-noetic-mavros-msgs
sudo apt install -y ros-noetic-ros-control ros-noetic-ros-controllers ros-noetic-gazebo-ros-control
sudo apt install -y ros-noetic-teleop-twist-keyboard ros-noetic-robot-localization

echo "=== Step 4: Python deps ==="
pip3 install pymavlink pyserial pyyaml numpy

echo "=== Step 5: PX4 SITL ==="
if [ ! -d "$HOME/PX4-Autopilot" ]; then
    cd ~
    git clone https://github.com/PX4/PX4-Autopilot.git --branch v1.14.0 --depth 1
    cd PX4-Autopilot
    bash ./Tools/setup/ubuntu.sh --no-sim-tools
    make px4_sitl_default gazebo-classic
fi

echo "=== Step 6: Build workspace ==="
cd ~/air_ground_sim_ws
source /opt/ros/noetic/setup.bash
catkin build

echo "=== Done! ==="
echo "Run: source ~/air_ground_sim_ws/devel/setup.bash"
echo "Test: make test-all"
```

```bash
chmod +x ~/air_ground_sim_ws/setup_all.sh
```

## 8.6 更新 `package.xml` 补全依赖

确认所有包的 `package.xml` 都已包含所需 `<exec_depend>`：

**`com_bridge/package.xml`** 应包含：
```xml
<exec_depend>mavros</exec_depend>
<exec_depend>air_ground_interfaces</exec_depend>
```

**`lab_server/package.xml`** 应包含：
```xml
<exec_depend>air_ground_interfaces</exec_depend>
<exec_depend>nav_msgs</exec_depend>
<exec_depend>sensor_msgs</exec_depend>
```

## 交付产物

1. `make launch-full` 一键启动整个空地联合仿真系统
2. `make test-all` 运行全部模块测试
3. `make help` 列出所有可用命令
4. `setup_all.sh` 可在全新 Ubuntu 20.04 上一键完成环境搭建
