# Task-02: PX4 SITL 无人机仿�?+ 传感器插�?

## 前置条件

- Task-01 已完成，ROS 工作空间可正常编�?
- 磁盘空闲 �?5GB（PX4 源码 + 工具链）

## 目标

�?Gazebo 11 中启动一架搭载深度相机、GPS �?IMU 的无人机，通过 MAVROS �?ROS 通信，可�?`rostopic` 查看传感器数据�?

---

## 2.1 安装 PX4 工具�?

```bash
# 克隆 PX4-Autopilot（使�?v1.14 稳定分支，与 Pixhawk 6C 固件匹配�?
cd ~
git clone https://github.com/PX4/PX4-Autopilot.git --branch v1.14.0 --depth 1
cd PX4-Autopilot

# 运行 Ubuntu 安装脚本（安装交叉编译工具链等）
bash ./Tools/setup/ubuntu.sh

# 重启后继续（某些 udev 规则需重启生效�?
# 如果你选择跳过重启，执行：
sudo usermod -a -G dialout $USER
```

## 2.2 首次编译 SITL

```bash
cd ~/PX4-Autopilot
make px4_sitl_default gazebo-classic
# 首次编译 10-20 分钟，等待完�?
# 编译成功后会看到 Gazebo 窗口启动（手动关闭）
```

## 2.3 创建无人�?Gazebo 模型（搭载深度相机）

PX4 原生 Iris 模型已有 IMU/GPS/气压计（通过 Gazebo-PX4 桥接发布�?MAVROS）�?
我们只需扩展一个深度相�?sensor，直接嵌入到机体�?`base_link` 中，无需独立静态模型�?

**文件：`~/PX4-Autopilot/Tools/simulation/gazebo-classic/sitl_gazebo-classic/models/iris_depth_camera/iris_depth_camera.sdf.jinja`**

```xml
<?xml version="1.0" ?>
<sdf version="1.5">
  <model name="iris_depth_camera">

    <!-- 复用 PX4 Iris 核心（含 IMU/GPS/气压�?电机�?-->
    {% include 'iris/iris_base.sdf.jinja' %}

    <!--
      深度相机插件 (Intel RealSense D435i 模拟)
      直接附加在机�?base_link 上，前向安装，向下倾斜 45°�?
      �?sensor 标签必须位于 iris_base 中已�?link �?scope 内，
      此处通过 joint 方式嵌入：为相机创建轻量 link，再�?fixed joint 刚性绑定到 base_link�?
    -->
    <link name="depth_camera_link">
      <pose>0.15 0 0.03 0 0.785 0</pose>  <!-- 前向 15cm，低�?45° -->
      <inertial>
        <mass>0.001</mass>  <!-- 极轻，不影响飞行力学 -->
        <inertia>
          <ixx>1e-7</ixx><ixy>0</ixy><ixz>0</ixz>
          <iyy>1e-7</iyy><iyz>0</iyz>
          <izz>1e-7</izz>
        </inertia>
      </inertial>
      <sensor name="depth_sensor" type="depth">
        <update_rate>15</update_rate>
        <camera>
          <horizontal_fov>1.51844</horizontal_fov>  <!-- 87° -->
          <image>
            <width>320</width>
            <height>240</height>
            <format>R8G8B8</format>
          </image>
          <clip>
            <near>0.1</near>
            <far>10.0</far>
          </clip>
        </camera>
        <plugin name="depth_camera_controller" filename="libgazebo_ros_depth_camera.so">
          <alwaysOn>true</alwaysOn>
          <updateRate>15.0</updateRate>
          <cameraName>drone/depth_camera</cameraName>
          <imageTopicName>rgb/image_raw</imageTopicName>
          <depthImageTopicName>depth/image_raw</depthImageTopicName>
          <pointCloudTopicName>depth/points</pointCloudTopicName>
          <pointCloudCutoff>0.4</pointCloudCutoff>
          <frameName>drone_depth_link</frameName>
        </plugin>
      </sensor>
    </link>

    <joint name="depth_camera_joint" type="fixed">
      <parent>base_link</parent>
      <child>depth_camera_link</child>
    </joint>

    <!--
      GPS/IMU: PX4 SITL 原生提供，无需额外 Gazebo 插件�?
      数据通过 MAVROS 发布:
      - GPS:   /mavros/global_position/global
      - IMU:   /mavros/imu/data
      参见 §2.10 �?gps_converter.py 做本�?ENU 转换�?
    -->

  </model>
</sdf>
```

删除独立模型创建步骤（不再需要）�?
```bash
# 不再需�?~/.gazebo/models/depth_camera/ �?~/.gazebo/models/gps/
# 传感器已嵌入 iris_depth_camera.sdf.jinja
```

