# STM32F407VET6 麦轮底盘固件

> Layer 1 硬件层 · 只做运动控制，不做感知 / 决策 / 通信路由
> 对应任务：[task-10](../../../project-prometheus-tasks/task-10-stm32-mecanum-firmware.md) · 协议规范：[ADR-0003](../../../docs/decisions/ADR-0003.md)

树莓派 5 通过 UART 下发车体速度 `(vx, vy, ω)`，本固件做逆运动学解算、
四轮独立 PID 速度闭环，并以 20Hz 回传轮速、电流与故障码。

```
树莓派 5 (Layer 2)  ──UART 115200 8N1──▶  STM32F407VET6 (Layer 1)  ──▶  4× 520 编码器电机
   car_preprocessor.py                      逆运动学 + 4×PID @1kHz          麦轮底盘
```

---

## 一、编译与测试

```bash
cd src/firmware/stm32_mecanum

make test      # 宿主机单元测试（只要有 gcc/clang 就能跑，不需要 ARM 工具链）
make           # 交叉编译 → build/stm32_mecanum.{elf,bin,hex}
make size      # 打印各段占用
make flash     # OpenOCD + ST-Link 烧录
make clean
```

### 依赖

| 用途 | 工具 | 说明 |
|------|------|------|
| 交叉编译 | `arm-none-eabi-gcc` + `libnewlib-arm-none-eabi` | Ubuntu: `apt install gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi` |
| 单元测试 | 任意 C99 编译器 | Windows 上 MinGW gcc 亦可 |
| 烧录 | OpenOCD + ST-Link | 仅上板时需要 |

**不需要** STM32Cube / HAL 库 / CCS：固件自带最小寄存器层（`src/stm32f407_regs.h`），
克隆下来就能编。若将来确需官方 HAL，在 `src/stm32f4xx_conf.h` 里定义 `USE_HAL_DRIVER` 切换。

### 单元测试覆盖

63 个用例，Host 侧全绿：

| 文件 | 覆盖内容 |
|------|----------|
| `test_crc16.c` | 标准测试向量 `0x29B1`、增量式一致性、逐比特错误检出、字节序敏感性 |
| `test_kinematics.c` | 前进 / 左移 / 双向自转 / 零输入 / 限幅 / 等比缩放不失真 / 逆正解往返 / 线性叠加 / 非法入参 |
| `test_pid.c` | 初始化与复位、饱和时不积分、抗积分饱和、输出限幅、阶跃响应（超调 <20%、稳态误差 <5%）、负载扰动抑制 |
| `test_protocol.c` | 黄金帧字节序列、LEN 语义、0~251 全长度往返、CRC/EOF 错误注入、失同步与空闲重同步、全部命令分发与应答 |
| `test_encoder.c` | 16 位计数器正反向回绕、测速窗口保持、EMA 系数、四轮方向符号各自生效、四轮独立、reset 语义 |

> `test_crc16.c` 与 `test_pid.c` 测的是 `common/` 里的共享实现，因此**只在本工程存在一份**；
> `common/faults.c` 的测试同理，落在 `mspm0_diff/test/test_faults.c`。
> 共享模块的测试只写一遍，避免两处用例要同步维护。

---

## 二、引脚定义

> 轮序号（俯视图，车头朝上）：`[0]` 左前 `[1]` 右前 / `[2]` 左后 `[3]` 右后
> 全部映射集中在 [`src/board_config.h`](src/board_config.h)，换接线只改那一个文件。

### 2.1 电机 PWM —— TIM1，20kHz

| 轮 | 引脚 | 复用 | 定时器通道 |
|:---:|------|:---:|------|
| 0 左前 | PE9  | AF1 | TIM1_CH1 |
| 1 右前 | PE11 | AF1 | TIM1_CH2 |
| 2 左后 | PE13 | AF1 | TIM1_CH3 |
| 3 右后 | PE14 | AF1 | TIM1_CH4 |

### 2.2 电机方向 —— TB6612FNG 双 H 桥 ×2

| 轮 | IN1 | IN2 |
|:---:|------|------|
| 0 左前 | PD0 | PD1 |
| 1 右前 | PD2 | PD3 |
| 2 左后 | PD4 | PD5 |
| 3 右后 | PD6 | PD7 |

真值表：前进 = IN1 高 / IN2 低 · 后退 = IN1 低 / IN2 高 · 滑行 = 双低 · 刹车 = 双高

### 2.3 编码器 —— AB 相，定时器编码器模式，4 倍频

