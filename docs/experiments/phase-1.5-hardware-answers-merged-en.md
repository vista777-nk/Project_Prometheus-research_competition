# Phase 1.5 Hardware Answers — Merged from Six Folders + Electronics Team

> **Status**: All questions that have answers from datasheets, SDK code, factory firmware,
> and the electronics team's on-site inspection are compiled here.  
> **Date**: 2026-08-03  
> **For**: ChatGPT / firmware porting / bench deployment
>
> **⚠ IMPORTANT**: Throughout this document, items marked **🆇 GPT DECIDE** are decisions
> the electronics team explicitly delegated with "we don't know / no experience / never
> used this before / you decide". Items marked **🆇 MISSING** are physically unavailable
> (not delivered, blocked by logistics, not yet measurable). Items marked
> **🆇 UNVERIFIED** have a proposed value from the software team that the electronics
> team has not yet tested (board not powered on, not soldered, etc.).

---

## 1. MC520P30 Motor & Encoder Specifications

| Parameter | Value | Source |
|---|---|---|
| Gearbox reduction ratio | **30:1** | Confirmed by electronics team from motor nameplate |
| Encoder PPR (motor shaft) | **13** pulses per revolution | Electronics team: "电机一圈13个方波" |
| Quadrature decoding | **4×** (A+B both edges) | Firmware design |
| Output shaft counts per revolution | 13 × 4 × 30 = **1560** | Computed (was previously 1320 with PPR=11) |
| No-load speed @12V | **360 ± 20 RPM** | Electronics team |
| Rated speed | **Unknown** 🆇 MISSING | Not printed on motor; vendor did not provide |
| Rated torque | **1.5 kg·cm** | Electronics team (from motor label) |
| Motor wiring (6 pins) | Motor power (+), Motor power (−), Encoder VCC, Encoder GND, Encoder A-phase, Encoder B-phase | Electronics team: wires are self-crimped by the team, **no standard colour coding from vendor** 🆇 MISSING; colour-to-function mapping must be traced with a multimeter before powering on |

> ⚠ **Correction to firmware**: `ENCODER_PPR` must change from `11.0f` to `13.0f`, and
> `ENCODER_COUNTS_PER_REV` from `1320` to `1560` in both `board_config.h` files.
> `MOTOR_MAX_RPM` should change from `330.0f` to `360.0f`.

---

## 2. Chassis Mechanical Dimensions (Assembled & Measured)

### 2.1 Differential chassis (R5 baseplate, 65 mm rubber wheels)

| Parameter | Value | Notes |
|---|---|---|
| Wheel diameter (loaded effective) | **65 mm − ≤3 mm compression ≈ 62 mm** 🆇 MISSING | Exact compression not measured; ≤3 mm is the team's estimate (load condition not specified). Must be verified with actual vehicle weight |
| Wheel radius for kinematics | **0.031 m** (31 mm) | Use 62 mm effective / 2 |
| Track width (contact centre-to-centre) | **166 mm** | Measured by electronics team |
| Estimated mass (without Pi payload) | 1.25–1.5 kg 🆇 MISSING | "目前没法给你准确值" — no scale available |

### 2.2 Mecanum chassis (R5 baseplate, 80 mm mecanum wheels)

| Parameter | Value | Notes |
|---|---|---|
| Wheel diameter (loaded effective) | **80 mm − ≤1 mm ≈ 79 mm** 🆇 MISSING | Hard roller, minimal compression. Exact value not measured |
| Wheel radius for kinematics | **0.0395 m** (39.5 mm) | Use 79 mm / 2 |
| Wheelbase (front-rear, centre-to-centre) | **124 mm** | Measured (narrower than simulation assumption of 200 mm — "因为轮子比较宽嘛") |
| Track width (left-right, centre-to-centre) | **166 mm** | Same as differential |
| Lx = wheelbase/2 | **62 mm** = 0.062 m | |
| Ly = track_width/2 | **83 mm** = 0.083 m | |
| Roller orientation | **Classic X-layout (standard)** | |
| Estimated mass (without Pi payload) | 1.25–1.5 kg 🆇 MISSING | "目前没法给你准确值" — no scale available |

### 2.3 Wheel placement convention (top-down view, vehicle front ↑)

