# Raspberry Pi AI 交接：Phase 1.5 实机接入

> **目标读者：明天在良乡实验室、直接操作两台 Raspberry Pi 5 的 AI。**
>
> 这是一份新的实机交接，不是已归档旧交接的续写。服务器侧在 2026-08-03 已完成
> 最终审计，详见 [服务器收口报告](./phase-1.5-server-readiness-2026-08-03.md)。
> 硬件事实以 [滚动硬件基线](./phase-1.5-hardware-baseline.md)、
> [ADR-0018](../decisions/ADR-0018.md) 和 [ADR-0019](../decisions/ADR-0019.md) 为准；
> 本文只把下一步执行顺序、停止条件和缺失信息集中到一处。

## 0. 接手后先确认四件事

```bash
pwd
git status --short --branch
git log --oneline -5
git remote -v
```

1. 工作目录应是新仓库 `project-prometheus`，远端应是
   `vista777-nk/Project_Prometheus-research_competition`；不要再使用旧的
   `research_compitition` 路径或依赖 GitHub 重定向。
2. 使用项目负责人指定的当前工作分支；不要直接推送 `main`。
3. 先确认 GitHub Actions 为当前提交产生了运行记录。工作流现应对**任意分支 push**
   触发；如果没有运行，先修 CI，不能把“没有红灯”当“全绿”。
4. 阅读 `CONVENTIONS.md`。提交信息必须是 Conventional Commits + 简体中文；
   ADR 不可修改，只能由新 ADR 替代或补充。

推荐随后运行：

```bash
make smoke-phase1
bash src/deployment/validate.sh
```

预期为零失败、零跳过。`SKIP` 表示当前环境没有真正检查该项，不等于通过。

## 1. 绝对安全边界

- 地面车第一次上电必须架空车轮，电源限流，急停可触达；未确认编码器方向、RC
  failsafe 和串口看门狗前，不得落地运行。
- 无人机所有软件、飞控、数传和电机方向检查先**拆桨**；未经负责人批准，不进入
  系留或实飞阶段。
- Pi 不直接产生电机 PWM，不直接测 HC-SR04 Echo，也不接收地面车 RC。
  这些硬实时和安全功能属于底盘 MCU；无人机飞行与 RC 属于 Pixhawk。
- `EDGE_MODE=real` 才是实验数据。`mock` 必须显示 DEGRADED，`sim` 只用于仿真；
  real 缺设备时失败关闭是正确行为，禁止自动回退。
- 未确认的 pinmux、PX4 参数、USB 身份和网络地址不得写进生产配置。
- 服务器的 ROS Master 与 TCP 9090 只监听 `127.0.0.1`。不得为图省事把 ROS 1
  或未认证的 9090 暴露到校园网。

## 2. 当前已经完成到哪里

| 范围 | 已完成且有证据 | 不能据此宣称的事项 |
|---|---|---|
| 服务器 | 用户 systemd 服务和健康 timer 已启用；5 个服务节点健康；11311/9090 仅回环；故障恢复做过真实注入 | 良乡到中关村链路可达、跨校区时钟一致 |
| Pi 部署 | Debian 13/ARM64/8 GB/64 GB 基线预检、Docker/Compose/systemd/udev/健康检查、离线镜像更新与回滚流程已实现 | 已在真实 Pi 安装或开机自启成功 |
| 车机 Linux 驱动 | pyserial、Linux SMBus、A2M12、ICM42688、OpenMV、底盘桥和失败关闭有 81 个 Host 用例 | 线材、电平、真实 USB 身份、传感器外参已验证 |
| STM32 麦轮 | 运动学、PID、协议、编码器、DRV8871、TIM8 四路 HC-SR04 已实现；Host 80/80 | 候选接线已焊接并通过示波器/负载验收；RC 第二 UART 已进控制环 |
| MSPM0 差速 | 运动学、协议、PID、RC 安全和软件正交解码有 Host 71/71 | CI 的 `ci-link` 产物可烧录；SysConfig/pinmux/ISR 已完成 |
| 无人机 | MAVROS + D435i 真实入口、设备失败关闭和模块边界已写入部署 | Pixhawk/ESC/GPS/数传参数已冻结，或无人机已经可飞 |
| CI/静态门禁 | 7 个 job、Phase 1 冒烟、部署校验与 lint 均有本地入口 | 当前 Pi 上的硬件结论已被 CI 覆盖 |

## 3. 两台 Pi 的角色与固定基线

