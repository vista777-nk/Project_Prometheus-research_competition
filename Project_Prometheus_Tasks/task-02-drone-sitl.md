# Task-02: PX4 SITL 无人机仿真 + 传感器插件

## 前置条件

- Task-01 已完成，ROS 工作空间可正常编译
- 磁盘空闲 ≥ 5GB（PX4 源码 + 工具链）

## 目标

在 Gazebo 11 中启动一架搭载深度相机、GPS 和 IMU 的无人机，通过 MAVROS 与 ROS 通信，可用 `rostopic` 查看传感器数据。

---

## 2.1 获取 PX4 源码

> **阶段 A 已完成**：PX4 v1.14.0 (commit `b8c541d`) 已克隆至 `~/PX4-Autopilot`，
> 30 个子模块全部锁定且完整，工作树干净。占用约 1.0 GB。

```bash
# 如尚未克隆，执行：
cd ~
git clone https://github.com/PX4/PX4-Autopilot.git --branch v1.14.0 --depth 1
cd PX4-Autopilot
git submodule update --init --recursive
```

### 阶段 B：安装 SITL 最小依赖

不安装完整 `ubuntu.sh`（含 NuttX/ARM 工具链、jMAVSim、静态检查工具等）。
仅安装 SITL + Gazebo Classic 必需项：

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  ninja-build \
  libgstreamer-plugins-base1.0-dev \
  python3-venv
```

创建隔离的 PX4 Python 虚拟环境：

```bash
/usr/bin/python3 -m venv --system-site-packages \
  ~/air_ground_sim_ws/.venv-px4-v1.14

~/air_ground_sim_ws/.venv-px4-v1.14/bin/pip install empy==3.3.4
~/air_ground_sim_ws/.venv-px4-v1.14/bin/pip install \
  -r ~/PX4-Autopilot/Tools/setup/requirements.txt
```

安装 MAVROS GeographicLib 数据集：

```bash
sudo /opt/ros/noetic/lib/mavros/install_geographiclib_datasets.sh
```

> **不安装**：NuttX/ARM 工具链、`genromfs`、`kconfig-frontends`、jMAVSim 的 Java/Ant、
> `cppcheck`/`lcov`/`shellcheck` 等检查工具、非本任务需要的 GStreamer 编解码插件。

## 2.2 首次编译 SITL（仅构建，不启动）

```bash
cd ~/PX4-Autopilot
DONT_RUN=1 make px4_sitl_default gazebo-classic
# 首次编译 10-20 分钟
# DONT_RUN=1 仅编译，不自动启动 Gazebo
```

编译完成后设置 PX4/Gazebo 环境（每次新终端都需要）：

```bash
# 添加到 ~/.bashrc 或独立脚本
source ~/PX4-Autopilot/Tools/simulation/gazebo-classic/setup_gazebo.bash \
  ~/PX4-Autopilot ~/PX4-Autopilot/build/px4_sitl_default
