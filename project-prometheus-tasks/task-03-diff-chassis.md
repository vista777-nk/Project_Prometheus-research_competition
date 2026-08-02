# Task-03：差速底盘仿真（电赛规格）

## 目标

在 Ubuntu 20.04、ROS Noetic 和 Gazebo Classic 11 中建立两轮差速小车：

- 车体尺寸和轮径按电赛亚克力底盘、520 编码器电机建模；
- 使用 `gazebo_ros_control` 和 `diff_drive_controller`；
- `/car/cmd_vel` 支持前进、后退和转向；
- `/car/odom` 与 `/car/joint_states` 提供里程计和轮编码器仿真；
- Task-03 可独立运行，并作为 Task-04 麦轮底盘和 Task-05 车载传感器的基础。

---

## 已验证环境

| 项目 | 实测值 |
|---|---|
| Gazebo Classic | 11.15.1 |
| `gazebo_ros_control` | 2.9.3 |
| `diff_drive_controller` | 0.22.0 |
| `controller_manager` | 0.20.0 |
| 物理引擎 | ODE，1 ms step |

Gazebo Classic 和 ROS Noetic 已结束官方生命周期，但本项目锁定 Ubuntu 20.04 和
PX4 v1.14，因此仿真框架继续使用该组合。构建时出现相关 EOL 提示属于上游弃用警告，
不是本包编译错误。

---

## 3.1 实现文件

```text
air_ground_car_bringup/
├── config/
│   ├── chassis_params.yaml
│   └── diff_chassis_control.yaml
├── launch/
│   └── car_diff.launch
├── scripts/
│   └── test_diff.sh
├── urdf/
│   ├── car_base.urdf.xacro
│   └── diff_chassis.urdf.xacro
└── worlds/
    └── empty.world
```

`car_base.urdf.xacro` 定义可复用车体；`diff_chassis.urdf.xacro` 在其上增加差速轮、
被动支撑轮和传动。Task-04 将复用同一个车体定义。

---

## 3.2 车体模型

实测实现参数：

| 参数 | 值 |
|---|---|
| 车体长 × 宽 × 高 | 0.25 × 0.20 × 0.08 m |
| 车体质量 | 2.5 kg |
| 车体质心高度 | 0.073 m |
| 驱动轮有效半径 | 0.031 m |
| 驱动轮宽度 | 0.026 m |
| 轮距 | 0.166 m |
| 单轮质量 | 0.05 kg |
| 前后球形支撑轮半径 | 0.012 m |

模型原点位于地面，`base_link` 的碰撞体和惯量原点抬高到车体中心。这样生成高度
可以保持 `z=0`，并避免把车体几何中心误当成地面接触点。

盒体和圆柱惯量由质量与尺寸计算，不使用任意占位数值。传感器支架通过固定关节安装
在车体上方，供 Task-05 扩展。

---

## 3.3 差速轮、支撑轮与传动

左右轮使用 xacro 宏生成：

- 连续关节：`left_wheel_joint`、`right_wheel_joint`；
- 关节轴沿车体 y 方向，轮子沿车体 x 方向滚动；
- 最大关节速度 40 rad/s（覆盖 360±20 RPM 空载公差），最大 effort 5 N·m；
- 每个轮关节均配置 `SimpleTransmission`；
- 硬件接口为 `hardware_interface/VelocityJointInterface`。

前后支撑采用固定球形 caster，并设置低摩擦系数。球体无需额外自转自由度；低摩擦
接触可以提供竖直支撑，同时减少对差速转向的阻碍。

驱动轮使用较高地面摩擦，`fdir1` 指向滚动方向。模型不再加载
`libgazebo_ros_diff_drive.so`，避免它与 `diff_drive_controller` 双重控制同一车体。

Gazebo 展开后的模型包含：

```text
6 links
5 joints
2 transmissions
```

---

## 3.4 ros_control 配置

配置文件：

```text
config/diff_chassis_control.yaml
```

控制器位于 `/car` 命名空间：

- `joint_state_controller`：30 Hz 发布轮关节状态；
- `diff_drive_controller`：闭环轮速里程计和速度控制。

关键参数：

```yaml
wheel_separation: 0.166
wheel_radius: 0.031
cmd_vel_timeout: 0.5
publish_rate: 30
open_loop: false
enable_odom_tf: true
```

速度限制：

| 方向 | 最大速度 | 最大加速度 |
|---|---:|---:|
| linear x | 1.0 m/s | 2.0 m/s² |
| angular z | 3.0 rad/s | 5.0 rad/s² |

### 520 电机闭环近似

`gazebo_ros_control` 的轮速 PID 为：

```yaml
left_wheel_joint:  {p: 0.02, i: 0.0, d: 0.0, i_clamp: 0.0}
right_wheel_joint: {p: 0.02, i: 0.0, d: 0.0, i_clamp: 0.0}
```

这会将轮速误差转换为受 URDF effort 上限约束的轮端力矩，比 Gazebo 的理想
`SetVelocity` 更接近带闭环调速的 520 电机。`p=1.0` 在当前小轮和轻量车体上会造成
明显接触振荡；`p=0.02` 已通过连续前进、反向和原地转向实测。

`chassis_params.yaml` 保存差速/麦轮共用的机械元数据。520 电机编码器标称为
11 PPR，实际轮端计数还需要结合减速比和正交 x4 计数方式，不能把 11 直接当作
轮端一圈脉冲数。

---

