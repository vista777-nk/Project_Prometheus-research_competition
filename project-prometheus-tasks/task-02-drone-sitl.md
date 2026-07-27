# Task-02：PX4 SITL 无人机仿真与传感器适配

## 目标

在 Ubuntu 20.04、ROS Noetic 和 Gazebo Classic 11 中运行 PX4 v1.14.0
Iris SITL，并使用 PX4 官方 `iris_depth_camera` SDF 提供 RGB、深度和点云。
MAVROS 提供 GPS 与 IMU，项目适配层将原始数据映射到 `/drone/*` 话题。

Task-02 保持可独立运行，不依赖尚未完成的 Task-03 世界文件。Task-08 总装时再将
无人机和车辆合并到同一 Gazebo 实例。

---

## 已验证环境

| 项目 | 实测值 |
|---|---|
| PX4 | v1.14.0，commit `b8c541dd7277ed735139d7d1bfb829d61fbe29fb` |
| PX4 目录 | `~/PX4-Autopilot` |
| Python 环境 | `~/air_ground_sim_ws/.venv-px4-v1.14`，Python 3.8.10 |
| Gazebo Classic | 11.15.1 |
| GStreamer | 1.16.3 |
| PX4 SITL | `build/px4_sitl_default/bin/px4` |
| Gazebo 插件 | 39 个插件已构建 |

---

## 2.1 获取 PX4 v1.14.0

阶段 A 已完成。全新环境使用以下命令：

```bash
git clone \
  --depth 1 \
  --branch v1.14.0 \
  --recurse-submodules \
  --shallow-submodules \
  https://github.com/PX4/PX4-Autopilot.git \
  ~/PX4-Autopilot
```

验证主仓库和所有递归子模块：

```bash
git -C ~/PX4-Autopilot rev-parse HEAD
git -C ~/PX4-Autopilot submodule status --recursive
```

`submodule status` 输出不得以 `-`（未初始化）或 `+`（提交不匹配）开头。

---

## 2.2 安装 SITL 最小依赖

阶段 B 已完成。无需安装 NuttX/ARM 工具链、jMAVSim、静态检查器等完整开发依赖。

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  ninja-build \
  libgstreamer-plugins-base1.0-dev \
  python3-venv \
  xvfb
```

创建与 Conda、用户 site-packages 隔离的 PX4 Python 环境：

```bash
/usr/bin/python3 -m venv --system-site-packages \
  ~/air_ground_sim_ws/.venv-px4-v1.14

export PYTHONNOUSERSITE=1
~/air_ground_sim_ws/.venv-px4-v1.14/bin/python -m pip install \
  -r ~/PX4-Autopilot/Tools/setup/requirements.txt

# PX4 v1.14 需要 Empy 3.x；NumPy 1.24.4 解决 Python 3.8 下
# 系统 NumPy 1.17.4 与 Pandas 2.0.3 的依赖冲突。
~/air_ground_sim_ws/.venv-px4-v1.14/bin/python -m pip install --upgrade \
  empy==3.3.4 \
  numpy==1.24.4
```

安装 MAVROS 使用的 GeographicLib 数据集：

```bash
sudo /opt/ros/noetic/lib/mavros/install_geographiclib_datasets.sh
```

阶段 B 验收结果：

- 官方 requirements 共 29 项，全部满足；
- 28 个关键模块导入通过；
- Empy 3.3.4、NumPy 1.24.4；
- GeographicLib 的 geoid、gravity 和 magnetic 数据均存在。

Ubuntu 全局 `launchpadlib` 缺少可选 `testresources` 的 `pip check` 告警与 PX4
构建无关。

`xvfb` 是无物理显示器环境中的运行时依赖。`gui=false` 只是不启动 `gzclient`；
Gazebo Classic 的深度相机仍需 X 渲染上下文。若 `DISPLAY` 为空且未安装 Xvfb，
Gazebo 会报告 `Unable to create DepthCameraSensor. Rendering is disabled.`。

---

## 2.3 首次编译（不启动仿真）

阶段 C 已完成。

```bash
cd ~/PX4-Autopilot
export PYTHONNOUSERSITE=1
export PATH=~/air_ground_sim_ws/.venv-px4-v1.14/bin:$PATH
unset PYTHONHOME CONDA_PREFIX CONDA_DEFAULT_ENV
source /opt/ros/noetic/setup.bash
DONT_RUN=1 make px4_sitl_default gazebo-classic
```

`DONT_RUN=1` 只编译 PX4 和 Gazebo Classic 插件，不启动 PX4、Gazebo 或 MAVROS。

### 浅克隆的 NuttX 标签修复

PX4 v1.14 的版本头生成脚本会读取 NuttX 的 `nuttx-x.y.z` 标签。若浅克隆报错：

```text
px_update_git_header.py ... IndexError: list index out of range
```

只需补取 NuttX 子模块标签，不必展开完整提交历史：

```bash
git -C ~/PX4-Autopilot/platforms/nuttx/NuttX/nuttx \
  fetch --tags --force --depth=1 origin
