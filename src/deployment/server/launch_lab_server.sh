#!/usr/bin/env bash
# 启动 Phase 1.5 实验室服务器裸机 ROS 栈；供 systemd 与人工恢复共同调用。
set -euo pipefail

fail() {
    printf '[air-ground-server] %s\n' "$1" >&2
    exit 1
}

repo_root="${AIR_GROUND_REPO:-}"
[[ -n "${repo_root}" ]] || fail "AIR_GROUND_REPO 未设置"
[[ -r "${repo_root}/scripts/setup_runtime.sh" ]] ||
    fail "仓库运行环境脚本不存在: ${repo_root}/scripts/setup_runtime.sh"

network_config="${AIR_GROUND_NETWORK_CONFIG:-${repo_root}/src/air_ground_com_bridge/config/network_server.yaml}"
server_config="${AIR_GROUND_SERVER_CONFIG:-${repo_root}/src/air_ground_lab_server/config/server_params.yaml}"
[[ -r "${network_config}" ]] || fail "服务器网络配置不存在: ${network_config}"
[[ -r "${server_config}" ]] || fail "服务器参数配置不存在: ${server_config}"

export ROS_MASTER_URI="${ROS_MASTER_URI:-http://127.0.0.1:11311}"
export ROS_IP="${ROS_IP:-127.0.0.1}"
export ROS_LOG_DIR="${ROS_LOG_DIR:-/data2/air-ground-server/ros-log}"

# TCP JSON 尚无 TLS/认证；跨校区只允许受控隧道。ROS 1 更不能直接跨 WAN 暴露
# Master 和随机 TCPROS 端口。若在隔离实验室局域网确有需要，必须显式解锁。
if [[ "${AIR_GROUND_ALLOW_REMOTE_ROS:-0}" != "1" ]]; then
    [[ "${ROS_IP}" == "127.0.0.1" ]] ||
        fail "ROS_IP=${ROS_IP} 非回环；跨校区默认拒绝暴露 ROS（见 ADR-0017）"
    case "${ROS_MASTER_URI}" in
        http://127.0.0.1:11311|http://localhost:11311) ;;
        *) fail "ROS_MASTER_URI=${ROS_MASTER_URI} 非回环；跨校区默认拒绝远程 ROS" ;;
    esac
fi

[[ -d "${ROS_LOG_DIR}" && -w "${ROS_LOG_DIR}" ]] ||
    fail "ROS_LOG_DIR 不存在或不可写: ${ROS_LOG_DIR}"

export AIR_GROUND_WS="${repo_root}"
# shellcheck disable=SC1091
source "${repo_root}/scripts/setup_runtime.sh" ros
cd "${repo_root}"

roslaunch_bin="${AIR_GROUND_ROSLAUNCH:-roslaunch}"
exec "${roslaunch_bin}" air_ground_bringup lab-server-real.launch \
    "network_config:=${network_config}" \
    "server_config:=${server_config}"
