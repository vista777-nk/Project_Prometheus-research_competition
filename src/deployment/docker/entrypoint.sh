#!/bin/bash
# =============================================================================
# entrypoint.sh — 按环境变量决定这个容器是无人机边缘还是车机边缘
#
# 设计原则: **启动失败必须说人话**。
# 边缘节点跑在一台没有显示器、可能还挂在无人机上的树莓派里,
# 一条 "RLException: unused args" 对现场的人毫无帮助。
# 因此这里在 roslaunch 之前把所有能自查的前提逐条查掉, 并打印可执行的下一步。
# =============================================================================
set -euo pipefail

# shellcheck disable=SC1091
source "${AIR_GROUND_ROS_SETUP:-/opt/ros/noetic/setup.bash}"
# shellcheck disable=SC1091
source "${AIR_GROUND_WS_SETUP:-/home/airground/catkin_ws/devel/setup.bash}"

ROLE="${AIR_GROUND_ROLE:-car}"
CHASSIS="${AIR_GROUND_CHASSIS:-diff}"
EDGE_MODE="${EDGE_MODE:-real}"      # real=真硬件 | mock=假硬件(降级) | sim=仿真
export ROS_MASTER_URI="${ROS_MASTER_URI:-http://localhost:11311}"

# ROS_IP 不设的话, 多网卡 (WiFi + 4G + 以太网) 的树莓派会随机挑一个地址注册到
# Master, 结果是 Master 能看到节点、但订阅方连不上它。实机上必须显式指定。
if [[ -z "${ROS_IP:-}" ]]; then
    echo "[entrypoint] 启动中止: ROS_IP 未设置" >&2
    echo "[entrypoint]   在 /opt/air-ground/.env 写入本机对机器人可达的固定地址。" >&2
    echo "[entrypoint]   不允许由 ROS 在多网卡设备上猜测通告地址。" >&2
    exit 1
fi
export ROS_IP

echo "=========================================="
echo " Air-Ground Edge Node"
echo "   Role      : ${ROLE}"
echo "   Chassis   : ${CHASSIS}"
echo "   Edge mode : ${EDGE_MODE}"
echo "   Master    : ${ROS_MASTER_URI}"
echo "   ROS_IP    : ${ROS_IP:-<unset, 多网卡下有风险>}"
echo "   Image     : ${AIR_GROUND_BUILD_COMMIT:-local} @ ${AIR_GROUND_BUILD_TIME:-unknown}"
echo "=========================================="

# --- 降级状态上报 -----------------------------------------------------------
#
# 为什么需要这个: 降级发生在 roslaunch **之前**, 此刻一个 ROS 话题都还没有,
# 一条 echo 只会留在容器日志里, 上位机和健康检查都看不见。
#
# 写进 /home/airground/.ros/log/ —— 这是 compose 里 bind 到宿主机
# /var/log/air-ground 的目录, 于是容器外的 healthcheck/check_nodes.py 读得到。
#
# 运行期能力应由 task-14 已交付的 Capability 消息表达；这个文件只记录
# roslaunch 之前就能确定的启动模式与镜像身份，供容器外健康检查读取。
STATE_DIR="${AIR_GROUND_STATE_DIR:-/home/airground/.ros/log}"
STATE_FILE="${STATE_DIR}/edge-state.env"

DEGRADED=0
DEGRADED_REASON=""

# 每次启动都重写, 而不是只在降级时写 —— 否则上一次降级留下的文件会一直
# 挂在那里, 让一次已经修好的部署永远显示为降级。
write_state() {
    mkdir -p "${STATE_DIR}" 2>/dev/null || true
    {
        echo "# 由 entrypoint.sh 在每次容器启动时重写。手工改它没有意义。"
        echo "AIR_GROUND_ROLE=${ROLE}"
        echo "AIR_GROUND_CHASSIS=${CHASSIS}"
        echo "AIR_GROUND_EDGE_MODE=${EDGE_MODE}"
        echo "AIR_GROUND_LAUNCH=${LAUNCH_FILE:-}"
        echo "AIR_GROUND_SENSOR_BACKEND=${SENSOR_BACKEND:-}"
        echo "AIR_GROUND_IMAGE=${AIR_GROUND_BUILD_COMMIT:-local}"
        echo "AIR_GROUND_DEGRADED=${DEGRADED}"
        echo "AIR_GROUND_DEGRADED_REASON=${DEGRADED_REASON}"
    } > "${STATE_FILE}" 2>/dev/null || {
        # 写不进去不该拦住启动 —— 状态文件是附加信息, 不是启动前提。
        echo "[entrypoint] ⚠ 无法写状态文件 ${STATE_FILE}, 健康检查将看不到降级信息" >&2
        echo "[entrypoint]   检查 compose 的 volumes: 是否挂了 /var/log/air-ground" >&2
    }
}

