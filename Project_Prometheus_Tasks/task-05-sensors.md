# Task-05: 车机传感器插件完整配置

## 前置条件

- Task-03、04 完成（车体 URDF 存在）
- Gazebo 9 可用

## 目标

将以下传感器集成到车体 URDF 中：
1. **OpenMV 模拟** — RGB 相机 + 2-DOF 云台（模拟真实 OpenMV H7 Plus + 舵机云台）
2. **2D 激光雷达** — 模拟入门级 RPLIDAR A1（360°，12m 量程）
3. **4× 超声波传感器** — 前后左右各一，模拟 HC-SR04

---

## 5.1 传感器支架 + 所有传感器的 URDF 宏

**文件：`~/air_ground_sim_ws/src/car_bringup/urdf/car_sensors.urdf.xacro`**

```xml
<?xml version="1.0"?>
<robot xmlns:xacro="http://www.ros.org/wiki/xacro">

  <!-- ============================================================ -->
  <!-- 二维云台 + OpenMV 相机                                      -->
  <!-- ============================================================ -->
  <xacro:macro name="openmv_gimbal" params="parent_link">
    <!-- 云台底座（pan 轴） -->
    <link name="gimbal_pan_link">
      <inertial>
        <mass value="0.03"/>
        <inertia ixx="1e-5" ixy="0" ixz="0" iyy="1e-5" iyz="0" izz="1e-5"/>
      </inertial>
    </link>
    <joint name="gimbal_pan_joint" type="revolute">
      <parent link="${parent_link}"/>
      <child link="gimbal_pan_link"/>
      <origin xyz="0.12 0 0.06" rpy="0 0 0"/>  <!-- 车头前方 12cm, 高 6cm -->
      <axis xyz="0 0 1"/>
      <limit lower="-1.57" upper="1.57" effort="0.5" velocity="3.0"/>  <!-- ±90° -->
    </joint>

    <!-- 云台俯仰臂（tilt 轴） -->
    <link name="gimbal_tilt_link">
      <inertial>
        <mass value="0.02"/>
        <inertia ixx="1e-5" ixy="0" ixz="0" iyy="1e-5" iyz="0" izz="1e-5"/>
      </inertial>
    </link>
    <joint name="gimbal_tilt_joint" type="revolute">
      <parent link="gimbal_pan_link"/>
      <child link="gimbal_tilt_link"/>
      <origin xyz="0 0 0.03" rpy="0 0 0"/>
      <axis xyz="0 1 0"/>
      <limit lower="-0.785" upper="0.785" effort="0.3" velocity="2.0"/>  <!-- ±45° -->
    </joint>

    <!-- OpenMV 相机（固定在 tilt 臂末端） -->
    <link name="openmv_camera_link">
      <inertial>
        <mass value="0.02"/>
        <inertia ixx="1e-5" ixy="0" ixz="0" iyy="1e-5" iyz="0" izz="1e-5"/>
      </inertial>
      <visual>
        <geometry>
          <box size="0.035 0.035 0.015"/>
        </geometry>
        <material name="black"/>
      </visual>
    </link>
    <joint name="openmv_camera_joint" type="fixed">
      <parent link="gimbal_tilt_link"/>
      <child link="openmv_camera_link"/>
      <origin xyz="0.02 0 0" rpy="0 0 0"/>
    </joint>
  </xacro:macro>

  <!-- ============================================================ -->
  <!-- Gazebo 相机插件（OpenMV 模拟）                             -->
  <!-- ============================================================ -->
  <xacro:macro name="openmv_gazebo_plugin">
    <gazebo reference="openmv_camera_link">
      <sensor name="openmv_camera" type="camera">
        <update_rate>10</update_rate>             <!-- OpenMV 帧率较低 -->
        <camera>
          <horizontal_fov>1.13446</horizontal_fov> <!-- 65° FOV -->
          <image>
            <width>320</width>                     <!-- QVGA，模拟 OpenMV -->
            <height>240</height>
            <format>R8G8B8</format>
          </image>
          <clip>
            <near>0.05</near>
            <far>30.0</far>
          </clip>
        </camera>
        <plugin name="openmv_camera_plugin" filename="libgazebo_ros_camera.so">
          <alwaysOn>true</alwaysOn>
          <updateRate>10.0</updateRate>
          <cameraName>car/openmv</cameraName>
          <imageTopicName>image_raw</imageTopicName>
          <cameraInfoTopicName>camera_info</cameraInfoTopicName>
          <frameName>openmv_camera_link</frameName>
          <hackBaseline>0.0</hackBaseline>
          <distortionK1>0.0</distortionK1>
          <distortionK2>0.0</distortionK2>
          <distortionK3>0.0</distortionK3>
          <distortionT1>0.0</distortionT1>
          <distortionT2>0.0</distortionT2>
        </plugin>
      </sensor>
    </gazebo>
  </xacro:macro>

  <!-- ============================================================ -->
  <!-- 2D 激光雷达（RPLIDAR A1 模拟）                              -->
  <!-- ============================================================ -->
  <xacro:macro name="lidar_2d" params="parent_link">
    <link name="lidar_link">
      <inertial>
        <mass value="0.17"/>
        <inertia ixx="1e-4" ixy="0" ixz="0" iyy="1e-4" iyz="0" izz="1e-4"/>
      </inertial>
      <visual>
        <geometry>
          <cylinder radius="0.05" length="0.04"/>
        </geometry>
        <material name="car_gray"/>
      </visual>
    </link>
    <joint name="lidar_joint" type="fixed">
      <parent link="${parent_link}"/>
      <child link="lidar_link"/>
      <origin xyz="0 0 0.17" rpy="0 0 0"/>        <!-- 车顶最高处 -->
    </joint>
  </xacro:macro>

  <xacro:macro name="lidar_gazebo_plugin">
    <gazebo reference="lidar_link">
      <sensor name="lidar_2d" type="ray">
        <pose>0 0 0 0 0 0</pose>
        <update_rate>10</update_rate>              <!-- RPLIDAR A1: 10Hz -->
        <ray>
          <scan>
            <horizontal>
              <samples>360</samples>               <!-- 1° 分辨率 -->
              <resolution>1</resolution>
              <min_angle>-3.14159</min_angle>
              <max_angle>3.14159</max_angle>
            </horizontal>
          </scan>
          <range>
            <min>0.15</min>                        <!-- RPLIDAR A1 盲区 -->
            <max>12.0</max>                        <!-- RPLIDAR A1 最大量程 -->
            <resolution>0.01</resolution>
          </range>
          <noise>
            <type>gaussian</type>
            <mean>0.0</mean>
            <stddev>0.015</stddev>                 <!-- ±1.5cm 噪声 -->
          </noise>
        </ray>
        <plugin name="lidar_plugin" filename="libgazebo_ros_laser.so">
          <topicName>scan</topicName>
          <frameName>lidar_link</frameName>
        </plugin>
      </sensor>
    </gazebo>
  </xacro:macro>

  <!-- ============================================================ -->
  <!-- 超声波传感器 ×4（前后左右）                                 -->
  <!-- ============================================================ -->
  <xacro:macro name="ultrasonic_sensor" params="name parent_link x y z yaw">
    <link name="ultrasonic_${name}_link">
      <inertial>
        <mass value="0.005"/>
        <inertia ixx="1e-6" ixy="0" ixz="0" iyy="1e-6" iyz="0" izz="1e-6"/>
      </inertial>
    </link>
    <joint name="ultrasonic_${name}_joint" type="fixed">
      <parent link="${parent_link}"/>
      <child link="ultrasonic_${name}_link"/>
      <origin xyz="${x} ${y} ${z}" rpy="0 0 ${yaw}"/>
    </joint>
  </xacro:macro>

  <xacro:macro name="ultrasonic_gazebo_plugin" params="name">
    <gazebo reference="ultrasonic_${name}_link">
      <sensor name="ultrasonic_${name}" type="ray">
        <update_rate>25</update_rate>              <!-- HC-SR04: 25Hz -->
        <ray>
          <scan>
            <horizontal>
              <samples>1</samples>                 <!-- 单波束 -->
              <resolution>1</resolution>
              <min_angle>0</min_angle>
              <max_angle>0</max_angle>
            </horizontal>
          </scan>
          <range>
            <min>0.02</min>                        <!-- HC-SR04: 2cm-4m -->
            <max>4.0</max>
            <resolution>0.003</resolution>         <!-- 3mm 精度 -->
          </range>
        </ray>
        <plugin name="ultrasonic_${name}_plugin" filename="libgazebo_ros_laser.so">
          <topicName>ultrasonic/${name}</topicName>
          <frameName>ultrasonic_${name}_link</frameName>
        </plugin>
      </sensor>
    </gazebo>
  </xacro:macro>

  <!-- ============================================================ -->
  <!-- IMU (ICM42688 模拟) — 直接使用 Gazebo IMU 插件             -->
  <!-- ============================================================ -->
  <xacro:macro name="imu_sensor" params="parent_link">
    <gazebo reference="${parent_link}">
      <sensor name="car_imu" type="imu">
        <update_rate>100</update_rate>
        <imu>
          <angular_velocity>
            <x><noise type="gaussian" mean="0" stddev="0.0005"/></x> <!-- 低温漂 -->
            <y><noise type="gaussian" mean="0" stddev="0.0005"/></y>
            <z><noise type="gaussian" mean="0" stddev="0.0005"/></z>
          </angular_velocity>
          <linear_acceleration>
            <x><noise type="gaussian" mean="0" stddev="0.001"/></x>
            <y><noise type="gaussian" mean="0" stddev="0.001"/></y>
            <z><noise type="gaussian" mean="0" stddev="0.001"/></z>
          </linear_acceleration>
        </imu>
        <plugin name="car_imu_plugin" filename="libgazebo_ros_imu_sensor.so">
          <topicName>imu/data</topicName>
          <frameName>base_link</frameName>
        </plugin>
      </sensor>
    </gazebo>
  </xacro:macro>

</robot>
```