export ROS_PACKAGE_PATH=$ROS_PACKAGE_PATH:~/PX4-Autopilot
export ROS_PACKAGE_PATH=$ROS_PACKAGE_PATH:~/PX4-Autopilot/Tools/simulation/gazebo-classic/sitl_gazebo-classic
```

> 以上环境变量是 PX4 官方 v1.14 ROS/Gazebo 接口的必要条件。

## 2.3 无人机模型（使用官方 iris_depth_camera）

PX4 v1.14 **已内置** `iris_depth_camera` 模型（由 `iris` + `depth_camera` 组合而成），
位于 `Tools/simulation/gazebo-classic/sitl_gazebo-classic/models/iris_depth_camera/`。

**无需创建任何新模型文件。**

官方深度相机参数（来源：`models/depth_camera/depth_camera.sdf.jinja`）：

| 参数 | 值 |
|------|-----|
| 分辨率 | 848 × 480 |
| 帧率 | 10 Hz |
| RGB Topic | `/camera/rgb/image_raw` |
| Depth Topic | `/camera/depth/image_raw` |
| PointCloud Topic | `/camera/depth/points` |
| frame_id | `camera_link` |

> **注意**：官方相机 topic 在 `/camera/` 命名空间下。
> 后续在 §2.7 通过 remap 适配到项目约定的 `/drone/` 命名空间。
>
> PX4 v1.14 内置的完整模型列表（共 89 个）位于
> `Tools/simulation/gazebo-classic/sitl_gazebo-classic/models/`，
> 包括 `iris_stereo_camera`、`iris_rplidar`、`iris_fpv_cam` 等，可根据研究需要直接选用。

## 2.4 Airframe 策略（使用默认 Iris + SDF 覆盖）

> **关键发现**：v1.14 虽有 `iris_depth_camera` 模型，但**没有**对应的
> `gazebo-classic_iris_depth_camera` airframe 文件。
> 直接设置 `vehicle:=iris_depth_camera` 会触发 `Unknown model` 错误。

**正确方法**：使用默认 Iris airframe，通过 `sdf` 参数覆盖模型文件：

```bash
# 等价于在 launch 中设置：
# vehicle := iris
# sdf := iris_depth_camera
```

PX4 的 `mavros_posix_sitl.launch` 支持通过 `sdf` 参数指定 `models/` 下的模型目录名，
框架会自动加载对应的 `.sdf.jinja` 文件。

**无需创建任何自定义 airframe 文件。**

## 2.5 创建 air_ground_drone_bringup Launch 文件

**文件：`~/air_ground_sim_ws/src/air_ground_drone_bringup/launch/drone_sitl.launch`**

```xml
<launch>
  <!-- PX4 SITL: Iris airframe + iris_depth_camera SDF 覆盖 -->
  <arg name="vehicle" default="iris"/>
  <arg name="sdf"     default="iris_depth_camera"/>
  <arg name="world"   default="$(find air_ground_car_bringup)/worlds/empty.world"/>
  <arg name="gui"     default="false"/>  <!-- 无头模式，不启动 gzclient -->
  <arg name="x" default="0.0"/>
  <arg name="y" default="0.0"/>
  <arg name="z" default="5.0"/>

  <!-- PX4 SITL + Gazebo + MAVROS（mavros_posix_sitl.launch 已内置 MAVROS 启动） -->
  <include file="$(find px4)/launch/mavros_posix_sitl.launch">
    <arg name="vehicle" value="$(arg vehicle)"/>
    <arg name="sdf"     value="$(arg sdf)"/>
    <arg name="world"   value="$(arg world)"/>
    <arg name="gui"     value="$(arg gui)"/>
    <arg name="x"       value="$(arg x)"/>
    <arg name="y"       value="$(arg y)"/>
    <arg name="z"       value="$(arg z)"/>
  </include>
</launch>
```

> **关键修正**：
> - `vehicle` 设为 `iris`（默认 airframe），用 `sdf` 参数覆盖为 `iris_depth_camera` 模型
> - `mavros_posix_sitl.launch` 本身已启动 MAVROS，**无需重复 include `px4.launch`**
> - 该 Launch 无 `headless` 参数，`gui=false` 即不启动 `gzclient`（渲染界面），物理仿真照常运行
> - 支持 `x y z R P Y` 初始位姿参数，默认从 5m 高度起飞

## 2.6 创建空世界文件

**统一使用** `air_ground_car_bringup` 的 `empty.world`（Task-03 创建），drone 不单独维护一份：
```bash
# 不需要独立创建 air_ground_drone_bringup/worlds/，引用 air_ground_car_bringup 的即可
# launch 文件中使用 $(find air_ground_car_bringup)/worlds/empty.world
```

## 2.7 创建传感器适配 Launch

> 官方深度相机发布在 `/camera/` 命名空间下，需 remap 到项目约定的 `/drone/` 命名空间。

**文件：`~/air_ground_sim_ws/src/air_ground_drone_bringup/launch/drone_sensors.launch`**

```xml
<launch>
  <!-- 深度相机 RGB → 项目命名空间 -->
  <node pkg="topic_tools" type="relay" name="relay_drone_rgb"
        args="/camera/rgb/image_raw /drone/camera/rgb/image_raw" output="screen"/>

  <!-- 深度相机 Depth → 项目命名空间 -->
  <node pkg="topic_tools" type="relay" name="relay_drone_depth"
        args="/camera/depth/image_raw /drone/camera/depth/image_raw" output="screen"/>

  <!-- 深度相机点云降采样（轻量化） + 项目命名空间 -->
  <node pkg="nodelet" type="nodelet" name="depth_downsample"
        args="load pcl/VoxelGrid pcl_manager" output="screen">
    <param name="filter_field_name" value="z"/>
    <param name="filter_limit_min" value="0.1"/>
    <param name="filter_limit_max" value="8.0"/>
    <param name="leaf_size" value="0.05"/>  <!-- 5cm voxel -->
    <remap from="~input" to="/camera/depth/points"/>
    <remap from="~output" to="/drone/camera/depth/points_downsampled"/>
  </node>

  <!-- GPS 坐标转 ROS Pose -->
  <node pkg="air_ground_drone_bringup" type="gps_converter.py" name="gps_converter" output="screen">
    <rosparam>
      home_lat: 47.397742
      home_lon: 8.545594
      home_alt: 488.0
    </rosparam>
  </node>
