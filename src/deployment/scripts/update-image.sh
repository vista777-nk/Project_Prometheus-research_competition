#!/usr/bin/env bash
# =============================================================================
# update-image.sh — 离线镜像更新 (U 盘 → docker load → 校验 → 可选重启)
#
# 背景: Phase 1 没有 registry, 镜像靠 `docker save | gzip` 走 U 盘分发
# (见 README §4.3)。手工敲那几条命令本身不难, 难的是每次都记得做完
# **全部**步骤 —— 而漏掉的那几步恰好是出事时最贵的:
#
#   · 忘了备份旧镜像       → 新镜像有问题时无法回滚, 场地上只能干等
#   · 忘了看架构           → x86 镜像 load 进 Pi 会成功, 直到容器启动才
#                            报 "exec format error", 现象像是代码坏了
#   · 忘了看剩余空间       → load 到一半磁盘满, docker 留下半个镜像
#   · load 完直接重启服务  → 打断正在跑的实验
#
# 所以这个脚本的价值不在"少敲几行", 在**把这四条变成必然执行**。
#
# 用法:
#   update-image.sh                          # 自动找 U 盘上的镜像包
#   update-image.sh /media/usb/edge-v2.tar.gz
#   update-image.sh --restart                # 载入后重启边缘节点 (会中断实验)
#   update-image.sh --rollback               # 回滚到上一个镜像
#   update-image.sh --dry-run                # 只报告要做什么
#
# 退出码: 0 成功 · 1 失败 · 2 用法错误
# =============================================================================
set -euo pipefail

IMAGE="${AIR_GROUND_IMAGE:-air-ground-edge:v1}"
BACKUP_TAG="${IMAGE%%:*}:previous"

# 树莓派 OS 自动挂载 U 盘的位置。/mnt 是手工挂载的习惯位置。
SEARCH_DIRS=(/media /mnt /run/media)

# Pi 是 ARM64。x86 镜像能 load 进来但跑不起来 —— 这是离线分发最常见的事故。
EXPECT_ARCH="${AIR_GROUND_EXPECT_ARCH:-arm64}"

ARCHIVE=""
DO_RESTART=0
DO_ROLLBACK=0
DRY_RUN=0
ASSUME_YES=0

die()  { echo "update-image: $*" >&2; exit 1; }

# 当前正在跑的边缘节点单元名。两个角色互为 Conflicts, 最多只会有一个。
# 找不到就返回空 —— 调用方据此决定是"无需重启"还是"提示手工起"。
active_edge_unit() {
    systemctl list-units --type=service --state=active --no-legend         'air-ground-*-edge.service' 2>/dev/null | awk '{print $1}' | head -1
}
note() { echo "[update-image] $*"; }
run()  {
    if [[ ${DRY_RUN} -eq 1 ]]; then
        echo "[dry-run] $*"
    else
        "$@"
    fi
}

usage() {
    # 打印文件头注释, 到第一个非注释行为止。
    # 不写死行号: 行号会随注释增删而漂, 漂了以后帮助信息就开始胡说八道。
    awk 'NR>1 { if ($0 !~ /^#/) exit; sub(/^# ?/, ""); print }' "$0"
    exit 2
}

# --- 参数 --------------------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --restart)  DO_RESTART=1 ;;
        --rollback) DO_ROLLBACK=1 ;;
        --dry-run)  DRY_RUN=1 ;;
        --yes|-y)   ASSUME_YES=1 ;;
        --image)
            shift
            [[ $# -gt 0 ]] || die "--image 后面要跟镜像 tag"
            IMAGE="$1"; BACKUP_TAG="${IMAGE%%:*}:previous"
            ;;
        -h|--help)  usage ;;
        -*)         echo "未知选项: $1" >&2; usage ;;
        *)          ARCHIVE="$1" ;;
    esac
    shift
done

command -v docker >/dev/null 2>&1 || die "找不到 docker 命令"

