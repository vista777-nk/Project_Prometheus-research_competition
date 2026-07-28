# Task-13: CI 交叉编译流水线

> **状态：🔴 待开始** | **优先级：🥈 高** | **预计耗时：3h**
>
> **适用环境**：任意 OS（纯 YAML + Shell 脚本）
> **硬件依赖**：无
> **依赖**：task-10, task-11 的固件目录结构就位（可先搭建 CI 骨架，固件就位后自动激活）

---

## 前置条件

- 仓库已有 `.github/workflows/ci.yml`（Phase 0 的 ROS 编译测试）
- 了解 GitHub Actions 基础语法
- **不需要**：Ubuntu 20.04 桌面、真实 ARM/MSPM0 硬件

---

## 目标

将当前 CI 从单一的"ROS 编译 + 测试"扩展为多维度验证流水线：

| Job | 触发条件 | 运行环境 | 产物 |
|-----|:---:|------|------|
| `build-and-test-ros` | 每次 push/PR | ubuntu:20.04 容器 | 测试报告 |
| `build-stm32-firmware` | push/PR，`src/firmware/stm32_mecanum/**` 变更 | ubuntu-latest + arm-gcc | `stm32_mecanum.bin` |
| `build-mspm0-firmware` | push/PR，`src/firmware/mspm0_diff/**` 变更 | ubuntu-latest + arm-gcc | `mspm0_diff.bin` |
| `build-docker-edge` | push/PR，`src/deployment/docker/**` 变更 | ubuntu-latest + Docker | 镜像构建验证 |
| `lint-scripts` | 每次 push/PR | ubuntu-latest | Lint 报告 |

**原则**：每个固件 job 独立运行，互不阻塞。ROS job 不动（已有，仅重命名）。

---

## 架构影响

| 维度 | 内容 |
|------|------|
| **Affected Capability** | DevOps: CI/CD 验证流水线 — 覆盖 ROS、STM32、MSPM0、Docker、Lint 五大维度 |
| **Modified Interface** | 新增 `.github/workflows/ci.yml` 中的 4 个独立 job · 新增 `src/firmware/common/` 共享代码目录 |
| **New Dependency** | `gcc-arm-none-eabi` · `shellcheck` · `yamllint` · `docker/build-push-action` |
| **ADR Required** | ADR-0007: 选择 GitHub Actions over Jenkins/自建 CI 的理由 · ADR-0008: 固件共享代码目录 (`common/`) 设计 |
| **Risk Level** | 🟢 Low — CI 是只读验证，不修改任何源码；失败不阻塞其他 job |

> **铁律回顾 (RESEARCH_PHILOSOPHY.md §四「验证先行」)**：  
> 每个子系统都要有"无硬件可跑"的验证方式。CI 是验证先行的工程基础设施。

## 未来演进

| 维度 | 今天 (Phase 1) | 明天 (Phase 2+) |
|------|---------------|-----------------|
| **Replaceable Component** | GitHub Actions (免费 tier) | 自建 GitLab CI / Jenkins (私有仓库) · 硬件在环测试 (HIL) |
| **Permanent Interface** | CI job 命名约定 (`build-*-firmware`) · 固件 artifact 上传路径 · 单元测试 Makefile target (`make test`) | 保持不变 |
| **Temporary Implementation** | `ubuntu-latest` runner · apt 安装 arm-gcc · 无性能测试 | v2: 自托管 runner (树莓派 ARM64 原生) · FPGA/GPU CI · 性能回归测试 (cycle-accurate) · 覆盖率报告 (gcov/lcov) |

---

## 可执行步骤

### 13.1 共享固件目录 (`src/firmware/common/`)

在扩展 CI 之前，先建立固件项目的共享代码目录：

```bash
cd /path/to/research_compitition
mkdir -p src/firmware/common
cd src/firmware/common
```

创建以下共享模块：

```
src/firmware/common/
├── pid.c / pid.h            # PID 控制器 (task-10 和 task-11 共用)
├── crc16.c / crc16.h        # CRC16-CCITT (协议帧校验, 两个固件共用)
├── protocol_frame.c / .h    # 帧打包/解包 (SOF/EOF/转义, 共用)
└── test/
    └── unity.c / unity.h    # Unity Test 框架 (单头文件)
```

> **Subagent 注意**：`common/` 的代码应是无 HAL 依赖的纯 C99，能在 Host GCC 和 arm-none-eabi-gcc 下都能编译。如果 task-10 已完成但未提取 common，在此任务中完成提取重构。