两台均以 Raspberry Pi 5 Model B 8 GB、AArch64、Debian 13 Trixie、**标称 64 GB
microSD**、UID 1000 用户 `airground` 为验收基线。ROS Noetic 不裸装在 Trixie；
继续运行于 Focal ARM64 容器。

| Pi | 角色 | 必需连接 | 不属于它的连接 |
|---|---|---|---|
| 车机 Pi | 一套共享车载智能载荷，在差速/麦轮控制模块间断电换装 | `/dev/mcu`、ICM42688 `/dev/i2c-1`、A2M12 `/dev/rplidar`、OpenMV `/dev/openmv` | HC-SR04 GPIO、DRV8871 PWM、IA6B |
| 无人机 Pi | 机载边缘计算与可换视觉载荷 | Pixhawk TELEM2 `/dev/pixhawk`；D435i USB3，或未来确认的双 CSI | 915 MHz 空中电台（接 Pixhawk TELEM1）、IA6B（接 Pixhawk） |

当前确认的车载参数：

- ICM42688：I²C 地址 `0x69`、WHO_AM_I `0x47`、100 Hz、±4g/±500dps；
  实物 Y 前/X 左/Z 下，发布前映射 `(sensor_y, sensor_x, -sensor_z)`。
- RPLIDAR A2M12：256000 bps；前部安装，约前 225° 可用，遮挡角仍需实测。
- OpenMV：H7 Plus OPENMV4P/H743、固件 4.5.9、USB CDC；云台由 OpenMV 直接控制。
- MCU 串口：Pi 40 针 GPIO14/15 对应 `/dev/ttyAMA0`；`/dev/ttyAMA10` 是 3 针
  Debug UART，不能当生产接口。

## 4. 第一天：64 GB 卡与宿主机验收

### 4.1 烧录和恢复能力

1. 使用 64 位 Debian 13 Trixie，创建 UID 1000 的 `airground` 用户并预置 SSH 公钥。
2. 为两台 Pi 记录镜像来源、烧录时间和卡的实际容量；不要把卡序列号、MAC、校园网
   临时 IP 提交进仓库。
3. 完成一次关机、拔卡、重启验证；随后制作可恢复的镜像或克隆卡，并至少做一次
   恢复演练。仅“有备份文件”不算恢复能力。

在每台 Pi 的仓库根执行：

```bash
python3 src/deployment/healthcheck/check_pi_host.py --stage base --json
lsblk -o NAME,SIZE,FSTYPE,MOUNTPOINTS
df -h /
```

中止条件：根文件系统小于 54 GiB、不是 aarch64/Pi 5/Debian 13、内存明显低于
8 GB 型号预期，先解决镜像或卡问题，不继续安装。

### 4.2 打开确认过的接口

先备份 `/boot/firmware/config.txt`，再通过 `raspi-config` 或等价配置完成：

```text
dtparam=i2c_arm=on
dtoverlay=uart0-pi5
```

关闭串口登录终端、保留硬件 UART，重启后核对：

```bash
ls -l /dev/i2c-1 /dev/ttyAMA0
python3 src/deployment/healthcheck/check_pi_host.py --stage deploy --role car
# 无人机 Pi 使用 --role drone
```

当前 BOM 不需要 SPI，不要为了“以后可能用”提前打开。车机缺 `/dev/i2c-1` 或任一角色
缺 `/dev/ttyAMA0` 时，deploy 预检应失败。

## 5. 第二步：部署软件，但先不启用服务

```bash
bash src/deployment/install.sh --role car --dry-run
sudo bash src/deployment/install.sh --role car
# 另一台改为 --role drone
```

安装器不会自动启用服务，也不会覆盖已有 `/opt/air-ground/.env`。逐项核对：

```bash
getent group dialout i2c
sudo -e /opt/air-ground/.env
/opt/air-ground/scripts/require-image.sh
```

必须按本机实况填写：`ROLE`、车机的 `CHASSIS`、`EDGE_MODE=real`、
`HOST_GID_DIALOUT`、`HOST_GID_I2C`、角色设备路径，以及获批准网络方案给出的
`ROS_MASTER_URI`/`ROS_IP`。样例里的 `192.168.1.x` 是隔离网计划，不是已授权的
跨校区地址。

离线镜像包必须同时带 SHA-256；用 `update-image.sh` 校验架构、空间和哈希，并保留
上一个镜像用于回滚。不要在良乡依赖现场联网重新构建镜像。

只有 `.env`、设备节点和镜像都核对后才启用：