# --- 回滚 --------------------------------------------------------------------
# 放在最前面: 需要回滚的时候通常很急, 不该再被前面的检查拦一遍。
if [[ ${DO_ROLLBACK} -eq 1 ]]; then
    docker image inspect "${BACKUP_TAG}" >/dev/null 2>&1 \
        || die "没有可回滚的镜像 ${BACKUP_TAG} (上一次更新前才会生成)"

    note "回滚: ${BACKUP_TAG} → ${IMAGE}"
    run docker tag "${BACKUP_TAG}" "${IMAGE}"
    rb_unit="$(active_edge_unit)"
    if [[ ${DO_RESTART} -eq 1 && -n "${rb_unit}" ]]; then
        note "重启 ${rb_unit}"
        run sudo systemctl restart "${rb_unit}"
    else
        note "镜像已切回。生效需要重启边缘节点:"
        note "  sudo systemctl restart ${rb_unit:-air-ground-car-edge}"
    fi
    exit 0
fi

# --- 1. 找镜像包 --------------------------------------------------------------
if [[ -z "${ARCHIVE}" ]]; then
    note "未指定路径, 在 ${SEARCH_DIRS[*]} 下查找镜像包…"
    mapfile -t found < <(
        for d in "${SEARCH_DIRS[@]}"; do
            [[ -d "${d}" ]] || continue
            find "${d}" -maxdepth 4 -type f \
                \( -name '*.tar.gz' -o -name '*.tgz' -o -name '*.tar' \) \
                2>/dev/null
        done | sort
    )

    case "${#found[@]}" in
        0) die "没找到任何镜像包。把 edge-*.tar.gz 拷到 U 盘后重试, 或直接给路径。" ;;
        1) ARCHIVE="${found[0]}"; note "找到: ${ARCHIVE}" ;;
        *)
            echo "找到多个候选, 请显式指定一个:" >&2
            printf '  %s\n' "${found[@]}" >&2
            exit 2
            ;;
    esac
fi

[[ -f "${ARCHIVE}" ]] || die "文件不存在: ${ARCHIVE}"

# --- 2. 校验完整性 (若有 .sha256 旁挂文件) ------------------------------------
# U 盘拷贝中途拔出会留下一个大小差不多但内容截断的文件, docker load 的报错
# ("unexpected EOF") 看不出是传输问题还是镜像问题。有校验文件就一定要验。
SUMFILE="${ARCHIVE}.sha256"
if [[ -f "${SUMFILE}" ]]; then
    note "校验 SHA-256…"
    if [[ ${DRY_RUN} -eq 0 ]]; then
        ( cd "$(dirname "${ARCHIVE}")" && sha256sum -c "$(basename "${SUMFILE}")" ) \
            || die "SHA-256 不匹配 —— 传输过程中损坏了, 重新拷一遍"
    fi
else
    note "⚠ 没有 ${SUMFILE##*/}, 跳过完整性校验"
    note "  下次在开发机上顺手生成: sha256sum edge-v1.tar.gz > edge-v1.tar.gz.sha256"
fi

# --- 3. 磁盘空间 --------------------------------------------------------------
# 解压后的镜像通常是压缩包的 2–3 倍。留 3 倍余量, 不够就现在说, 别 load 到
# 一半磁盘满 —— 那会留下一堆无主 layer, 还得手工 docker system prune。
ARCHIVE_KB=$(du -k "${ARCHIVE}" | cut -f1)
NEED_KB=$(( ARCHIVE_KB * 3 ))
DOCKER_ROOT="$(docker info --format '{{.DockerRootDir}}' 2>/dev/null || echo /var/lib/docker)"
AVAIL_KB=$(df -Pk "${DOCKER_ROOT}" 2>/dev/null | awk 'NR==2 {print $4}')
AVAIL_KB="${AVAIL_KB:-0}"
if [[ "${AVAIL_KB}" -gt 0 && "${AVAIL_KB}" -lt "${NEED_KB}" ]]; then
    die "${DOCKER_ROOT} 剩余 $((AVAIL_KB/1024)) MB, 预计需要 $((NEED_KB/1024)) MB。
  先清理: docker image prune -a   (会删掉所有没有容器在用的镜像, 含 ${BACKUP_TAG})"
fi
note "空间检查通过 (需 ~$((NEED_KB / 1024)) MB, 可用 $((AVAIL_KB / 1024)) MB)"

# --- 4. 备份当前镜像 ----------------------------------------------------------
OLD_ID=""
if docker image inspect "${IMAGE}" >/dev/null 2>&1; then
    OLD_ID="$(docker image inspect --format '{{.Id}}' "${IMAGE}")"
    note "备份当前镜像 ${IMAGE} → ${BACKUP_TAG}"
    run docker tag "${IMAGE}" "${BACKUP_TAG}"