</launch>
```

## 2.8 创建 GPS 转换脚本（参数化）

**文件：`~/air_ground_sim_ws/src/air_ground_drone_bringup/scripts/gps_converter.py`**

```python
#!/usr/bin/env python3
"""Convert NavSatFix to local ENU Pose for server consumption.

Home position loaded from rosparam, defaulting to Gazebo origin.
"""
import math
import rospy
from sensor_msgs.msg import NavSatFix
from geometry_msgs.msg import PoseStamped


def compute_meters_per_deg(lat_rad: float):
    """Compute meters-per-degree at given latitude (WGS84)."""
    m_per_deg_lat = (
        111132.92 - 559.82 * math.cos(2 * lat_rad)
        + 1.175 * math.cos(4 * lat_rad) - 0.0023 * math.cos(6 * lat_rad)
    )
    m_per_deg_lon = (
        111412.84 * math.cos(lat_rad)
        - 93.5 * math.cos(3 * lat_rad) + 0.118 * math.cos(5 * lat_rad)
    )
    return m_per_deg_lat, m_per_deg_lon


class GpsConverter:
    def __init__(self):
        self.home_lat = rospy.get_param("~home_lat", 47.397742)
        self.home_lon = rospy.get_param("~home_lon", 8.545594)
        self.home_alt = rospy.get_param("~home_alt", 488.0)

        lat_rad = math.radians(self.home_lat)
        self.m_per_deg_lat, self.m_per_deg_lon = compute_meters_per_deg(lat_rad)

        self.pub = rospy.Publisher("/drone/gps/local_pose", PoseStamped, queue_size=10)
        self.sub = rospy.Subscriber(
            "/mavros/global_position/global", NavSatFix,
            self.callback, queue_size=10
        )
        rospy.loginfo(f"[GPS Converter] Home: ({self.home_lat}, {self.home_lon}, {self.home_alt})")

    def callback(self, msg: NavSatFix):
        pose = PoseStamped()
        pose.header = msg.header
        pose.header.frame_id = "map"
        pose.pose.position.x = (msg.longitude - self.home_lon) * self.m_per_deg_lon
        pose.pose.position.y = (msg.latitude - self.home_lat) * self.m_per_deg_lat
        pose.pose.position.z = msg.altitude - self.home_alt
        pose.pose.orientation.w = 1.0
        self.pub.publish(pose)


if __name__ == "__main__":
    rospy.init_node("gps_converter")
    GpsConverter()
    rospy.spin()
```

```bash
chmod +x ~/air_ground_sim_ws/src/air_ground_drone_bringup/scripts/gps_converter.py
```

> 家位置 (home_lat/lon/alt) 从 launch 文件的 `<rosparam>` 加载，无需修改源码。

## 2.9 创建传感器配置 YAML

**文件：`~/air_ground_sim_ws/src/air_ground_drone_bringup/config/drone_sensors.yaml`**

```yaml
# Drone sensor configuration
# 深度相机参数与 PX4 v1.14 官方 iris_depth_camera 模型一致
depth_camera:
  resolution: [848, 480]
  fps: 10
  min_range: 0.1
  max_range: 10.0
  fov_h: 87.0
  topic_rgb: "/camera/rgb/image_raw"          # 官方 topic
  topic_depth: "/camera/depth/image_raw"
  topic_points: "/camera/depth/points"
  frame_id: "camera_link"

# 项目命名空间（remap 后）
depth_camera_projected:
  topic_rgb: "/drone/camera/rgb/image_raw"
  topic_depth: "/drone/camera/depth/image_raw"
  topic_points: "/drone/camera/depth/points_downsampled"
  frame_id: "drone_camera_link"

gps:
  update_rate: 10  # Hz
  home_lat: 47.397742
  home_lon: 8.545594
  home_alt: 488.0
  topic_raw: "/mavros/global_position/global"
  topic_local: "/drone/gps/local_pose"

imu:
  topic: "/mavros/imu/data"
  update_rate: 100  # Hz (from Pixhawk estimator)

mavlink:
  udp_port: 14540
  gcs_port: 14557
```

## 2.10 验证脚本（超时 + 精确检查 + 可靠清理）

**文件：`~/air_ground_sim_ws/src/air_ground_drone_bringup/scripts/test_drone.sh`**

```bash
#!/bin/bash
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'
TIMEOUT=60  # 最长等待秒数
PASS=0
FAIL=0

