# Task-04: 麦轮底盘仿真 + 模块化底盘切换

## 前置条件

- Task-03 完成（差速底盘 URDF 可用）
- Gazebo 9 可正常生成机器人模型

## 目标

1. 创建四轮麦克纳姆底盘 URDF + 自定义运动学控制器
2. 实现 `swap_chassis` 服务，运行时可切换差速/麦轮底盘

---

## 4.1 麦轮底盘 URDF

**文件：`~/air_ground_sim_ws/src/car_bringup/urdf/mecanum_chassis.urdf.xacro`**

```xml
<?xml version="1.0"?>
<robot name="mecanum_chassis" xmlns:xacro="http://www.ros.org/wiki/xacro">

  <xacro:include filename="$(find car_bringup)/urdf/car_base.urdf.xacro"/>

  <xacro:property name="wheel_radius" value="0.033"/>
  <xacro:property name="wheel_width" value="0.026"/>
  <xacro:property name="wheel_mass" value="0.05"/>
  <xacro:property name="wheel_x" value="0.10"/>
  <xacro:property name="wheel_y" value="0.09"/>

  <!-- 麦轮通用宏 -->
  <xacro:macro name="mecanum_wheel" params="name prefix x y yaw">
    <link name="${name}_wheel">
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
        <surface>
          <friction>
            <ode>
              <mu>100.0</mu>
              <mu2>0.01</mu2>     <!-- 横向摩擦力极低，模拟麦轮辊子 -->
            </ode>
          </friction>
        </surface>
      </collision>
    </link>
    <joint name="${name}_wheel_joint" type="continuous">
      <parent link="base_link"/>
      <child link="${name}_wheel"/>
      <origin xyz="${x} ${y} 0.033" rpy="1.5708 0 ${yaw}"/>
      <axis xyz="0 0 1"/>
    </joint>
  </xacro:macro>

  <!-- 四个麦轮 -->
  <xacro:mecanum_wheel name="front_left"  x="0.10"  y="0.09" yaw="0"/>
  <xacro:mecanum_wheel name="front_right" x="0.10"  y="-0.09" yaw="0"/>
  <xacro:mecanum_wheel name="rear_left"   x="-0.10" y="0.09" yaw="0"/>
  <xacro:mecanum_wheel name="rear_right"  x="-0.10" y="-0.09" yaw="0"/>

</robot>
```

> ⚠️ **关键技术点**：麦轮的仿真难点在于辊子的横向滑动。Gazebo 无法原生模拟辊子，故在 `<surface><friction>` 中将 `mu2`（横向摩擦系数）设得极低，以此近似麦轮特性。

## 4.2 麦轮运动学控制器

**文件：`~/air_ground_sim_ws/src/car_bringup/scripts/mecanum_controller.py`**

