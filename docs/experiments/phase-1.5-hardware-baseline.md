# Phase 1.5 实机硬件基线（滚动记录）

> 本文件是 BOM、模块边界和上机缺口的滚动权威表。长期边界已经由
> [ADR-0018](../decisions/ADR-0018.md) 冻结；尚缺的机械尺寸、pinmux 和电气参数
> 继续在这里补充，不能用常见型号参数代填。

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

## 2. 确认 BOM

| 类别 | 型号与数量 |
|---|---|
| 计算与主控 | Raspberry Pi 5 8 GB ×2；64 GB microSD ×2；MSPM0G3507 开发板 ×1；STM32F407VET6 最小系统板 ×1 |
| 差速底盘 | R5 标准板尺寸套件；65 mm 橡胶轮 ×2；MC520P30 12 V 编码电机 ×2 |
| 麦轮底盘 | R5 标准板尺寸套件；80 mm 麦克纳姆轮 ×4；MC520P30 12 V 编码电机 ×4 |
| 电机驱动 | DRV8871 单 H 桥驱动板 ×6，每个电机一片 |
| 车载感知 | ICM42688 ×1；HC-SR04 ×8；RPLIDAR A2M12 ×1；二维云台 ×1；OpenMV ×1 |
| 无人机视觉 | Intel RealSense D435i CB ×1，或 Raspberry Pi Camera ×2 |
| 无人机本体 | Pixhawk 6C + PM07 + M9N ×1 套；F450 延长脚架 ×1；915 MHz/500 mW 数传一对；A2212 980KV ×4；BL32 30A ESC ×4；1045 自锁桨与保护罩若干 |
| 遥控 | Flysky i6 + IA6B ×3 套 |
| 供电/耗材 | 9–24 V 转 5 V/5 A ×3；匹配电池、线材、面包板与工具若干 |

## 3. 五个物理模块与所有权

| 模块 | 组成 | 硬实时/接口所有者 |
|---|---|---|
| 共享车载智能载荷 | Pi 5 + ICM42688 + A2M12 + 二维云台/OpenMV | Pi 管感知；A2M12 256000 bps；一套载荷在两底盘间换装 |
| 差速控制模块 | 差速底盘 + MSPM0 + DRV8871 ×2 + HC-SR04 ×4 + IA6B + 5 V/5 A | MSPM0 管电机、编码器、RC、超声波和安全；Pi 只走 `/dev/mcu` |
| 麦轮控制模块 | 麦轮底盘 + STM32F407 + DRV8871 ×4 + HC-SR04 ×4 + IA6B + 5 V/5 A | STM32 管电机、编码器、RC、超声波和安全；Pi 只走 `/dev/mcu` |
| 无人机视觉载荷 | D435i CB，或双 Raspberry Pi Camera | `DRONE_VISION` 选择；D435i 路径可部署，双 CSI 待型号/profile |
| 无人机本体 | Pi 5 + IA6B + Pixhawk 全套 + F450 + 数传 + 动力 + 5 V/5 A | Pixhawk 管飞行与 RC；TELEM1 接空中电台，TELEM2 接 Pi；Pi 独立供电 |

共享载荷只有一套，因此“切换底盘”是断电后的机械/电气换装，不是运行时自动机构。
两套底盘控制模块可以分别台架测试，但不能同时构成两台完整感知车。

## 4. 本次采样状态（不是永久规格）

- 可用内存约 5.7 GiB，2 GiB zram 未使用；容量满足边缘节点基线。
- 温度 62.6°C、CPU 约 2.4 GHz，`throttled=0x0`；这只是开机 51 分钟、
  负载约 1.1 时的单点数据，不能代替装车后的满载热测试。
- 当前 32 GB 卡根分区约 29 GiB、已用约 21%；换 64 GB 卡后重新测量。
- Wi-Fi 已联网，采样时以太网无载波且地址由 DHCP 分配。最终实验网静态地址仍待
  两校区网络方案确认，不把本次临时地址固化进配置。

## 5. 已发现的部署前缺口

1. `/boot/firmware/config.txt` 中 `dtparam=i2c_arm=on` 仍被注释，当前只有
   `/dev/i2c-13`、`/dev/i2c-14`，没有车机配置要求的 `/dev/i2c-1`。