# --- 前置自检 ---------------------------------------------------------------

die() {
    echo "[entrypoint] 启动中止: $1" >&2
    shift
    for line in "$@"; do
        echo "[entrypoint]   $line" >&2
    done
    exit 1
}

# 期望存在的设备。缺了不一定致命 (可能这次就是想跑没接传感器的空转),
# 所以只警告不退出 —— 但要警告得足够显眼。
warn_missing_device() {
    local dev="$1" what="$2"
    if [[ ! -e "${dev}" ]]; then
        echo "[entrypoint] ⚠ 设备缺失: ${dev} (${what})" >&2
        echo "[entrypoint]   容器内看不到该设备的常见原因:" >&2
        echo "[entrypoint]     1) 宿主机上确实没插" >&2
        echo "[entrypoint]     2) compose 的 devices: 段没挂进来" >&2
        echo "[entrypoint]     3) udev 规则没生效, 设备名漂移了 (见 network/99-air-ground-devices.rules)" >&2
    fi
}

case "${ROLE}" in
    car)
        warn_missing_device /dev/mcu "下位机 STM32/MSPM0 串口"
        ;;
    drone)
        warn_missing_device /dev/pixhawk "Pixhawk 6C 飞控"
        ;;
esac

# --- 启动 -------------------------------------------------------------------

# roslaunch 对命令行传入的未声明参数是**硬错误** (RLException: unused args),
# 所以参数名必须和 launch 文件里的 <arg name=...> 完全一致。
#   car_edge.launch  声明的是 default_chassis, 不是 chassis
#   drone_edge.launch 只声明 config_file, 不接受底盘参数
# task-12 §12.2 原文写的是 `chassis:=`, 照抄会在第一次启动就失败。
case "${ROLE}" in
    car)
        LAUNCH_PKG="air_ground_car_bringup"
        SENSOR_BACKEND=""
        case "${EDGE_MODE}" in
            real)
                LAUNCH_FILE="car_edge_real.launch"
                SENSOR_BACKEND="real"
                ;;
            mock)
                LAUNCH_FILE="car_edge_real.launch"
                SENSOR_BACKEND="mock"
                DEGRADED=1
                DEGRADED_REASON="EDGE_MODE=mock —— 传感器节点使用假硬件后端，数据不得作为实机证据"
                ;;
            sim)
                LAUNCH_FILE="car_edge.launch"
                ;;
            *)
                die "未知车机模式 '${EDGE_MODE}'" \
                    "EDGE_MODE 只接受 real、mock 或 sim。" \
                    "实机部署用 real；无硬件接口冒烟用 mock；仿真用 sim。"
                ;;
        esac

        package_path="$(rospack find "${LAUNCH_PKG}" 2>/dev/null)" ||
            die "找不到 ROS package '${LAUNCH_PKG}'" \
                "确认镜像中的 catkin 工作空间已成功构建。"
        if [[ ! -f "${package_path}/launch/${LAUNCH_FILE}" ]]; then
            die "找不到 ${LAUNCH_PKG}/launch/${LAUNCH_FILE}" \
                "EDGE_MODE=${EDGE_MODE} 不会自动降级；修复镜像或显式选择其他模式。"
        fi

        write_state
        launch_args=("default_chassis:=${CHASSIS}")
        if [[ -n "${SENSOR_BACKEND}" ]]; then
            launch_args+=("backend:=${SENSOR_BACKEND}")
        fi
        exec roslaunch "${LAUNCH_PKG}" "${LAUNCH_FILE}" "${launch_args[@]}"
        ;;

    drone)
        case "${EDGE_MODE}" in
            real|sim) ;;
            mock)
                die "无人机角色不支持 EDGE_MODE=mock" \
                    "无人机的 SITL 与 Pixhawk 都经 MAVROS 适配；请选择 real 或 sim。"
                ;;
            *)
                die "未知无人机模式 '${EDGE_MODE}'" \
                    "EDGE_MODE 只接受 real 或 sim。"
                ;;
        esac
        LAUNCH_FILE="drone_edge.launch"
        SENSOR_BACKEND="mavros"
        write_state
        exec roslaunch air_ground_drone_bringup "${LAUNCH_FILE}"
        ;;

    *)
        die "未知角色 '${ROLE}'" \
            "AIR_GROUND_ROLE 只接受 car 或 drone。" \
            "检查 systemd unit 的 Environment= 或 compose 的 environment: 段。"
        ;;
esac