pass() { echo -e "${GREEN}[PASS]${NC} $1"; PASS=$((PASS + 1)); }
fail() { echo -e "${RED}[FAIL]${NC} $1"; FAIL=$((FAIL + 1)); }

# 超时等待 topic 出现
grep_topic() {
    local topic="$1" pattern="$2" label="$3"
    local start=$(date +%s)
    while true; do
        if timeout 3 rostopic echo "$topic" -n 1 2>/dev/null | grep -q "$pattern"; then
            pass "$label"
            return 0
        fi
        if [ $(($(date +%s) - start)) -ge $TIMEOUT ]; then
            fail "$label (timeout after ${TIMEOUT}s)"
            return 1
        fi
        sleep 2
    done
}

cleanup() {
    echo "[CLEANUP] Stopping drone simulation..."
    rosnode kill -a 2>/dev/null || true
    kill %1 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT

echo "=== Task-02 Drone SITL Verification ==="

# 1. Start drone in background (headless)
echo "--- Starting PX4 SITL (gui=false) ---"
roslaunch air_ground_drone_bringup drone_sitl.launch gui:=false &

# 2. Check MAVROS state (wait for connection)
grep_topic "/mavros/state" "connected.*True" "MAVROS connected"

# 3. Check depth camera (official topic)
grep_topic "/camera/depth/image_raw" "height" "Depth camera publishing"

# 4. Check GPS
grep_topic "/mavros/global_position/global" "latitude" "GPS data publishing"

# 5. Check IMU
grep_topic "/mavros/imu/data" "angular_velocity" "IMU data publishing"

# 6. Check project-namespace topics (if drone_sensors.launch is also running)
grep_topic "/drone/gps/local_pose" "position" "GPS → local ENU conversion"

echo "=== Results: ${PASS} passed, ${FAIL} failed ==="
[ $FAIL -eq 0 ] && echo "[OK]" || echo "[NG]"
exit $FAIL
```

```bash
chmod +x ~/air_ground_sim_ws/src/air_ground_drone_bringup/scripts/test_drone.sh
```

> 改进点：
> - 60 秒超时，避免无限等待
> - 精确布尔检查（`connected.*True` 而非仅 `connected`）
> - `trap EXIT` 确保无论成功或失败都清理 ROS 进程
> - 彩色输出，一目了然

## 2.11 更新 `air_ground_drone_bringup/CMakeLists.txt`

（无需额外修改——模型使用 PX4 官方内置，不安装独立的 worlds/ 或 models/ 目录）

## 交付产物

1. `roslaunch air_ground_drone_bringup drone_sitl.launch gui:=false` 能无渲染启动无人机仿真
2. `/mavros/state` topic 显示 `connected: True`
3. 官方相机 topic（`/camera/rgb/image_raw`、`/camera/depth/image_raw`）有数据发布
4. GPS（`/mavros/global_position/global`）、IMU（`/mavros/imu/data`）有数据发布
5. 项目命名空间 topic（`/drone/gps/local_pose` 等）经 remap 后可正常订阅
6. `test_drone.sh` 全部 PASS

## 轻量化提示

- 华为轻薄本无独显：Gazebo 务必用 `gui:=false` 启动，物理计算照常但无渲染
- 官方深度相机分辨率为 848×480 / 10Hz，对于仅做结构验证的仿真可接受；如需进一步降低 CPU：
  - 在 `drone_sensors.launch` 中启用 VoxelGrid 降采样（5cm 体素）
  - 或创建自定义 `iris_depth_camera_lowres` 模型覆盖分辨率
- PX4 SITL 默认跑在 `nice -20`，如需进一步降低 CPU，编辑 `mavros_posix_sitl.launch` 中 PX4 进程的 `nice` 值

## 进阶（v2 计划）：单 Gazebo 共享世界

当前 task-02 和 task-03 各自启动独立 Gazebo 实例，这在单机开发时会导致双 gzserver 资源浪费。
**计划在 task-08 集成阶段**改为统一方案：

1. 先启动一个 Gazebo（通过 `air_ground_car_bringup` 或独立 launch）
2. 在这个世界内 spawn 无人机模型（通过 `gazebo_ros/spawn_model`，不依赖 PX4 自带 launch 的 gzserver）
3. 共用同一坐标系，为后续空地协同仿真打基础

此改动不影响 task-02/03 的独立开发和测试。待 task-02~07 全部跑通后，在 task-08 总装时实施。
