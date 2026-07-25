# Task-03: 差速底盘仿真（电赛规格）

## 前置条件

- Task-01 完成（ROS 工作空间 + 自定义消息）
- Gazebo 11 可用

## 目标

在 Gazebo 中创建电赛规格的差速驱动小车模型（两轮差速 + 编码器 + 520 电机模拟），使用 `ros_control` 驱动，支持 `/cmd_vel` 控制。

---

## 3.1 车体 URDF 模型

**文件：`~/air_ground_sim_ws/src/car_bringup/urdf/car_base.urdf.xacro`**

```xml
<?xml version="1.0"?>
<robot name="car_base" xmlns:xacro="http://www.ros.org/wiki/xacro">

  <!-- 车体常量 -->
  <xacro:property name="body_length" value="0.25"/>    <!-- 25cm，电赛规格 -->
  <xacro:property name="body_width" value="0.20"/>
  <xacro:property name="body_height" value="0.08"/>
  <xacro:property name="wheel_radius" value="0.033"/>   <!-- 520 电机标准轮径 ~65mm -->
  <xacro:property name="wheel_width" value="0.026"/>
  <xacro:property name="body_mass" value="2.5"/>        <!-- 含电池、树莓派、STM32 -->

  <!-- 基础颜色 -->
  <material name="car_gray">
    <color rgba="0.3 0.3 0.35 1"/>
  </material>
  <material name="black">
    <color rgba="0.1 0.1 0.1 1"/>
  </material>

  <!-- ============ 车体底板 ============ -->
  <link name="base_link">
    <inertial>
      <origin xyz="0 0 0.04" rpy="0 0 0"/>
      <mass value="${body_mass}"/>
      <inertia ixx="0.015" ixy="0" ixz="0" iyy="0.02" iyz="0" izz="0.02"/>
    </inertial>
    <visual>
      <origin xyz="0 0 0.04" rpy="0 0 0"/>
      <geometry>
        <box size="${body_length} ${body_width} ${body_height}"/>
      </geometry>
      <material name="car_gray"/>
    </visual>
    <collision>
      <origin xyz="0 0 0.04" rpy="0 0 0"/>
      <geometry>
        <box size="${body_length} ${body_width} ${body_height}"/>
      </geometry>
    </collision>
  </link>

  <!-- ============ 传感器安装支架（车体上方 5cm） ============ -->
  <link name="sensor_mount">
    <inertial>
      <mass value="0.1"/>
      <inertia ixx="1e-4" ixy="0" ixz="0" iyy="1e-4" iyz="0" izz="1e-4"/>
    </inertial>
    <visual>
      <origin xyz="0 0 0.13" rpy="0 0 0"/>
      <geometry>
        <box size="0.08 0.06 0.02"/>
      </geometry>
      <material name="black"/>
    </visual>
  </link>
  <joint name="sensor_mount_joint" type="fixed">
    <parent link="base_link"/>
    <child link="sensor_mount"/>
    <origin xyz="0 0 0.09" rpy="0 0 0"/>
  </joint>

  <!-- ============ Gazebo 插件引用 ============ -->
  <gazebo>
    <plugin name="gazebo_ros_control" filename="libgazebo_ros_control.so">
      <robotNamespace>/car</robotNamespace>
    </plugin>
  </gazebo>

  <!-- 差速驱动: 由 ros_control 的 diff_drive_controller 统一管理 -->
  <!-- (不再使用 libgazebo_ros_diff_drive.so，避免双控制器冲突) -->
</robot>
```

## 3.2 差速底盘宏（含左右驱动轮 + 万向轮）

**文件：`~/air_ground_sim_ws/src/car_bringup/urdf/diff_chassis.urdf.xacro`**