### 13.2 扩展 CI 配置文件

编辑 `.github/workflows/ci.yml`，在现有 `build-and-test` job 之后追加以下新 job：

#### Job: build-stm32-firmware

```yaml
  build-stm32-firmware:
    name: Build STM32F407 Mecanum Firmware
    runs-on: ubuntu-latest
    needs: []  # 不依赖 ROS job，并行运行
    steps:
      - uses: actions/checkout@v4

      - name: Install ARM GCC Toolchain
        run: |
          sudo apt update
          sudo apt install -y gcc-arm-none-eabi binutils-arm-none-eabi

      - name: Verify Toolchain
        run: arm-none-eabi-gcc --version

      - name: Build STM32 Firmware
        run: |
          cd src/firmware/stm32_mecanum
          make -j$(nproc) CROSS_COMPILE=arm-none-eabi-

      - name: Run Host Unit Tests
        run: |
          cd src/firmware/stm32_mecanum
          make test

      - name: Upload Firmware Binary
        uses: actions/upload-artifact@v4
        with:
          name: stm32-mecanum-firmware
          path: |
            src/firmware/stm32_mecanum/build/*.bin
            src/firmware/stm32_mecanum/build/*.elf
          retention-days: 30
```

#### Job: build-mspm0-firmware

```yaml
  build-mspm0-firmware:
    name: Build MSPM0G3507 Diff Firmware
    runs-on: ubuntu-latest
    needs: []
    steps:
      - uses: actions/checkout@v4

      - name: Install ARM GCC Toolchain (for Cortex-M0+)
        run: |
          sudo apt update
          sudo apt install -y gcc-arm-none-eabi binutils-arm-none-eabi

      - name: Build MSPM0 Firmware
        run: |
          cd src/firmware/mspm0_diff
          make -j$(nproc) CROSS_COMPILE=arm-none-eabi- \
            CFLAGS="-mthumb -mcpu=cortex-m0plus -mfloat-abi=soft"

      - name: Run Host Unit Tests
        run: |
          cd src/firmware/mspm0_diff
          make test

      - name: Upload Firmware Binary
        uses: actions/upload-artifact@v4
        with:
          name: mspm0-diff-firmware
          path: |
            src/firmware/mspm0_diff/build/*.bin
            src/firmware/mspm0_diff/build/*.elf
          retention-days: 30
```

#### Job: build-docker-edge

```yaml
  build-docker-edge:
    name: Build Docker Edge Image
    runs-on: ubuntu-latest
    needs: []
    steps:
      - uses: actions/checkout@v4

      - name: Set up Docker Buildx
        uses: docker/setup-buildx-action@v3

      - name: Build Docker Image (dry-run, no push)
        uses: docker/build-push-action@v6
        with:
          context: .
          file: src/deployment/docker/Dockerfile.edge
          push: false
          load: false
          cache-from: type=gha
          cache-to: type=gha,mode=max
          tags: air-ground-edge:ci-test
```

#### Job: lint-scripts

```yaml
  lint-scripts:
    name: Lint Scripts
    runs-on: ubuntu-latest
    needs: []
    steps:
      - uses: actions/checkout@v4

      - name: ShellCheck
        run: |
          sudo apt update && sudo apt install -y shellcheck
          find . -name "*.sh" -not -path "./.git/*" -print0 |
            xargs -0 shellcheck -x -f tty

      - name: YAML Lint
        run: |
          pip3 install yamllint
          yamllint -d "{extends: relaxed, rules: {line-length: disable}}" \
            src/deployment/docker/docker-compose.edge.yml \
            src/**/config/*.yaml 2>/dev/null || true

      - name: systemd Unit Syntax Check
        if: always()
        run: |
          for unit in src/deployment/systemd/*.service; do
            echo "Checking: $unit"
            # Note: systemd-analyze verify requires systemd, available on ubuntu-latest
            systemd-analyze verify "$unit" 2>&1 || echo "(expected warnings for non-systemd host)"
          done
```

### 13.3 路径变更触发优化

为避免每次提交都跑全部 job，可使用 GitHub Actions 的 `paths` 过滤器：

```yaml
on:
  push:
    branches: [main, feat/*, fix/*]
    paths:
      - 'src/firmware/stm32_mecanum/**'
      - 'src/firmware/mspm0_diff/**'
      - 'src/firmware/common/**'
      - 'src/deployment/**'
      - 'src/air_ground_*/**'
      - '.github/workflows/ci.yml'
  pull_request:
    branches: [main]
  workflow_dispatch:
```