2. 当前只观察到 `/dev/ttyAMA10`，它是 Pi 5 的 3 针 Debug UART，不是 40 针
   GPIO14/15。生产接线若使用 40 针排针，需配置 `dtoverlay=uart0-pi5`，重启后
   验收 `/dev/ttyAMA0`；旧计划的 `/dev/ttyAMA1` 已废止。
3. SPI 尚未启用；现有确认设备不依赖 SPI，因此等后续硬件清单到齐再决定。
4. `vcgencmd get_camera` 返回 “Command not registered” 不能证明相机不可用。
   D435i 走 USB3、OpenMV 走 USB 串口；双 Raspberry Pi Camera 模式需等待两枚
   相机的具体型号与 CSI/libcamera profile 后再验收。
5. 64 GB 新卡烧录时应创建 UID 1000 的 `airground` 用户。现有 systemd 单元和
   容器 bind mount 以这个身份为权限边界，换用别的 UID 会导致日志与设备权限错配。

## 6. 换卡后的两阶段预检

```bash
# 刚烧完 64 GB 卡：核对板型、架构、系统、内存和实际分区容量
python3 src/deployment/healthcheck/check_pi_host.py --stage base

# Docker 与接口配置完成后（另一台 Pi 用 --role drone）
python3 src/deployment/healthcheck/check_pi_host.py --stage deploy --role car
```

`base` 阶段故意不要求 Docker，方便区分“镜像/硬件基线错误”和“软件尚未安装”。
`deploy` 阶段才检查 Docker/Compose；车机还要求 `/dev/i2c-1`，两种角色都要求
40 针排针 UART `/dev/ttyAMA0`。传感器稳定名和 USB 身份在实物到齐后追加。

## 7. 仍待电子组/机械组确认

- 两台 Pi 的散热器/风扇及安装风道；5 V/5 A 模块的压降、纹波和瞬态实测；
- MC520P30 编码器 PPR、减速比、12 V 空载/负载最高转速；
- 两底盘组装后的轴距、轮距、轮序和编码器正方向；
- MSPM0/STM32 的最终 PWM、IN2、QEI、HC-SR04、急停和 IA6B pinmux；
- HC-SR04 ECHO 电平转换与四路防串扰轮询间隔；
- IA6B 输出协议（iBUS/PPM/PWM）、通道映射、失控值和三套接收机绑定关系；
- ICM42688 I²C 地址；USB 设备 VID/PID/序列号与稳定名；
- 二维云台舵机型号、工作电压、限位、PWM/总线协议，以及由 Pi 还是 OpenMV 驱动；
- D435i CB 连接形态，或双 CSI 相机具体型号、端口和 libcamera profile；
- 数传固件/协议和地面端宿主；Pixhawk TELEM1/TELEM2 参数实测；
- 电池电芯数、容量、C 数、接头、低压阈值及 F450 实际起飞重量/重心；
- 64 GB 卡型号、耐久等级，以及是否准备可直接替换的克隆备份卡；
- 梁乡实验网与中关村服务器之间的受控隧道地址和断线恢复策略。

## 8. 已完成的软件对齐

- DRV8871 IN1/IN2 控制替代旧 TB6612/BTS7960 假设；电流遥测明确为 `NaN`；
- MCU 协议 v0.2 增加四路超声波快照，Pi 生产桥验证板型/底盘/版本并失败关闭；
- A2M12 固定 256000 bps；轮半径为差速 32.5 mm、麦轮 40 mm；
- 真车启动不再直接用 Pi GPIO 驱动 HC-SR04；
- 无人机真实入口包含 MAVROS + D435i + GPS/点云适配；双 CSI 未确认时拒绝启动；
- compose 允许把宿主 UART/USB 路径映射到稳定容器路径，915 MHz 电台不再被错误地
  作为两台 Pi 的公共必需设备。

## 9. 参考

- [Docker Engine on Debian](https://docs.docker.com/engine/install/debian/)
- [ROS REP-3 Target Platforms](https://www.ros.org/reps/rep-0003.html)