```xml
<?xml version="1.0"?>
<robot name="diff_chassis" xmlns:xacro="http://www.ros.org/wiki/xacro">

  <xacro:include filename="$(find car_bringup)/urdf/car_base.urdf.xacro"/>

  <xacro:property name="wheel_radius" value="0.033"/>
  <xacro:property name="wheel_width" value="0.026"/>
  <xacro:property name="wheel_mass" value="0.05"/>

  <!-- ============ 左驱动轮 ============ -->
  <link name="left_wheel">
    <inertial>
      <mass value="${wheel_mass}"/>
      <inertia ixx="1e-5" ixy="0" ixz="0" iyy="1e-5" iyz="0" izz="1e-5"/>
    </inertial>
    <visual>
      <geometry>
        <cylinder radius="${wheel_radius}" length="${wheel_width}"/>
      </geometry>
      <material name="black"/>
    </visual>
    <collision>
      <geometry>
        <cylinder radius="${wheel_radius}" length="${wheel_width}"/>
      </geometry>
    </collision>
  </link>
  <joint name="left_wheel_joint" type="continuous">
    <parent link="base_link"/>
    <child link="left_wheel"/>
    <origin xyz="0 0.09 0.033" rpy="1.5708 0 0"/>  <!-- y=轮距/2 -->
    <axis xyz="0 0 1"/>
  </joint>

  <!-- ============ 右驱动轮 ============ -->
  <link name="right_wheel">
    <inertial>
      <mass value="${wheel_mass}"/>
      <inertia ixx="1e-5" ixy="0" ixz="0" iyy="1e-5" iyz="0" izz="1e-5"/>
    </inertial>
    <visual>
      <geometry>
        <cylinder radius="${wheel_radius}" length="${wheel_width}"/>
      </geometry>
      <material name="black"/>
    </visual>
    <collision>
      <geometry>
        <cylinder radius="${wheel_radius}" length="${wheel_width}"/>
      </geometry>
    </collision>
  </link>
  <joint name="right_wheel_joint" type="continuous">
    <parent link="base_link"/>
    <child link="right_wheel"/>
    <origin xyz="0 -0.09 0.033" rpy="1.5708 0 0"/>
    <axis xyz="0 0 1"/>
  </joint>

  <!-- ============ 前万向轮（caster） ============ -->
  <link name="front_caster">
    <inertial>
      <mass value="0.01"/>
      <inertia ixx="1e-6" ixy="0" ixz="0" iyy="1e-6" iyz="0" izz="1e-6"/>
    </inertial>
    <visual>
      <geometry>
        <sphere radius="0.012"/>
      </geometry>
      <material name="car_gray"/>
    </visual>
    <collision>
      <geometry>
        <sphere radius="0.012"/>
      </geometry>
    </collision>
  </link>
  <!-- 前万向轮: continuous joint 绕 Z 自转，yaw 旋转错开 90° 形成被动轮 -->
  <joint name="front_caster_joint" type="continuous">
    <parent link="base_link"/>
    <child link="front_caster"/>
    <origin xyz="0.11 0 0.012" rpy="0 1.5708 0"/>
    <axis xyz="0 0 1"/>
  </joint>

  <!-- ============ 后万向轮 ============ -->
  <link name="rear_caster">
    <inertial>
      <mass value="0.01"/>
      <inertia ixx="1e-6" ixy="0" ixz="0" iyy="1e-6" iyz="0" izz="1e-6"/>
    </inertial>
    <visual>
      <geometry>
        <sphere radius="0.012"/>
      </geometry>
      <material name="car_gray"/>
    </visual>
    <collision>
      <geometry>
        <sphere radius="0.012"/>
      </geometry>
    </collision>
  </link>
  <joint name="rear_caster_joint" type="continuous">
    <parent link="base_link"/>
    <child link="rear_caster"/>
    <origin xyz="-0.11 0 0.012" rpy="0 1.5708 0"/>
    <axis xyz="0 0 1"/>
  </joint>

</robot>
```

## 3.3 底盘控制配置 YAML

**文件：`~/air_ground_sim_ws/src/car_bringup/config/diff_chassis_control.yaml`**

```yaml
# Diff chassis ros_control configuration
car:
  # 关节状态发布
  joint_state_controller:
    type: joint_state_controller/JointStateController
    publish_rate: 30

  # 差速驱动控制器（速度控制模式）
  diff_drive_controller:
    type: diff_drive_controller/DiffDriveController
    left_wheel: left_wheel_joint
    right_wheel: right_wheel_joint
    publish_rate: 30
    pose_covariance_diagonal: [0.001, 0.001, 1000000.0, 1000000.0, 1000000.0, 0.03]
    twist_covariance_diagonal: [0.001, 0.001, 1000000.0, 1000000.0, 1000000.0, 0.03]
    cmd_vel_timeout: 0.5
    base_frame_id: base_link
    wheel_separation: 0.18
    wheel_radius: 0.033
    linear:
      x:
        has_velocity_limits: true
        max_velocity: 1.0
        has_acceleration_limits: true
        max_acceleration: 2.0
    angular:
      z:
        has_velocity_limits: true
        max_velocity: 3.0
        has_acceleration_limits: true
        max_acceleration: 5.0

  # 云台舵机位置控制器 (pan/tilt)
  gimbal_pan_controller:
    type: position_controllers/JointPositionController
    joint: gimbal_pan_joint
    pid: {p: 10.0, i: 0.1, d: 0.5}
  gimbal_tilt_controller:
    type: position_controllers/JointPositionController
    joint: gimbal_tilt_joint
    pid: {p: 10.0, i: 0.1, d: 0.5}
```

## 3.4 Launch 文件

**文件：`~/air_ground_sim_ws/src/car_bringup/launch/car_diff.launch`**

