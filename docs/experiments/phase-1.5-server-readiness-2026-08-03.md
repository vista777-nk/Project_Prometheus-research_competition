# Phase 1.5 中关村服务器收口报告（2026-08-03）

> 本报告是硬件工作迁移到良乡 Raspberry Pi 前的服务器状态快照。它不替代
> [滚动硬件基线](./phase-1.5-hardware-baseline.md) 或
> [Pi AI 交接](./AI_HANDOFF.md)，也不表示 Phase 1.5 已完成。

## 1. 结论

服务器侧可在无人值守状态继续提供本地 ROS/TCP 基础服务：用户 systemd 服务和周期
健康检查均 enabled + active，五个项目服务节点齐全，ROS Master 11311 与 TCP 9090
只监听回环。工作区当前可继续开发，远端仓库 URL 已从旧重定向地址改为当前正式地址。

不能带走的风险有两项：

1. 根分区已用 97%，约剩 80 GiB；`/data2` 约剩 1.1 TiB。bag、镜像、模型、标定数据
   和 ROS 日志必须放 `/data2`，不得继续消耗根分区。
2. 良乡↔中关村的受控隧道、断线恢复和三机时间偏差尚未实测。服务器本地健康不能
   证明跨校区链路健康。

## 2. CI 未自动启动的根因与修复

### 2.1 证据

- `.github/workflows/ci.yml` 原来只在 `main`、`feat/*`、`fix/*` push 时触发；
- 当前分支为 `task-new`，不匹配上述任何模式；
- GitHub 公共 API 在审计时返回 `task-new` 的 CI workflow run 数为 **0**；
- 最近一次 `CI — Build & Test` push run 是 2026-08-02 的 `feat/task-XX` run #41，
  说明 workflow 本身 active，不是 Actions 被仓库级禁用。

这符合 GitHub Actions 的过滤语义：配置 `push.branches` 后，只有匹配分支才触发。
手动 `workflow_dispatch` 的存在不会使 push 自动运行。

### 2.2 修复

- 移除 `push.branches` 白名单，改为任意分支 push 都触发；
- 保留 `pull_request.branches: [main]`，合并门禁不变；
- 在 `scripts/smoke_test_phase1.sh` 增加契约守卫：必须存在 push 事件，且该事件下
  不得再出现 `branches` 或 `branches-ignore`；
- 没有只追加 `task-new`，因为那会在下一次分支改名时复发，而且旧白名单本就漏掉
  `CONVENTIONS.md` 允许的 `docs/*`、`exp/*`。

修复只有推送后才能由 GitHub 产生新 run；提交前本地验证结果见 §6。

## 3. 仓库与归档状态

| 项 | 审计结果 |
|---|---|
| 工作目录 | `/data2/project/vista/project200/project-prometheus` |
| 分支 | `task-new`，审计基线 `1317718` 与 `origin/task-new` 同步 |
| 正式远端 | `https://github.com/vista777-nk/Project_Prometheus-research_competition.git` |
| 旧交接 | `AI_HANDOFF.md` 与 `CLAUDE.md` 的旧内容已归档到 `obsolete-documentation/` |
| 新交接 | `docs/experiments/AI_HANDOFF.md` 已重写为 Raspberry Pi 专用实机交接；不恢复 live `CLAUDE.md` |
| 历史 ADR | 不修改；原有指向 `AI_HANDOFF.md` 的链接继续指向新的 live 交接入口 |

README、SECURITY、部署手册与边缘 systemd 的旧仓库 URL 已同步为正式远端。任务书正文
中的旧目录名属于开工时历史方案，按项目规则不改正文；执行时以新交接和当前 README
为准。

## 4. 常驻服务审计

2026-08-03 02:52–02:57 CST 的只读审计结果：

