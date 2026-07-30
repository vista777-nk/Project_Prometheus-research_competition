#!/usr/bin/env bash
# =============================================================================
# wait-for-device.sh — 等待设备节点出现, 超时后**大声**失败
#
# 存在的理由: systemd 的 `Requires=dev-xxx.device` 在设备不出现时是静默阻塞的,
# 单元停在 inactive、journal 里什么也没有。在场地上排查这个非常费时间。
# 这里换成显式等待 + 一条说明该查什么的日志。
#
# 用法:
#   wait-for-device.sh [--timeout N] [--warn-only] /dev/pixhawk [/dev/mcu ...]
#
#   --timeout N   最长等待秒数, 默认 30
#   --warn-only   超时只警告并返回 0 (不阻止服务启动)。
#                 用于飞控这类"没有它其余部分仍有意义"的设备 ——
#                 相机、数传、遥测在飞控没接时照样该跑起来。
#                 不加此参数则超时返回 1, 由调用方 (systemd ExecStartPre) 中止启动。
# =============================================================================
set -euo pipefail

TIMEOUT=30
WARN_ONLY=0
DEVICES=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout)
            TIMEOUT="${2:?--timeout 需要一个秒数}"
            shift 2
            ;;
        --warn-only)
            WARN_ONLY=1
            shift
            ;;
        -h|--help)
            sed -n '2,20p' "$0"
            exit 0
            ;;
        -*)
            echo "wait-for-device: 未知选项 $1" >&2
            exit 2
            ;;
        *)
            DEVICES+=("$1")
            shift
            ;;
    esac
done

if [[ ${#DEVICES[@]} -eq 0 ]]; then
    echo "wait-for-device: 至少要给一个设备路径" >&2
    exit 2
fi

all_present() {
    local d
    for d in "${DEVICES[@]}"; do
        [[ -e "${d}" ]] || return 1
    done
    return 0
}

elapsed=0
while ! all_present; do
    if [[ ${elapsed} -ge ${TIMEOUT} ]]; then
        missing=()
        for d in "${DEVICES[@]}"; do
            [[ -e "${d}" ]] || missing+=("${d}")
        done

        {
            echo "wait-for-device: 等待 ${TIMEOUT}s 后仍缺失: ${missing[*]}"
            echo "  按顺序查这几项:"
            echo "    1) 线接了吗 —— lsusb 看设备在不在总线上"
            echo "    2) udev 规则装了吗 —— ls -l /dev/ | grep -E 'pixhawk|mcu|telem'"
            echo "       没有符号链接就是规则没生效:"
            echo "         sudo udevadm control --reload-rules && sudo udevadm trigger"
            echo "    3) VID/PID 对得上吗 —— udevadm info -a -n /dev/ttyACM0 | grep -m2 idVendor"
            echo "       对不上就改 network/99-air-ground-devices.rules 里的 idVendor/idProduct"
        } >&2

        if [[ ${WARN_ONLY} -eq 1 ]]; then
            echo "wait-for-device: --warn-only, 继续启动 (相关功能将不可用)" >&2
            exit 0
        fi
        exit 1
    fi
    sleep 1
    elapsed=$((elapsed + 1))
done

echo "wait-for-device: 全部就绪 (${DEVICES[*]}), 耗时 ${elapsed}s"
exit 0