| 轮 | 定时器 | A 相 | B 相 | 复用 |
|:---:|:---:|------|------|:---:|
| 0 左前 | TIM2 | PA15 | PB3 | AF1 |
| 1 右前 | TIM3 | PA6  | PA7 | AF2 |
| 2 左后 | TIM4 | PB6  | PB7 | AF2 |
| 3 右后 | TIM5 | PA0  | PA1 | AF2 |

### 2.4 其他

| 功能 | 引脚 | 说明 |
|------|------|------|
| 上位机串口 TX | PA9  | AF7，USART1 → 树莓派 `/dev/ttyAMA0` RX |
| 上位机串口 RX | PA10 | AF7，USART1 ← 树莓派 TX |
| 电流采样 | PC0~PC3 | ADC1_IN10~IN13，对应轮 0~3 |
| 硬件急停 | PB0 | 内部上拉 + **常闭**急停开关，详见 §五 |
| 状态灯 | PC13 | 慢闪 = 正常 · 快闪 = 有故障 · 常亮 = 急停锁死 |

> 接线时注意：串口是**交叉**接的（STM32 的 TX 接树莓派的 RX），
> 且两边必须共地，否则会出现"能收到字节但 CRC 全错"的经典现象。

---

## 三、通信协议

完整规范见 [ADR-0003](../../../docs/decisions/ADR-0003.md)，此处只列本板相关部分。

### 3.1 帧格式

```
┌────────┬────────┬──────────┬──────────────┬──────────┬────────┐
│  SOF   │  LEN   │   CMD    │    DATA      │  CRC16   │  EOF   │
│  0xA5  │  n+4   │  1 byte  │ 0~251 bytes  │ LSB→MSB  │  0x5A  │
└────────┴────────┴──────────┴──────────────┴──────────┴────────┘

LEN = CMD(1) + DATA(n) + CRC(2) + EOF(1) = n + 4
CRC = CRC-16/CCITT-FALSE，覆盖 CMD + DATA（不含 SOF/LEN/CRC/EOF）
所有多字节标量小端序；float 为 IEEE-754 binary32
```

### 3.2 命令表

| CMD | 方向 | 名称 | DATA | 响应 |
|-----|------|------|------|------|
| `0x01` | Pi→STM32 | SET_VELOCITY | `vx(f32) vy(f32) ω(f32)` = 12 B | ACK |
| `0x02` | Pi→STM32 | EMERGENCY_STOP | 无 | ACK |
| `0x03` | Pi→STM32 | PING | 无 | PONG |
| `0x11` | STM32→Pi | TELEMETRY | 4×RPM(f32) + 4×电流(f32) + 故障码(u16) = 34 B | — |
| `0x12` | STM32→Pi | ACK | 被确认的 CMD(u8) = 1 B | — |
| `0x13` | STM32→Pi | PONG | major,minor,patch,board_type(0x01),chassis_type(0x01) = 5 B | — |
| `0xFF` | STM32→Pi | ERROR | 错误码(u8) + 变长详情 | — |

TELEMETRY 由固件每 50ms（20Hz）主动上报，Pi 无需轮询。

### 3.3 故障码位图（TELEMETRY 末 2 字节）

| 位 | 名称 | 含义 |
|:---:|------|------|
| 0x0001 | OVERCURRENT | 任一电机电流超过阈值，已刹车 |
| 0x0002 | STALL | 有目标转速但轮子不转（堵转 / 编码器断线） |
| 0x0004 | CMD_TIMEOUT | 超过 500ms 未收到 SET_VELOCITY，已自动刹停 |
| 0x0008 | ESTOP | 硬件急停被触发（锁死，需复位） |
| 0x0010 | KINEMATICS_SAT | 速度指令超出底盘能力，已等比缩放 |
| 0x0020 | UART_ERROR | 距上次遥测有新增 CRC 错误 / 缓冲溢出 |

### 3.4 黄金帧（可直接用于上位机自测）

| 帧 | 十六进制字节 |
|------|--------------|
| PING | `A5 04 03 93 D1 5A` |
| EMERGENCY_STOP | `A5 04 02 B2 C1 5A` |
| SET_VELOCITY(vx=1.0, vy=0, ω=0) | `A5 10 01 00 00 80 3F 00 00 00 00 00 00 00 00 0D E5 5A` |
| ACK(0x01) | `A5 05 12 01 3F 68 5A` |
| PONG(v0.1.0, STM32, 麦轮) | `A5 09 13 00 01 00 01 01 D0 8F 5A` |

