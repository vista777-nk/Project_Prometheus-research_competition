# MSPM0G3507 差速底盘固件

> **Task-11** · Phase 1 基础设施 · 与 [`stm32_mecanum`](../stm32_mecanum/) 共享 [`common/`](../common/)
>
> 状态：算法层完成并测试覆盖（Host 70/70）· HAL 移植层待接 TI SDK 与真实硬件

---

## 1. 这是什么

差速底盘（TI 电赛亚克力车架，2× 520 编码器电机）的下位机固件。职责只有一条：

```
树莓派5  ──UART 115200 8N1──▶  MSPM0G3507  ──PWM/方向──▶  2× 520 编码器电机
         二进制帧 + CRC16          本固件              ◀──AB 相编码器──
```

接收 `(v, ω)`，做差速逆解、两路独立 PID 速度环（1kHz），回报实际转速、电流与故障码。
**不做感知、不做决策、不做通信路由。**

### 电赛合规声明

- 主控芯片：**TI MSPM0G3507**（Cortex-M0+ @80MHz，128KB Flash / 32KB SRAM）
- **运动闭环全部在本芯片内完成**：编码器采样、逆运动学、PID、PWM 输出
- 树莓派5 只做感知与通信中继，**不参与任何电机控制回路**
- 选型理由与合规边界见 [ADR-0004](../../../docs/decisions/ADR-0004.md)

---

## 2. ⚠ 构建剖面 —— 请先读这一节

本工程有两个构建剖面，**默认那个产出的固件不能驱动硬件**。

| 剖面 | 命令 | 移植层 | 能烧录吗 | 用途 |
|------|------|--------|:---:|------|
| `ci-link`（默认） | `make` | 空实现 | ❌ 烧得进去，但读不到编码器、推不动 PWM | CI 验证构建图与代码语义 |
| `driverlib` | `make PROFILE=driverlib` | TI DriverLib | ✅ | 真实硬件 |

产出文件名带剖面后缀（`mspm0_diff-ci-link.bin`），`make` 时会打印横幅，
`make flash` 在 `ci-link` 下会直接拒绝执行 —— 三道提示，防的是同一件事。

### 为什么默认剖面是残缺的

麦轮固件（task-10）自带一份手写的 STM32F4 寄存器层，因为 STM32F4 的寄存器映射
公开且可逐条核对。MSPM0G3507 不是这个情况：TI 官方支持的路径是
**DriverLib + SysConfig 生成引脚配置**，而 MSPM0 SDK 无法在 GitHub Actions 上免登录安装。

面对这个约束有两条路：

- **(a)** 凭印象手写一套 MSPM0 寄存器地址。能编过、CI 会绿、看起来很完整 ——
  然后在某个人真的烧录时，以最难排查的方式失败。
- **(b)** 把移植层收窄成十几个原语函数，默认给空实现，并在每一处标明这不是可烧录固件。

本工程选 **(b)**。代价写在标题上，收益是没有人会因为一份看起来很完整的固件浪费一整天。

**被空实现掉的只有 `port_stub.c` 里那十几个寄存器原语。**
运动学、协议、PID、测速窗口、环形缓冲、故障状态机全部是真实代码，
且被 70 个 Host 用例覆盖。连 1kHz 控制中断本身在 `ci-link` 下也是真跑的
（SysTick 是 ARM 内核外设，与 TI 无关）。

要得到可烧录固件 → §7。

---

## 3. 引脚定义

> ⚠ 下表是按 LP-MSPM0G3507 LaunchPad 的**建议**排布，尚未经 SysConfig 生成核对。
> 上板前必须走 §7 的流程。固件代码只依赖 `board_config.h` 里的符号名，改接线不动算法。

轮序号约定（俯视图，车头朝上）：`[0] 左轮 LEFT` · `[1] 右轮 RIGHT`

### 3.1 电机（TB6612FNG）

| 功能 | 引脚 | 外设 | 说明 |
|------|------|------|------|
| 左轮 PWM | PB4 | TIMA0_C0 | 20kHz，避开可听频段 |
| 右轮 PWM | PB1 | TIMA0_C1 | 同上 |
| 左轮方向 A | PB6 | GPIO | TB6612 AIN1 |
| 左轮方向 B | PB7 | GPIO | TB6612 AIN2 |
| 右轮方向 A | PB8 | GPIO | TB6612 BIN1 |
| 右轮方向 B | PB9 | GPIO | TB6612 BIN2 |