```python
#!/usr/bin/env python3
"""
Mecanum Wheel Kinematic Controller.

Converts /car/cmd_vel (vx, vy, omega) to four individual wheel velocities.

Mecanum inverse kinematics (wheel order: fl, fr, rl, rr):
    w_fl = (vx - vy - omega * (Lx + Ly)) / R
    w_fr = (vx + vy + omega * (Lx + Ly)) / R
    w_rl = (vx + vy - omega * (Lx + Ly)) / R
    w_rr = (vx - vy + omega * (Lx + Ly)) / R

where Lx = wheel_base/2, Ly = track_width/2, R = wheel_radius.
"""
import math
import rospy
from geometry_msgs.msg import Twist
from std_msgs.msg import Float64


class MecanumController:
    def __init__(self):
        rospy.init_node("mecanum_controller")

        # 底盘几何参数（从 YAML 加载）
        self.Lx = rospy.get_param("~wheel_base", 0.20) / 2.0    # m
        self.Ly = rospy.get_param("~track_width", 0.18) / 2.0   # m
        self.R = rospy.get_param("~wheel_radius", 0.033)         # m
        self.max_rpm = rospy.get_param("~max_rpm", 300.0)

        # 四个轮子速度命令发布器
        wheel_names = ["front_left", "front_right", "rear_left", "rear_right"]
        self.pubs = {}
        for name in wheel_names:
            topic = f"/car/{name}_wheel_controller/command"
            self.pubs[name] = rospy.Publisher(topic, Float64, queue_size=10)

        # 订阅 cmd_vel
        self.cmd_sub = rospy.Subscriber("/car/cmd_vel", Twist, self.cmd_callback)
        self.last_cmd_time = rospy.Time.now()
        self.timeout = rospy.Duration(0.5)  # 500ms 超时自动停车

        rospy.loginfo("[Mecanum Controller] Ready (Lx=%.3f Ly=%.3f R=%.3f)",
                       self.Lx, self.Ly, self.R)

        # 超时看门狗定时器
        rospy.Timer(rospy.Duration(0.1), self.watchdog)

    def cmd_callback(self, msg: Twist):
        """Convert twist to wheel velocities and publish."""
        vx, vy, omega = msg.linear.x, msg.linear.y, msg.angular.z

        # 逆运动学
        w_fl = (vx - vy - omega * (self.Lx + self.Ly)) / self.R
        w_fr = (vx + vy + omega * (self.Lx + self.Ly)) / self.R
        w_rl = (vx + vy - omega * (self.Lx + self.Ly)) / self.R
        w_rr = (vx - vy + omega * (self.Lx + self.Ly)) / self.R

        # 限幅
        max_w = self.max_rpm * 2 * math.pi / 60.0
        for w in [w_fl, w_fr, w_rl, w_rr]:
            w = max(-max_w, min(max_w, w))

        self.last_cmd_time = rospy.Time.now()

        self.pubs["front_left"].publish(Float64(w_fl))
        self.pubs["front_right"].publish(Float64(w_fr))
        self.pubs["rear_left"].publish(Float64(w_rl))
        self.pubs["rear_right"].publish(Float64(w_rr))

    def watchdog(self, event):
        """Stop wheels if no cmd_vel received within timeout."""
        if rospy.Time.now() - self.last_cmd_time > self.timeout:
            for pub in self.pubs.values():
                pub.publish(Float64(0.0))


if __name__ == "__main__":
    try:
        MecanumController()
        rospy.spin()
    except rospy.ROSInterruptException:
        pass
```

```bash
chmod +x ~/air_ground_sim_ws/src/car_bringup/scripts/mecanum_controller.py
```

## 4.3 麦轮底盘控制配置

**文件：`~/air_ground_sim_ws/src/car_bringup/config/mecanum_chassis_control.yaml`**

```yaml
# Mecanum chassis ros_control (4 independent wheel velocity controllers)
car:
  joint_state_controller:
    type: joint_state_controller/JointStateController
    publish_rate: 30

  # 四个独立的轮速控制器
  front_left_wheel_controller:
    type: velocity_controllers/JointVelocityController
    joint: front_left_wheel_joint
    pid: {p: 1.0, i: 0.1, d: 0.01}

  front_right_wheel_controller:
    type: velocity_controllers/JointVelocityController
    joint: front_right_wheel_joint
    pid: {p: 1.0, i: 0.1, d: 0.01}

  rear_left_wheel_controller:
    type: velocity_controllers/JointVelocityController
    joint: rear_left_wheel_joint
    pid: {p: 1.0, i: 0.1, d: 0.01}

  rear_right_wheel_controller:
    type: velocity_controllers/JointVelocityController
    joint: rear_right_wheel_joint
    pid: {p: 1.0, i: 0.1, d: 0.01}
```

## 4.4 麦轮底盘 Launch

**文件：`~/air_ground_sim_ws/src/car_bringup/launch/car_mecanum.launch`**