```
       FRONT
   [0] FL    FR [1]
   [2] RL    RR [3]
       REAR
```

FL = front-left, FR = front-right, RL = rear-left, RR = rear-right.

### 2.4 CoG / payload mounting

- CoG offset from geometric centre: 🆇 MISSING — "在阶段2 EQA 开始前不可能获得准确参数，先按照底盘几何中心来算" (cannot obtain accurate parameters before Phase 2 EQA; assume geometric centre for now).
- Pi, LiDAR, gimbal/OpenMV are mounted on top of the chassis. Specific coordinates TBD.

> ⚠ **Corrections to firmware `board_config.h`**:
> - Differential: `CHASSIS_TRACK_WIDTH_M` = `0.166f`, `CHASSIS_WHEEL_RADIUS_M` = `0.031f`
> - Mecanum: `CHASSIS_WHEEL_BASE_M` = `0.124f`, `CHASSIS_TRACK_WIDTH_M` = `0.166f`,
>   `CHASSIS_WHEEL_RADIUS_M` = `0.0395f` (or keep 0.040f as conservative rounding)

---

## 3. MCU Pin Assignment Tables

> ⚠ **CRITICAL**: The electronics team has **not soldered a single wire yet**.
> The MSPM0 board "arrived today, never even powered on"（板子今天才收到，连电都没上过）.
> The STM32 board likewise has zero connections made.
> **🆇 GPT DECIDE**: The electronics team explicitly delegated ALL pin-level wiring
> design to the software team: "没焊接呢 / 没接呢，由你来规划 / 怎么接线你来设计吧"
> (not soldered / not connected / you plan it / you design the wiring).
>
> The tables below are the **software team's proposed design** based on peripheral
> conflict analysis of each MCU's datasheet. They have NOT been verified on hardware.
> The electronics team will wire according to whatever final pinout GPT/software provides.

### 3.1 STM32F407VET6 — Mecanum Controller

#### DRV8871 ×4 (IN1 = PWM, IN2 = direction GPIO)

Each DRV8871 module has 4 input pins (GND, VM, IN1, IN2) and 4 output pins (GND, VM, OUT1, OUT2).

| Wheel | IN1 (PWM) | Timer Channel | IN2 (Direction) | GPIO Port |
|:---:|------|:---:|------|:---:|
| 0 FL | PE9 | TIM1_CH1, AF1 | PD0 | GPIOD |
| 1 FR | PE11 | TIM1_CH2, AF1 | PD2 | GPIOD |
| 2 RL | PE13 | TIM1_CH3, AF1 | PD4 | GPIOD |
| 3 RR | PE14 | TIM1_CH4, AF1 | PD6 | GPIOD |

PWM frequency: **20 kHz** (above audible range).  
DRV8871 truth table: FORWARD = IN1 HIGH / IN2 LOW; REVERSE = IN1 LOW / IN2 HIGH;
COAST = both LOW; BRAKE = both HIGH.

🆇 UNVERIFIED — not soldered.

#### Encoders (AB-phase, timer encoder mode, 4× quadrature)

| Wheel | Timer | A-phase | B-phase | AF | Direction sign |
|:---:|:---:|------|------|:---:|:---:|
| 0 FL | TIM2 | PA15 | PB3 | AF1 | +1 |
| 1 FR | TIM3 | PA6 | PA7 | AF2 | −1 |
| 2 RL | TIM4 | PB6 | PB7 | AF2 | +1 |
| 3 RR | TIM5 | PA0 | PA1 | AF2 | −1 |

If any wheel spins opposite to command, flip the sign in `board_config.h` — **do not swap wires**.
🆇 UNVERIFIED — not soldered.

#### HC-SR04 Ultrasonic ×4 🆇 GPT DECIDE

