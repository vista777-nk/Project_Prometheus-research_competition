# 空地联合具身智能研究平台 — 总索引

> 本文档是项目入口。详细内容已拆分为三个专题文件。

## 📖 必读文件

| 文件 | 内容 | 更新频率 |
|------|------|:---:|
| [**PLATFORM.md**](./PLATFORM.md) | 平台架构（分层、包结构、通信、部署） | 🟢 很少 |
| [**ROADMAP.md**](./ROADMAP.md) | 研究路线（EQA → 竞赛 → World Model → 毕设） | 🔴 经常 |
| [**ICD.md**](./ICD.md) | 接口控制文档（Observation / Mission / WorldState / Capability） | 🟡 稳定 |

## 📋 实施任务清单

以下 9 个 task 是仿真框架的搭建步骤，按依赖顺序排列：

| 任务 | 内容 | 依赖 | 预计耗时 |
|------|------|------|---------|
| [task-01](./task-01-env-setup.md) | 环境搭建 + ROS 工作空间脚手架 | 无 | 1h |
| [task-02](./task-02-drone-sitl.md) | PX4 SITL 无人机仿真 + 传感器插件 | task-01 | 2h |
| [task-03](./task-03-diff-chassis.md) | 差速底盘仿真（电赛规格） | task-01 | 2h |
| [task-04](./task-04-mecanum-chassis.md) | 麦轮底盘仿真 + 模块化切换 | task-03 | 2h |
| [task-05](./task-05-sensors.md) | 传感器插件完整配置 | task-02,03 | 1.5h |
| [task-06](./task-06-com-bridge.md) | 空地通信桥（MAVLink + TCP） | task-02,03 | 2h |
| [task-07](./task-07-edge-server.md) | 边缘预处理 + 服务器节点 | task-01,06 | 2h |
| [task-08](./task-08-integration.md) | 集成总装 Launch + Makefile | task-02~07 | 1.5h |
| [task-09](./task-09-validation.md) | 仿真验证 + 测试脚本 | task-08 | 1h |

### 依赖拓扑

```
task-01 ──┬── task-02 ──┬── task-05 ──┐
          │             │              │
          └── task-03 ──┤              ├── task-06 ── task-07 ── task-08 ── task-09
                         │              │
                         └── task-04 ──┘
```

## 🚀 快速开始

```bash
# 设置环境 (仅首次)
bash ~/air_ground_sim_ws/setup_all.sh

# 编译
cd ~/air_ground_sim_ws && make build

# 一键启动
make launch-full

# 端到端测试
bash src/e2e_test.sh
```

## 🔧 给 Subagent 的通用规范

1. **代码风格**：Python 3.8+，PEP 8；ROS 节点使用 `rospy`
2. **注释语言**：英文变量名 + 中文模块 docstring
3. **分层铁律**：Layer 4 (Research) 不 import `mavros` / `gazebo_msgs`
4. **容错**：传感器断连 `rospy.logwarn`，不 crash
5. **轻量化**：仿真传感器默认 ≤ 30Hz，Gazebo headless
6. **可配置**：所有参数从 `config/*.yaml` 读取
7. **带测试**：每个 task 含 `test_*.sh` 验证脚本

---

*版本: v4.0 · 日期: 2026-07-25 · 经 ChatGPT 、 混元3 、 豆包审阅后重构*