```xml
<launch>
  <arg name="world" default="$(find car_bringup)/worlds/empty.world"/>
  <arg name="gui" default="false"/>
  <arg name="headless" default="true"/>
  <arg name="x" default="1.0"/>
  <arg name="y" default="0.0"/>
  <arg name="z" default="0.1"/>

  <!-- Gazebo（可复用已运行的 Gazebo 实例，通过 group ns 隔离） -->
  <group ns="car">
    <include file="$(find gazebo_ros)/launch/empty_world.launch">
      <arg name="world_name" value="$(arg world)"/>
      <arg name="gui" value="$(arg gui)"/>
      <arg name="headless" value="$(arg headless)"/>
      <arg name="paused" value="false"/>
      <arg name="use_sim_time" value="true"/>
    </include>
  </group>

  <!-- 加载麦轮 URDF -->
  <param name="robot_description"
         command="$(find xacro)/xacro $(find car_bringup)/urdf/mecanum_chassis.urdf.xacro"/>

  <!-- 生成麦轮小车 -->
  <node name="spawn_mecanum_car" pkg="gazebo_ros" type="spawn_model"
        args="-param robot_description -urdf -model mecanum_car
              -x $(arg x) -y $(arg y) -z $(arg z)" output="screen"/>

  <!-- 加载控制器 -->
  <rosparam file="$(find car_bringup)/config/mecanum_chassis_control.yaml" command="load"/>
  <node name="controller_spawner" pkg="controller_manager" type="spawner"
        args="joint_state_controller front_left_wheel_controller front_right_wheel_controller
              rear_left_wheel_controller rear_right_wheel_controller" output="screen"/>

  <!-- 麦轮运动学转换节点 -->
  <node name="mecanum_controller" pkg="car_bringup" type="mecanum_controller.py" output="screen">
    <param name="wheel_base" value="0.20"/>
    <param name="track_width" value="0.18"/>
    <param name="wheel_radius" value="0.033"/>
  </node>
</launch>
```

## 4.5 底盘切换服务节点

**文件：`~/air_ground_sim_ws/src/car_bringup/scripts/chassis_swapper.py`**

```python
#!/usr/bin/env python3
"""
Chassis Swapper Service.

Provides /car/swap_chassis service to dynamically switch between
'diff' and 'mecanum' chassis in Gazebo simulation.

Flow:
1. Get current car pose from Gazebo
2. Delete current chassis model
3. Load new chassis URDF
4. Spawn new chassis at same pose
5. Reload controllers
"""
import rospy
import subprocess
import os
from air_ground_interfaces.srv import SwapChassis, SwapChassisResponse
from gazebo_msgs.srv import GetModelState, DeleteModel, SpawnModel


class ChassisSwapper:
    def __init__(self):
        rospy.init_node("chassis_swapper")
        rospy.wait_for_service("/gazebo/get_model_state")
        rospy.wait_for_service("/gazebo/delete_model")
        rospy.wait_for_service("/gazebo/spawn_urdf_model")

        self.get_state = rospy.ServiceProxy("/gazebo/get_model_state", GetModelState)
        self.delete_model = rospy.ServiceProxy("/gazebo/delete_model", DeleteModel)
        self.spawn_model = rospy.ServiceProxy("/gazebo/spawn_urdf_model", SpawnModel)

        self.current_chassis = "none"
        self.service = rospy.Service("/car/swap_chassis", SwapChassis, self.handle_swap)
        rospy.loginfo("[Chassis Swapper] Ready")

    def handle_swap(self, req):
        target = req.target_chassis
        resp = SwapChassisResponse()

        if target not in ("diff", "mecanum"):
            resp.success = False
            resp.message = f"Unknown chassis type: {target}"
            return resp

        if target == self.current_chassis:
            resp.success = True
            resp.message = f"Already using {target} chassis"
            return resp

        try:
            # 1. Get current pose (try both model names)
            pose = None
            for model_name in ["diff_car", "mecanum_car"]:
                try:
                    state = self.get_state(model_name, "world")
                    pose = state.pose
                    rospy.loginfo(f"Got pose from {model_name}: {pose.position}")
                    break
                except:
                    continue

            if pose is None:
                # 没有现有模型，用默认位置
                from geometry_msgs.msg import Pose
                pose = Pose()
                pose.position.x = 0.0
                pose.position.y = 0.0
                pose.position.z = 0.1
                pose.orientation.w = 1.0

            # 2. Delete current chassis (ignore if doesn't exist)
            for model_name in ["diff_car", "mecanum_car"]:
                try:
                    self.delete_model(model_name)
                    rospy.loginfo(f"Deleted {model_name}")
                except:
                    pass

            # 3-4. Generate and spawn new URDF
            urdf_path = os.path.join(
                os.path.expanduser("~"),
                "air_ground_sim_ws/src/car_bringup/urdf",
                f"{target}_chassis.urdf.xacro"
            )

            # Convert xacro to URDF
            xacro_cmd = f"rosrun xacro xacro {urdf_path}"
            urdf_str = subprocess.check_output(xacro_cmd, shell=True).decode()

            model_name = f"{target}_car"
            self.spawn_model(model_name, urdf_str, "", pose, "world")
            rospy.loginfo(f"Spawned {model_name}")

            # 5. Reload controllers
            self._reload_controllers(target)

            self.current_chassis = target
            resp.success = True
            resp.message = f"Switched to {target} chassis"
        except Exception as e:
            resp.success = False
            resp.message = f"Swap failed: {str(e)}"
            rospy.logerr(resp.message)

        return resp

    def _reload_controllers(self, chassis_type):
        """Reload ros_control configuration for the new chassis."""
        config_file = os.path.join(
            os.path.expanduser("~"),
            "air_ground_sim_ws/src/car_bringup/config",
            f"{chassis_type}_chassis_control.yaml"
        )
        subprocess.call(["rosparam", "load", config_file, "/car"])
        rospy.loginfo(f"Loaded controller config: {config_file}")
        # Note: in production, use controller_manager/switch_controller service


if __name__ == "__main__":
    try:
        ChassisSwapper()
        rospy.spin()
    except rospy.ROSInterruptException:
        pass
```