else
    note "本地还没有 ${IMAGE}, 这是首次载入"
fi

# --- 5. 载入 ------------------------------------------------------------------
note "载入 ${ARCHIVE} …(几百 MB 的包在 Pi 上约 1–3 分钟)"
if [[ ${DRY_RUN} -eq 1 ]]; then
    echo "[dry-run] gunzip -c ${ARCHIVE} | docker load"
else
    case "${ARCHIVE}" in
        *.tar.gz|*.tgz) gunzip -c "${ARCHIVE}" | docker load ;;
        *)              docker load -i "${ARCHIVE}" ;;
    esac
fi

# --- 6. 校验载入结果 ----------------------------------------------------------
if [[ ${DRY_RUN} -eq 0 ]]; then
    docker image inspect "${IMAGE}" >/dev/null 2>&1 || die "载入后仍然没有 ${IMAGE}。
  包里的 tag 可能和期望的不一致, 看看 docker image ls 里多出了什么, 然后:
    docker tag <实际tag> ${IMAGE}"

    NEW_ID="$(docker image inspect --format '{{.Id}}'           "${IMAGE}")"
    NEW_ARCH="$(docker image inspect --format '{{.Architecture}}' "${IMAGE}")"

    # 架构不对是离线分发最常见的事故: 开发机上忘了 --platform linux/arm64,
    # load 会成功, 一直到容器启动才报 exec format error。这里当场拦下。
    if [[ "${NEW_ARCH}" != "${EXPECT_ARCH}" ]]; then
        note "回滚到载入前的镜像…"
        if [[ -n "${OLD_ID}" ]]; then docker tag "${BACKUP_TAG}" "${IMAGE}"; fi
        die "镜像架构是 ${NEW_ARCH}, 本机需要 ${EXPECT_ARCH}。
  开发机上重新构建时带上平台参数:
    docker buildx build --platform linux/${EXPECT_ARCH} ... --load ."
    fi

    if [[ -n "${OLD_ID}" && "${NEW_ID}" == "${OLD_ID}" ]]; then
        note "⚠ 镜像 ID 与更新前完全相同 —— 这个包和已装的是同一个"
        note "  ${NEW_ID}"
        note "  (不是错误, 但如果你期望它有变化, 说明拷错了包)"
    fi

    note "载入完成"
    note "  ID   : ${NEW_ID}"
    note "  架构 : ${NEW_ARCH}"
    docker image inspect --format \
        '  构建 : {{index .Config.Labels "org.opencontainers.image.revision"}} @ {{index .Config.Labels "org.opencontainers.image.created"}}' \
        "${IMAGE}" 2>/dev/null || true
fi

# --- 7. 重启 ------------------------------------------------------------------
# 默认**不**重启。镜像更新常在实验间隙做, 而重启会掐掉正在跑的容器 ——
# 新镜像要到下次启动才生效, 这个延迟是有意的。
if [[ ${DO_RESTART} -eq 0 ]]; then
    note ""
    note "新镜像要下次启动才生效。方便的时候执行:"
    note "  sudo systemctl restart air-ground-car-edge     # 无人机换成 drone"
    note "出问题可回滚:  update-image.sh --rollback"
    exit 0
fi

ACTIVE="$(systemctl list-units --type=service --state=active --no-legend \
          'air-ground-*-edge.service' 2>/dev/null | awk '{print $1}' | head -1)"
if [[ -z "${ACTIVE}" ]]; then
    note "没有正在运行的边缘节点单元, 无需重启"
    exit 0
fi

if [[ ${ASSUME_YES} -eq 0 && ${DRY_RUN} -eq 0 ]]; then
    read -rp "重启 ${ACTIVE} 会中断正在跑的实验。继续? [y/N] " reply
    [[ "${reply}" == "y" || "${reply}" == "Y" ]] || { note "已取消, 镜像仍已载入"; exit 0; }
fi

note "重启 ${ACTIVE}"
run sudo systemctl restart "${ACTIVE}"
run sleep 5
if systemctl is-active --quiet "${ACTIVE}"; then
    note "${ACTIVE} 已起来"
else
    die "${ACTIVE} 没起来。看日志: journalctl -u ${ACTIVE} -n 50 --no-pager
  回滚: update-image.sh --rollback --restart"
fi
