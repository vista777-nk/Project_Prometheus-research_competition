# RESEARCH_PHILOSOPHY.md — 设计哲学与科研宪法

> **本文档是项目的"宪法"。**  
> 如果说 PLATFORM.md 定义了系统的骨架，ICD.md 定义了模块之间的契约，ROADMAP.md 定义了前进的方向，  
> 那么本文档定义的是：**为什么我们这样设计，以及什么原则永远不可违反。**
>
> 未来一年里，你会遇到无数诱惑：  
> 「换 ROS2 Humble 吧。」「直接把服务器改成 Jetson。」「这里重写更简单。」「不用 ICD 了。」  
> 没有一份设计哲学，项目很容易在半年后变成一个"越来越能跑，但越来越没人敢改"的系统。
>
> **本文档比代码寿命更长。**

---

## 〇、核心定位

这个仓库不是：

```
Git Repository
```

而是：

```
Laboratory Memory（实验室记忆）
```

它承载的不只是代码，而是：

- 设计决策的来龙去脉
- 接口的演化历史
- 每一次实验的成功与失败
- 论文背后的工程基础
- 未来任何一个人（包括未来的你）都能理解的"为什么"

**铁律**：不删除，不覆盖，不因为"重写"而丢弃历史。  
CRAIC 做完，保留。全国电赛做完，保留。World Model 做完，保留。  
2028、2029、2030，甚至读博——所有东西都还能找到。

---

## 一、五条不可违反的宪法

### 第一条：Platform First（平台优先）

> 任何论文都不能破坏平台。论文应该建立在平台之上。

**错误模式**（永远避免）：

```
为了论文
    ↓
改平台
    ↓
另一篇论文又推翻
```

**正确模式**：

```
平台稳定
    ↓
论文 A 在平台上新增模块
    ↓
论文 B 在平台上新增另一个模块
    ↓
平台继续运行，两篇论文的成果共存
```

**具体含义**：

- 平台代码 (`air_ground_sim_ws`) 的 Layer 1~3 一旦稳定，不应为任何单篇论文修改
- 论文的贡献应该在 Layer 4 (Research) 中体现，以新增 package 的形式加入
- 如果论文需要平台改动，改动必须向上游合入平台，且向后兼容
- **平台必须比任何一篇论文寿命更长**

---

### 第二条：Interface Before Implementation（接口先行）

> 任何模块，先设计接口，后写代码。

**错误模式**（永远避免）：

```
先写代码
    ↓
再封装
    ↓
接口是事后产物
```

**正确模式**：

```
先 ICD（接口定义）
    ↓
再 Package（包结构声明）
    ↓
再代码（实现）
```

**具体含义**：

- 任何新模块（VLM、Planner、World Model、SLAM 等）的第一步永远是：在 ICD.md 中新增接口定义
- 接口评审通过后，在 `air_ground_interfaces` 包中定义 ROS 消息/服务
- 最后才写实现代码
- 接口是对外的承诺；实现是对承诺的兑现。**承诺比兑现更严肃**
- 这条原则你们已经在 ICD 中迈出了第一步——把它变成不可违反的铁律

---

### 第三条：Everything Produces Knowledge（万物产生知识）

> 机器人不是为了产生 Topic。机器人应该产生 Knowledge。

这不是一个技术决策，而是一种**认知框架的转变**。

**旧思维**（传感器视角）：

| 模块 | 输出 |
|------|------|
| 深度相机 | `Image` |
| SLAM | `Map` |
| Planner | `Velocity` |

**新思维**（知识视角）：

| 模块 | 输出 |
|------|------|
| 深度相机 | `Observation` |
| SLAM | `World State Update` |
| Planner | `Mission` |

**具体含义**：