TB6612 方向真值表（`PortMotorDirection`）：

| IN1 | IN2 | 状态 |
|:---:|:---:|------|
| 0 | 0 | `COAST` 滑行（高阻） |
| 1 | 0 | `FORWARD` 正转 |
| 0 | 1 | `REVERSE` 反转 |
| 1 | 1 | `BRAKE` 短接刹车 |

### 3.2 编码器

| 轮 | 定时器 | A 相 | B 相 | 方向符号 |
|----|--------|------|------|:---:|
| 左 | TIMG8（QEI） | PA12 | PA13 | `+1` |
| 右 | TIMG7（QEI） | PA14 | PA15 | `−1` |

方向符号在 `board_config.h` 里。**若某轮方向反了，改符号，不要改接线** ——
接线是物理事实，符号是软件约定，改软件不会在下次拆装时丢失。

输出轴每转计数 = PPR 11 × 4 倍频 × 减速比 30 = **1320**

### 3.3 串口与其他

| 功能 | 引脚 | 说明 |
|------|------|------|
| UART TX | PA10 | 对接树莓派 `/dev/ttyAMA1` |
| UART RX | PA11 | 115200 8N1 |
| 左电流采样 | PA24 / ADC0_CH4 | TB6612 分流电阻 + 运放 |
| 右电流采样 | PA25 / ADC0_CH5 | 同上 |
| 硬件急停 | PA18 | **常闭 (NC) 接法**，见下 |
| 状态灯 | PA0 | LaunchPad 板载 LED |

**急停接法（安全关键）**：按常闭接。回路完好且未按下时引脚被外部拉低；
按下**或线缆断开**都会因内部上拉变高 → 触发急停。**断线即停，失效安全。**
在 1kHz 控制中断里直接检测，不依赖串口通信。触发后**锁存**，只能复位退出。

---

## 4. 通信协议

帧结构、CRC、字节序与麦轮固件**完全一致**（[ADR-0003](../../../docs/decisions/ADR-0003.md)），
树莓派端可以用同一个帧解析器处理两块板子。

```
┌────────┬────────┬──────────┬──────────────┬──────────┬────────┐
│  SOF   │  LEN   │   CMD    │    DATA      │  CRC16   │  EOF   │
│  0xA5  │  n+4   │  1 byte  │ 0~251 bytes  │ LSB→MSB  │  0x5A  │
└────────┴────────┴──────────┴──────────────┴──────────┴────────┘
LEN = CMD(1) + DATA(n) + CRC(2) + EOF(1)，CRC 覆盖 CMD+DATA
CRC-16/CCITT-FALSE（poly 0x1021, init 0xFFFF, 不反射），标准向量 "123456789" → 0x29B1
帧内所有多字节标量一律小端序
```

### 4.1 命令表

| CMD | 方向 | 名称 | DATA | 长度 |
|-----|------|------|------|:---:|
| `0x01` | Pi→MCU | `SET_VELOCITY` | `v(f32) ω(f32)` | 8 B |
| `0x02` | Pi→MCU | `EMERGENCY_STOP` | — | 0 B |
| `0x03` | Pi→MCU | `PING` | — | 0 B |
| `0x10` | Pi→MCU | `EXTENSION` | `子命令(u8) + 变长载荷` | ≥1 B |
| `0x11` | MCU→Pi | `TELEMETRY` | `rpm(f32×2) + 电流(f32×2) + 故障码(u16)` | 18 B |
| `0x12` | MCU→Pi | `ACK` | 被确认的 CMD(u8) | 1 B |
| `0x13` | MCU→Pi | `PONG` | `major,minor,patch,board,chassis` | 5 B |
| `0xFF` | MCU→Pi | `ERROR` | 错误码(u8) + 变长详情 | ≥1 B |

**与麦轮固件的差异只有三处**：`SET_VELOCITY` 8B（vs 12B）、`TELEMETRY` 18B（vs 34B）、
`board_type/chassis_type` = `0x02/0x02`（vs `0x01/0x01`）。帧结构一字不差。