```bash
sudo systemctl enable --now air-ground-car-edge.service
sudo systemctl enable --now air-ground-healthcheck.timer
# 无人机使用 air-ground-drone-edge.service
```

SSH 加固最后做；必须先从第二个终端确认密钥登录成功，避免把自己锁在实验室之外。

## 6. 第三步：设备发现与稳定名

每接入一个 USB/串口设备，只接一个并采集：

```bash
lsusb -t
udevadm info --query=property --name=/dev/ttyUSB0
dmesg --ctime | tail -80
```

记录 VID、PID、产品字符串和可用序列号，再修改
`src/deployment/network/99-air-ground-devices.rules`。如果同型号设备没有唯一序列号，
使用物理端口路径并记录换口影响；不能用当前恰好出现的 `ttyUSB0` 作为生产名。

期望稳定名：

- 车机：`/dev/mcu`、`/dev/rplidar`、`/dev/openmv`；
- 无人机：`/dev/pixhawk`；D435i 必须通过 `check-usb3.sh` 确认运行在 USB3。

稳定名建立后重新运行 `install.sh`（幂等）、重新加载 udev，并做拔插/重启各一次。

## 7. 第四步：车机逐件台架验收

顺序必须是 MCU → 单个传感器 → 预处理 → 全链路。使用
`car_edge_real.launch` 的 `enable_*` 参数隔离当前被测设备，不能用一次满配启动掩盖
具体故障。

### 7.1 底盘 MCU

STM32 可以生成真实固件；MSPM0 默认只生成不可烧录的 `ci-link` 产物。MSPM0 必须先
按 `src/firmware/mspm0_diff/README.md` 完成 TI SDK + SysConfig、GPIO 双边沿 ISR 和
真实剖面链接，移除编译期门禁后再烧录。

车轮架空、驱动动力断开时先验证协议：

```bash
CHASSIS=mecanum bash src/deployment/test/test-serial-loopback.sh /dev/mcu
# 差速改为 CHASSIS=diff
```

必须收到匹配的板卡/底盘/最低协议版本 PONG，并持续收到顺序固定为
`front/rear/left/right` 的四路超声波快照。错板、错底盘、CRC 错误或数据中断都要停。

然后完成编码器 A/B 方向、每圈 1560 counts、最高速漏计数、单电机方向、急停、
500 ms 速度指令看门狗，以及三套 IA6B 的通道/端点/failsafe 记录。共用 RC 状态机虽
已有 Host 测试，两块 MCU 的第二 UART 仍未接入控制环；接入并实测前不得宣称 RC 可用。

### 7.2 共享载荷

1. ICM42688：`i2cdetect -y 1` 看到 `0x69`；核对 WHO_AM_I、三个静置朝向的轴符号、
   温漂和噪声。
2. A2M12：确认 256000 bps、转向、零角、有效 225° 与云台遮挡角；测量相对底盘外参。
3. OpenMV/云台：确认 USB CDC、生产检测脚本、舵机独立 BEC、堵转电流、机械软限位和
   精确安装坐标。
4. 每个节点单独在 real 模式稳定运行后，再启用预处理器并核对
   `/car/observation` 的 modalities 与超声波顺序。

未关闭的接口问题：`openmv_bridge.py` 当前确认发布 detections，但
`/car/openmv/image_raw` 是否有真实发布者仍待上机。若没有，不要临时伪造话题；记录
实际 OpenMV 输出能力并提出 ADR 方案。

## 8. 第五步：无人机无桨验收

无人机硬件参数尚未冻结。到货后按
`src/deployment/network/drone-hardware.md` 执行，至少完成：

1. Pixhawk 6C/PM07/M9N 确认型号、固件和参数备份；IA6B 直接接 Pixhawk；
2. TELEM1 接 915 MHz 空中电台，地面端接地面站；TELEM2 接 Pi `/dev/pixhawk`；
3. 无桨核对 4 个 A2212/BL32 的编号、方向、ESC 协议/校准、failsafe 与电流；
4. D435i 模式验证 RGB、深度和点云频率、USB3、功耗和外参；
5. 查询真实 PX4/MAVROS 参数后再决定 MAVLink 2 签名。模板刻意留空，未知参数名
   不得写入生效配置；
6. 记录 4S 5200mAh 电池实测、整机重量、重心和推重比，再由负责人批准动力台、
   系留和保护区实飞。

双 Raspberry Pi Camera 模式还没有相机型号、CSI 端口或 libcamera profile；入口失败
关闭是预期行为。不要从 D435i 配置推断双 CSI 参数。

## 9. 尚缺或不确定的信息

