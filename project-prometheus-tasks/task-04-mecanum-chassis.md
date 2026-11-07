# Task-04：麦轮底盘仿真与模块化切换

## 目标

在 Task-03 差速底盘基础上实现可横移的四轮麦克纳姆底盘，并提供运行时底盘切换：

- `/car/cmd_vel` 支持前进、横移和转向；
- 四个轮关节由独立 `JointVelocityController` 驱动；
- `/car/odom`、`/car/joint_states` 和 TF 保持可用；
- `/car/swap_chassis` 在 `diff` 与 `mecanum` 之间事务化切换；
- 切换前后保持世界位姿和稳定的上层 ROS 接口；
- 自动验收覆盖运动、控制器状态、双向切换和回切恢复。

---

## 已验证环境

| 项目 | 实测值 |
|---|---|
| Ubuntu | 20.04 |
| ROS | Noetic 1.17.4 |
| Gazebo Classic | 11.15.1 |
| `gazebo_ros_control` | 2.9.3 |
| `ros_controllers` | 0.22.0 |
| 物理引擎 | ODE，1 ms step |

Gazebo Classic 和 ROS Noetic 已结束官方生命周期，但项目为匹配 PX4 v1.14 固定使用
该组合。构建中的 EOL 信息是上游弃用警告，不是本包编译失败。

---

## 4.1 实现文件

```text
air_ground_car_bringup/
├── config/
│   └── mecanum_chassis_control.yaml
├── launch/
│   └── car_mecanum.launch
├── scripts/
│   ├── chassis_swapper.py
│   ├── mecanum_controller.py
│   └── test_mecanum.sh
├── src/
│   └── mecanum_planar_drive_plugin.cpp
├── test/
│   └── test_mecanum_controller.py
└── urdf/
    └── mecanum_chassis.urdf.xacro
```

底盘复用 Task-03 的 `car_base.urdf.xacro`、`diff_chassis.urdf.xacro`、
`diff_chassis_control.yaml` 和 `empty.world`。

---

## 4.2 麦轮模型

实测模型参数：

| 参数 | 值 |
|---|---:|
| 有效轮半径 | 0.0395 m |
| 轮宽 | 0.026 m |
| 单轮质量 | 0.05 kg |
| 前后轮轴中心距 | 0.124 m |
| 左右轮中心距 | 0.166 m |
| 最大关节速度 | 40 rad/s（覆盖 360±20 RPM 空载公差） |
| 最大关节 effort | 5 N·m |

四轮名称固定为：

```text
front_left_wheel_joint
front_right_wheel_joint
rear_left_wheel_joint
rear_right_wheel_joint
```

每个轮关节均为连续关节，并具有
`hardware_interface/VelocityJointInterface` 的 `SimpleTransmission`。
轮子惯量由质量和圆柱尺寸计算，不使用占位惯量。

### X/O 辊子接触近似

URDF 使用 `<gazebo reference="...">` 设置 `mu1`、`mu2` 和 `fdir1`。四轮的
`fdir1` 按 X/O 排列交替，次摩擦方向设置为低摩擦，用于近似自由滚子。

单个各向异性圆柱无法在轮体旋转的所有相位准确表示独立辊子。仅依赖该接触近似时，
Gazebo 的横移量会随轮角变化，不能形成可重复验收。因此模型还加载项目内的
`libmecanum_planar_drive_plugin.so`：

- 插件接收由四轮受限转速正解得到的 `/car/mecanum_cmd_vel`；
- 将机体系 `vx`、`vy` 和 `wz` 转换为世界系模型速度；
- 保留 z、roll、pitch 方向的物理速度；
- 0.5 s 无命令后自动将平面速度清零；
- ROS 回调队列只在 Gazebo 更新线程中处理，不创建后台线程，模型删除时可安全卸载。

`gazebo_ros_control` 仍实际驱动四个轮关节，平面插件只提供麦轮辊子接触的确定性运动
近似。

---

## 4.3 逆运动学与限速