## 5.2 更新 `diff_chassis.urdf.xacro` 引入传感器

**文件：`~/air_ground_sim_ws/src/car_bringup/urdf/diff_chassis.urdf.xacro`**

在文件末尾 `</robot>` 之前追加：

```xml
  <!-- ====== 引入传感器 ====== -->
  <xacro:include filename="$(find car_bringup)/urdf/car_sensors.urdf.xacro"/>

  <!-- OpenMV 云台相机 -->
  <xacro:openmv_gimbal parent_link="sensor_mount"/>
  <xacro:openmv_gazebo_plugin/>

  <!-- 2D 激光雷达 -->
  <xacro:lidar_2d parent_link="sensor_mount"/>
  <xacro:lidar_gazebo_plugin/>

  <!-- 4× 超声波 -->
  <xacro:ultrasonic_sensor name="front"  parent_link="base_link" x="0.125" y="0"     z="0.04" yaw="0"/>
  <xacro:ultrasonic_sensor name="rear"   parent_link="base_link" x="-0.125" y="0"     z="0.04" yaw="3.14159"/>
  <xacro:ultrasonic_sensor name="left"   parent_link="base_link" x="0"     y="0.10"  z="0.04" yaw="1.5708"/>
  <xacro:ultrasonic_sensor name="right"  parent_link="base_link" x="0"     y="-0.10" z="0.04" yaw="-1.5708"/>

  <xacro:ultrasonic_gazebo_plugin name="front"/>
  <xacro:ultrasonic_gazebo_plugin name="rear"/>
  <xacro:ultrasonic_gazebo_plugin name="left"/>
  <xacro:ultrasonic_gazebo_plugin name="right"/>

  <!-- IMU -->
  <xacro:imu_sensor parent_link="base_link"/>
```