> **上位机启动校验**：发 `PING`，检查 `PONG` 中 `board_type`+`chassis_type` 是否与
> `CHASSIS` 环境变量一致，不匹配立刻拒绝启动。接错板子在这一步就该被拦下，
> 而不是等到车往错误的方向开出去。

`TELEMETRY` 由固件每 50ms（20Hz）主动上报，Pi 端不需要轮询。

### 4.2 错误码

| 码 | 名称 | 含义 |
|----|------|------|
| `0x01` | `UNKNOWN_CMD` | 命令字不在本板命令表中 |
| `0x02` | `BAD_LENGTH` | DATA 长度与命令定义不符（详情带回实际长度） |
| `0x03` | `BAD_VALUE` | 载荷含 NaN / Inf |
| `0x04` | `TX_OVERFLOW` | 发送侧组帧失败 |
| `0x05` | `NOT_IMPLEMENTED` | 命令字合法但本固件未注册处理器（如未接扩展外设） |

### 4.3 故障位图（`TELEMETRY` 末尾 u16，小端）

**与麦轮固件逐位一致** —— 上位机只需写一套解码逻辑。

| 位 | 名称 | 生命周期 |
|----|------|----------|
| `0x0001` | `OVERCURRENT` | 跟随实际电流，20Hz 更新 |
| `0x0002` | `STALL` | 跟随实际状态，1kHz 更新（见下方注意） |
| `0x0004` | `CMD_TIMEOUT` | 收到新指令即清除 |
| `0x0008` | `ESTOP` | **锁存**，只能复位退出 |
| `0x0010` | `KINEMATICS_SAT` | 跟随实际状态，1kHz 更新 |
| `0x0020` | `UART_ERROR` | 按"距上次遥测的增量"判定，非累计值 |

> ⚠ **位与位之间的优先级也属于协议**：`OVERCURRENT` 或 `ESTOP` 置位期间电机已刹停，
> 堵转检测停止更新，`STALL` 位保持旧值。上位机此时**应忽略 `STALL`**。
> 只定义每一位单独的含义是不够的。

### 4.4 黄金帧（跨端自测向量）

`PING` → `PONG` 的完整字节序列。三端（MSPM0 / STM32 / Pi 端 Python）实现必须逐字节一致：

```
Pi → MCU  (PING):
  A5 04 03 <crc_lo> <crc_hi> 5A          CRC over {03}

MCU → Pi  (PONG, v0.1.0, board=0x02, chassis=0x02):
  A5 09 13 00 01 00 02 02 <crc_lo> <crc_hi> 5A
                          └ CRC over {13 00 01 00 02 02}
```

CRC 值刻意不写死在这里 —— 见 `test/test_protocol.c` 的 `test_pong_golden_frame()`，
那里用 `crc16_ccitt()` 独立算出并断言。硬编码一个未经验算的常数，
只会把"测试通过"变成"测试和实现一起错"。

---

## 5. 编译与测试

```bash
cd src/firmware/mspm0_diff

make test        # Host 单元测试，不需要 ARM 工具链
make             # 交叉编译 (ci-link 剖面，不可烧录)
make size        # 各段占用
make clean
make help
```

### 5.1 测试覆盖（70 个用例）

| 文件 | 用例 | 覆盖 |
|------|:---:|------|
| `test_kinematics.c` | 14 | §11.6 的 6 个必测用例 + 曲率保持 + 饱和阈值 + 往返 + NaN/NULL/非法几何 |
| `test_protocol.c` | 17 | 命令表、载荷布局、错误注入、`0x10` 扩展、黄金帧、跨板帧兼容、空闲重同步 |
| `test_encoder.c` | 11 | 16 位回绕（正反向）、测速窗口保持、EMA 系数、方向符号、双轮独立 |
| `test_faults.c` | 21 | 故障状态机全部迁移：三类生命周期 + 位间优先级 + 轮数边界 + NULL 安全 |
| `test_control_loop.c` | 7 | 逆解+双 PID+正解**整链**：直线 / 弧线 / 原地旋转 / 非对称负载 / 饱和路径 / 急停复位 |