## 2.4 创建 PX4 启动脚本（airframe 文件�?

**文件：`~/PX4-Autopilot/ROMFS/px4fmu_common/init.d-posix/airframes/4016_iris_depth_camera`**

```bash
#!/bin/sh
# @name iris_depth_camera
# @type Simulation
# @class Quadrotor (450mm)
# @maintainer user

. ${R}etc/init.d/rcS
. ${R}etc/init.d-posix/rc.sim

param set-default SYS_AUTOSTART 4016
# Note: CAL_*_ID params are ignored in SITL (sensors come from Gazebo plugins).
#       Real calibration IDs only needed for hardware Pixhawk.
```
param set-default CAL_MAG0_ID 1310984
param set-default IMU_GYRO_RATEMAX 800

# Disable default sensors; we use Gazebo plugins
param set SENS_IMU_MODE 0
param set SENS_BARO_MODE 0
param set SENS_MAG_MODE 0
```

## 2.5 创建 air_ground_drone_bringup Launch 文件

**文件：`~/air_ground_sim_ws/src/air_ground_drone_bringup/launch/drone_sitl.launch`**

```xml
<launch>
  <!-- PX4 SITL with Iris Depth Camera -->
  <arg name="vehicle" default="iris_depth_camera"/>
  <arg name="world" default="$(find air_ground_car_bringup)/worlds/empty.world"/>
  <arg name="gui" default="false"/>  <!-- headless for lightweight laptop -->
  <arg name="headless" default="true"/>

  <include file="$(find px4)/launch/mavros_posix_sitl.launch">
    <arg name="vehicle" value="$(arg vehicle)"/>
    <arg name="world" value="$(arg world)"/>
    <arg name="gui" value="$(arg gui)"/>
    <arg name="headless" value="$(arg headless)"/>
  </include>

  <!-- MAVROS 连接 -->
  <include file="$(find mavros)/launch/px4.launch">
    <arg name="fcu_url" value="udp://:14540@localhost:14557"/>
    <arg name="gcs_url" value=""/>
  </include>
</launch>
```

## 2.6 创建空世界文�?

**统一使用** `air_ground_car_bringup` �?`empty.world`（Task-03 创建），drone 不单独维护一份：
```bash
# 不需要独立创�?air_ground_drone_bringup/worlds/，引�?air_ground_car_bringup 的即�?
# launch 文件中使�?$(find air_ground_car_bringup)/worlds/empty.world
```

## 2.7 创建深度相机 Launch（独立传感器启动�?

**文件：`~/air_ground_sim_ws/src/air_ground_drone_bringup/launch/drone_sensors.launch`**

```xml
<launch>
  <!-- 深度相机点云降采样（轻量化） -->
  <node pkg="nodelet" type="nodelet" name="depth_downsample"
        args="load pcl/VoxelGrid pcl_manager" output="screen">
    <param name="filter_field_name" value="z"/>
    <param name="filter_limit_min" value="0.1"/>
    <param name="filter_limit_max" value="8.0"/>
    <param name="leaf_size" value="0.05"/>  <!-- 5cm voxel -->
    <remap from="~input" to="/drone/depth_camera/depth/points"/>
    <remap from="~output" to="/drone/depth/points_downsampled"/>
  </node>

  <!-- GPS 坐标�?ROS Pose -->
  <node pkg="air_ground_drone_bringup" type="gps_converter.py" name="gps_converter" output="screen"/>
</launch>
```

## 2.8 创建 GPS 转换脚本

**文件：`~/air_ground_sim_ws/src/air_ground_drone_bringup/scripts/gps_converter.py`**

```python
#!/usr/bin/env python3
"""Convert NavSatFix to local ENU Pose for server consumption."""
import rospy
from sensor_msgs.msg import NavSatFix
from geometry_msgs.msg import PoseStamped

# Home position (set to Gazebo origin)
HOME_LAT = 47.397742
HOME_LON = 8.545594
HOME_ALT = 488.0


def gps_callback(msg: NavSatFix, pub: rospy.Publisher):
    """Simple flat-earth conversion (valid for <10km range)."""
    # Earth radius at latitude
    import math
    lat_rad = math.radians(HOME_LAT)
    meters_per_deg_lat = 111132.92 - 559.82 * math.cos(2 * lat_rad) + 1.175 * math.cos(4 * lat_rad)
    meters_per_deg_lon = 111412.84 * math.cos(lat_rad) - 93.5 * math.cos(3 * lat_rad)

    pose = PoseStamped()
    pose.header = msg.header
    pose.header.frame_id = "map"
    pose.pose.position.x = (msg.longitude - HOME_LON) * meters_per_deg_lon
    pose.pose.position.y = (msg.latitude - HOME_LAT) * meters_per_deg_lat
    pose.pose.position.z = msg.altitude - HOME_ALT
    pose.pose.orientation.w = 1.0
    pub.publish(pose)


