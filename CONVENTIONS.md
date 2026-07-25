# CONVENTIONS.md �?项目规范与约�?

> **本文档定义本项目的四项基础规范�?*
>
> 如果你是新加入的贡献者（包括未来的你），这是第一份应该读的文档�?
> 如果你要做一个设计决策但不确定怎么做，先查这份文档�?
> 如果这份文档没有覆盖你的情况，做出选择后，**把新约定加进�?*�?

---

## 目录

- [一、Repository（仓库规范）](#一repository仓库规范)
- [二、Workspace（工作空间规范）](#二workspace工作空间规范)
- [三、Naming（命名规范）](#三naming命名规范)
- [四、Convention（编码与文档规范）](#四convention编码与文档规�?

---

## 一、Repository（仓库规范）

### 1.1 分支策略

本项目的分支模型是最简化的 GitHub Flow�?

```
main ────────────────────────────── �?(稳定，永远可部署)
     \
      feat/task-01-env-setup ──●──�?(开发完成后合并�?main，分支删�?
```

| 分支类型 | 命名格式 | 用�?| 生命周期 |
|----------|----------|------|:---:|
| `main` | �?| 稳定主线，永远可运行 | 永久 |
| `feat/<description>` | `feat/task-03-diff-chassis` | 新功�?/ 新任�?| 合并后删�?|
| `fix/<description>` | `fix/icd-typo` | Bug 修复 | 合并后删�?|
| `docs/<description>` | `docs/adr-0002` | 纯文档变�?| 合并后删�?|
| `exp/<description>` | `exp/vlm-baseline-test` | 实验性分支（可能不合入） | 视情况保留或删除 |

**铁律**�?
- `main` 分支永远不直接推送。所有变更通过分支 + 合并进入�?
- 合并前确�?`main` 上的代码可以编译 / 仿真可以启动（至少冒烟测试通过）�?
- 实验分支 (`exp/`) 可以不合�?`main`，但**不能删除**——保留为实验记录�?

### 1.2 Commit Message 规范

遵循 [Conventional Commits](https://www.conventionalcommits.org/) 1.0.0，使用简体中文描述：

```
<type>(<scope>): <简短描�?

<详细说明（可选）>

<脚注（可选）>
```

**type 类型**�?

| Type | 用�?| 示例 |
|------|------|------|
| `feat` | 新功�?| `feat(task-02): 添加 PX4 SITL 无人�?Gazebo 模型` |
| `fix` | Bug 修复 | `fix(bridge): 修复 MAVLink 心跳超时导致的重连失败` |
| `docs` | 文档变更 | `docs(philosophy): 新增 RESEARCH_PHILOSOPHY.md` |
| `refactor` | 重构（不改变功能�?| `refactor(sensors): 将传感器配置从硬编码改为 YAML` |
| `test` | 测试 | `test(e2e): 添加空地通信端到端测试` |
| `chore` | 杂项（构建、依赖、工具） | `chore(ci): 添加 .gitignore` |
| `style` | 格式调整 | `style: PEP 8 格式化所�?Python 文件` |
| `perf` | 性能优化 | `perf(bridge): 减少 JSON 序列化开销` |

**scope 范围**：用任务编号 (`task-03`)、模块名 (`bridge`, `sensors`, `planner`)、或文档类型 (`philosophy`, `icd`, `roadmap`)�?

**示例**�?

```
feat(task-04): 实现麦轮底盘逆运动学控制�?

- 新增 mecanum_controller.py
- 支持 /car/cmd_vel �?四轮转速解�?
- 添加底盘类型动态切换服�?/car/switch_chassis
- 通过 task-04 冒烟测试

Ref: task-04-mecanum-chassis.md
```

### 1.3 Tag 策略

| Tag 格式 | 含义 | 示例 |
|----------|------|------|
| `v<major>.<minor>.<patch>` | 平台版本发布 | `v1.0.0` |
| `paper/<name>-submit` | 论文提交时的代码快照 | `paper/eqa-icra2027-submit` |
| `milestone/<name>` | 里程碑节�?| `milestone/sim-framework-done` |

---

## 二、Workspace（工作空间规范）

### 2.1 仓库目录约定

```
research_compitition/                   # 仓库根目�?(Git root)
├── README.md                           # 项目简介与导航
├── CONVENTIONS.md                      # 本文�?
├── .gitignore                          # Git 忽略规则
�?
├── Research_Diary.md                   # 科研日记 (持续更新)
├── Memo_on_Division_of_Labor_Suggestions.md  # 备忘录类文档
�?
├── Project_Prometheus_Tasks/           # 📁 核心项目文档
�?  ├── 00-OVERVIEW.md                  #   总索�?
�?  ├── RESEARCH_PHILOSOPHY.md          #   设计哲学 (宪法)
�?  ├── PLATFORM.md                     #   平台架构
�?  ├── ICD.md                          #   接口控制文档
�?  ├── ROADMAP.md                      #   研究路线
�?  └── task-01~09-*.md                 #   实施任务
�?
├── docs/                               # 📁 长期文档
�?  ├── decisions/                      #   ADR (架构决策记录)
�?  �?  ├── ADR-0001.md                 #
�?  �?  └── ...
�?  └── experiments/                    #   实验记录 (未来)
�?      └── ...
�?
├── Obsolete_or_Outdated_Documentation/ # 📁 归档 (历史文档，只�?
�?
└── src/                                # 📁 源代�?(未来，ROS workspace 映射)
    └── ...
```

### 2.2 目录命名约定

| 规则 | 示例 | 说明 |
|------|------|------|
| **顶层目录�?kebab-case** | `project-docs/`, `obsolete-docs/` | 全小�?+ 连字�?|
| **Git 仓库内的 ROS package 目录例外** | `air_ground_interfaces/` | �?snake_case，遵�?ROS 惯例 |
| **不以数字开�?* | �?`1-docs/` �?�?`docs/` | |
| **不嵌套过�?* | 最大深�?3 �?| `docs/decisions/ADR-0001.md` �?|

> **注意**：当前仓库中 `Project_Prometheus_Tasks/` �?`Obsolete_or_Outdated_Documentation/` 使用了混合命名风格。这是历史遗留，将在合适时机（例如 v1.0.0 发布前）统一重命名�?*在新创建的目录中，严格遵�?kebab-case�?*

### 2.3 ROS 工作空间映射

仿真代码�?ROS 工作空间位于 `~/air_ground_sim_ws/`（Ubuntu 端），其 `src/` 目录通过 Git submodule 或符号链接与仓库�?`src/` 关联�?

```
~/air_ground_sim_ws/          # ROS 工作空间 (�?Ubuntu �?
├── src/
�?  ├── air_ground_interfaces/   # 自定义消息包
�?  ├── air_ground_drone_bringup/              # 无人机仿�?
�?  ├── air_ground_car_bringup/                 # 车机仿真
�?  ├── air_ground_com_bridge/              # 通信�?
�?  ├── air_ground_lab_server/             # 边缘预处�?+ 服务�?
�?  └── ...
├── build/
├── devel/
└── setup_all.sh
```

> **约定**：ROS package 在仓库中按功能分组，每个 package 对应一个目录�?

---

## 三、Naming（命名规范）

### 3.1 文件命名

| 文件类型 | 命名格式 | 示例 |
|----------|----------|------|
| Markdown 文档 | `UPPER_SNAKE_CASE.md` (核心文档) | `PLATFORM.md`, `ICD.md`, `ROADMAP.md` |
| 任务文档 | `kebab-case.md` | `task-01-env-setup.md` |
| Python 模块 | `snake_case.py` | `world_model.py`, `mecanum_controller.py` |
| Launch 文件 | `kebab-case.launch` | `full-sim.launch`, `drone-only.launch` |
| YAML 配置 | `snake_case.yaml` | `sensor_config.yaml`, `robot_params.yaml` |
| Shell 脚本 | `snake_case.sh` | `setup_all.sh`, `test_bridge.sh` |
| URDF/Xacro | `snake_case.urdf.xacro` | `diff_chassis.urdf.xacro` |
| ADR 文档 | `ADR-NNNN-kebab-case.md` | `ADR-0001-adopt-conventional-commits.md` |

### 3.2 ROS 命名

#### Package

```
air_ground_<功能>
```

| Package | 层级 | 功能 |
|---------|:---:|------|
| `air_ground_interfaces` | Layer 3 | 自定义消�?服务定义 |
| `air_ground_drone_bringup` | Layer 1 | 无人机仿�?|
| `air_ground_car_bringup` | Layer 1 | 车机仿真 |
| `air_ground_com_bridge` | Layer 2 | 空地/车服通信�?|
| `air_ground_lab_server` | Layer 2~3 | 边缘预处�?+ 服务�?|
| `air_ground_vlm` | Layer 4 | VLM 研究模块 |
| `air_ground_slam` | Layer 4 | SLAM 研究模块 |
| `air_ground_planner` | Layer 4 | Planner 研究模块 |

**铁律**�?
- 所�?package �?`air_ground_` 为前缀，避免与 ROS 官方包或第三方包冲突�?
- Layer 4 package 永远不依�?`mavros`、`gazebo_msgs`、`sensor_msgs` 等硬件相关包�?

#### Topics

```
/<robot_id>/<namespace>/<specific>
```

| 模式 | 示例 |
|------|------|
| 传感器数�?| `/drone/rgb/image_raw` |
| 抽象观测 | `/drone/observation` |
| 机器人状�?| `/car/robot_state` |
| 控制指令 | `/car/cmd_vel` |
| 世界状�?| `/world_state` |
| 任务/使命 | `/mission` |

**铁律**�?
- Topic 名用 `snake_case`�?
- �?`/robot_id` 为第一级命名空间（`drone` / `car`）�?
- 全局 Topic（如 `/world_state`, `/mission`）不�?robot_id 前缀�?
- 抽象接口�?(Layer 3) �?Topic 命名遵循 ICD.md�?

#### Nodes

```
<robot_id>_<功能>
```

| Node | 示例 |
|------|------|
| `drone_sensor_fusion` | 无人机传感器融合节点 |
| `car_sensor_fusion` | 车机传感器融合节�?|
| `mavlink_bridge` | MAVLink 协议翻译节点 |
| `tcp_bridge` | TCP 通信桥节�?|
| `world_model` | 世界模型节点 |
| `vlm_inference` | VLM 推理节点 |

#### Services

```
/<robot_id>/<动词>_<宾语>
```

| 模式 | 示例 |
|------|------|
| 切换底盘 | `/car/switch_chassis` |
| 起飞/降落 | `/drone/takeoff`, `/drone/land` |
| 查询能力 | `/car/get_capability` |

### 3.3 代码命名

| 元素 | 格式 | 示例 |
|------|------|------|
| Python �?| `PascalCase` | `WorldModel`, `ObservationAggregator` |
| Python 函数/方法 | `snake_case` | `process_observation()`, `update_world_state()` |
| Python 变量 | `snake_case` | `robot_id`, `latest_observation` |
| Python 常量 | `UPPER_SNAKE_CASE` | `MAX_FREQUENCY_HZ`, `DEFAULT_TIMEOUT_S` |
| C++ �?| `PascalCase` | `MecanumController` |
| C++ 函数 | `camelCase` �?`snake_case` | （与 ROS 现有代码风格一致） |
| ROS 消息字段 | `snake_case` | `robot_id`, `timestamp`, `modalities` |

---

## 四、Convention（编码与文档规范�?

### 4.1 Python 编码规范

- 严格遵循 **PEP 8**�?
- 使用 **4 空格缩进**（不�?Tab）�?
- 最大行�?**100 字符**（ROS 惯例，比标准 PEP 8 �?79 略宽）�?
- 所有公共函�?�?**必须�?docstring**（Google Style �?NumPy Style）�?
- 鼓励使用 **Type Hints**（Python 3.8+ 支持）�?

```python
"""传感器观测聚合节点�?

将多个传感器 Topic 聚合为统一�?Observation 消息�?
�?ICD v1.0 规范发布�?/<robot_id>/observation�?

Note:
    本节点属�?Layer 2 (Bridge)，负责协议翻译�?
    Layer 4 研究代码不直接依赖本节点的输出格式�?
"""

from typing import Optional, List
from air_ground_interfaces.msg import Observation


class ObservationAggregator:
    """聚合多个传感器源为统一 Observation 消息�?

    Attributes:
        robot_id: 机器人标识符 ("drone" �?"car")�?
        max_frequency_hz: 最大发布频�?(Hz)�?
    """

    def __init__(self, robot_id: str, max_frequency_hz: float = 10.0) -> None:
        """初始化聚合器�?

        Args:
            robot_id: 机器人标识符�?
            max_frequency_hz: 最大发布频率，默认 10 Hz�?
        """
        self.robot_id = robot_id
        self.max_frequency_hz = max_frequency_hz

    def process_observation(self, modalities: List[str]) -> Optional[Observation]:
        """处理并聚合一次多模态观测�?

        Args:
            modalities: 本次携带的模态列表，�?["rgb", "depth", "imu"]�?

        Returns:
            聚合后的 Observation 消息，若数据不完整则返回 None�?
        """
        ...
```

### 4.2 注释语言

| 内容 | 语言 | 理由 |
|------|:---:|------|
| 变量名、函数名、类�?| **English** | 编程语言惯例，ROS 生态兼�?|
| 模块/�?docstring | **中文** | 项目团队母语，降低理解门�?|
| 行内注释 | **中文** | 同上 |
| Commit message | **中文** | 同上 |
| Git 分支�?| **English** | 工具链兼容�?|
| Markdown 文档 | **中文** | 同上 |

### 4.3 ROS 规范

- 优先使用 **rospy**（Python），核心算法可用 **roscpp**（C++）�?
- 所有参数从 **YAML 配置文件** 读取，不硬编码�?
- 每个 package 必备文件�?
  ```
  <package>/
  ├── CMakeLists.txt          # 构建规则
  ├── package.xml             # 包元数据
  ├── launch/                 # Launch 文件
  �?  └── <name>.launch
  ├── config/                 # YAML 配置文件
  �?  └── <name>.yaml
  ├── src/                    # 源代�?
  �?  └── <node>.py
  └── test/                   # 测试脚本
      └── test_<name>.sh
  ```
- 传感器断连时 **`rospy.logwarn` 警告**，不 crash�?
- 仿真传感器默�?**�?30 Hz**，优�?headless 模式�?

### 4.4 测试规范

- 每个 task 完成后必须通过**冒烟测试**（`test_<task>.sh`）�?
- 冒烟测试至少验证：节点启动、Topic 发布/订阅、无 crash 退出�?
- 端到端测试脚本：`src/e2e_test.sh`（task-09 产出）�?
- 测试结果记录�?`docs/experiments/` 中�?

### 4.5 文档规范

- **ADR 不可�?*：已采纳�?ADR 永远不修改，只由�?ADR 替代。详�?[RESEARCH_PHILOSOPHY.md §二](./Project_Prometheus_Tasks/RESEARCH_PHILOSOPHY.md#二architecture-decision-records-adr)�?
- **Obsolete 不删�?*：过时的文档移入 `Obsolete_or_Outdated_Documentation/`，保留历史�?
- **日记持续更新**：`Research_Diary.md` 记录关键决策、遇到的问题、解决思路。格式自由，�?*必须写日�?*�?

### 4.6 代码审查清单

合并�?`main` 前，确认以下项目�?

- [ ] 通过了所�?task 的冒烟测�?
- [ ] 新模块在 ICD.md 中有对应的接口定义（如果需要）
- [ ] �?package 遵循 `air_ground_` 命名前缀
- [ ] Layer 4 代码没有 import 硬件相关�?
- [ ] 参数全部�?YAML 读取，无硬编�?
- [ ] 公共函数/类有 docstring
- [ ] Commit message 符合 Conventional Commits 格式
- [ ] 相关文档已更�?

---

## 附录：快速查�?

### 我想知道某个东西该怎么命名…�?

| 场景 | 查这�?|
|------|--------|
| 新建一�?Git 分支 | [§1.1 分支策略](#11-分支策略) |
| 写一�?commit message | [§1.2 Commit Message 规范](#12-commit-message-规范) |
| 新建一个文�?目录 | [§2.2 目录命名约定](#22-目录命名约定) + [§3.1 文件命名](#31-文件命名) |
| 新建一�?ROS package | [§3.2 ROS 命名 �?Package](#package) |
| 新建一�?ROS topic | [§3.2 ROS 命名 �?Topics](#topics) |
| 写一�?Python �?函数 | [§3.3 代码命名](#33-代码命名) + [§4.1 Python 编码规范](#41-python-编码规范) |
| 合并代码�?main �?| [§4.6 代码审查清单](#46-代码审查清单) |

---

> *"命名和缓存失效是计算机科学的两大难题�? �?Phil Karlton*
>
> 本文档旨在消除第一个�?

---

*版本: v1.0 · 日期: 2026-07-25 · 起草�? DeepSeek（依�?ChatGPT 终审意见整理�?