**同样更新 `mecanum_chassis.urdf.xacro`**，在 `</robot>` 前加入相同内容。

## 5.3 传感器参数配置 YAML

**文件：`~/air_ground_sim_ws/src/car_bringup/config/car_sensors.yaml`**

```yaml
# Car sensor configuration (mirrors real hardware specs)
openmv:
  resolution: [320, 240]
  fps: 10
  fov_h: 65.0
  format: "rgb8"

  # 云台控制
  gimbal:
    pan_range: [-90, 90]     # degrees
    tilt_range: [-45, 45]
    pan_topic: "/car/gimbal/pan/command"
    tilt_topic: "/car/gimbal/tilt/command"

lidar:
  model: "RPLIDAR_A1"
  samples: 360
  scan_rate: 10       # Hz
  min_range: 0.15     # m
  max_range: 12.0     # m
  topic: "/car/scan"

ultrasonic:
  count: 4
  positions:
    front:  [0.125, 0,     0.04, 0]
    rear:   [-0.125, 0,    0.04, 180]
    left:   [0,      0.10, 0.04, 90]
    right:  [0,     -0.10, 0.04, -90]
  min_range: 0.02     # m
  max_range: 4.0      # m

imu:
  model: "ICM42688"
  update_rate: 100    # Hz
  gyro_noise: 0.0005  # rad/s/√Hz
  accel_noise: 0.001  # m/s²/√Hz
```

## 5.4 传感器话题一览

所有话题在 `/car` 命名空间下：