def main():
    rospy.init_node("gps_converter")
    pub = rospy.Publisher("/drone/gps/local_pose", PoseStamped, queue_size=10)
    rospy.Subscriber("/mavros/global_position/global", NavSatFix,
                     lambda msg: gps_callback(msg, pub), queue_size=10)
    rospy.loginfo("[GPS Converter] Running...")
    rospy.spin()


if __name__ == "__main__":
    main()
```

```bash
chmod +x ~/air_ground_sim_ws/src/air_ground_drone_bringup/scripts/gps_converter.py
```

## 2.9 创建传感器配�?YAML

**文件：`~/air_ground_sim_ws/src/air_ground_drone_bringup/config/drone_sensors.yaml`**

```yaml
# Drone sensor configuration
depth_camera:
  resolution: [320, 240]
  fps: 15
  min_range: 0.1
  max_range: 8.0
  fov_h: 87.0
  fov_v: 58.0

gps:
  update_rate: 10  # Hz
  home_lat: 47.397742
  home_lon: 8.545594
  home_alt: 488.0

imu:
  topic: "/mavros/imu/data"
  update_rate: 100  # Hz (from Pixhawk estimator)

mavlink:
  udp_port: 14540
  gcs_port: 14557
```

## 2.10 验证脚本

**文件：`~/air_ground_sim_ws/src/air_ground_drone_bringup/scripts/test_drone.sh`**

```bash
#!/bin/bash
echo "=== Task-02 Drone SITL Verification ==="

# 1. Start drone in background (headless)
roslaunch air_ground_drone_bringup drone_sitl.launch gui:=false headless:=true &
DRONE_PID=$!
sleep 15  # Wait for PX4 to boot

# 2. Check MAVROS topics
echo "--- Checking /mavros/state ---"
rostopic echo /mavros/state -n 1 2>/dev/null | grep -q "connected" && echo "[PASS]" || echo "[WARN]"

# 3. Check depth camera
echo "--- Checking /drone/depth_camera/depth/image_raw ---"
rostopic echo /drone/depth_camera/depth/image_raw -n 1 2>/dev/null | grep -q "height" && echo "[PASS]" || echo "[WARN]"

# 4. Check GPS
echo "--- Checking /mavros/global_position/global ---"
rostopic echo /mavros/global_position/global -n 1 2>/dev/null | grep -q "latitude" && echo "[PASS]" || echo "[WARN]"

# 5. Check IMU
echo "--- Checking /mavros/imu/data ---"
rostopic echo /mavros/imu/data -n 1 2>/dev/null | grep -q "angular_velocity" && echo "[PASS]" || echo "[WARN]"

# Cleanup
kill $DRONE_PID 2>/dev/null
wait $DRONE_PID 2>/dev/null
echo "=== Done ==="
```

## 2.11 更新 `air_ground_drone_bringup/CMakeLists.txt`

（无需额外修改——不再安装独立的 worlds/ 目录�?

## 交付产物

1. `roslaunch air_ground_drone_bringup drone_sitl.launch` 能无 GUI 启动无人机仿�?
2. `/mavros/state` topic 显示 `connected: True`
3. 深度相机、GPS、IMU 话题均有数据发布
4. `test_drone.sh` 全部 PASS

## 轻量化提�?

- 华为轻薄本无独显：Gazebo 务必�?`headless:=true` 启动，物理计算照常但无渲�?
- 深度相机分辨率设�?320×240/15Hz，否�?Gazebo 渲染管线（即�?headless）也会在传感器线程吃�?CPU
- PX4 SITL 默认跑在 `nice -20`，如需进一步降�?CPU，编�?`mavros_posix_sitl.launch` �?PX4 进程�?`nice` �?

## 进阶（v2 计划）：�?Gazebo 共享世界

当前 task-02 �?task-03 各自启动独立 Gazebo 实例，这在单机开发时会导致双 gzserver 资源浪费�?
**计划�?task-08 集成阶段**改为统一方案�?

1. 先启动一�?Gazebo（通过 `air_ground_car_bringup` 或独�?launch�?
2. 在这个世界内 spawn 无人机模型（通过 `gazebo_ros/spawn_model`，不依赖 PX4 自带 launch �?gzserver�?
3. 共用同一坐标系，为后续空地协同仿真打基础

此改动不影响 task-02/03 的独立开发和测试。待 task-02~07 全部跑通后，在 task-08 总装时实施�?
