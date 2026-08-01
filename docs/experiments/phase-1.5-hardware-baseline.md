# Phase 1.5 实机硬件基线（滚动记录）

> 本文件记录已经由项目负责人确认、但仍会随电子组参数继续补充的实机事实。
> 它不是不可变 ADR；接口与传感器型号全部确认后，再把需要长期冻结的决策写入 ADR。

## 1. Raspberry Pi 边缘计算基线

2026-08-01 收到一台代表性设备的实测信息。车机与无人机允许有不影响部署的
小差异，但交付验收以本表为下限：

| 项目 | 确认基线 |
|---|---|
| 主板 | Raspberry Pi 5 Model B Rev 1.1 |
| 架构 | AArch64 / ARM64，4 核 Cortex-A76，最高约 2.4 GHz |
| 内存 | 8 GB |
| 宿主系统 | Debian GNU/Linux 13（Trixie），64 位 |
| 内核实测 | `6.18.34+rpt-rpi-2712` |
| 系统盘 | **标称 64 GB microSD**；当前用于采样的 32 GB 卡不属于交付基线，将更换 |
| 边缘运行时 | Docker Engine + Compose；ROS Noetic 保持在 Ubuntu 20.04/Focal ARM64 容器内 |

兼容依据：Docker 官方安装文档列出 Debian 13（Trixie）与 `arm64`；ROS REP-3
把 Noetic 的目标平台锁定为 Ubuntu 20.04（Focal），生命周期为 2020-05 至
2025-05。因此升级 Pi 宿主系统不等于升级 ROS 用户空间，容器边界必须保留。

不把设备序列号、MAC 地址、当前校园网 IP 写入仓库。这些值既不是可复用配置，
又会造成不必要的设备识别信息泄露。

## 2. 本次采样状态（不是永久规格）

- 可用内存约 5.7 GiB，2 GiB zram 未使用；容量满足边缘节点基线。
- 温度 62.6°C、CPU 约 2.4 GHz，`throttled=0x0`；这只是开机 51 分钟、
  负载约 1.1 时的单点数据，不能代替装车后的满载热测试。
- 当前 32 GB 卡根分区约 29 GiB、已用约 21%；换 64 GB 卡后重新测量。
- Wi-Fi 已联网，采样时以太网无载波且地址由 DHCP 分配。最终实验网静态地址仍待
  两校区网络方案确认，不把本次临时地址固化进配置。

## 3. 已发现的部署前缺口

1. `/boot/firmware/config.txt` 中 `dtparam=i2c_arm=on` 仍被注释，当前只有
   `/dev/i2c-13`、`/dev/i2c-14`，没有车机配置要求的 `/dev/i2c-1`。
2. 当前只观察到 `/dev/ttyAMA10`。Pi 5 上的 UART 设备号必须在电子组给出最终
   接线后，通过 `/dev/serial*`、设备树 overlay 和实机回环测试确认；现阶段不把
   `/dev/ttyAMA0` 或 `/dev/ttyAMA1` 当成既成事实。
3. SPI 尚未启用；现有确认设备不依赖 SPI，因此等后续硬件清单到齐再决定。
4. `vcgencmd get_camera` 返回 “Command not registered” 不能证明相机不可用。
   项目当前计划的 D435i 走 USB、OpenMV 走串口；CSI 相机若加入清单，再用当前
   Raspberry Pi 相机栈的枚举工具单独验收。
5. 64 GB 新卡烧录时应创建 UID 1000 的 `airground` 用户。现有 systemd 单元和
   容器 bind mount 以这个身份为权限边界，换用别的 UID 会导致日志与设备权限错配。

## 4. 换卡后的两阶段预检

```bash
# 刚烧完 64 GB 卡：核对板型、架构、系统、内存和实际分区容量
python3 src/deployment/healthcheck/check_pi_host.py --stage base

# Docker 与接口配置完成后（另一台 Pi 用 --role drone）
python3 src/deployment/healthcheck/check_pi_host.py --stage deploy --role car
```

`base` 阶段故意不要求 Docker，方便区分“镜像/硬件基线错误”和“软件尚未安装”。
`deploy` 阶段才检查 Docker/Compose；车机还要求 `/dev/i2c-1`。传感器稳定名、
UART 映射、相机、雷达、飞控和数传验收将在后续参数到齐后追加。

## 5. 仍待电子组/机械组确认

- 两台 Pi 的供电模块额定与瞬态能力、散热器/风扇及安装风道；
- 车机与无人机各 UART 的物理引脚、设备树 overlay、波特率和用途；
- 传感器最终型号、总线、地址、VID/PID/序列号与供电电压；
- 相机、雷达、飞控、数传的实际连接方式与稳定设备名；
- 64 GB 卡型号、耐久等级，以及是否准备可直接替换的克隆备份卡；
- 梁乡实验网与中关村服务器之间的受控隧道地址和断线恢复策略。

## 6. 参考

- [Docker Engine on Debian](https://docs.docker.com/engine/install/debian/)
- [ROS REP-3 Target Platforms](https://www.ros.org/reps/rep-0003.html)