**本工程没有 `test_crc16.c` 与 `test_pid.c`。** `common/crc16.c` 与 `common/pid.c`
是与麦轮固件共用的**同一份实现**，其单体测试在 `stm32_mecanum/test/` 下唯一存在。
同一份代码测两遍不增加信息，只增加两处要同步维护的用例。
本工程改测 `test_control_loop.c` —— 逆解 + 双路 PID + 正解**串起来**的组合行为，
那才是 task-11 特有的风险（单独测每一环都过、串起来跑偏，是控制固件最典型的失败方式）。

#### 故障状态机为什么在 `common/` 而不是 `main.c`

故障位图是**线上契约**，两块板逐位一致，所以位定义只存一份（`common/faults.h`）——
分别写在两个 `protocol.h` 里迟早会漂移。

更要紧的是判定逻辑。它原先写在 `main.c` 的 `static` 函数里，结果是
**一个单元测试都覆盖不到**：依赖 1kHz 中断、真实编码器、ADC 采样。
而这些逻辑恰恰是 task-10 评审阶段用真实缺陷换来的：

| 契约 | 原始缺陷 |
|------|----------|
| `FAULT_STALL` 跟随实际状态 | 只置不清 → 一次瞬时堵转，故障灯亮到复位 |
| `FAULT_UART_ERROR` 按增量判定 | 用累计计数 → 开机一次噪声，故障位永久挂着 |
| 停机期间 `STALL` 保持旧值 | 电机已刹停时"轮子不转"不构成堵转证据 |

抽成 `common/faults.c` 纯函数后，三条全部由 `test_faults.c` 逐条钉死。
**第三条尤其重要** —— 它是位间优先级规则，属于线上契约，但在抽出本模块之前，
整个仓库里没有任何一处能验证它。

`main.c` 现在只负责采集输入与执行处置（刹停、PID 复位），不再自己拼位图。

### 5.2 Cortex-M0+ 软浮点开销

M0+ **既没有 FPU，也没有硬件整数除法指令**，浮点全是 `__aeabi_*` 库调用。
1kHz 控制中断里的粗略估算（数量级估计，非实测）：

| 环节 | 浮点操作 | 估计周期 |
|------|:---:|:---:|
| `diff_inverse_kinematics` | ~10 | ~600 |
| `pid_update` × 2 | ~8 each，含 1 次除法 | ~1000 |
| `encoder_update`（摊薄） | 少量 | ~100 |
| `motor_set_duty` × 2 + ISR 开销 | — | ~400 |
| **合计** | | **~2000–3000 周期** |

@80MHz ≈ **25–38µs**，占 1ms 控制周期的 **3–4%**。余量充足。

代码里为此做了两件事：`kinematics.c` 预先算好 `1/R` 与 `track/2`，热路径只做乘法；
`encoder.c` 的换算系数是编译期常量。

> **已知的一处未优化**：`common/pid.c` 的微分项写作 `(error - prev) / dt`，
> 每周期两次浮点除法（~300 周期）。改成传入 `1/dt` 可以省掉，
> 但那要动与麦轮固件共享的代码。3–4% 的占用不值得现在动它，
> 记在这里作为 Phase 2 的候选优化。

---

## 6. 电赛扩展预留

竞赛现场常要临时接循迹 / 避障 / 灰度模块。为了不在赛场上改固件结构，预留了：

- **协议**：`CMD_EXTENSION`（`0x10`），子命令字节由参赛队现场约定，
  固件把它路由给 `ProtocolHandlers.on_extension` 回调
- **硬件**：`EXT_ADC_CHANNEL_COUNT` 4 路 ADC（灰度阵列等模拟量）+
  `EXT_GPIO_COUNT` 8 路 GPIO（开关量、蜂鸣器、拨码）

**Phase 1 刻意不注册 `on_extension`。** 未注册时协议层明确回
`ERROR/NOT_IMPLEMENTED`，而不是假装 ACK —— "上位机以为固件支持、固件其实没实现"
是赛场上最难查的一类问题。接入外设时在 `main.c` 补一个回调即可，协议不用改。

---

## 7. 上板流程与检查清单

### 7.1 生成 SysConfig 配置

