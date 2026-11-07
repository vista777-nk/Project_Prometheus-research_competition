#!/usr/bin/env bash
# =============================================================================
# alert.sh — 健康检查的告警出口
#
# 由 air-ground-healthcheck.service 的 ExecStopPost 调用，
# 从 $SERVICE_RESULT / $EXIT_STATUS 拿上一步的结果。
#
# 设计取舍: **只告警, 不自动重启边缘节点。**
# 网络抖一下就把正在跑的实验掐掉, 代价比多亮一会儿灯大得多。
# 真正的自动降级(本地缓存模式)是 Phase 2 的事, 见 task-12 评审建议 4。
#
# 告警通道按"现场看得见"排序:
#   1. journald  —— 一定有, 事后可查
#   2. 板载 LED  —— 现场一眼可见, 不需要接电脑
#   3. 蜂鸣器    —— 可选, 默认关 (无人机上没人想听它叫)
# =============================================================================
set -uo pipefail   # 刻意不加 -e: 告警链路里任何一环失败都不该让整条中断

# systemd 在 ExecStopPost 里注入这两个变量; 手动运行时给个默认值
EXIT_STATUS="${EXIT_STATUS:-0}"
SERVICE_RESULT="${SERVICE_RESULT:-success}"

# 与 agcheck.py 的退出码约定保持一致
case "${EXIT_STATUS}" in
    0) LEVEL="ok";      MESSAGE="边缘节点健康" ;;
    1) LEVEL="critical"; MESSAGE="ROS Master 不可达" ;;
    2) LEVEL="warning";  MESSAGE="必需话题缺发布者" ;;
    3) LEVEL="warning";  MESSAGE="健康检查配置错误" ;;
    # 4 = 降级运行。刻意**不**升到 critical: 节点在跑、数据在发, 只是能力不全。
    # 亮红灯会让现场以为要停飞, 而实际处置是"记下来, 别把结论当全功能实机结果"。
    4) LEVEL="warning";  MESSAGE="降级运行 (能力集不完整, 见 /var/log/air-ground/edge-state.env)" ;;
    *) LEVEL="warning";  MESSAGE="健康检查异常退出 (status=${EXIT_STATUS}, result=${SERVICE_RESULT})" ;;
esac

# --- 1. journald -------------------------------------------------------------
if command -v systemd-cat >/dev/null 2>&1; then
    echo "[${LEVEL}] ${MESSAGE}" | systemd-cat -t air-ground-alert -p "${LEVEL/ok/info}"
else
    echo "[${LEVEL}] ${MESSAGE}"
fi

[[ "${LEVEL}" == "ok" ]] && exit 0

# --- 2. 板载 LED -------------------------------------------------------------
# 树莓派的绿色 ACT 灯。写 trigger 需要 root, 非 root 时静默跳过 ——
# 告警脚本本身不该因为权限问题而失败。
LED_TRIGGER="/sys/class/leds/ACT/trigger"
if [[ -w "${LED_TRIGGER}" ]]; then
    case "${LEVEL}" in
        critical) echo heartbeat > "${LED_TRIGGER}" 2>/dev/null || true ;;
        warning)  echo timer     > "${LED_TRIGGER}" 2>/dev/null || true ;;
    esac
fi

# --- 3. 蜂鸣器 (可选) --------------------------------------------------------
# 默认关闭。无人机在飞的时候没人想听它叫, 而且它也盖不过桨叶声。
# 需要时在 .env 里设 AIR_GROUND_BUZZER=1 并接好 GPIO。
if [[ "${AIR_GROUND_BUZZER:-0}" == "1" && "${LEVEL}" == "critical" ]]; then
    BUZZER_GPIO="${AIR_GROUND_BUZZER_GPIO:-529}"   # Pi5 的 GPIO 编号有偏移, 见 README
    if [[ -w "/sys/class/gpio/gpio${BUZZER_GPIO}/value" ]]; then
        for _ in 1 2 3; do
            echo 1 > "/sys/class/gpio/gpio${BUZZER_GPIO}/value" 2>/dev/null || true
            sleep 0.2
            echo 0 > "/sys/class/gpio/gpio${BUZZER_GPIO}/value" 2>/dev/null || true
            sleep 0.2
        done
    fi
fi

exit 0