- 每个模块的输出应该回答一个更高层次的认知问题，而非仅仅传递原始数据
- `Observation` 携带的不仅是像素，还包括时间戳、传感器位姿、置信度、模态标记
- `WorldState` 是对环境的统一认知表示，而非某一种算法的中间产物
- `Mission` 表达的是任务意图，而非底层控制指令
- Layer 4 (Research) 只与 Knowledge 交互，不与 Raw Data 交互
- 这与 ICD 中已确立的方向高度一致：研究层只依赖抽象接口，永不引用具体硬件协议

---

### 第四条：Simulation is the First Robot（仿真即第一台机器人）

> 不要把 Gazebo 当成"假的机器人"。把 Gazebo 当成"第一台机器人"。

**关键洞察**：

```
Gazebo（仿真）
    ↓
PX4 SITL（软件在环）
    ↓
真实 Pixhawk（硬件在环）
    ↓
全实机部署
```

这四者之间变化的只有一件事：**Hardware Adapter（硬件适配器）**。

Research Layer（Layer 4）**完全不变**。

**具体含义**：

- 仿真环境不是临时脚手架——它是平台的第一种部署形态
- 在仿真中验证通过的算法，切换到实机时只需替换 Layer 2 (Bridge) 的适配器
- 仿真测试与实机测试同等严肃：都需要自动化测试脚本、都需要记录实验日志
- 永远不在仿真中"走捷径"——仿真代码的结构必须如实反映实机代码的结构
- 这正是你们把平台（PLATFORM.md）和研究路线（ROADMAP.md）拆分的最大价值所在

---

### 第五条：Every Research Module Must Be Replaceable（每个研究模块必须可替换）

> 真正稳定的是 Capability（能力），而不是 Algorithm（算法）。

**不可接受的情况**：

```
换 YOLOv11 → GroundingDINO
        ↓
    Planner 崩溃，需要重写
```

```
换 Qwen2.5-VL → GPT-6
        ↓
    Mission 逻辑全改
```

```
换 RTAB-Map → Gaussian Splatting Mapping
        ↓
    World Model 接口断裂
```

**正确架构**：

```
Capability:  Object Detection
    ├── 实现 A: YOLOv11        ← 今天
    └── 实现 B: GroundingDINO  ← 明天
                ↓
          Planner 不受影响（只依赖 Detection 抽象接口）

Capability:  Visual Understanding
    ├── 实现 A: Qwen2.5-VL     ← 今天
    └── 实现 B: GPT-6          ← 明天
                ↓
          Mission 不受影响（只依赖 VLM 抽象接口）

Capability:  Spatial Understanding
    ├── 实现 A: RTAB-Map              ← 今天
    └── 实现 B: Gaussian Splatting   ← 明天
                ↓
          World Model 不受影响（只依赖 SLAM 抽象接口）
```

**具体含义**：

- 每个研究能力（Object Detection、VLM、SLAM、Planner）在 ICD 中定义一个抽象接口
- 具体算法是实现细节，可以在不触动系统其他部分的情况下替换
- 接口的稳定性是第一优先级；实现的性能是第二优先级
- 这与现代机器人软件架构强调的"低耦合、高内聚、面向演化"原则一致，也是近年来机器人 World Model 设计总结出的核心经验

---

## 二、Architecture Decision Records (ADR)

### 为什么需要 ADR

任何一个人——包括未来的你——都应该知道：**当初为什么这样设计。**

项目会经历无数次决策：

- 为什么用 ROS Noetic 而不是 ROS2 Humble？
- 为什么服务器不解析 MAVLink？
- 为什么 Observation 不是 Image？
- 为什么选择 PX4 v1.14 而不是 v1.15？
- 为什么先做差速底盘再做麦轮？

这些决策如果只存在于当时的聊天记录或脑海中，半年后就丢失了。

### ADR 规范

所有重大架构决策，在 `docs/decisions/` 目录下创建 ADR 文件：

```
docs/
    decisions/
        ADR-0001-why-ros-noetic.md
        ADR-0002-why-server-not-parse-mavlink.md
        ADR-0003-why-observation-not-image.md
        ADR-0004-why-px4-v1.14.md
        ...
```

