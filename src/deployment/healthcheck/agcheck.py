#!/usr/bin/env python3
"""空地边缘节点健康判定 —— 纯逻辑部分。

设计意图
--------
把"怎么判定健康"和"怎么去问 ROS Master"分开，理由与固件那边把寄存器访问
收拢进移植层是同一条（见 ADR-0004 §决策-3）：**判定逻辑是最容易出错、
又最难在现场复现的部分**，它不该和网络 I/O 绑在一起。

分开之后 `test_healthcheck.py` 可以在任何有 Python 3 的机器上跑，
不需要 ROS、不需要网络、不需要树莓派。

职责边界
--------
本模块**不**负责判断容器进程是否存活——那已经由 systemd `Restart=always`
和 docker 管了。这里管的是**语义级**健康：

* ROS Master 还应答吗？
* 本角色该发的话题，发布者还在吗？

这两件事挂掉时进程往往还活得好好的，重启策略完全看不见。
"""

from __future__ import annotations

from typing import Iterable, NamedTuple, Sequence

# --- 退出码约定 (systemd unit 与告警脚本都依赖这组值) ------------------------
EXIT_OK = 0
EXIT_MASTER_DOWN = 1
EXIT_TOPICS_MISSING = 2
EXIT_USAGE = 3
#: 节点活着、话题齐全, 但**能力集不完整**(例: 实机传感器 launch 缺失, 已降级)。
#: 单独给一个码而不是复用 0/2, 是因为它既不是"健康"也不是"故障":
#: 实验可以继续跑, 但结论不能当成实机全功能下的结论。
#: systemd unit 里把 4 列进 SuccessExitStatus —— 降级是**预期状态**, 不该刷 failed。
EXIT_DEGRADED = 4

#: 各角色必须存在发布者的话题。
#:
#: 取自 config/car_edge.yaml 与 config/drone_edge.yaml 的 topics 段——
#: 改那两个文件时必须同步改这里，否则健康检查会盯着一个不存在的话题报警。
#: capability 是 latched 话题，只在启动时发一次，因此它的"有发布者"
#: 恰好是"节点起来过"的可靠指标。
REQUIRED_TOPICS: dict[str, tuple[str, ...]] = {
    "car": ("/car/observation", "/car/state", "/car/capability"),
    "drone": ("/drone/observation", "/drone/state", "/drone/capability"),
}


#: 边缘容器在启动时写下的状态文件名。
#:
#: 容器内路径 /home/airground/.ros/log/edge-state.env, 经 compose 的 bind mount
#: 在宿主机上就是 /var/log/air-ground/edge-state.env —— 健康检查跑在**容器外**,
#: 靠这个文件跨过容器边界读到 entrypoint 的自检结论。
#: 用文件而不是 ROS 话题, 是因为降级恰恰发生在 roslaunch **之前**,
#: 那时还没有任何话题可发。
STATE_FILE_NAME = "edge-state.env"


class DegradedState(NamedTuple):
    """entrypoint 写下的降级结论。"""

    degraded: bool
    reason: str


def parse_state_file(text: str | None) -> DegradedState:
    """解析 ``KEY=VALUE`` 形式的状态文件内容。

    传 ``None`` 表示文件不存在 —— 那**不算降级**。老版本镜像不写这个文件,
    把"没有文件"判成降级会让所有存量部署一起变黄。
    真正的容器没起来会由话题缺失 (EXIT_TOPICS_MISSING) 报出来, 那条优先级更高。
    """
    if not text:
        return DegradedState(False, "")

    values: dict[str, str] = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        values[key.strip()] = value.strip().strip('"').strip("'")

    flag = values.get("AIR_GROUND_DEGRADED", "0")
    return DegradedState(
        degraded=flag not in ("", "0", "no", "false", "False"),
        reason=values.get("AIR_GROUND_DEGRADED_REASON", "").strip(),
    )


class HealthReport(NamedTuple):
    """一次健康检查的结论。"""

    exit_code: int
    summary: str
    details: tuple[str, ...]

    @property
    def healthy(self) -> bool:
        return self.exit_code == EXIT_OK


def parse_master_uri(uri: str) -> tuple[str, int]:
    """把 ``http://host:port`` 拆成 (host, port)。

    单独抽出来是因为这里错得很隐蔽：ROS_MASTER_URI 少写 ``http://`` 时，
    ``xmlrpc.client.ServerProxy`` 会抛一句 "unsupported XML-RPC protocol"，
    与"Master 挂了"的表现完全不同，但排查时很容易混为一谈。

    :raises ValueError: URI 格式不合法，附带能直接照着改的说明
    """
    if not uri or not isinstance(uri, str):
        raise ValueError("ROS_MASTER_URI 为空。检查 /opt/air-ground/.env")

    stripped = uri.strip()
    if "://" not in stripped:
        raise ValueError(
            f"ROS_MASTER_URI 缺少协议头: {stripped!r}\n"
            f"  应该写成 http://{stripped}  (ROS 1 的 Master 走 XML-RPC over HTTP)"
        )

    scheme, _, rest = stripped.partition("://")
    if scheme not in ("http", "https"):
        raise ValueError(f"ROS_MASTER_URI 协议应为 http, 实际是 {scheme!r}")

    rest = rest.rstrip("/")
    host, sep, port_text = rest.partition(":")
    if not host:
        raise ValueError(f"ROS_MASTER_URI 没有主机名: {stripped!r}")
    if not sep:
        # ROS Master 默认端口
        return host, 11311

    try:
        port = int(port_text)
    except ValueError as exc:
        raise ValueError(
            f"ROS_MASTER_URI 端口不是数字: {port_text!r}"
        ) from exc

    if not (0 < port < 65536):
        raise ValueError(f"ROS_MASTER_URI 端口越界: {port}")

    return host, port


