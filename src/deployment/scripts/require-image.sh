#!/usr/bin/env bash
# =============================================================================
# require-image.sh — 启动前确认边缘节点镜像已在本地
#
# 为什么需要这个脚本:
# Phase 1 的镜像分发是**离线**的 (docker save/load 走 U 盘), 没有 registry。
# 若直接让 systemd 跑 `docker compose pull`, 在没有网络的场地上必然失败,
# 而 ExecStartPre 失败会让整个单元启动失败 —— 现象是"实验室里能起, 拉到
# 场地就起不来", 且日志里只有一句 manifest unknown。
#
# 这里改为检查本地镜像, 缺了就打印能照着执行的补救命令。
#
# 用法: require-image.sh [image-tag]
#       不传参时从环境变量 AIR_GROUND_IMAGE 取, 默认 air-ground-edge:v1
# =============================================================================
set -euo pipefail

IMAGE="${1:-${AIR_GROUND_IMAGE:-air-ground-edge:v1}}"

if ! command -v docker >/dev/null 2>&1; then
    echo "require-image: 找不到 docker 命令。" >&2
    echo "  安装: curl -fsSL https://get.docker.com | sudo sh" >&2
    exit 1
fi

if docker image inspect "${IMAGE}" >/dev/null 2>&1; then
    echo "require-image: ${IMAGE} 已就绪"
    exit 0
fi

cat >&2 <<EOF
require-image: 本地没有镜像 ${IMAGE}, 边缘节点无法启动。

Phase 1 用离线分发, 不从 registry 拉取。按下面两步之一处理:

  A) 从开发机把镜像拷过来 (推荐, 无需 Pi 上编译):
       开发机> docker save ${IMAGE} | gzip > edge.tar.gz
       开发机> sha256sum edge.tar.gz > edge.tar.gz.sha256    # 供 Pi 上校验
       # 拷到 U 盘, 插到树莓派上
       Pi>      /opt/air-ground/scripts/update-image.sh
       # 该脚本会自动找包、验 SHA-256、查架构、备份旧镜像后再载入。
       # 手工等价命令: gunzip -c /media/usb/edge.tar.gz | docker load

  B) 在树莓派上就地构建 (慢, 首次约 30-60 分钟):
       Pi> cd /opt/air-ground/repo
       Pi> docker build -f src/deployment/docker/Dockerfile.edge -t ${IMAGE} .

确认结果: docker image ls | grep air-ground-edge
EOF
exit 1