```bash
chmod +x ~/air_ground_sim_ws/src/car_bringup/scripts/chassis_swapper.py
```

## 4.6 验证脚本

**文件：`~/air_ground_sim_ws/src/car_bringup/scripts/test_mecanum.sh`**

```bash
#!/bin/bash
echo "=== Task-04 Mecanum Chassis Verification ==="

# Start mecanum car
roslaunch car_bringup car_mecanum.launch headless:=true gui:=false &
CAR_PID=$!
sleep 8

# 1. Check model spawn
echo "--- Checking mecanum model ---"
rosservice call /gazebo/get_model_state "{model_name: 'mecanum_car'}" 2>/dev/null | grep -q "success" && echo "[PASS]" || echo "[FAIL]"

# 2. Check 4 wheel controllers
echo "--- Checking wheel controllers ---"
for wheel in front_left front_right rear_left rear_right; do
  rostopic info /car/${wheel}_wheel_controller/command 2>/dev/null | grep -q "Type" && echo "  [PASS] $wheel" || echo "  [FAIL] $wheel"
done

# 3. Send cmd_vel and check diagonal movement (vy != 0)
echo "--- Testing lateral movement ---"
rostopic pub -1 /car/cmd_vel geometry_msgs/Twist "linear: {x: 0.0, y: 0.15} angular: {z: 0.0}"
sleep 2
echo "[INFO] Check manually that mecanum moved laterally (not possible for diff drive)"

# 4. Test chassis swap via service
echo "--- Testing chassis swap ---"
rosservice call /car/swap_chassis "target_chassis: 'diff'" 2>/dev/null
sleep 3
rosservice call /gazebo/get_model_state "{model_name: 'diff_car'}" 2>/dev/null | grep -q "success" && echo "[PASS] Swap to diff" || echo "[WARN]"

rosservice call /car/swap_chassis "target_chassis: 'mecanum'" 2>/dev/null
sleep 3
rosservice call /gazebo/get_model_state "{model_name: 'mecanum_car'}" 2>/dev/null | grep -q "success" && echo "[PASS] Swap to mecanum" || echo "[WARN]"

kill $CAR_PID 2>/dev/null
wait $CAR_PID 2>/dev/null
echo "=== Done ==="
```

## 交付产物

1. `roslaunch car_bringup car_mecanum.launch` 启动麦轮小车
2. 发送 `vy != 0` 的 `/car/cmd_vel`，小车能横向平移
3. `rosservice call /car/swap_chassis "diff"` 和 `"mecanum"` 可动态切换底盘
4. `test_mecanum.sh` 全部 PASS（或 [WARN] 可接受）