> ⚠ **`port_driverlib.c` 从未被任何编译器读过。** 它在仓库里、15 个原语都写全了，
> 但 CI 只构建 `ci-link` 剖面，本地也没有 SDK。它不是骨架，但也**不是经过验证的代码** ——
> 第一次 `make PROFILE=driverlib` 大概率会因为实例名对不上而报一串编译错误。
> 这是预期的，按下面第 5 步处理即可。

1. 安装 [TI MSPM0 SDK](https://www.ti.com/tool/MSPM0-SDK) 与 CCS（或独立 SysConfig）
2. 按 §3 的引脚表配置：TIMA0（PWM 双通道）/ TIMG8 + TIMG7（QEI）/ UART0 /
   ADC0 / GPIO / 一个 1kHz 周期定时器
3. **UART 必须使能接收超时（RX timeout）中断** —— 这是拆帧空闲重同步的物理层基础，
   漏掉它会让一个杂散字节吞掉最多 257 字节的正常数据
4. 生成 `ti_msp_dl_config.h` / `.c`
5. 核对生成的实例名与 `src/port_driverlib.c` 使用的宏名一致
   （**不一致就改 `port_driverlib.c`，不要改生成物**）。
   本工程用的宏名：`MOTOR_PWM_INST` / `MOTOR_PWM_C0_IDX` / `MOTOR_PWM_C1_IDX` /
   `ENCODER_L_INST` / `ENCODER_R_INST` / `CONTROL_TIMER_INST` / `UART_COMM_INST` /
   `ADC_CURRENT_INST` / `GPIO_MOTOR_PORT` / `GPIO_ESTOP_PORT` / `GPIO_LED_PORT`
6. DriverLib 的 API 签名可能随 SDK 版本变化（尤其 `DL_ADC12_configConversionMem`
   的参数个数）。以本机 SDK 的头文件为准，不要以本文件为准。

### 7.2 核对链接脚本

`linker/MSPM0G3507.ld` 里的内存布局错了固件根本起不来 —— 这不是"跑偏"那类问题：

```
FLASH  ORIGIN 0x00000000  LENGTH 128K
SRAM   ORIGIN 0x20200000  LENGTH  32K   →  末端 0x20208000，_estack 即在此
```

**起始地址与长度都要核对**，尤其长度：`_estack = ORIGIN + LENGTH`，
长度写大了栈顶会落在不存在的地址上，第一次压栈就 HardFault；
写小了则白白浪费 SRAM 且可能与 SDK 的假设冲突。

核对方式（按可靠性排序）：

1. **直接 diff SDK 自带的链接脚本** —— 最可靠：
   ```bash
   diff <(grep -A4 'MEMORY' $MSPM0_SDK/source/ti/devices/msp/m0p/linker_files/gcc/mspm0g350x.lds) \
        <(grep -A4 'MEMORY' linker/MSPM0G3507.ld)
   ```
2. 数据手册 SLASEZ4 §Memory Organization
3. `PROFILE=driverlib` 时**直接改用 SDK 自带的链接脚本与启动文件**，
   本工程自带的这两份只服务于 `ci-link` 剖面

### 7.3 首次上电检查清单

**每一步都在电机脱开负载（轮子架空）的状态下做。**

- [ ] `make PROFILE=driverlib` 编译通过，`make size` 显示 Flash < 128K、SRAM < 32K
- [ ] 烧录后状态灯以 1Hz 慢闪 → 主循环在跑，无故障
- [ ] `ESTOP_REQUIRE_HARDWARE` 设为 `1` 且急停回路**已接好**；
      断开急停线 → 灯常亮、电机锁死 → **失效安全验证通过**
- [ ] 发 `PING`，收到 `PONG` 且 `board=0x02 chassis=0x02`
- [ ] 不发任何指令，确认 500ms 后 `TELEMETRY` 的故障码出现 `CMD_TIMEOUT (0x0004)`
- [ ] 手动转动左轮，`TELEMETRY` 中左轮 RPM 符号为**正**（否则翻 `ENCODER_DIR_SIGN_LEFT`）
- [ ] 右轮同上
- [ ] 发 `SET_VELOCITY(v=0.1, ω=0)`，两轮同向转动，RPM 接近且为正
- [ ] 发 `SET_VELOCITY(v=0, ω=0.5)`，**右轮正转、左轮反转**（逆时针为正）
- [ ] 堵住一个轮 800ms，确认故障码出现 `STALL (0x0002)`；松手后**故障位自动清除**
- [ ] 落地空跑，按 §8 复整定 PID
- [ ] 上负载前最后确认一次急停可用

---

## 8. PID 整定

默认增益与麦轮固件相同（同型号电机、同控制频率），但**差速底盘负载分布不同** ——
只有两个轮承担全部牵引力，实车必须复整定。

```c
#define PID_KP_DEFAULT     0.0035f   // ≈ 1/MOTOR_MAX_RPM，静态前馈量级
#define PID_KI_DEFAULT     0.030f
#define PID_KD_DEFAULT     0.00004f
#define PID_INTEGRAL_LIMIT 40.0f
```

**整定准则（task-10 用一次红掉的测试换来的）**：

```
ki × integral_limit ≥ output_limit
```

否则积分器无法独立顶到满占空比，症状是"带载后转速永远差一截"。
本工程 `0.030 × 40 = 1.2 ≥ 1.0`，留 20% 余量覆盖电池掉压与负载增大。

整定顺序：先 `kp`（加到刚出现振荡再退 30%）→ 再 `ki`（消除稳态误差）→
`kd` 通常保持很小甚至为 0（编码器量化噪声会被微分放大）。

---

## 9. 代码结构

```
mspm0_diff/
├── Makefile                    双剖面构建 (ci-link / driverlib)
├── linker/MSPM0G3507.ld        ⚠ 上板前核对内存布局
├── src/
│   ├── board_config.h          唯一的"魔数集散地"：几何/编码器/PID/时序/引脚
│   ├── version.h               版本 + board/chassis 编码 + 构建剖面标记
│   │
│   ├── kinematics.c/.h         差速逆解 + 正解        ← 纯算法，Host 可测
│   ├── protocol.c/.h           命令表与载荷布局        ← 纯逻辑，Host 可测
│   ├── encoder.c/.h            回绕/测速窗口/EMA      ← 纯逻辑，Host 可测
│   ├── motor.c/.h              占空比 → PWM+方向映射
│   ├── uart.c/.h               收发环形缓冲
│   ├── main.c                  1kHz 控制中断 + 主循环
│   │
│   ├── mspm0_conf.h            编译剖面开关
│   ├── startup_mspm0g3507.c    Cortex-M0+ 向量表 + Reset_Handler
│   │
│   ├── port_stub.c             移植层空实现 (CI)，但 SysTick 与控制中断是真的
│   └── port_driverlib.c        移植层 TI DriverLib 实现 (真实硬件)
│       （移植层接口在 ../common/mcu_port.h，与麦轮固件共用同一份）
└── test/                       70 个 Host 用例
```

### 移植层为什么值得多一层间接

麦轮固件把寄存器操作直接写在 `encoder.c` / `motor.c` / `uart.c` 里，代价是
**测速窗口、EMA、环形缓冲、占空比映射这些真正容易出错的逻辑测不到**。

这里把二者切开后，`encoder.c` 的 11 个 Host 用例（16 位回绕、窗口保持、
EMA 系数、方向符号）全部是麦轮固件测不了的。换 MCU 时也只需重写
`common/mcu_port.h` 那十几个函数，板级逻辑一行不动。

代价是每个控制周期多约 10 次函数调用，@80MHz 约 0.5µs —— 相对 1000µs 的周期可以忽略。
这个代价是刻意付的。

---

## 10. 相关文档

| 文档 | 内容 |
|------|------|
| [task-11](../../../project-prometheus-tasks/task-11-mspm0-diff-firmware.md) | 任务定义与验收标准 |
| [ADR-0003](../../../docs/decisions/ADR-0003.md) | 串口二进制帧协议统一设计 |
| [ADR-0004](../../../docs/decisions/ADR-0004.md) | TI 电赛合规主控选型 + 移植层分离决策 |
| [stm32_mecanum](../stm32_mecanum/README.md) | 麦轮固件，共享 `common/` |
| `chassis_params.yaml` | 仿真侧几何参数，改动必须与本工程同步 |