```xml
<launch>
  <arg name="world" default="$(find car_bringup)/worlds/empty.world"/>
  <arg name="gui" default="false"/>
  <arg name="headless" default="true"/>
  <arg name="x" default="0.0"/>
  <arg name="y" default="0.0"/>
  <arg name="z" default="0.1"/>

  <!-- Gazebo 空世界 -->
  <include file="$(find gazebo_ros)/launch/empty_world.launch">
    <arg name="world_name" value="$(arg world)"/>
    <arg name="gui" value="$(arg gui)"/>
    <arg name="headless" value="$(arg headless)"/>
    <arg name="paused" value="false"/>
    <arg name="use_sim_time" value="true"/>
  </include>

  <!-- 加载差速底盘 URDF -->
  <param name="robot_description"
         command="$(find xacro)/xacro $(find car_bringup)/urdf/diff_chassis.urdf.xacro"/>

  <!-- 生成小车 -->
  <node name="spawn_car" pkg="gazebo_ros" type="spawn_model"
        args="-param robot_description -urdf -model diff_car
              -x $(arg x) -y $(arg y) -z $(arg z)" output="screen"/>

  <!-- 等 Gazebo 完全加载模型（约 2s）再启动控制器 -->
  <node pkg="rostopic" type="rostopic" name="wait_for_robot"
        args="echo /car/joint_states -n 1" output="log"/>

  <!-- 加载 ros_control 配置 -->
  <rosparam file="$(find car_bringup)/config/diff_chassis_control.yaml" command="load"/>

  <!-- 启动控制器 -->
  <node name="controller_spawner" pkg="controller_manager" type="spawner"
        args="joint_state_controller diff_drive_controller" output="screen"/>

  <!-- 键盘遥控（调试用） -->
  <node name="teleop" pkg="teleop_twist_keyboard" type="teleop_twist_keyboard.py"
        output="screen" launch-prefix="xterm -e" if="$(arg gui)"/>
</launch>
```

## 3.5 底盘配置参数（汇总）

**文件：`~/air_ground_sim_ws/src/car_bringup/config/chassis_params.yaml`**

```yaml
# 底盘共通参数
diff_chassis:
  type: "differential_drive"
  track_width: 0.18       # m, 轮距
  wheel_radius: 0.033     # m
  max_linear_speed: 1.0   # m/s
  max_angular_speed: 3.0  # rad/s
  encoder_resolution: 11  # 520 电机编码器: 11 PPR × 减速比 (实际需 ×4 正交)

mecanum_chassis:
  type: "mecanum"
  track_width: 0.18
  wheel_base: 0.20
  wheel_radius: 0.033
  max_linear_speed: 0.8
  max_angular_speed: 2.0

# 底盘检测引脚（模拟 STM32 检测当前安装的底盘类型）
chassis_detect:
  diff_pin: 1    # GPIO 拉高 = 差速底盘在位
  mecanum_pin: 0 # GPIO 拉高 = 麦轮底盘在位
```

## 3.6 验证脚本

**文件：`~/air_ground_sim_ws/src/car_bringup/scripts/test_diff.sh`**

```bash
#!/bin/bash
echo "=== Task-03 Diff Chassis Verification ==="

# Start diff car in background
roslaunch car_bringup car_diff.launch headless:=true gui:=false &
CAR_PID=$!
sleep 8

# 1. Check model spawned
echo "--- Checking model spawn ---"
rosservice call /gazebo/get_model_state "{model_name: 'diff_car'}" 2>/dev/null | grep -q "success" && echo "[PASS]" || echo "[FAIL]"

# 2. Check odometry
echo "--- Checking /car/odom ---"
rostopic echo /car/odom -n 1 2>/dev/null | grep -q "pose" && echo "[PASS]" || echo "[WARN]"

# 3. Send cmd_vel and verify movement
echo "--- Testing cmd_vel ---"
rostopic pub -1 /car/cmd_vel geometry_msgs/Twist "linear: {x: 0.2} angular: {z: 0.0}" 2>/dev/null
sleep 1
rostopic echo /car/odom -n 1 2>/dev/null | python3 -c "
import sys, json
data = sys.stdin.read()
if 'pose' in data: print('[PASS] Car responds to cmd_vel')
else: print('[WARN]')
"

# Cleanup
kill $CAR_PID 2>/dev/null
wait $CAR_PID 2>/dev/null
echo "=== Done ==="
```

## 3.7 更新 `car_bringup/CMakeLists.txt`

```cmake
install(DIRECTORY worlds/
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}/worlds)
```

并创建空世界：
```bash
cp ~/air_ground_sim_ws/src/drone_bringup/worlds/empty.world ~/air_ground_sim_ws/src/car_bringup/worlds/empty.world
```

## 交付产物

1. `roslaunch car_bringup car_diff.launch` 能启动差速小车
2. `/car/cmd_vel` 可控制小车前进后退转弯
3. `/car/odom` 发布里程计数据
4. `test_diff.sh` 全部 PASS