| 检查 | 结果 |
|---|---|
| `air-ground-lab-server.service` | enabled / active / running，MainPID 存在，当前 `NRestarts=0` |
| `air-ground-lab-server-healthcheck.timer` | enabled / active / waiting，约每 6 分钟触发 |
| ROS 节点 | `/coordinator`、`/eqa_engine`、`/slam_node`、`/tcp_server`、`/world_model` + `/rosout` |
| 禁止节点 | 0；没有本地 edge/client 自连进程 |
| TCP 健康 | `127.0.0.1:9090` 可接受连接 |
| 监听面 | rosmaster `127.0.0.1:11311`；tcp_receiver `127.0.0.1:9090` |
| 健康日志 | 2026-08-02 抽查记录持续为 `[OK]`，service 每次退出成功 |

机器可读健康输出：

```json
{"forbidden_nodes": [], "master_uri": "http://127.0.0.1:11311", "missing_nodes": [], "ok": true, "registered_nodes": ["/coordinator", "/eqa_engine", "/rosout", "/slam_node", "/tcp_server", "/world_model"], "tcp_accepting": true, "tcp_endpoint": "127.0.0.1:9090"}
```

## 5. 主机资源与运行环境

| 项 | 审计值 | 判断 |
|---|---:|---|
| 根分区 `/` | 2.7 TiB，总用 2.5 TiB，剩 80 GiB，97% | 风险；禁止新增大文件 |
| `/data2` | 1.9 TiB，总用 688 GiB，剩 1.1 TiB，39% | 项目大数据落盘位置 |
| 内存 | 503 GiB，总可用约 480 GiB | 充足 |
| Swap | 2.0 GiB，已用约 2.0 GiB | 非阻塞；当前 RAM 仍充足，后续观察异常换页 |
| 时区/同步 | Asia/Shanghai，`NTPSynchronized=yes` | 服务器本机正常；仍需与两台 Pi 比对 |
| 运行时间 | 约 41 天 | 稳定，但不能代替断电恢复演练 |
| 当前 Catkin 产物 | `build` 25 MiB、`devel` 5.2 MiB、`logs` 2.5 MiB | 体积小；`install` 当前不存在 |

## 6. 提交前验证

本次改动完成后的最终命令与结果记录在这里，便于 Pi 端判断“服务器最后验证了什么”：

| 验证 | 结果 |
|---|---|
| Catkin build / ROS unit | 6/6 package 构建成功；82 tests，0 error/failure/skip |
| Phase 1 冒烟 | 65/65，0 failure，0 skip；MAVLink 签名真实执行 |
| 部署静态校验 | 55/55，0 failure，0 skip |
| STM32 Host | 80/80 |
| MSPM0 Host | 71/71 |
| 传感器 Host | 81/81 |
| Lint | ShellCheck 0.10.0、yamllint 1.38.0、flake8 7.1.1 均零发现 |
| CI 触发守卫负向测试 | 合成 `push.branches: [main]` 被正确识别为受限配置 |
| Markdown 本地链接 / `git diff --check` | 345 个本地链接零缺失；diff 检查零发现 |
| 复测后服务器健康 | service/timer 仍 active；健康 JSON `ok=true`；11311/9090 仍仅回环 |
| GitHub CI | 本报告提交并 push 后才会出现；不得提前写成通过 |

## 7. 留给 Pi 端的服务器相关工作

1. 从良乡通过获批隧道连接 `127.0.0.1:9090` 的服务器端入口；不暴露 ROS 1。
2. 做一次主动断隧道、恢复和重复消息/积压检查，记录恢复时间。
3. 让两台 Pi 与服务器对同一时间参考源，记录三机最大偏差；现有 chrony 样例不能
   代替实测。
4. 确认 Pi 侧本地安全在服务器断开时仍独立成立。
5. 服务器产生的新 ROS 日志、bag、镜像和模型继续落到 `/data2`，并监控根分区。

## 8. 快速复核命令

```bash
systemctl --user is-enabled air-ground-lab-server.service \
  air-ground-lab-server-healthcheck.timer
systemctl --user is-active air-ground-lab-server.service \
  air-ground-lab-server-healthcheck.timer
python3 src/deployment/server/check_lab_server.py --json
ss -ltn 'sport = :11311 or sport = :9090'
df -h / /data2
timedatectl show -p NTPSynchronized -p Timezone
```

任何节点缺失、TCP 不可接受连接、端口不再只监听回环，或健康 timer inactive，都应先
恢复服务器基线，再进行跨校区联调。