每个 ADR 文件遵循统一格式：

```markdown
# ADR-NNNN: 简短标题

**状态**：提议 | 已采纳 | 已废弃 | 已替代

**日期**：YYYY-MM-DD

**背景**：为什么需要做这个决策？

**决策**：我们选择了什么？

**备选方案**：我们还考虑了什么？为什么没有选？

**后果**：这个决策带来了什么影响？（正面 + 负面）
```

### ADR 铁律

- **ADRs are immutable（不可变）**：已采纳的 ADR 永远不修改，只能由新的 ADR 替代
- **每个 ADR 都有编号**：从 0001 开始，永不重复
- **废弃的 ADR 保留不删**：它记录了一段真实的设计历史

---

## 三、Laboratory Memory 的组成部分

这个仓库作为 Laboratory Memory，由以下层次构成：

| 层 | 内容 | 载体 | 寿命 |
|----|------|------|:---:|
| **宪法** | 设计哲学、不可违反的原则 | RESEARCH_PHILOSOPHY.md (本文档) | 项目全周期 |
| **架构** | 分层、包结构、通信拓扑 | PLATFORM.md | 极少变动 |
| **契约** | 模块间接口定义 | ICD.md | 稳定 |
| **路线** | 研究计划、论文目标 | ROADMAP.md | 经常更新 |
| **决策** | 为什么这样设计 | docs/decisions/ADR-*.md | 永久保留 |
| **实施** | 工程步骤 | task-01~09 | 完成后归档 |
| **代码** | 实现 | Git 仓库 | 永久保留 |
| **历史** | 每一次提交、每一次实验 | Git History | 永久保留 |
| **日志** | 实验记录、失败记录 | docs/experiments/ | 永久保留 |
| **论文** | 学术产出 | 仓库根目录或独立目录 | 永久保留 |

---

## 四、长期愿景

### 不要做成"最厉害的本科项目"

这样的目标太小了。

### 要做成"一套能够持续演进五年以上的机器人研究平台"

如果五年后：

有人加入你的实验室。

第一件事不是：

> "重新搭一个框架。"

而是：

> "Clone 仓库。"

然后：

- 新增一个机器人（Model + URDF + Bridge）
- 新增一个 Planner（实现 ICD 中的 Planner 接口）
- 新增一个 VLM（替换现有 VLM 实现）
- 新增一个 World Model（替换现有 World Model 实现）

整个系统继续运行。

那么，你们成功的就不是一个项目，而是一套真正意义上的 **Research Infrastructure（科研基础设施）**。

---

## 五、角色平衡

> **执行端（subagent 集群）负责让系统可以实现；**  
> **DeepSeek 负责让系统能够正确前行；**  
> **ChatGPT 负责让系统能够成长；**  
> **而你，负责让它拥有真正值得探索的科学问题。**

如果这四件事情始终保持平衡，这套平台未来承载的不只是 EQA、CRAIC 和毕业设计，而是你整个研究生涯最早、也最珍贵的一块基石。

---

## 附录：快速自检清单

每当面临一个设计决策时，问自己：

1. **Platform First**：这个改动会破坏平台吗？还是建立在平台之上？
2. **Interface Before Implementation**：接口定义了吗？评审了吗？写进 ICD 了吗？
3. **Everything Produces Knowledge**：这个模块的输出是 Knowledge 还是 Raw Data？
4. **Simulation is the First Robot**：仿真里跑过了吗？切换实机只需要改 Adapter 吗？
5. **Every Module Must Be Replaceable**：如果明天换掉这个算法，系统其他部分会受影响吗？
6. **ADR**：这个决策值得写一份 ADR 吗？

---

> *"We shape our tools, and thereafter our tools shape us." — John M. Culkin*
>
> 这份文档塑造了平台；希望平台也能塑造你的研究之路。

---

*最后更新：2026-07-25*  
*审阅者：ChatGPT（终审）*  
*起草者：DeepSeek（基于 ChatGPT 终审意见整理）*