```

随后重新运行构建命令，Ninja 会复用已完成的对象。本机补取后仍保持 PX4 锁定的
NuttX commit `de41e7feaeffaec3ce65327e9569e8fdb553ca3d`。

### 构建产物验收

```bash
test -x ~/PX4-Autopilot/build/px4_sitl_default/bin/px4
test -f ~/PX4-Autopilot/build/px4_sitl_default/build_gazebo-classic/libgazebo_mavlink_interface.so
test -f ~/PX4-Autopilot/Tools/simulation/gazebo-classic/sitl_gazebo-classic/models/depth_camera/depth_camera.sdf
```

---

## 2.4 每个新终端加载 PX4/Gazebo 环境

```bash
source /usr/share/gazebo/setup.sh
source /opt/ros/noetic/setup.bash
source ~/air_ground_sim_ws/devel/setup.bash

source ~/PX4-Autopilot/Tools/simulation/gazebo-classic/setup_gazebo.bash \
  ~/PX4-Autopilot \
  ~/PX4-Autopilot/build/px4_sitl_default

export ROS_PACKAGE_PATH=$ROS_PACKAGE_PATH:~/PX4-Autopilot
export ROS_PACKAGE_PATH=$ROS_PACKAGE_PATH:~/PX4-Autopilot/Tools/simulation/gazebo-classic/sitl_gazebo-classic
```

`/usr/share/gazebo/setup.sh` 会加入 Gazebo Classic 的系统插件目录
`/usr/lib/x86_64-linux-gnu/gazebo-11/plugins`。这是 ROS OpenNI 相机插件解析
`libDepthCameraPlugin.so` 所必需的。其余步骤使 ROS 能找到 `px4`、
`mavlink_sitl_gazebo`，并使 Gazebo 能找到 PX4 模型与插件。

---

## 2.5 官方模型与正确 airframe 策略

PX4 v1.14 已内置：

```text
Tools/simulation/gazebo-classic/sitl_gazebo-classic/models/
├── iris/
├── depth_camera/
└── iris_depth_camera/
```

`iris_depth_camera.sdf` 组合 `model://iris` 和 `model://depth_camera`，无需维护
自定义模型。

但 v1.14 不存在 `gazebo-classic_iris_depth_camera` airframe。PX4 的
`posix_sitl.launch` 会设置：

```text
PX4_SIM_MODEL=gazebo-classic_<vehicle>
```

因此 `vehicle:=iris_depth_camera` 会导致 `Unknown model`。正确组合是：

- `vehicle:=iris`：使用官方 Iris airframe；
- `sdf:=.../models/iris_depth_camera/iris_depth_camera.sdf`：用完整路径覆盖模型。

`sdf` 参数是文件路径，不是模型目录名。

---

## 2.6 启动文件

实现文件：

- `air_ground_drone_bringup/launch/drone_sitl.launch`
- `air_ground_drone_bringup/launch/drone_sensors.launch`

`drone_sitl.launch`：

- 使用 `vehicle=iris`；
- 使用官方 `iris_depth_camera.sdf` 完整路径；
- 使用 PX4 自带 `worlds/empty.world`；
- 默认 `gui=false`，不启动 `gzclient`；
- 默认从地面 `z=0` 生成，避免未解锁飞机从 5 m 坠落；
- 只 include 一次 `mavros_posix_sitl.launch`，避免 MAVROS 和 UDP 端口重复；
- 默认同时 include `drone_sensors.launch`。

官方 launch 不提供 `headless` 参数。无渲染运行只需：

```bash
roslaunch air_ground_drone_bringup drone_sitl.launch gui:=false
```

可选参数包括 `x y z R P Y`、`paused`、`verbose`、`interactive`、
`respawn_gazebo`、`respawn_mavros`、`fcu_url` 和 `start_sensors`。

---

## 2.7 传感器与项目话题

PX4 官方深度相机实测参数：

| 参数 | 值 |
|---|---|
| 分辨率 | 848 × 480 |
| 更新率 | 10 Hz |
| 水平视场 | 1.5009831567 rad，约 86° |
| 裁剪范围 | 0.001–65.535 m |
| frame | `camera_link` |

`drone_sensors.launch` 提供以下适配：

| 来源 | 项目侧输出 | 处理 |
|---|---|---|
| `/iris/camera/rgb/image_raw` | `/drone/camera/rgb/image_raw` | relay |
| `/iris/camera/depth/image_raw` | `/drone/camera/depth/image_raw` | relay |
| `/iris/camera/depth/points` | `/drone/camera/depth/points_downsampled` | PCL VoxelGrid，5 cm |
| `/mavros/global_position/global` | `/drone/gps/local_pose` | WGS84 → 局部 ENU |

VoxelGrid 使用独立 `drone_pcl_manager` nodelet manager，过滤 z=0.2–8.0 m。
配置位于 `config/drone_sensors.yaml`，由 VoxelGrid 和 GPS 转换节点分别加载到
各自的私有参数空间；其中 `leaf_size`、`filter_field_name` 和过滤上下限直接驱动
PCL nodelet，`gps` 子树驱动 ENU 转换节点。

