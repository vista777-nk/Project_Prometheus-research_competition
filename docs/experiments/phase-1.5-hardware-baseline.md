# Phase 1.5 实机硬件基线（滚动记录）

> 本文件是 BOM、模块边界和上机缺口的滚动权威表。长期边界已经由
> [ADR-0018](../decisions/ADR-0018.md) 冻结；地面车参数、电气与 RC 安全基线由
> [ADR-0019](../decisions/ADR-0019.md) 冻结。未到货或未实测参数不能用常见值代填。
> Raspberry Pi 实操顺序、停止条件与证据格式见
> [AI_HANDOFF.md](./AI_HANDOFF.md)；服务器离场状态见
> [2026-08-03 收口报告](./phase-1.5-server-readiness-2026-08-03.md)。

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

## 5. 已确认的地面车工程参数

| 项目 | 差速模块 | 麦轮模块 |
|---|---|---|
| MC520P30 | 30:1；13 PPR；AB 四倍频；1560 counts/输出轴转；12V 空载 360±20 RPM | 同左 |
| 有效轮半径 | 0.031m（含当前压缩估计） | 0.0395m（含当前压缩估计） |
| 轮距 | 0.166m | 0.166m |
| 轴距 | 不适用 | 0.124m（Lx=0.062m） |
| 轮序 | left/right | FL/FR/RL/RR，经典 X 型 |
| 电池 | 3S 2200mAh 25C XT60 | 3S 2200mAh 25C XT60 |
| MCU | LP-MSPM0G3507，当前工程时钟基线 32MHz | STM32F407VET6，8MHz HSE→168MHz |

“360 RPM”是空载值，额定负载转速未知；真实最大速度和 PID 必须在离地、落地、
载荷三个阶段分别测。有效半径也必须用直线里程标定，不能只用卡尺外径。

### 5.1 ICM42688、A2M12 与云台/OpenMV

| 设备 | 冻结配置 | 仍需实测 |
|---|---|---|
| ICM42688-P | `/dev/i2c-1`、地址 0x69、WHO_AM_I 0x47、100Hz、±4g/±500dps；Y 前/X 左/Z 下，发布前映射 `(y,x,-z)` | `i2cdetect`、静置轴符号、噪声/温漂 |
| RPLIDAR A2M12 | 256000 bps，安装在前部；前向约 225° 可用，后部约 135° 被云台遮挡 | USB VID/PID/序列号、实际遮挡角与外参 |
| OpenMV | H7 Plus OPENMV4P/H743，固件 4.5.9，USB CDC 接 Pi；工厂 UART3 为 115200 8N1 | USB 身份、生产检测脚本 |
| 二维云台 | Jibot1-V2 Model B，2×PWM15S；50Hz，500–2500µs，中位 1500µs；pan ±90°、tilt +45°/−60°；OpenMV 直接控制 | 精确安装坐标、舵机电流与机械软限位 |

### 5.2 HC-SR04 与 MCU 候选接线

所有 Echo 均先通过 2.2k/3.3k 分压，5V 高电平约降至 3V。固定顺序
front/rear/left/right，每 50ms 只触发一路，Trigger 为 10µs；超时上报 `0xFFFF`。

| 功能 | MSPM0 差速 | STM32 麦轮 |
|---|---|---|
| 电机 PWM | PB8/PB9 = TIMA0_C0/C1 | PE9/11/13/14 = TIM1_CH1..4 |
| 电机 IN2 | PB6/PB7 | PD0/2/4/6 |
| 编码器 | PA12/13、PA15/16，GPIO 双边沿软件解码 | TIM2/3/4/5 硬件编码器模式 |
| Pi UART | PA10/PA11 UART0 | PA9/PA10 USART1 |
| IA6B | PA9 UART1_RX | PA3 USART2_RX |
| HC-SR04 Trig | PB0/1/4/13 | PD8/9/10/11 |
| HC-SR04 Echo | PA17/22/24/25 + TIMG12 时基 | PC6/7/8/9 = TIM8_CH1..4 |
| 急停/灯 | PA18 NC / PB2 | PB0 NC / PC13 |

MSPM0 旧答复的 PB4/PB1 PWM、PA14 和 TIMG7 QEI 已作废。上表仍是
**SysConfig 候选接线**，不是焊接授权；STM32 表已进入固件但仍须示波器验收。

### 5.3 IA6B 通道与失联基线

- iBUS-SERVO，115200 8N1；CH1=右杆水平，CH2=右杆垂直，CH4=左杆水平；
- 差速用 CH2 前后、CH1 转向；麦轮用 CH2 前后、CH1 横移、CH4 旋转；
- CH5=AUX ARM，CH6=MANUAL/AUTO。CH3 不参与地面车速度；
- failsafe：CH1/2/4=1500、CH5=低。100ms 无有效帧、CH5 低、通道越界或物理
  常闭急停触发时立即刹停；链路恢复不自动解锁；
- 解锁要求 CH5 低→高且三根运动通道连续 3s 位于中心死区；AUTO 下杆量偏离立即接管。

共用 iBUS 帧解析器及 CH5 解锁、100ms 失联、3s 中位保持、MANUAL/AUTO/杆量接管
状态机已入库并通过 Host 测试；两块 MCU 的第二 UART 尚未把它接入控制环。接入前
必须在车轮架空状态记录三套发射机的真实通道、端点和 failsafe 帧。

## 6. 供电与配电门禁

车用 3S 电池经总保险/反接保护后形成星形母线：动力支路直供各 DRV8871；受保护
5V 支路给 Pi 5 与逻辑；云台另配 5–8.4V 舵机 BEC。所有通信地共地，但 Pi USB
不得反向给 MCU/舵机供电。每个动力、Pi、舵机支路分别加保险。