---

## 四、运动学与整定

### 4.1 逆运动学

与仿真侧 [`mecanum_controller.py`](../../air_ground_car_bringup/scripts/mecanum_controller.py)
**必须保持一致**。两处不一致会导致"仿真调好的策略搬到实车上跑偏"。

```
lever = Lx + Ly                          Lx = wheel_base/2 = 0.10 m
ω0 = (vx - vy - ω·lever) / R             Ly = track_width/2 = 0.09 m
ω1 = (vx + vy + ω·lever) / R             R  = 0.033 m
ω2 = (vx + vy - ω·lever) / R
ω3 = (vx - vy + ω·lever) / R             RPM = ω_wheel × 60 / 2π
```

**限幅用等比缩放，不是逐轮硬钳位**：任一轮超过 `max_rpm` 时四轮同乘一个系数。
硬钳位会改变各轮转速的比例关系，让实际运动方向偏离指令方向；等比缩放只降速不改方向。

几何参数取自 [`config/chassis_params.yaml`](../../air_ground_car_bringup/config/chassis_params.yaml)
的 `mecanum_chassis` 段，在 `board_config.h` 中以 `#define` 落地。

> 改底盘尺寸时，`chassis_params.yaml` 与 `board_config.h` 必须同步改。
> Phase 2 会把参数改成上电时由 Pi 下发，届时这条约束自然消失。

### 4.2 PID 整定

默认值在 `board_config.h`，由 `test_pid.c` 的一阶电机模型（τ≈80ms）验收：

| 参数 | 默认值 | 整定要点 |
|------|--------|----------|
| `PID_KP_DEFAULT` | 0.0035 | 量纲是"占空比 / RPM"。满量程 330 RPM 对应满占空比，故基准约 1/330 |
| `PID_KI_DEFAULT` | 0.030 | 消除稳态误差，主要补偿电池掉压与负载变化 |
| `PID_KD_DEFAULT` | 0.00004 | 编码器量化噪声会被微分放大，宁小勿大 |
| `PID_INTEGRAL_LIMIT` | 40.0 | **准则：`ki × integral_limit ≥ output_limit`**，否则带载后转速永远差一截 |

抗饱和策略是**条件积分**：输出已顶到限幅、且误差还会把输出推向同一方向时，本周期不累加积分。

### 4.3 编码器测速

`ENCODER_COUNTS_PER_REV = PPR(11) × 4倍频 × 减速比(30) = 1320`

按 1ms 采样算转速，分辨率只有 `60000/1320 ≈ 45 RPM/计数`——比整个调速范围的 1/8 还大。
因此转速在 **10ms 窗口**上测量（分辨率约 4.5 RPM）再做一阶低通，
而速度环仍跑满 1kHz，每 10 个周期拿到一个新测量值。
Phase 2 若要更高精度，应改用 M/T 法（同时测计数与相邻边沿间隔）。

---

## 五、安全设计

| 机制 | 触发条件 | 动作 | 恢复方式 |
|------|----------|------|----------|
| 硬件急停 | PB0 被拉高 | 立即刹车，锁死 | **仅能复位 MCU** |
| 指令看门狗 | 500ms 未收到 SET_VELOCITY | 目标速度清零，刹停 | 收到新指令自动恢复 |
| 过流保护 | 任一轮电流 > 2.5A | 刹车 | 电流回落自动恢复 |
| 堵转检测 | 目标 >30 RPM 但实测 <3 RPM 持续 800ms | 置故障位上报 | 轮子转起来自动清除 |

### 硬件急停按常闭（NC）接法

急停开关串在 PB0 与 GND 之间，常态闭合把引脚拉低；**按下或线缆断开**都会因内部上拉
变成高电平并触发急停。断线即停 —— 失效安全（fail-safe）。

> ⚠️ **台架调试注意**：没接急停开关时，PB0 被上拉为高，固件开机即锁死、电机不动。
> 台架上可临时把 `board_config.h` 的 `ESTOP_REQUIRE_HARDWARE` 改成 `0`，
> **上车前必须改回 `1`**。

---

## 六、代码结构