## 3.5 Launch 拓扑

启动：

```bash
roslaunch air_ground_car_bringup car_diff.launch gui:=false
```

`car_diff.launch` 依次配置：

1. Gazebo `empty_world.launch`；
2. `/robot_description`；
3. `diff_car` 模型生成；
4. `/car/controller_manager` 下的两个控制器；
5. `/car` 命名空间的 `robot_state_publisher`；
6. 项目稳定话题 relay。

静态展开得到以下节点：

```text
/gazebo
/spawn_car
/car_controller_spawner
/car/car_robot_state_publisher
/relay_car_cmd_vel
/relay_car_odom
```

可选参数包括 `world`、`model_name`、`gui`、`paused`、`verbose` 和
`x y z R P Y`。

`headless` 参数为下游任务保留兼容入口。Gazebo Classic 11 的官方
`empty_world.launch` 明确说明该参数无实际作用；真正避免启动 `gzclient` 的参数是
`gui=false`。

控制器 spawner 自身会等待 `/car/controller_manager` 服务，无需增加固定 sleep，也
不能用 `/car/joint_states` 作为控制器启动前置条件，否则会形成等待尚未启动的
`joint_state_controller` 的循环依赖。

---

## 3.6 项目话题

ros_control 的原始话题带控制器名称。launch 使用 relay 提供稳定项目接口：

| 项目接口 | ros_control 接口 | 类型 |
|---|---|---|
| `/car/cmd_vel` | `/car/diff_drive_controller/cmd_vel` | `geometry_msgs/Twist` |
| `/car/odom` | `/car/diff_drive_controller/odom` | `nav_msgs/Odometry` |
| `/car/joint_states` | 控制器直接发布 | `sensor_msgs/JointState` |

上层任务只依赖 `/car/cmd_vel` 和 `/car/odom`，无需感知具体底盘控制器名称。该抽象
也是 Task-04 切换差速/麦轮底盘的基础。

---

## 3.7 世界文件

Task-02 使用 PX4 官方世界；车包独立维护：

```text
worlds/empty.world
```

世界包含 sun、ground plane、标准重力和 ODE 物理配置：

```xml
<max_step_size>0.001</max_step_size>
<real_time_update_rate>1000</real_time_update_rate>
```

Task-08 总装时再将空地模型合并到同一 Gazebo 实例。

---

## 3.8 构建与依赖

```bash
cd ~/air_ground_sim_ws
source /opt/ros/noetic/setup.bash
catkin build air_ground_car_bringup
source devel/setup.bash
```

直接运行依赖包括：

- `controller_manager`
- `diff_drive_controller`
- `gazebo_ros`
- `gazebo_ros_control`
- `joint_state_controller`
- `robot_state_publisher`
- `topic_tools`
- `xacro`
- 自动验收使用的 `roslaunch`、`rosnode`、`rosparam`、`rosservice`、`rostopic`
  和 `python3-yaml`

检查依赖：

```bash
rosdep check --from-paths ~/air_ground_sim_ws/src --ignore-src
```

---

## 3.9 自动验收

运行：

```bash
rosrun air_ground_car_bringup test_diff.sh
```

可覆盖默认位置和等待时间：

```bash
AIR_GROUND_WS=/path/to/air_ground_sim_ws \
TASK03_TIMEOUT_SECONDS=60 \
TASK03_COMMAND_DURATION_SECONDS=2 \
rosrun air_ground_car_bringup test_diff.sh
```

脚本验证 7 项：

1. Gazebo 中存在 `diff_car`；
2. `joint_state_controller` 和 `diff_drive_controller` 均为 `running`；
3. `/car/joint_states` 包含轮关节；
4. `/car/odom` 正常发布；
5. 0.25 m/s 指令使车体沿当前朝向前进超过 0.10 m；
6. -0.25 m/s 指令使车体沿当前朝向后退超过 0.10 m；
7. 0.8 rad/s 指令使 yaw 变化超过 0.35 rad。

运动判定读取 `/gazebo/get_model_state` 的世界真值，并在初始车体朝向上投影位移；
因此不是只检查话题存在，而是确认物理模型确实响应控制。

脚本使用独立进程组，只清理自己启动的 Gazebo、roslaunch 和 ROS master。若运行前
已有 ROS master，不会将其关闭。退出时先让 controller spawner 停止并卸载控制器，
再终止 Gazebo，避免控制器卸载与仿真退出竞态。日志保留在 `/tmp/task03-diff.*`。

为避免服务器 Conda base 污染 ROS Noetic 的 Python 3.8 环境，脚本显式重置 PATH、
Python 和 Conda 变量，并在加载官方 setup 脚本时临时关闭 `nounset`。

阶段运行时实测结果：

```text
Task-03 results: 7 passed, 0 failed
```

退出后无 ROS/Gazebo 进程、11311/11345 端口或关键错误日志残留。

---

## Task-03 完成判据

- [x] 差速车体、左右驱动轮和前后支撑轮模型完成；
- [x] 两个轮关节 transmission 与 `gazebo_ros_control` 接入完成；
- [x] `/car/cmd_vel`、`/car/odom`、`/car/joint_states` 可用；
- [x] Catkin 构建、xacro、URDF→SDF、world 和 launch 静态检查通过；
- [x] 前进、后退、转向运行时验收 7/7 通过；
- [x] 清理后无 Task-03 遗留进程、端口或关键错误；
- [x] 按 Conventional Commits 提交 Task-03。