`mecanum_controller.py` 订阅 `/car/cmd_vel`。轮序为 FL、FR、RL、RR，令：

```text
k = (wheel_base + track_width) / 2

w_fl = (vx - vy - k*wz) / radius
w_fr = (vx + vy + k*wz) / radius
w_rl = (vx + vy - k*wz) / radius
w_rr = (vx - vy + k*wz) / radius
```

控制器特性：

- 检查几何参数、指令和限速是否为有限数；
- 最大轮速由 `max_rpm=360` 换算；
- 超限时按同一比例缩放全部轮速，保持运动方向和曲率；
- 同时发布四个轮速命令；
- 用正运动学从受限轮速重建机体速度，再发布 `/car/mecanum_cmd_vel`；
- `/car/cmd_vel` 超时 0.5 s 自动停车；
- 节点退出时发布四轮和机体零速度。

正运动学和逆运动学是独立纯函数，便于单元测试。单测覆盖三种基本运动、正逆解往返、
比例限速、非法几何和非有限输入，共 7 项。

---

## 4.4 控制器与里程计

`mecanum_chassis_control.yaml` 在 `/car` 下配置：

- `joint_state_controller`；
- `front_left_wheel_controller`；
- `front_right_wheel_controller`；
- `rear_left_wheel_controller`；
- `rear_right_wheel_controller`。

四个轮速 PID 位于 `gazebo_ros_control/pid_gains`，均为：

```yaml
{p: 0.02, i: 0.0, d: 0.0, i_clamp: 0.0}
```

该增益与 Task-03 的 effort 受限轮速控制处于同一稳定区间。较大比例增益会使轻量轮组
进入 effort 饱和振荡，因此未采用原草案中的 `p=1.0`。

麦轮运行时，常驻控制器从 `/gazebo/model_states` 读取 `mecanum_car` 的真实世界
位姿和速度：

- 发布 `/car/odom`；
- 将世界系线速度转换为机体系速度；
- 发布 `odom -> base_link` TF；
- 麦轮模型不存在时停止发布。

切换到差速底盘后，Task-03 的 `diff_drive_controller` 重新接管 `/car/odom` 和 TF。
因此两类底盘不会同时发布相同里程计接口。

---

## 4.5 Launch 拓扑

启动：

```bash
roslaunch air_ground_car_bringup car_mecanum.launch \
  gui:=false headless:=true
```

静态节点：

```text
/gazebo
/spawn_mecanum_car
/mecanum_controller
/chassis_swapper
/relay_car_cmd_vel_diff
/relay_car_odom_diff
```

启动流程：

1. 启动 Gazebo `empty.world`；
2. 展开并加载麦轮 URDF；
3. 加载麦轮 ros_control 参数；
4. 生成 `mecanum_car`；
5. `chassis_swapper` 等待新控制器管理器后加载并启动五个控制器；
6. 启动受切换节点管理的 `/car/car_robot_state_publisher`。

可选参数包括 `world`、`model_name`、`gui`、`paused`、`verbose` 和
`x y z R P Y`。`headless` 仅保留 Gazebo Classic 兼容入口，实际关闭 GUI 使用
`gui=false`。

两个 diff relay 在麦轮阶段没有上游数据；切换到差速底盘后自动恢复统一接口：

```text
/car/cmd_vel -> /car/diff_drive_controller/cmd_vel
/car/diff_drive_controller/odom -> /car/odom
```

---

## 4.6 事务化底盘切换

服务定义沿用 Task-01：

```text
/car/swap_chassis
air_ground_interfaces/SwapChassis
```

请求值只接受 `diff` 或 `mecanum`。切换事务顺序：

1. 获取当前模型世界位姿；
2. 停止并卸载当前控制器；
3. 停止当前 `robot_state_publisher`；
4. 删除当前模型，并确认旧 `/car/controller_manager` 已注销；
5. 加载目标控制器 YAML 和目标 URDF；
6. 在保存位姿生成目标模型；
7. 等待新 `/car/controller_manager`；
8. 加载、启动并核验目标控制器；
9. 重启 `robot_state_publisher`；
10. 更新 `/car/current_chassis`。