The electronics team has 4 HC-SR04 modules. Each module pinout: GND, ECHO, TRIG, VCC.
**🆇 GPT DECIDE**: "一个模块具体引脚有四个：按顺序为——GND、ECHO、TRIG、VCC；但是接入顺序尚未设计，由你来规划" (the specific pins are GND/ECHO/TRIG/VCC but the connection order hasn't been designed — you plan it).

**🆇 GPT DECIDE — Please assign 8 specific GPIO pins** (4 TRIG output + 4 ECHO input with timer input capture capability) on STM32F407VET6. The ECHO pins need timer channels for accurate pulse-width measurement.

**🆇 GPT DECIDE — Level shifting**: "暂未考虑，请你给出建议（实验室有电阻可用）" (haven't considered it, please give a recommendation — we have resistors in the lab). ECHO is 5V output. On STM32F407 many GPIOs are FT (5V-tolerant), but on MSPM0G3507 ALL GPIOs are 3.3V-only (max 3.6V). A uniform resistive divider on every ECHO line is recommended for both MCUs to avoid mistakes during chassis swap.

**🆇 GPT DECIDE — Polling strategy**: "没经验，你来决定吧" (no experience, you decide). Polling order, inter-trigger interval, and crosstalk mitigation strategy all need to be specified.

Default assumptions from firmware:
- Polling order: front → rear → left → right
- 50 ms minimum between triggers
- `0xFFFF` in CMD_ULTRASONIC (`0x14`) frame means "unavailable"

#### IA6B Receiver (iBUS) 🆇 GPT DECIDE

| Function | Proposed Pin | Notes |
|---|---|---|
| iBUS signal | PA3 | USART2 RX, AF7, 115200 8N1 |
| Power | 5V from buck converter | IA6B operates at 4.0–6.5 V |

🆇 UNVERIFIED — "没接呢，由你来规划" (not connected, you plan it).

#### Raspberry Pi UART

| Function | Pin | Notes |
|---|---|---|
| TX → Pi RX | PA9 | USART1, AF7, 115200 8N1 |
| RX ← Pi TX | PA10 | USART1, AF7 |

Voltage level: STM32 is 3.3 V; Pi GPIO UART is also 3.3 V — **direct connection, no level shifter needed**.
Both sides must share a common GND.
🆇 UNVERIFIED — not soldered.

#### Emergency Stop

| Function | Pin | Notes |
|---|---|---|
| ESTOP input | PB0 | Internal pull-up, **normally-closed (NC)** switch to GND |
| Status LED | PC13 | Slow blink = normal, fast blink = fault, solid = ESTOP locked |

Fail-safe: if the wire breaks or the switch is pressed, PB0 goes HIGH → immediate motor brake, latched until MCU reset.
🆇 UNVERIFIED — not soldered.

---

### 3.2 MSPM0G3507 — Differential Controller

⚠ **🆇 UNVERIFIED**: "板子今天才收到，连电都没上过" (board arrived today, never powered on).
SysConfig has **never been run**, `port_driverlib.c` has **never been compiled**.
All pins below are software-proposed; they MUST be validated through TI SysConfig before any wire is soldered.

#### DRV8871 ×2 🆇 GPT DECIDE

| Wheel | IN1 (PWM) | Timer Channel | IN2 (Direction) | GPIO |
|:---:|------|:---:|------|:---:|
| Left | PB4 | TIMA0_C0 | PB6 | GPIOB |
| Right | PB1 | TIMA0_C1 | PB8 | GPIOB |

PWM frequency: **20 kHz**.
🆇 UNVERIFIED — not soldered. "没接呢，由你来规划."

#### Encoders (QEI mode, 4× quadrature, TIMG8/TIMG7) 🆇 GPT DECIDE

| Wheel | Timer | A-phase | B-phase | Direction sign |
|:---:|:---:|------|------|:---:|
| Left | TIMG8 | PA12 | PA13 | +1 |
| Right | TIMG7 | PA14 | PA15 | −1 |

🆇 UNVERIFIED — not soldered. "没接呢，由你来规划."

#### HC-SR04 ×4 🆇 GPT DECIDE

Same situation as STM32: **🆇 GPT DECIDE — please assign specific TRIG/ECHO pins** and recommend a level-shifting circuit. The MSPM0 is NOT 5V-tolerant. "暂未考虑，请你给出建议."

#### IA6B Receiver (iBUS) 🆇 GPT DECIDE

| Function | Proposed Pin | Notes |
|---|---|---|
| iBUS signal | PA8 | UART1 RX (or timer-capture-capable UART pin) |
| Power | 5V from buck converter | |

🆇 UNVERIFIED — "没接呢，由你来规划."

#### Raspberry Pi UART

| Function | Pin | Notes |
|---|---|---|
| TX → Pi RX | PA10 | UART0, 115200 8N1 |
| RX ← Pi TX | PA11 | UART0 |

Pi container maps this to `/dev/mcu`. Both sides 3.3 V — direct connection.
🆇 UNVERIFIED — not soldered.

#### Emergency Stop

| Function | Pin | Notes |
|---|---|---|
| ESTOP input | PA18 | NC switch to GND, internal pull-up |
| Status LED | PA0 | LaunchPad onboard LED |

🆇 UNVERIFIED — not soldered.

---

## 4. Flysky IA6B Receiver / RC Safety 🆇 GPT DECIDE

> ⚠ **The electronics team has NEVER used Flysky i6/IA6B before.**
> Every decision below was delegated with "第一次用，不知道，你来决定吧"
> (first time using this, don't know, you decide). The values below are the
> **software team's proposal** and have NOT been configured or tested on actual hardware.

| Decision | Proposed Value | Status |
|---|---|---|
| **Protocol** | **iBUS** (single-wire serial) | 🆇 GPT DECIDE — electronics team accepted the recommendation but has not configured it |
| **Baud rate** | 115200 | 🆇 GPT DECIDE — "第一次用，不知道，你来决定吧" |
| **Channel map** (🆇 GPT DECIDE — verify Flysky i6 physical constraints: CH1–CH4 are stick axes, CH5/CH6 are 2-position switches or knobs) | | 🆇 GPT DECIDE — "第一次用，不知道，你来决定吧" |
| — CH1 | Throttle (forward/backward, right stick vertical) | Proposed |
| — CH2 | Steering/Strafe (left stick horizontal — turns diff, strafes mecanum) | Proposed |
| — CH3 | Yaw (right stick horizontal — rotate in place) | Proposed |
| — CH4 | Arm/Enable (2-position switch: low = disarmed, high = armed) | Proposed |
| — CH5 | Emergency Stop (2-position switch: low = normal, high = ESTOP) | Proposed |
| — CH6 | Mode select (optional) | Proposed |
| **Failsafe values** | All channels to neutral/midpoint (1500 µs), CH4=disarmed, CH5=ESTOP active | 🆇 GPT DECIDE — "第一次用，不知道，你来决定吧". Must be set ON the Flysky i6 transmitter |
| **Control priority** | RC always wins: RC arm + ESTOP override Pi commands | 🆇 GPT DECIDE — "第一次用，不知道，你来决定吧" |
| **Arming gesture** | CH4 low→high + throttle at minimum for 3 seconds → ARMED | 🆇 GPT DECIDE — "第一次用，不知道，你来决定吧" |
| **Receiver binding** | 3 receivers: diff chassis, mecanum chassis, drone (Pixhawk) | Not yet bound — "还没连接和上电，不知道" 🆇 UNVERIFIED |

---

## 5. Sensors & Shared Payload

### 5.1 ICM42688 IMU

| Parameter | Value |
|---|---|
| Breakout board | DAIMXA custom board (no specific model number) 🆇 MISSING |
| Pinout (in order) | 3.3-5V, GND, SCL/SCLK, SDA/MOSI, AD0/MISO, CS, INT1/INT, INT2 |
| I²C address | **0x69** 🆇 GPT DECIDE — "真看不出来，我们决定先按照0x69来算" (can't tell by looking, we'll go with 0x69). The SDK driver also uses 0x69. BUT the project `real_sensors.yaml` currently says 0x68. **Must run `i2cdetect -y 1` on the Pi to confirm.** |
| WHO_AM_I | Register `0x75`, expected value `0x47` |
| I²C bus on Pi | `/dev/i2c-1`, SDA/SCL on GPIO2/GPIO3 |
| Interrupt pin | 🆇 GPT DECIDE — "第一次用不知道，请你给出建议" (first time using, don't know, please give your recommendation). Software team proposes: **not needed initially**, poll at 100 Hz via I²C. Add INT later only if timing jitter proves unacceptable. |
| Mounting orientation | **Y forward, X left, Z down** (relative to `base_link`) |
| Default ranges | ±4 g accelerometer, ±500 dps gyroscope (proposed; not yet configured) |

### 5.2 HC-SR04 Ultrasonic ×8 (4 per chassis)

| Parameter | Value |
|---|---|
| Level shifting on ECHO | 🆇 GPT DECIDE — "暂未考虑，请你给出建议（实验室有电阻可用）". Proposed: **resistive divider 2.2 kΩ + 3.3 kΩ** (5V→~3.0V at GPIO). Please confirm or suggest alternative. |
| Trigger pulse | 10 µs HIGH from MCU GPIO (3.3 V sufficient for HC-SR04 minimum VIH) |
| Minimum polling interval | 🆇 GPT DECIDE — "没经验，你来决定吧". Proposed: **50 ms** between triggers |
| Polling order | Front → Rear → Left → Right (proposed; 🆇 GPT DECIDE) |
| Unavailable value | `0xFFFF` in the CMD_ULTRASONIC (`0x14`) frame |

### 5.3 RPLIDAR A2M12

| Parameter | Value |
|---|---|
| Baud rate | **256000** bps (SLAMTEC official spec) |
| Mounting position | **Front of chassis** |
| Field of view | **~225° forward hemisphere** (rear ~135° occluded by gimbal assembly) |
| USB VID/PID/serial | 🆇 MISSING — "物流原因，暂时无法获知" (logistics delay, cannot obtain yet) |
| udev stable name | `/dev/rplidar` (to be created via udev rule once VID/PID/serial are known) |

### 5.4 OpenMV Camera

| Parameter | Value |
|---|---|
| Model | **OpenMV Cam H7 Plus** (`OPENMV4P-STM32H743`) |
| Expansion board | **MV4 variant 1** (2-ch PWM servo + downstream MCU UART interface; **NO TFT LCD**) |
| Firmware | **v4.5.9**, MicroPython **v1.23.0-r19** |
| Connection to Pi | 🆇 GPT DECIDE — "没经验，你来决定吧". Proposed: **USB CDC** (direct USB cable to Pi, appears as `/dev/openmv`) |
| Serial parameters | UART3, 115200 8N1 (for OpenMV-to-MCU in competition mode) |
| Competition script | 🆇 MISSING — "什么都没写好，我们都是小白" (nothing written, we're all beginners) |
| Competition control chain | OpenMV ↔ MSPM0/STM32 via UART (Pi is **not allowed** on competition field per contest rules) |

### 5.5 Two-Axis Gimbal

| Parameter | Value |
|---|---|
| Model | **2-axis gimbal Model B** (Jibot1-V2) |
| Servo model | **PWM15S** ×2 |
| Operating voltage | 5–8.4 V, **independently powered** via dedicated voltage converter board |
| PWM frequency range | **50–330 Hz**. 🆇 GPT DECIDE — which exact frequency? Proposal: **50 Hz** (20 ms period, standard for analogue servos) |
| PWM pulse range | 500–2500 µs, centre 1500 µs |
| Pan (bottom servo) angle limit | **±90°** |
| Tilt (top servo) angle limit | **+45° to −60°** |
| Controller (competition) | 🆇 GPT DECIDE — "电赛不允许携带 Pi 上场，需要由OpenMV和下位机（MSPM0G3507/STM32F407VET6）联合控制" (competition forbids Pi on field; OpenMV + MCU must jointly control). Proposed: OpenMV drives servos directly via `Servo(1)`/`Servo(2)`; MCU receives OpenMV tracking data via UART |
| Controller (research, with Pi) | OpenMV drives servos; Pi sends tracking targets via UART |
| Mounting position | **Rear of chassis**, roughly in line with rear axle. Exact coordinates unknown 🆇 MISSING |
| OpenMV mounting | **Above** the gimbal (camera on top of tilt servo) |

### 5.6 DRV8871 Motor Driver Module

| Parameter | Value |
|---|---|
| Module source | [xinlucity.com product #441](https://www.xinlucity.com/?s=resourcedetail/index/id/441.html) |
| Peak current | **3.6 A** |
| Factory default current limit | **2 A** (set by onboard 0805 SMD resistor) |
| Current limit formula | Per vendor: 限流值 (A) = 64KV / 电阻 (KΩ). Interpreted as: I_limit (A) ≈ 64 / R_sense (kΩ). Verify exact formula from product datasheet. |
| Default R_sense | ~32 kΩ (2 A limit). Resistor is 0805 package — user-replaceable if higher/lower limit needed. |
| Current telemetry | **None** 🆇 MISSING — "似乎没有，你可以上链接自己再确认一下" (seems like there isn't one, check the product page yourself). DRV8871 has no analog current feedback output. TELEMETRY current fields stay `NaN`. |
| External current sensing | **Not present**. Software OVCURRENT fault bit remains disabled. |

---

## 6. Power & Deployment

### 6.1 Batteries

| Item | Car | Drone |
|---|---|---|
| Chemistry | LiPo 3S | LiPo 4S |
| Capacity | 2200 mAh | 5200 mAh |
| Discharge rate | 25C | 35C |
| Connector | XT60 | XT60 |
| Nominal voltage | 11.1 V | 14.8 V |

### 6.2 5V/5A Step-Down Converter

| Parameter | Value |
|---|---|
| Model | 🆇 MISSING — "无具体型号；还未到货" (no specific model; hasn't arrived yet) |
| Input | 9–24 V |
| Output | 5 V / 5 A |
| PD input | Supported |
| Terminal | 3.8 mm (possibly XT60 compatible) |
| Loaded output voltage / ripple | 🆇 MISSING — not delivered, cannot measure |

### 6.3 Power Topology 🆇 GPT DECIDE

The electronics team said: "没经验，你来设计吧" (no experience, you design it).

**Proposed topology** (🆇 GPT DECIDE — please review and correct):

```
Car Battery (3S XT60, 11.1V nom)
  │
  ├─→ 5V/5A buck converter ─┬─→ Raspberry Pi 5
  │                          ├─→ OpenMV H7 Plus (USB)
  │                          ├─→ RPLIDAR A2M12 (USB)
  │                          ├─→ MCU boards: MSPM0 LaunchPad VIN + STM32 VIN
  │                          │   (each has onboard 3.3V LDO → chip)
  │                          └─→ IA6B receiver (operates 4.0–6.5V)
  │
  ├─→ Direct battery voltage → DRV8871 VM pins (×2 diff / ×4 mecanum)
  │
  └─→ Dedicated voltage converter (5–8.4V) → Gimbal servos PWM15S ×2
```

⚠ **Decision**: MCU boards are powered from the 5V buck converter (VIN pin), **NOT from Pi USB**.
Rationale: independent power means a Pi reboot does not kill motor control; the MCU can
hold the ESTOP-latched state and maintain command-timeout safety across Pi restarts.

**Common GND**: 🆇 GPT DECIDE — need a grounding plan. Proposal: star-ground or bus-bar.
All subsystems share a single ground reference. Battery negative → terminal strip → all GND pins.

> 🆇 **GPT — Please critically review this power topology.** The electronics team has no
> experience with power distribution design. We specifically need your scrutiny on:
> 1. Is a single 5V/5A buck converter sufficient for Pi 5 (up to ~3A peak) + OpenMV +
>    RPLIDAR + both MCU boards + IA6B? Should any of these be on a separate rail?
> 2. Are there any back-powering risks (e.g. Pi 5 USB-C PD, MCU boards with onboard
>    regulators, DRV8871 logic supply)?
> 3. Should the gimbal servo converter and the 5V logic buck share the same battery
>    input, or should they be isolated? Any noise coupling concerns from motor VM to
>    logic 5V?
> 4. What inrush current protection, fusing, or reverse-polarity protection do you
>    recommend between the XT60 battery connector and the distribution point?
> 5. Is the proposed common-GND approach safe for all subsystems, or does any device
>    require galvanic isolation (e.g. Pi USB ground loop with MCU UART GND)?

### 6.4 Raspberry Pi 5

| Parameter | Value |
|---|---|
| Model | Pi 5 Model B Rev 1.1, 8 GB RAM |
| OS | Debian 13 (Trixie), AArch64 |
| Storage | Lenovo 64 GB microSD, A2 V30 |
| Cooling | Heatsink + fan |
| Backup | Disk image backup created; **no spare physical SD card** 🆇 MISSING — "没钱买备份卡了，但是做了镜像备份" (no budget for a spare card, but we made a disk image backup) |
| Hostname / static IP | 🆇 MISSING — "暂时无法确认，有一个SD卡还没送到" (can't confirm yet, one SD card hasn't arrived) |
| Container image transfer | **USB drive** (no local container registry) |

### 6.5 Drone Status

> 🆇 MISSING — **ALL drone components are blocked from entering Beijing due to logistics.**
> "元器件暂时无法进入北京，先做无人车部分吧" (components can't enter Beijing for now; let's do the ground vehicles first).
>
> Deferred items: Pixhawk 6C firmware version & `.params` export, TELEM1/TELEM2
> baud rates & MAVLink version, motor numbering & rotation directions & ESC protocol,
> 915 MHz telemetry radio firmware & config, battery calibration, takeoff weight,
> GPS/compass mounting offset, dual Pi Camera models/CSI/lenses (or D435i only).

---

## 7. Key Parameter Changes Required in Firmware

Both `board_config.h` files (`stm32_mecanum` and `mspm0_diff`) need these updates:

| Constant | Old Value | New Value | Reason |
|---|---|---|---|
| `ENCODER_PPR` | 11.0f | **13.0f** | Measured: 13 pulses per motor revolution |
| `ENCODER_COUNTS_PER_REV` | 1320 | **1560** | 13 × 4 × 30 |
| `MOTOR_MAX_RPM` | 330.0f | **360.0f** | No-load speed confirmed |

`stm32_mecanum/board_config.h` additionally:

| Constant | Old Value | New Value |
|---|---|---|
| `CHASSIS_WHEEL_BASE_M` | 0.20f | **0.124f** |
| `CHASSIS_TRACK_WIDTH_M` | 0.18f | **0.166f** |

`mspm0_diff/board_config.h` additionally:

| Constant | Old Value | New Value |
|---|---|---|
| `CHASSIS_TRACK_WIDTH_M` | 0.18f | **0.166f** |
| `CHASSIS_WHEEL_RADIUS_M` | 0.0325f | **0.031f** |

`chassis_params.yaml` needs corresponding updates.

`real_sensors.yaml`:

| Field | Old Value | New Value |
|---|---|---|
| `icm42688.address` | 0x68 | **0x69** |

---

## 8. Linux Device Identity (To Be Collected After Assembly)

The following commands must be run on each Pi with **all assigned devices connected**.
Results are NOT yet available — the boards are not powered, not wired, not assembled.

```bash
ls -l /dev/ttyAMA* /dev/ttyACM* /dev/ttyUSB* /dev/video* 2>/dev/null
lsusb
i2cdetect -y 1
# For each serial device found:
udevadm info --query=property --name=/dev/ttyUSB0
udevadm info --query=property --name=/dev/ttyACM0
```

Devices needing VID/PID/serial capture:
- RPLIDAR A2M12 USB adapter 🆇 MISSING (logistics)
- OpenMV H7 Plus (USB CDC)
- Any USB-UART adapter used for bench-debug `/dev/mcu`
- Pixhawk 6C 🆇 MISSING (logistics)
- 915 MHz ground telemetry radio 🆇 MISSING (logistics)

---

## 9. 🆇 Summary: All GPT-Decide and Missing Items

### 9.1 🆇 GPT DECIDE — Electronics team said "you decide / no experience / please design"

| # | Item | What they said |
|---|---|---|
| D1 | **STM32 HC-SR04 pin assignments** (8 pins: 4 TRIG + 4 ECHO with timer capture) | "接入顺序尚未设计，由你来规划" |
| D2 | **MSPM0 HC-SR04 pin assignments** (8 pins) | "暂未考虑，请你给出建议" |
| D3 | **HC-SR04 level shifting circuit** (5V→3.3V on ECHO) | "暂未考虑，请你给出建议（实验室有电阻可用）" |
| D4 | **HC-SR04 polling strategy** (order, interval, crosstalk prevention) | "没经验，你来决定吧" |
| D5 | **IA6B iBUS baud rate** | "第一次用，不知道，你来决定吧" |
| D6 | **IA6B channel map** (6 channels → throttle/steer/yaw/arm/ESTOP/mode) | "第一次用，不知道，你来决定吧" |
| D7 | **IA6B failsafe values** per channel on transmitter loss | "第一次用，不知道，你来决定吧" |
| D8 | **RC vs Pi control priority** | "第一次用，不知道，你来决定吧" |
| D9 | **Arming gesture / sequence** | "第一次用，不知道，你来决定吧" |
| D10 | **ICM42688 I²C address confirmation** (0x68 vs 0x69) | "真看不出来，我们决定先按照0x69来算" — needs `i2cdetect` |
| D11 | **ICM42688 interrupt pin** — use or not? which GPIO? | "第一次用不知道，请你给出建议" |
| D12 | **OpenMV ↔ Pi connection method** (USB CDC vs UART via MCU) | "没经验，你来决定吧" |
| D13 | **Gimbal PWM frequency** (50–330 Hz range) | "你来决定吧" — software team proposes 50 Hz |
| D14 | **Gimbal controller ownership** (OpenMV vs MCU vs Pi) | Competition: OpenMV+MCU (Pi forbidden). Research: OpenMV with Pi UART commands |
| D15 | **Power topology** (which converter powers what, grounding scheme) | "没经验，你来设计吧". MCU independently powered from 5V buck, NOT from Pi USB — decided. Rest still 🆇 GPT DECIDE. |
| D16 | **DRV8871 R_sense value** — verify actual onboard resistor | "似乎没有 [电流采样]，你可以上链接自己再确认一下" |
| D17 | **STM32 IA6B iBUS UART pin** | "没接呢，由你来规划" |
| D18 | **MSPM0 IA6B iBUS UART pin** | "没接呢，由你来规划" |
| D19 | **MSPM0 SysConfig validation** — never run, never compiled | "板子今天才收到，连电都没上过" |
| D20 | **STM32 + MSPM0 ALL motor/encoder/power wiring** — nothing soldered | "没焊接呢 / 没接呢，由你来规划 / 怎么接线你来设计吧" |

### 9.2 🆇 MISSING — Physically unavailable or not measurable

| # | Item | Reason |
|---|---|---|
| M1 | Motor rated speed | Not on label, vendor didn't provide |
| M2 | Motor/encoder wire colour mapping | Team crimped their own wires; no standard colours; must multimeter-trace |
| M3 | Exact wheel compression under load | Not measured; estimates only (≤3mm rubber, ≤1mm mecanum) |
| M4 | Vehicle mass (both chassis) | No scale available; rough estimate 1.25–1.5 kg |
| M5 | CoG offset from geometric centre | Deferred to Phase 2 EQA |
| M6 | ICM42688 breakout model number | DAIMXA custom, no specific model printed |
| M7 | RPLIDAR USB VID/PID/serial | Logistics delay — device not physically accessible |
| M8 | 5V/5A converter model & loaded measurements | Not delivered yet |
| M9 | Pi hostname / static IP | One SD card not yet delivered |
| M10 | Spare microSD card | No budget ("没钱买备份卡了") — only disk image backup exists |
| M11 | OpenMV competition vision script | "什么都没写好，我们都是小白" |
| M12 | Gimbal/OpenMV exact mounting coordinates | Not measured |
| M13 | ALL drone components | "元器件暂时无法进入北京" — blocked from entering Beijing |
| M14 | Linux `lsusb` / `udevadm` / `i2cdetect` output | Boards not assembled, not powered |
| M15 | DRV8871 product page detailed schematic | Needs manual inspection of the product link |

### 9.3 🆇 UNVERIFIED — Proposed but not tested on hardware

| # | Item | Status |
|---|---|---|
| U1 | ALL STM32 pin assignments | Board never powered, zero wires soldered |
| U2 | ALL MSPM0 pin assignments | Board arrived today, never powered |
| U3 | MSPM0 SysConfig generation | Never executed; `port_driverlib.c` never compiled |
| U4 | IA6B binding to any receiver | "还没连接和上电，不知道" |
| U5 | iBUS protocol decoding on either MCU | No receiver connected |
| U6 | Encoder direction signs (+1/−1) | Wheels never spun with encoders connected |
| U7 | PWM frequency 20 kHz on DRV8871 | Never scoped |
| U8 | ESTOP NC switch behaviour | Switch not installed |
| U9 | I²C communication with ICM42688 | `i2cdetect` never run |

---

*Version: v2.0 · Date: 2026-08-03 · Now explicitly marks GPT_DECIDE / MISSING / UNVERIFIED for every item*
