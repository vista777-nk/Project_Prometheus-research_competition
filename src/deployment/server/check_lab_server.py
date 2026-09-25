#!/usr/bin/env python3
"""检查实验室服务器本地 ROS 节点与 TCP 接收端是否同时健康。"""

import argparse
import json
import os
import socket
import sys
import time
import xmlrpc.client
from typing import Dict, Iterable, Set


REQUIRED_NODES = {
    "/coordinator",
    "/eqa_engine",
    "/slam_node",
    "/tcp_server",
    "/world_model",
}
FORBIDDEN_NODES = {
    "/drone_car_bridge",
    "/edge_server_bridge",
}


def registered_nodes(master_uri: str) -> Set[str]:
    """从 ROS Master 的 system state 收集所有已注册节点。"""
    master = xmlrpc.client.ServerProxy(master_uri, allow_none=True)
    code, message, state = master.getSystemState(
        "/air_ground_lab_server_healthcheck"
    )
    if code != 1:
        raise RuntimeError(f"ROS Master 拒绝查询: {message}")
    nodes: Set[str] = set()
    for registrations in state:
        for _, owners in registrations:
            nodes.update(str(owner) for owner in owners)
    return nodes


def tcp_accepts(host: str, port: int, timeout: float = 1.0) -> bool:
    """建立并立即关闭 TCP 连接，验证监听端真实可接受连接。"""
    with socket.create_connection((host, port), timeout=timeout):
        return True


def evaluate(nodes: Iterable[str], tcp_ok: bool) -> Dict:
    """把观测事实转换成稳定、可测试的健康结论。"""
    node_set = set(nodes)
    missing = sorted(REQUIRED_NODES - node_set)
    forbidden = sorted(FORBIDDEN_NODES & node_set)
    return {
        "ok": not missing and not forbidden and tcp_ok,
        "missing_nodes": missing,
        "forbidden_nodes": forbidden,
        "tcp_accepting": bool(tcp_ok),
        "registered_nodes": sorted(node_set),
    }


def check_once(master_uri: str, host: str, port: int) -> Dict:
    """执行一次 ROS 与 TCP 联合检查，异常作为结构化错误返回。"""
    result = {
        "ok": False,
        "master_uri": master_uri,
        "tcp_endpoint": f"{host}:{port}",
    }
    try:
        nodes = registered_nodes(master_uri)
        tcp_ok = tcp_accepts(host, port)
        result.update(evaluate(nodes, tcp_ok))
    except (OSError, RuntimeError, xmlrpc.client.Error) as error:
        result["error"] = str(error)
    return result


def format_human(result: Dict) -> str:
    """生成适合 journalctl 和远程终端的一行结论。"""
    if result.get("ok"):
        return (
            "[OK] 实验室服务器健康：5 个服务节点，TCP "
            f"{result['tcp_endpoint']} 可接受连接"
        )
    details = []
    if result.get("missing_nodes"):
        details.append(f"缺节点={result['missing_nodes']}")
    if result.get("forbidden_nodes"):
        details.append(f"误启动客户端={result['forbidden_nodes']}")
    if result.get("tcp_accepting") is False:
        details.append("TCP 未监听")
    if result.get("error"):
        details.append(f"错误={result['error']}")
    return "[FAIL] 实验室服务器不健康：" + "；".join(details or ["未知错误"])


def main() -> int:
    """解析环境/命令行，在等待窗口内轮询并返回机器可读退出码。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--master-uri",
        default=os.environ.get("ROS_MASTER_URI", "http://127.0.0.1:11311"),
    )
    parser.add_argument(
        "--host",
        default=os.environ.get("AIR_GROUND_HEALTH_HOST", "127.0.0.1"),
    )
    parser.add_argument(
        "--port",
        type=int,
        default=int(os.environ.get("AIR_GROUND_HEALTH_PORT", "9090")),
    )
    parser.add_argument("--wait", type=float, default=0.0)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or args.wait < 0.0:
        parser.error("port 必须为 1..65535，wait 必须非负")

    deadline = time.monotonic() + args.wait
    while True:
        result = check_once(args.master_uri, args.host, args.port)
        if result.get("ok") or time.monotonic() >= deadline:
            break
        time.sleep(min(1.0, max(0.0, deadline - time.monotonic())))

    if args.json:
        print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    else:
        print(format_human(result))
    return 0 if result.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