Pi 5 官方推荐插头处 5V/5A；A2M12 官方系统电流为 450–600mA。现有未知型号
9–24V→5V/5A 模块在完成以下测试前不能验收为整车电源：

1. 空载、2A、4A、5A 持续负载下的插头端电压与 20 分钟温升；
2. 电机启动/反转、雷达启动和云台快速转动时的最低电压、纹波和过冲；
3. Pi `dmesg`/`vcgencmd get_throttled` 无欠压，USB 高电流模式符合预期；
4. 断开任一支路不通过信号线/USB 回灌其他支路。

无人机电池基线为 4S 5200mAh 35C XT60；动力与飞控参数等到整套到货后另行冻结。

## 7. 已发现的部署前缺口

1. `/boot/firmware/config.txt` 中 `dtparam=i2c_arm=on` 仍被注释，当前只有
   `/dev/i2c-13`、`/dev/i2c-14`，没有车机配置要求的 `/dev/i2c-1`。
2. 当前只观察到 `/dev/ttyAMA10`，它是 Pi 5 的 3 针 Debug UART，不是 40 针
   GPIO14/15。生产接线若使用 40 针排针，需配置 `dtoverlay=uart0-pi5`，重启后
   验收 `/dev/ttyAMA0`；旧计划的 `/dev/ttyAMA1` 已废止。
3. SPI 尚未启用；现有确认设备不依赖 SPI，因此等后续硬件清单到齐再决定。
4. `vcgencmd get_camera` 返回 “Command not registered” 不能证明相机不可用。
   D435i 走 USB3、OpenMV 走 USB 串口；双 Raspberry Pi Camera 模式需等待两枚
   相机的具体型号与 CSI/libcamera profile 后再验收。
5. 64 GB 新卡烧录时应创建 UID 1000 的运行用户，推荐用户名为 `airground`；已有镜像
   可以保留其他用户名。systemd 单元和容器 bind mount 的权限边界是数字 UID 1000，
   换用其他 UID 会导致日志与设备权限错配。

## 8. 换卡后的两阶段预检

```bash
# 刚烧完 64 GB 卡：核对板型、架构、系统、内存和实际分区容量
python3 src/deployment/healthcheck/check_pi_host.py --stage base

# Docker 与接口配置完成后（另一台 Pi 用 --role drone）
python3 src/deployment/healthcheck/check_pi_host.py --stage deploy --role car
```

`base` 阶段故意不要求 Docker，方便区分“镜像/硬件基线错误”和“软件尚未安装”。
`deploy` 阶段才检查 Docker/Compose；车机还要求 `/dev/i2c-1`，两种角色都要求
40 针排针 UART `/dev/ttyAMA0`。传感器稳定名和 USB 身份在实物到齐后追加。

## 9. 仍待电子组/机械组确认

- 5V/5A 模块的型号、压降、纹波、瞬态与持续温升；新增云台舵机 BEC 的型号/容量；
- 两底盘载荷装好后的总质量、重心、轮胎压缩量、有效半径里程标定和编码器方向；
- MSPM0 SysConfig 无冲突生成、GPIO 中断接入与最高速漏计数；STM32 接线实测；
- 三套 IA6B 的真实通道/端点/failsafe 帧、AUX 开关映射与独立绑定；
- ICM42688 `i2cdetect`/轴符号；USB 设备 VID/PID/序列号与稳定名；
- 二维云台精确安装坐标、舵机堵转电流与 OpenMV 生产脚本；
- D435i CB 连接形态，或双 CSI 相机具体型号、端口和 libcamera profile；
- 数传固件/协议和地面端宿主；Pixhawk TELEM1/TELEM2 参数实测；
- 车/机低压阈值及 F450 实际起飞重量/重心；
- 64GB 卡镜像恢复演练与可直接替换的克隆备份卡；
- 梁乡实验网与中关村服务器之间的受控隧道地址和断线恢复策略。

## 10. 已完成的软件对齐

- DRV8871 IN1/IN2 控制替代旧 TB6612/BTS7960 假设；电流遥测明确为 `NaN`；
- MCU 协议 v0.2 增加四路超声波快照，Pi 生产桥验证板型/底盘/版本并失败关闭；
- A2M12 固定 256000 bps；差速/麦轮几何已同步为 31mm/39.5mm 有效半径、
  166mm 轮距与 124mm 麦轮轴距；编码器统一 1560 counts/rev、360RPM 空载限幅；
- ICM42688 地址改为 0x69，并按实物安装轴转换到 REP-103；
- STM32 TIM8 四路超声波捕获与 50ms 顺序调度已实现；共用 iBUS、RC 安全状态机和
  GPIO 正交解码器已实现；
- MSPM0 旧 pinmux 已拒绝，真实构建在 SysConfig/软件编码器完成前编译期失败；
- 真车启动不再直接用 Pi GPIO 驱动 HC-SR04；
- 无人机真实入口包含 MAVROS + D435i + GPS/点云适配；双 CSI 未确认时拒绝启动；
- compose 允许把宿主 UART/USB 路径映射到稳定容器路径，915 MHz 电台不再被错误地
  作为两台 Pi 的公共必需设备。

## 11. 参考

- [Docker Engine on Debian](https://docs.docker.com/engine/install/debian/)
- [ROS REP-3 Target Platforms](https://www.ros.org/reps/rep-0003.html)
- [Raspberry Pi 5 供电要求](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#power-supply)
- [SLAMTEC RPLIDAR A2 规格](https://www.slamtec.com/en/lidar/a2spec)
- [TI MSPM0G3507 数据手册](https://www.ti.com/lit/ds/symlink/mspm0g3507.pdf)