```
stm32_mecanum/
├── Makefile                      构建规则（交叉编译 + Host 测试）
├── linker/STM32F407VETx_FLASH.ld 512K Flash / 128K SRAM / 64K CCM
├── src/
│   ├── board_config.h            ★ 全部可整定量：几何、引脚、PID、阈值
│   ├── version.h                 版本号 + CI 注入的 commit / 构建时间
│   ├── kinematics.c/.h           逆 / 正运动学        ← 纯 C，可 Host 测试
│   ├── protocol.c/.h             命令分发 + 遥测组装  ← 纯 C，可 Host 测试
│   ├── encoder.c/.h              回绕 / 测速窗口 / EMA   ← 纯 C，可 Host 测试
│   ├── motor.c/.h                占空比 → PWM+方向映射  ← 纯 C
│   ├── uart.c/.h                 收发环形缓冲          ← 纯 C
│   ├── main.c                    初始化 + 1kHz 控制中断 + 主循环
│   │
│   ├── stm32f4xx_conf.h          编译目标切换（裸机 / HAL / Host 测试）
│   ├── stm32f407_regs.h          最小寄存器映射
│   ├── port_stm32f407.c          ★ 移植层实现 —— **唯一碰寄存器的文件**
│   └── startup_stm32f407vetx.c   向量表 + Reset_Handler（C 实现）
└── test/                         Host 单元测试（不参与交叉编译）

../common/                        与 task-11 MSPM0 固件共享，只存一份
├── pid.c/.h  crc16.c/.h  protocol_frame.c/.h  unity.c/.h
├── faults.c/.h                   故障位图（线上契约）+ 故障状态机
└── mcu_port.h                    移植层接口，两块板共用同一份
```

### 移植层为什么值得多一层间接

重构之前，寄存器操作散在 `bsp.c` / `encoder.c` / `motor.c` / `uart.c` / `main.c`
五个文件里，与测速窗口、环形缓冲、故障判定混在一起 —— 后果是
**16 位计数器回绕、EMA 滤波、故障位生命周期这些真正容易出错的逻辑只能上板验证**。
当时的 52 个用例里，没有一个能覆盖"计数器从 `0xFFF0` 走到 `0x0005`
到底被算成 `+21` 还是 `-65515`"。

收拢到 `common/mcu_port.h` 的十几个原语之后，`encoder.c` 变成纯逻辑，
11 个用例直接测掉；故障判定移入 `common/faults.c`，由 `test_faults.c` 覆盖。
代价是每个控制周期多约 10 次函数调用，@168MHz 约 0.2µs —— 相对 1000µs 可忽略。

换 MCU 时也只需重写 `port_stm32f407.c` 那一个文件，板级逻辑一行不动。

### 任务划分

| TIM6 中断（1kHz，硬实时） | 主循环（软实时） |
|---------------------------|------------------|
| 编码器采样 · 逆运动学 · 故障评估（含急停）· 四路 PID · PWM 输出 | 串口拆帧与命令分发 · 空闲重同步 · ADC 电流采样 · 20Hz 遥测 · 状态灯 |

速度环放在中断里，是因为 PID 的正确性依赖固定的 `dt`。
若与串口解析、ADC 轮询挤在同一个主循环里，一次 40 字节的遥测发送就能让控制周期抖动几毫秒，
积分项与微分项会随之失真。

---

## 七、上板检查清单

> **本固件的移植层（`port_stm32f407.c`）尚未在真实硬件上验证。**
> Phase 1 已达成"CI 编译通过 + Host 单元测试通过"（2026-08-01）。首次上板请按下表逐项确认。

- [ ] **时钟树**：用示波器测 PC13 状态灯周期是否为 1s（验证 168MHz + SysTick）
- [ ] **串口**：树莓派发 PING，确认收到 `A5 09 13 00 01 00 01 01 D0 8F 5A`
- [ ] **PWM**：示波器测 PE9 载频是否为 20kHz，占空比是否随指令变化
- [ ] **电机方向**：下发 `vx=+0.2`，确认四轮**全部**朝前推车；哪个反了就改
      `board_config.h` 里对应的 `ENCODER_DIR_SIGN_*` 或调换该轮 IN1/IN2 接线
- [ ] **编码器方向**：手动正向拨轮，确认遥测里该轮 RPM 为正
- [ ] **编码器计数**：手转一整圈，确认累计计数接近 1320；不符说明减速比或 PPR 填错
- [ ] **急停**：拔掉急停开关连线，确认电机立即停转且状态灯常亮
- [ ] **看门狗**：发一次 SET_VELOCITY 后停发，确认 500ms 后车自动停下
- [ ] **PID 整定**：空载跑通后再压载重跑，按 §4.2 微调

---

*Firmware v0.1.0 · Phase 1 · task-10*
