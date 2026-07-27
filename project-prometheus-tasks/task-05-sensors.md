# Task-05：车载传感器与二维云台

## 状态

| 项目 | 结果 |
|------|------|
| 依赖 | Task-03、Task-04 |
| 实施状态 | ✅ 已完成 |
| 完成日期 | 2026-07-27 |
| Task-05 验收 | `28 passed, 0 failed` |
| 回归验收 | Task-03 `7/7`；Task-04 `17/17` |

## 目标

为差速和麦轮底盘提供同一套车载传感器与云台，使底盘动态换模前后保持
稳定的话题、TF 和控制接口：

1. OpenMV RGB 相机与标准 optical frame；
2. 360° 二维激光雷达；
3. 前、后、左、右四个单波束超声波；
4. 车载 IMU；
5. 可限位、可在换模后恢复目标的 pan/tilt 二维云台。

## 实现结构

公共宏
[`car_sensors.urdf.xacro`](../src/air_ground_car_bringup/urdf/car_sensors.urdf.xacro)
由
[`car_base.urdf.xacro`](../src/air_ground_car_bringup/urdf/car_base.urdf.xacro)
统一实例化，因此差速、麦轮以及动态切换后重新生成的模型具有完全一致的
传感器和关节定义，不在两种底盘文件中重复维护。

所有 Gazebo ROS 插件显式使用 `/car` 命名空间。配置元数据与云台限位集中在
[`car_sensors.yaml`](../src/air_ground_car_bringup/config/car_sensors.yaml)。

### 实测配置

| 设备 | 配置 | 频率 | 量程/噪声 |
|------|------|:---:|-----------|
| OpenMV | RGB8，320×240，65° 水平视场 | 10 Hz | 0.05–30 m，图像噪声 σ=0.007 |
| 2D LiDAR | 360 点，360° | 10 Hz | 0.15–12 m，σ=0.015 m |
| 4× HC-SR04 | 单波束，前后左右 | 25 Hz | 0.02–4 m，σ=0.003 m |
| ICM42688 IMU | 角速度、线加速度、姿态 | 30 Hz | gyro σ=0.0005，accel σ=0.001 |
| 二维云台 | pan ±90°，tilt ±45° | 20 Hz 目标重发 | PositionJointInterface |

IMU 采用项目轻量化规范的 30 Hz，而不是草案中的 100 Hz。

## 公共接口

| 接口 | 消息类型 | frame |
|------|----------|-------|
| `/car/openmv/image_raw` | `sensor_msgs/Image` | `openmv_camera_optical_link` |
| `/car/openmv/camera_info` | `sensor_msgs/CameraInfo` | `openmv_camera_optical_link` |
| `/car/scan` | `sensor_msgs/LaserScan` | `lidar_link` |
| `/car/ultrasonic/front` | `sensor_msgs/LaserScan` | `ultrasonic_front_link` |
| `/car/ultrasonic/rear` | `sensor_msgs/LaserScan` | `ultrasonic_rear_link` |
| `/car/ultrasonic/left` | `sensor_msgs/LaserScan` | `ultrasonic_left_link` |
| `/car/ultrasonic/right` | `sensor_msgs/LaserScan` | `ultrasonic_right_link` |
| `/car/imu/data` | `sensor_msgs/Imu` | `imu_link` |
| `/car/gimbal/pan/command` | `std_msgs/Float64`，rad | — |
| `/car/gimbal/tilt/command` | `std_msgs/Float64`，rad | — |

超声波保留 `LaserScan` 类型，与 Task-06、Task-07 和 Task-09 的既有订阅接口
兼容。

## 云台控制与换模恢复

pan、tilt 关节分别使用
`hardware_interface/PositionJointInterface` 和
`position_controllers/JointPositionController`。差速、麦轮控制配置以及
Task-04 的换模控制器集合都包含这两个控制器。

常驻节点
[`gimbal_controller.py`](../src/air_ground_car_bringup/scripts/gimbal_controller.py)
提供稳定命令入口：

- 拒绝 NaN 和 Inf，不把非法值转发给 ros_control；
- pan 限位为 ±π/2，tilt 限位为 ±π/4；
- 以 20 Hz 重发最近有效目标；
- 底盘换模、控制器卸载并重建后，无需上层重新发送命令即可恢复姿态。

Task-04 的 `/car/swap_chassis` 服务、模型位姿保持和事务回滚协议未改变。

## 自动验收

验收脚本：
[`test_sensors.sh`](../src/air_ground_car_bringup/scripts/test_sensors.sh)

```bash
cd ~/air_ground_sim_ws
source /opt/ros/noetic/setup.bash
source devel/setup.bash
rosrun air_ground_car_bringup test_sensors.sh
```

脚本具有以下约束：

- 无 `DISPLAY` 时自动选择空闲显示号启动 Xvfb；
- 使用独立临时日志、命令超时和进程组清理；
- 在车体四向生成静态障碍物，要求 LiDAR 和每个超声波均返回有限距离；
- 校验 RGB8 图像尺寸与数据长度、CameraInfo、IMU 有限值和全部传感器 TF；
- 实际读取 `joint_states` 验证云台 ±90°/±45° 限位；
- 执行麦轮→差速→麦轮，并在每阶段重新校验传感器、控制器和云台目标恢复；
- 任何检查失败均计为 `[FAIL]`，不接受人工检查或 `[WARN]` 作为通过。

## 验证记录

2026-07-27 在 Ubuntu 20.04、ROS Noetic、Gazebo Classic 11 环境完成：

| 验证项 | 结果 |
|--------|------|
| XML、Python、Shell 静态检查 | ✅ |
| diff/mecanum Xacro 展开 | ✅ |
| diff/mecanum SDF 解析 | ✅，每套 7 个传感器插件 |
| 全工作空间 `catkin build` | ✅，5 个包 |
| `rosdep check --from-paths src --ignore-src` | ✅ |
| 云台与麦轮单元测试 | ✅，12 tests |
| Task-05 Gazebo 验收 | ✅，28 passed, 0 failed |
| Task-03 回归 | ✅，7 passed, 0 failed |
| Task-04 回归 | ✅，17 passed, 0 failed |

构建仅出现 Gazebo Classic 11 已结束上游生命周期的弃用提示，不影响本项目
锁定的 ROS Noetic/Gazebo 11 运行基线。

## 验收结论

- [x] 两类底盘默认拥有相同传感器、话题与 TF；
- [x] 所有公开话题位于 `/car` 命名空间；
- [x] 四向测距均通过实际障碍物验证；
- [x] 云台限位、非法输入和换模恢复均已自动验证；
- [x] Task-03、Task-04 无回归。