def extract_published_topics(system_state: Sequence) -> set[str]:
    """从 Master 的 ``getSystemState`` 结果里取出**有发布者**的话题名。

    返回结构是 ``[publishers, subscribers, services]``，其中
    ``publishers`` 形如 ``[[topic, [node, ...]], ...]``。

    只统计发布者列表非空的话题：一个话题可能因为有人订阅而出现在结构里，
    但没有任何发布者——那正是我们要报出来的故障。
    """
    if not system_state:
        return set()

    publishers = system_state[0] if len(system_state) > 0 else []
    topics: set[str] = set()
    for entry in publishers or ():
        if not entry or len(entry) < 2:
            continue
        name, nodes = entry[0], entry[1]
        if name and nodes:
            topics.add(str(name))
    return topics


def required_topics(role: str) -> tuple[str, ...]:
    """取某个角色必须存在的话题列表。未知角色返回空元组。"""
    return REQUIRED_TOPICS.get(role, ())


def evaluate(
    *,
    master_reachable: bool,
    master_error: str = "",
    role: str = "car",
    published: Iterable[str] | None = None,
    degraded: DegradedState | None = None,
) -> HealthReport:
    """综合判定健康状态。

    判定顺序是有意的：Master 不可达时**不再检查话题**。
    此时话题列表必然是空的，若一并报出来，一次网络抖动会同时产生
    "Master 不可达" + "三个话题全丢" 四条告警，把真正的根因淹掉。

    降级 (``EXIT_DEGRADED``) 排在最后：它是"功能不全"，不是"坏了"。
    Master 掉线或话题缺失时，先报那个——降级信息此刻只会分散注意力。
    """
    if not master_reachable:
        return HealthReport(
            exit_code=EXIT_MASTER_DOWN,
            summary="ROS Master 不可达",
            details=(
                master_error or "无错误详情",
                "按顺序查: 1) 服务器开着吗 2) 网线/WiFi 通吗 (ping)",
                "          3) ROS_MASTER_URI 对吗 (cat /opt/air-ground/.env)",
            ),
        )

    expected = required_topics(role)
    if not expected:
        return HealthReport(
            exit_code=EXIT_USAGE,
            summary=f"未知角色 {role!r}",
            details=(
                f"AIR_GROUND_ROLE 只接受: {', '.join(sorted(REQUIRED_TOPICS))}",
            ),
        )

    present = set(published or ())
    missing = tuple(t for t in expected if t not in present)

    if missing:
        return HealthReport(
            exit_code=EXIT_TOPICS_MISSING,
            summary=f"Master 正常, 但 {len(missing)}/{len(expected)} 个必需话题没有发布者",
            details=(
                "缺失: " + ", ".join(missing),
                "Master 活着而话题没了, 通常是边缘节点自己崩了或还没起完:",
                "  journalctl -u air-ground-*-edge -n 50 --no-pager",
                "  docker compose -f /opt/air-ground/docker/docker-compose.edge.yml ps",
            ),
        )

    if degraded is not None and degraded.degraded:
        return HealthReport(
            exit_code=EXIT_DEGRADED,
            summary=f"降级运行 (role={role}, 话题齐全但能力集不完整)",
            details=(
                degraded.reason or "entrypoint 未给出原因",
                "节点在跑, 数据在发, 但缺失的那部分能力不会有数据 ——",
                "拿这次实验的结论时要把这一条写进去。",
                f"来源: /var/log/air-ground/{STATE_FILE_NAME} (由容器 entrypoint 写入)",
            ),
        )

    return HealthReport(
        exit_code=EXIT_OK,
        summary=f"健康 (role={role}, {len(expected)} 个话题均有发布者)",
        details=tuple(f"  ✓ {t}" for t in expected),
    )


def format_report(report: HealthReport, timestamp: str) -> str:
    """渲染成一行标题 + 若干缩进详情，供 journald 记录。"""
    if report.healthy:
        marker = "OK"
    elif report.exit_code == EXIT_DEGRADED:
        marker = "DEGRADED"
    else:
        marker = "FAIL"
    lines = [f"[{timestamp}] {marker}: {report.summary}"]
    lines.extend(f"  {d}" for d in report.details)
    return "\n".join(lines)
