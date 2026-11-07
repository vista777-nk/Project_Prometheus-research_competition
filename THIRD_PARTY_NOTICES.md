# 第三方组件与许可声明（Third-Party Notices）

> 本文件列出项目使用或参考的主要第三方组件及其许可，供合规审查与再分发时参考。
> **它不是法律意见**；确切条款以各项目官方仓库与发行版中的许可文件为准。

## 运行时与工具链依赖

| 组件 | 用途 | 许可 |
|------|------|------|
| [PX4-Autopilot](https://github.com/PX4/PX4-Autopilot) | 无人机飞控固件与 SITL | BSD-3-Clause |
| [ROS Noetic](https://wiki.ros.org/noetic)（`ros_comm`、`catkin` 等） | 机器人中间件 | BSD-3-Clause |
| [Gazebo Classic 11](https://classic.gazebosim.org/) | 物理仿真 | Apache-2.0 |
| [MAVROS](https://github.com/mavlink/mavros) | MAVLink ↔ ROS 桥 | BSD-3-Clause |
| [MAVLink](https://github.com/mavlink/mavlink) / [pymavlink](https://github.com/ArduPilot/pymavlink) | 飞控通信协议与 Python 实现 | LGPL-3.0 |
| [OpenCV](https://opencv.org/)（`opencv-python-headless`） | 图像处理与标定 | Apache-2.0 |
| [NumPy](https://numpy.org/) | 数值计算 | BSD-3-Clause |
| [PyYAML](https://pyyaml.org/) | YAML 配置解析 | MIT |
| [Pillow](https://python-pillow.org/) | 图像读写 | HPND（PIL 风格许可） |
| [pyserial](https://github.com/pyserial/pyserial) | 串口访问 | BSD-3-Clause |
| [catkin_tools](https://catkin-tools.readthedocs.io/) | 工作空间构建 | Apache-2.0 |
| Ubuntu 20.04 基础镜像（`ros:noetic-ros-base-focal`） | 边缘容器基础镜像 | 各软件包分别适用其自身许可 |

> `opencv-python-headless` 的 wheel 内含 FFmpeg 等第三方库，其许可随 wheel 分发，
> 详见该发行包附带的 `LICENSE` 与 `THIRD_PARTY_NOTICES`。

## 固件与厂商资料

本仓库的 `src/firmware/` 为自研代码，不包含 ST/TI 的 HAL 或 SDK 源码。
以下目录收录厂商公开提供的**参考资料、数据手册、示例代码与工具说明**：

- `imu-icm42688/`
- `mspm0g3507/`
- `openmv-visual-module/`
- `r5-general-chassis-panel-specifications/`
- `stm32f407vet6/`
- `two-axis-gimbal/`

这些资料的版权归各自厂商所有，**不在本项目的 MIT 许可范围内**；仅作学习与硬件集成参考，
不得用于再分发或商业用途（除非获得厂商授权）。子目录的中文名称与数字前缀保留厂商原始组织方式。

## 硬件与器件

文中提及的 MC520P30、DRV8871、ICM42688、RPLIDAR A2M12、OpenMV H7 Plus、Pixhawk 6C、
M9N、A2212、BL32 等型号为其各自厂商的商标或产品名称，仅用于说明兼容性与接线方式。

## 本项目许可

本项目自身代码与文档以 [MIT License](./LICENSE) 发布，除非文件头另有说明。