> **权衡**：用 `paths` 可节约 CI 分钟数，但可能导致"改 README 不改代码时 CI 不跑"的混淆。建议 Phase 1 先不用 `paths` 过滤器，等 CI 分钟数真的不够时再加。

### 13.4 Makefile 规范（供固件项目使用）

为两个固件项目提供统一的 `Makefile` 模板。以下以 STM32 为例，MSPM0 结构相同仅参数不同：

```makefile
# src/firmware/stm32_mecanum/Makefile
# 适用于 arm-none-eabi-gcc, Cortex-M4 (STM32F407)

CROSS_COMPILE ?= arm-none-eabi-
CC      = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy
SIZE    = $(CROSS_COMPILE)size

# --- MCU Flags ---
CPU     = -mcpu=cortex-m4
FPU     = -mfloat-abi=hard -mfpu=fpv4-sp-d16
CFLAGS  = $(CPU) $(FPU) -mthumb -O2 -g -Wall -Wextra
CFLAGS  += -DSTM32F407xx -DUSE_HAL_DRIVER
LDFLAGS = $(CPU) $(FPU) -T linker/STM32F407VETx_FLASH.ld -Wl,--gc-sections

# --- Sources ---
SRCS  = $(wildcard src/*.c) $(wildcard ../common/*.c)
OBJS  = $(SRCS:.c=.o)
TARGET = build/stm32_mecanum

# --- Rules ---
.PHONY: all clean test

all: $(TARGET).bin

$(TARGET).elf: $(OBJS)
	@mkdir -p build
	$(CC) $(LDFLAGS) -o $@ $^
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Host Unit Tests (用 Host gcc 编译, 不交叉编译)
test:
	@mkdir -p build
	gcc -o build/test_runner \
	    test/test_kinematics.c test/test_pid.c test/test_protocol.c \
	    src/kinematics.c src/pid.c src/protocol.c src/crc16.c \
	    ../common/unity.c \
	    -I src -I ../common -I test \
	    -Wall -Wextra -DTEST_HOST
	./build/test_runner

clean:
	rm -rf build/
```

### 13.5 CI Badge

在 `README.md` 顶部加入 CI 状态 badge（全部 job 绿灯时显示 passing）：

```markdown
[![CI](https://github.com/vista777-nk/research_compitition/actions/workflows/ci.yml/badge.svg)](https://github.com/vista777-nk/research_compitition/actions/workflows/ci.yml)
```

---

## 验收标准

- [ ] `build-stm32-firmware` job 成功编译并产出 `.bin` artifact
- [ ] `build-mspm0-firmware` job 成功编译并产出 `.bin` artifact
- [ ] `build-docker-edge` job 成功构建 Docker 镜像（无需 push）
- [ ] `lint-scripts` job 对已有脚本无 critical 错误
- [ ] 所有固件单元测试 job 在 CI 中通过（绿灯）
- [ ] 原有 ROS `build-and-test` job 不受影响（重命名可，删除不可）
- [ ] CI badge 在 README 中可见

---

## 给 Subagent 的执行建议

1. **不要动已有的 `build-and-test` ROS job**：它已经在跑 56 个单元测试 + 33 个 E2E
2. **新增 job 全部设置 `needs: []`**：固件编译不需要 ROS 环境，并行运行可节省时间
3. **gcc-arm-none-eabi 在 ubuntu-latest (22.04/24.04) 上可直接 apt 安装**，不需要手动下载
4. **MSPM0 是 Cortex-M0+（无 FPU）**：CFLAGS 必须加 `-mfloat-abi=soft`，否则链接失败
5. **Docker job 只做 build 验证**（不 push 到 registry），因为 Phase 1 还不需要镜像分发
6. **shellcheck 首次运行会报大量 warning**：优先修 critical/error 级别，style 级别可暂置
7. **`systemd-analyze verify` 在非 systemd 主机上有警告是正常的**，用 `|| echo` 容错

---

## 参考资料

| 文件 | 内容 |
|------|------|
| `.github/workflows/ci.yml` | 现有 CI（需扩展） |
| `src/firmware/stm32_mecanum/Makefile` (task-10 产出) | STM32 构建规则 |
| `src/firmware/mspm0_diff/Makefile` (task-11 产出) | MSPM0 构建规则 |
| `src/deployment/docker/Dockerfile.edge` (task-12 产出) | Docker 构建 |

---

*版本: v1.0 · 日期: 2026-07-28 · Phase 1 · 验证先行*