控制器必须在删除模型前卸载，因为 `/car/controller_manager` 属于模型内的
`gazebo_ros_control` 插件；先删模型会使卸载服务在调用中消失。

切换节点使用带类型的 ROS 服务客户端和 Python xacro API，不使用 `shell=True`，
也不硬编码 `~/air_ground_sim_ws`。切换失败时会删除不完整目标模型并尝试恢复原底盘。
同一目标的重复请求按幂等成功处理，切换过程由互斥锁串行化。

示例：

```bash
rosservice call /car/swap_chassis \
  "{target_chassis: 'diff'}"

rosservice call /car/swap_chassis \
  "{target_chassis: 'mecanum'}"
```

---

## 4.7 稳定 ROS 接口

| 接口 | 类型 | 说明 |
|---|---|---|
| `/car/cmd_vel` | `geometry_msgs/Twist` | 两类底盘统一速度入口 |
| `/car/odom` | `nav_msgs/Odometry` | 当前底盘里程计 |
| `/car/joint_states` | `sensor_msgs/JointState` | 当前底盘关节状态 |
| `/car/swap_chassis` | `air_ground_interfaces/SwapChassis` | 动态切换 |
| `/car/current_chassis` | ROS 参数 | `diff` 或 `mecanum` |

上层节点无需知道当前底盘的控制器名称。

---

## 4.8 构建与依赖

```bash
cd ~/air_ground_sim_ws
source /opt/ros/noetic/setup.bash
catkin build air_ground_car_bringup
source devel/setup.bash
```

Task-04 新增的直接依赖包括：

- `controller_manager_msgs`
- `gazebo_dev`
- `gazebo_msgs`
- `roscpp`
- `tf2_ros`
- `velocity_controllers`
- 测试依赖 `python3-nose`

检查：

```bash
rosdep check --from-paths ~/air_ground_sim_ws/src --ignore-src
```

---

## 4.9 自动验收

运行：

```bash
cd ~/air_ground_sim_ws
source devel/setup.bash
rosrun air_ground_car_bringup test_mecanum.sh
```

脚本具有以下安全措施：

- 使用独立临时日志目录；
- 检测并拒绝复用已有 Gazebo 或 `/car/controller_manager`；
- 所有等待、服务和话题采样均有超时；
- 清理时先停止切换节点，使控制器有机会正常卸载；
- 之后按进程组停止 roslaunch 和脚本自行启动的 roscore；
- 不按名称全局杀死用户已有 ROS/Gazebo 进程。

自动检查 17 项：

1. 麦轮模型生成；
2. 五个麦轮控制器运行；
3. 四轮 joint states；
4. 麦轮 odom；
5. 前进世界位移；
6. 横向世界位移；
7. 世界偏航变化；
8. 麦轮切换到差速；
9. 差速模型生成；
10. 差速控制器运行；
11. 切换到差速时位姿保持；
12. 统一 `/car/cmd_vel` 驱动差速底盘；
13. 差速切回麦轮；
14. 麦轮模型恢复；
15. 麦轮控制器恢复；
16. 切回麦轮时位姿保持；
17. 往返切换后横移恢复。

实测结果：

```text
Task-04 results: 17 passed, 0 failed
```

运动学单元测试：

```text
Summary: 7 tests, 0 errors, 0 failures, 0 skipped
```

---

## 交付状态

- [x] 四轮麦轮 URDF、传动和控制器配置完成
- [x] 逆/正运动学、比例限速和超时停车完成
- [x] 可安全卸载的确定性平面运动插件完成
- [x] `/car/odom`、TF 和 `/car/joint_states` 可用
- [x] `diff` / `mecanum` 事务化双向切换完成
- [x] 世界位姿在切换中保持
- [x] 构建与 7 项单元测试通过
- [x] Gazebo 17 项运行时验收通过