| ID | 缺失事实 | 关闭方式/责任输入 |
|---|---|---|
| H1 | 两台 64 GB 卡实际容量、镜像与恢复结果 | Pi 操作者跑 base 预检并做恢复演练 |
| H2 | 5 V/5 A 降压模块型号、压降、纹波、过冲和 20 分钟温升 | 电子组电子负载 + 示波器，分别测 0/2/4/5 A |
| H3 | 云台独立 BEC 型号/容量、舵机堵转电流 | 电子组限流测试 |
| H4 | 两底盘满载质量、重心、有效半径、编码器方向/漏计数 | 机械组 + 架空/落地/载荷里程标定 |
| H5 | MSPM0 SysConfig、GPIO 双边沿中断和最终无冲突 pinmux | 固件/电子组生成工程并做真板编译与示波器验收 |
| H6 | STM32 候选接线、电平和 TIM8 捕获真板证据 | 电子组逐通道示波器验收 |
| H7 | 三套 IA6B 通道、端点、failsafe 和独立绑定 | 遥控/固件操作者逐套记录，测试 100 ms 失联 |
| H8 | ICM42688 真实总线/轴符号；A2M12/OpenMV/Pixhawk VID/PID/序列号 | Pi 操作者逐件枚举和拔插/重启测试 |
| H9 | A2M12、ICM42688、OpenMV/云台相对底盘外参和遮挡 | 机械测量 + 实机数据标定 |
| H10 | OpenMV 生产脚本与原始图像输出边界 | 视觉组给出脚本并实测话题 |
| H11 | D435i CB 实际连接形态，或双 CSI 型号/端口/profile | 电子/视觉组按实物确认 |
| H12 | Pixhawk 固件、TELEM1/2、M9N、数传、ESC、RC 和签名参数 | 无人机到货后无桨验收；不得外推 |
| H13 | F450 实际重量/重心/推重比和低压阈值 | 装机称重与动力台数据 |
| N1 | 良乡实验网地址、中关村入口、受控隧道和授权方式 | 网络负责人书面确认 |
| N2 | 隧道断线/重连、积压、三机时钟偏差 | 两校区联调，记录故障注入和恢复时间 |

这些是 Phase 1.5 的真实验收阻塞项，不用 mock、常见参数或“能启动”关闭。

## 10. 跨校区与服务器协作

中关村服务器在交接时健康，但根分区已用 97%；大文件、ROS 日志、bag 和镜像只写
`/data2`。服务器有充足内存；系统时间同步正常。远程不稳定时：

- Pi 本地缓存传感器/标定数据，使用临时文件 + 原子改名，避免半文件；
- 每批数据附 SHA-256、设备角色、UTC/CST 时间、commit SHA 和配置摘要；
- 应用层对唯一 TCP 会话做指数退避和重连，断线时车辆/无人机本地安全不依赖服务器；
- 不跨 WAN 运行 ROS 图。经批准的 VPN/SSH 隧道只转发所需 TCP 入口；
- 首次联通后做一次主动断隧道/恢复测试，并测三机相对同一参考源的时间偏差。

## 11. 每次实验要留下的证据

建议在 `docs/experiments/` 建日期明确的实验记录，在
`src/deployment/calibration/calibration_db/` 按其 README 归档标定结果。每条记录至少含：

- 日期、操作者、Pi 角色、Git commit、镜像 tag/digest、`.env` 非敏感摘要；
- 板卡/传感器型号与稳定设备名，不提交 MAC、序列号、密钥、校园网地址；
- 完整命令、原始退出码、PASS/FAIL/SKIP 数；
- 电压、电流、温度、频率、丢包/CRC、时间偏差和测量仪器；
- 明确写“已验证什么”和“没有验证什么”；失败尝试不得从记录中删除。

## 12. 收尾与提交

```bash
git diff --check
make smoke-phase1
bash src/deployment/validate.sh
git status --short --branch
```

涉及对应模块时再运行两个固件 Host 测试、传感器 Host 测试和真实 ROS 测试。提交前
确认新文件已加入索引，因为 `git ls-files` 型门禁看不到未跟踪文件。按
`CONVENTIONS.md` 使用简体中文 Conventional Commit，并在提交正文写明实测数字、
硬件状态和仍未验证项。提交后确认当前分支的 GitHub CI **实际出现且全绿**。

Phase 1.5 只有在上表硬件阻塞项、受控隧道/时钟、两车控制模块、共享载荷和无人机
分级验收均有真实证据后才能关闭。
