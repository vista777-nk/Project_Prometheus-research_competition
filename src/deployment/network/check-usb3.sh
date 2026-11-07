#!/usr/bin/env bash
# =============================================================================
# check-usb3.sh — 确认 RealSense D435i 挂在 USB3 口上
#
# D435i 插在 USB2 口上不会报错, 只会**悄悄降质**: 深度分辨率/帧率被砍,
# 或者出现周期性丢帧。这类问题在数据里表现为"SLAM 偶尔飘", 极难反查到接口上。
#
# 与 task-12 §12.5 原文的差异: 原文只扫描 /sys 下有没有任何 5000M 端口,
# 树莓派5 本身就有 USB3 口, 所以那个检查**恒为真** —— 相机插在 USB2 上也会
# 报"✓ USB3 SuperSpeed port found"。这里改为定位相机自己所在的那个端口。
#
# 用法: check-usb3.sh [vendor_id]   默认 8086 (Intel RealSense)
# 退出码: 0=在 USB3 上  1=在 USB2 上  2=没找到设备
# =============================================================================
set -euo pipefail

VENDOR="${1:-8086}"

found=0
degraded=0

for devdir in /sys/bus/usb/devices/*/; do
    [[ -f "${devdir}idVendor" ]] || continue
    [[ "$(cat "${devdir}idVendor")" == "${VENDOR}" ]] || continue

    found=1
    product="$(cat "${devdir}product" 2>/dev/null || echo '未知型号')"
    speed="$(cat "${devdir}speed" 2>/dev/null || echo '?')"

    case "${speed}" in
        5000|10000)
            echo "✓ ${product}: ${speed}M (SuperSpeed, USB3)"
            ;;
        480)
            echo "✗ ${product}: 480M (High-Speed, USB2) —— 带宽不足" >&2
            degraded=1
            ;;
        *)
            echo "? ${product}: ${speed}M (无法判定)" >&2
            degraded=1
            ;;
    esac
done

if [[ ${found} -eq 0 ]]; then
    echo "未找到 VID=${VENDOR} 的 USB 设备。" >&2
    echo "  相机没插, 或者 VID 不对 —— 用 lsusb 看看实际的厂商 ID。" >&2
    exit 2
fi

if [[ ${degraded} -eq 1 ]]; then
    echo "" >&2
    echo "把相机换到树莓派5 的**蓝色** USB3 口上 (靠外侧那两个)。" >&2
    echo "换口后重新枚举需要几秒, 再跑一次本脚本确认。" >&2
    exit 1
fi

exit 0