| 传感器 | 话题 | 消息类型 |
|--------|------|---------|
| OpenMV RGB | `/car/openmv/image_raw` | `sensor_msgs/Image` |
| 2D LiDAR | `/car/scan` | `sensor_msgs/LaserScan` |
| 超声波前 | `/car/ultrasonic/front` | `sensor_msgs/LaserScan` |
| 超声波后 | `/car/ultrasonic/rear` | `sensor_msgs/LaserScan` |
| 超声波左 | `/car/ultrasonic/left` | `sensor_msgs/LaserScan` |
| 超声波右 | `/car/ultrasonic/right` | `sensor_msgs/LaserScan` |
| IMU | `/car/imu/data` | `sensor_msgs/Imu` |
| 里程计 | `/car/odom` | `nav_msgs/Odometry` |

## 5.5 云台控制脚本（模拟舵机）

**文件：`~/air_ground_sim_ws/src/car_bringup/scripts/gimbal_controller.py`**

```python
#!/usr/bin/env python3
"""
Simple gimbal position controller for OpenMV pan-tilt in Gazebo.
Accepts /car/gimbal/pan/command and /car/gimbal/tilt/command (Float64 in radians).
"""
import rospy
from std_msgs.msg import Float64
from sensor_msgs.msg import JointState


class GimbalController:
    def __init__(self):
        rospy.init_node("gimbal_controller")
        # ros_control position_controllers/JointPositionController 期望的话题名:
        #   /car/gimbal_pan_controller/command  (非 /car/gimbal_pan_joint/command)
        self.pan_pub = rospy.Publisher("/car/gimbal_pan_controller/command", Float64, queue_size=10)
        self.tilt_pub = rospy.Publisher("/car/gimbal_tilt_controller/command", Float64, queue_size=10)

        self.sub_pan = rospy.Subscriber("/car/gimbal/pan/command", Float64, self.pan_cb)
        self.sub_tilt = rospy.Subscriber("/car/gimbal/tilt/command", Float64, self.tilt_cb)

        rospy.loginfo("[Gimbal Controller] Ready")

    def pan_cb(self, msg: Float64):
        # Clamp to ±90°
        val = max(-1.57, min(1.57, msg.data))
        self.pan_pub.publish(Float64(val))

    def tilt_cb(self, msg: Float64):
        # Clamp to ±45°
        val = max(-0.785, min(0.785, msg.data))
        self.tilt_pub.publish(Float64(val))


if __name__ == "__main__":
    GimbalController()
    rospy.spin()
```

```bash
chmod +x ~/air_ground_sim_ws/src/car_bringup/scripts/gimbal_controller.py
```

## 5.6 验证脚本

**文件：`~/air_ground_sim_ws/src/car_bringup/scripts/test_sensors.sh`**

```bash
#!/bin/bash
echo "=== Task-05 Car Sensors Verification ==="

roslaunch car_bringup car_diff.launch headless:=true gui:=false &
CAR_PID=$!
sleep 10

# 1. OpenMV camera
echo "--- Checking OpenMV camera ---"
rostopic echo /car/openmv/image_raw -n 1 2>/dev/null | grep -q "height" && echo "[PASS] OpenMV" || echo "[WARN] OpenMV"

# 2. 2D LiDAR
echo "--- Checking LiDAR ---"
rostopic echo /car/scan -n 1 2>/dev/null | grep -q "ranges" && echo "[PASS] LiDAR" || echo "[WARN] LiDAR"

# 3. Ultrasonic ×4
echo "--- Checking Ultrasonic ---"
for dir in front rear left right; do
  rostopic echo /car/ultrasonic/$dir -n 1 2>/dev/null | grep -q "ranges" && echo "  [PASS] $dir" || echo "  [WARN] $dir"
done

# 4. IMU
echo "--- Checking IMU ---"
rostopic echo /car/imu/data -n 1 2>/dev/null | grep -q "angular_velocity" && echo "[PASS] IMU" || echo "[WARN] IMU"

# 5. Gimbal test
echo "--- Testing gimbal ---"
rostopic pub -1 /car/gimbal/pan/command std_msgs/Float64 "data: 0.5" 2>/dev/null
rostopic pub -1 /car/gimbal/tilt/command std_msgs/Float64 "data: -0.3" 2>/dev/null
sleep 1
echo "[INFO] Check gimbal joints moved"

kill $CAR_PID 2>/dev/null
wait $CAR_PID 2>/dev/null
echo "=== Done ==="
```

## 交付产物

1. `car_sensors.urdf.xacro` 可被 diff 和 mecanum 底盘正确引用
2. 启动后所有传感器话题有数据
3. 云台可响应 `/car/gimbal/*/command` 话题
4. `test_sensors.sh` 至少所有传感器都能检测到话题存在