官方 GPS 模型更新率是 5 Hz。IMU 使用 MAVROS 的 `/mavros/imu/data`。

relay 不修改消息中的相机 frame，仍为官方 `camera_link`；后续多机器人 TF 统一在
Task-08 处理。PX4 v1.14 官方组合模型以生成模型名 `iris` 作为
`robotNamespace`，因此相机源话题实际位于 `/iris/camera/*`。

---

## 2.8 GPS 到局部 ENU

实现文件：

```text
air_ground_drone_bringup/scripts/gps_converter.py
```

节点读取以下私有参数：

- `~gps/home_lat`、`~gps/home_lon`、`~gps/home_alt`；
- `~gps/local_frame_id`；
- `~input_topic`、`~output_topic`。

默认 home 与 PX4 Gazebo 世界一致：

```yaml
home_lat: 47.397742
home_lon: 8.545594
home_alt: 488.0
```

转换适用于 home 附近的小范围仿真：

- x：east；
- y：north；
- z：up；
- `NavSatStatus` 未取得定位时不发布 Pose；
- 非有限 GPS 数值会被拒绝，不发布无效 Pose；
- 输出姿态使用单位四元数。

---

## 2.9 构建工作空间

```bash
cd ~/air_ground_sim_ws
source /opt/ros/noetic/setup.bash
catkin config \
  --extend /opt/ros/noetic \
  --cmake-args -DPYTHON_EXECUTABLE=/usr/bin/python3
catkin build
source devel/setup.bash
```

`CMakeLists.txt` 使用 `catkin_install_python` 安装 GPS 节点，并将验证脚本安装到
包的可执行目录。`test/test_gps_converter.py` 覆盖原点、ENU 轴方向、默认比例尺和
非有限输入，同时验证无定位拒绝与消息 Header 隔离，可通过 Catkin 测试目标重复执行。

阶段 D 实测 Catkin 构建无警告；Catkin 测试共 6 项，0 错误、0 失败。

运行依赖：

- `gazebo_ros`
- `gazebo_plugins`
- `mavros`
- `nodelet`
- `pcl_ros`
- `roslaunch`
- `rosparam`
- `rostopic`
- `topic_tools`
- `xvfb`（无物理显示器时提供虚拟 X 渲染上下文）

---

## 2.10 自动验收脚本

脚本：

```text
air_ground_drone_bringup/scripts/test_drone.sh
```

运行：

```bash
rosrun air_ground_drone_bringup test_drone.sh
```

可通过环境变量覆盖默认位置或等待时间：

```bash
PX4_AUTOPILOT_DIR=/path/to/PX4-Autopilot \
AIR_GROUND_WS=/path/to/air_ground_sim_ws \
TASK02_TIMEOUT_SECONDS=90 \
rosrun air_ground_drone_bringup test_drone.sh
```

脚本会验证：

1. `/mavros/state` 中 `connected: True`；
2. 官方 RGB 和深度图像均为 480 行；
3. 项目侧 RGB 和深度 relay 均为 480 行；
4. MAVROS GPS 和 IMU；
5. `/drone/gps/local_pose`；
6. 降采样点云。

各项检查使用 `rostopic echo --noarr` 订阅真实基础话题：这会触发相机和 PCL
nodelet 的惰性发布，同时不把图像或点云数组序列化到 Shell 内存。

脚本不会调用 `rosnode kill -a`。它使用独立进程组，只停止自己启动的 roslaunch、
PX4 和 Gazebo；若运行前已有 ROS master，也不会将其关闭。日志保留在 `/tmp` 并在
退出时打印具体目录。自动验收显式传入 `interactive:=false`，避免 PX4 shell 等待
终端输入；收到 `SIGINT` 或 `SIGTERM` 时会转换为退出状态，再由唯一的 EXIT 清理器
回收本次测试进程组。若 `DISPLAY` 为空，脚本会自动启动一个使用 Mesa 软件渲染的
Xvfb，并在测试结束时一并清理；不会启动 `gzclient` 或打开可见窗口。
脚本还会加载 Gazebo 的系统环境并预检 OpenNI 深度相机动态库，避免系统插件目录
未进入 `LD_LIBRARY_PATH` 时相机节点静默缺失。

阶段 E 无头实测 9 项全部通过，0 项失败；退出后 PX4、Gazebo、ROS、Xvfb
相关进程和测试端口均无残留。

---

## Task-02 完成判据

- [x] PX4 v1.14.0 与递归子模块准备完成；
- [x] SITL 最小系统依赖、Python requirements、GeographicLib 数据完成；
- [x] `px4_sitl_default gazebo-classic` 构建通过；
- [x] PX4 二进制、Gazebo 插件、ROS 深度相机 SDF 静态验收通过；
- [x] `air_ground_drone_bringup` Catkin 构建和 launch 静态检查通过；
- [x] 无头运行时 MAVROS、RGB、Depth、GPS、IMU、ENU Pose、点云全部通过；
- [x] 清理后无 Task-02 遗留进程；
- [x] 按 Conventional Commits 提交 Task-02。
