# Task-13: CI 交叉编译流水线

> **状态：✅ 已完成（2026-07-31）** | **优先级：🥈 高** | **实际耗时：~1h**
>
> **适用环境**：任意 OS（纯 YAML + Shell 脚本）
> **硬件依赖**：无
> **依赖**：task-10, task-11 的固件目录结构就位（可先搭建 CI 骨架，固件就位后自动激活）
>
> ⚠ **执行前必读**：本文档写于 2026-07-28。到实际执行时，文档里要求新建的 5 个 job
> **已有 4 个被 task-10/11/12 顺带建好**，真正缺的只有 `lint-scripts`。
> 请先读文末的[「与原方案的偏差」](#与原方案的偏差2026-07-31-实际执行)再动手，
> 不要照抄「可执行步骤」——照抄会重复建 job 并覆盖掉更强的已有实现。

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

创建以下共享模块（**权威布局 — task-10/11 以此为准**）：

```
src/firmware/common/               ← 共享库, 无 HAL 依赖的纯 C99
├── pid.h                          # PID 控制器头文件 (task-10/11 共用)
├── pid.c                          # PID 控制器实现
├── crc16.h                        # CRC-16/CCITT-FALSE 头文件
├── crc16.c                        # CRC 实现 (ADR-0003 标准)
├── protocol_frame.h               # 帧打包/解包头文件
├── protocol_frame.c               # SOF/EOF/转义/拆帧 (共用)
├── unity.h                        # Unity Test 框架 (单头文件, 从 Unity 官方引入)
└── unity.c                        # Unity Test 框架实现
```

> **注意**：`unity.c/.h` 是测试框架，放在 `common/` 下供所有固件项目的 Host 测试引用。固件交叉编译时不链接 `unity.c`。

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
          # globstar 确保 ** 递归生效
          shopt -s globstar
          yamllint -d "{extends: relaxed, rules: {line-length: disable}}" \
            src/deployment/docker/docker-compose*.yml \
            src/**/config/*.yaml 2>/dev/null || echo "(review yamllint warnings above)"

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
# MSPM0 版本: 修改 CPU/FPU/CFLAGS 即可, 结构相同

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
# 固件自有源文件
SRCS     = $(wildcard src/*.c)
# 共享库源文件 (projects 编译时拷贝到 build/common/)
COMMON_SRCS = $(wildcard ../common/pid.c ../common/crc16.c ../common/protocol_frame.c)
OBJS     = $(patsubst src/%.c, build/%.o, $(SRCS)) \
           $(patsubst ../common/%.c, build/common/%.o, $(COMMON_SRCS))
TARGET   = build/stm32_mecanum

# --- Rules ---
.PHONY: all clean test

all: $(TARGET).bin

$(TARGET).elf: $(OBJS)
	@mkdir -p build
	$(CC) $(LDFLAGS) -o $@ $^
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# 固件自有源文件编译
build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -I src -I ../common -c -o $@ $<

# 共享库源文件编译 (输出到 build/common/)
build/common/%.o: ../common/%.c
	@mkdir -p build/common
	$(CC) $(CFLAGS) -I ../common -c -o $@ $<

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

- [x] `build-stm32-firmware` job 成功编译并产出 `.bin` artifact —— **task-10 已交付**
- [x] `build-mspm0-firmware` job 成功编译并产出 `.bin` artifact —— **task-11 已交付**（`ci-link` 剖面，产物不可烧录，见 ADR-0004）
- [x] `build-docker-edge` job 成功构建 Docker 镜像（无需 push）—— **task-12 已交付**，拆为 `validate-deployment` + `build-edge-image`（ARM64/QEMU）
- [x] `lint-scripts` job 对已有脚本无 error 级告警 —— **实测 error 0 条**，门禁实际定在更严的 warning 级（0 条）
- [x] 所有固件单元测试 job 在 CI 中通过（绿灯）—— 麦轮 63 + 差速 70
- [x] 原有 ROS `build-and-test` job 不受影响 —— 一行未改
- [x] CI badge 在 README 中可见 —— 换成真 badge（原先是硬编码的假 badge）

---

## 与原方案的偏差（2026-07-31 实际执行）

本文档写于 2026-07-28，执行时已过期。逐条记录偏差与理由 —— 决策依据见
[ADR-0008](../docs/decisions/ADR-0008.md)。

### 偏差 1：5 个 job 里有 4 个已经存在，本任务只新增 1 个

| 文档要求的 job | 实际 |
|---|---|
| `build-and-test-ros` | 已有，且已含 `catkin build` + `catkin test`（覆盖评审建议 5） |
| `build-stm32-firmware` | task-10 已建，已含 `FW_COMMIT`/`FW_TIME` 版本注入（覆盖评审建议 1） |
| `build-mspm0-firmware` | task-11 已建 |
| `build-docker-edge` | task-12 已建，且更强：ARM64 + QEMU、`ROS_IP` 缺失反向验证 |
| `lint-scripts` | ❌ **本任务唯一新增** |

§13.1（`common/` 目录）、§13.2 的三个固件/Docker job、§13.4（Makefile 模板）
**全部无需重做**，`common/` 已有 8 个模块且 task-11 一行未改地复用过。

### 偏差 2：§13.2 的 `lint-scripts` 写法有三处不能照抄

原文的三个步骤都存在"永远不会失败"的问题：

| 原文 | 问题 | 实际实现 |
|---|---|---|
| `xargs -0 shellcheck -x -f tty` | 无 `--severity`，默认连 style 级都算失败（实测 38 条），会直接红 | `--severity=warning`，门禁定在实测 0 条的层级 |
| `yamllint ... \|\| echo "(review warnings)"` | `\|\| echo` 吞掉退出码，**永远不会失败** | 去掉 `\|\| echo`，改为真阻塞 |
| `shopt -s globstar` + 通配符列举路径 | 漏掉未列举的目录 | `git ls-files -z '*.yaml' '*.yml'`，范围与仓库内容一致 |

另外文件范围也用 `git ls-files` 取代 `find`，避免扫进未入库的临时文件。

### 偏差 3：不实现 §13.2 的 systemd 校验步骤

`systemd-analyze verify` 已由 `src/deployment/validate.sh` §6b 在
`validate-deployment` job 里真实执行（本地 SKIP，Linux CI 真跑）。
再抄一份就是两处要同步维护的等价逻辑。同时删掉了 `validate-deployment` 里
那个非阻塞的 shellcheck 步骤 —— 范围被全仓扫描完全覆盖。

### 偏差 4：ADR 写一份而不是两份，编号是 0008

- ADR-0007 号已被 task-12 的 `network_mode: host` 占用，**编号不可复用**；
- `common/` 的设计已由 ADR-0004 与 `src/firmware/README.md` 记录，再开一份是重复记账；
- "GitHub Actions vs Jenkins" 对本项目没有真实取舍代价，单独成篇是凑数。

改为写一份有真实取舍的 **[ADR-0008：CI 门禁分层](../docs/decisions/ADR-0008.md)**，
GitHub Actions 选型放进它的「被否决的方案」一节。

### 偏差 5：评审建议的取舍

| 建议 | 处置 |
|---|---|
| 1. 版本注入 | ✅ task-10/11 已实现（`FW_COMMIT`/`FW_TIME`） |
| 2. cppcheck + flake8 | flake8 ✅ 阻塞（实测 0 条）；**cppcheck ⚠ 仅告警** —— 见下方已知限制 |
| 3. CI 缓存优化（apt cache / ccache） | ❌ 不做。当前 CI 时长无压力，属性能优化；且会在「钉版本」之外再引入一层不确定性 |
| 4. TI 官方工具链校验 | ❌ 超出 CI 范围，记入 task-11 已知限制 |
| 5. ROS 包验证 | ✅ 已有 job 本来就跑全工作区 `catkin build` + `catkin test` |

### 已知限制（需首轮 CI 确认）

1. **cppcheck 的输出一次都没跑过** —— 提交时本机既无 cppcheck 也无 make。
   因此它被设成 `continue-on-error`，job 名里带 `ADVISORY`。
   首轮 CI 输出出来后再决定升为阻塞还是撤掉。
   **这是刻意的非阻塞，不是「以后再收紧」的托词**（ADR-0008 §决策-1）。
2. **shellcheck 的 `-x`（跟随 source）在 Linux 上的行为未实测** ——
   本地跑的是 Windows 版 0.10.0。CI 用同版本二进制以尽量对齐。
3. **badge URL 需仓库有一次 workflow 运行记录才会正常渲染**。

### 提交前的本地实测基线

```
shellcheck 0.10.0  22 个 *.sh       error 0 / warning 0（修掉 4 条后）/ info 37
yamllint   1.38.0  14 个 *.y[a]ml   0 条（含 ci.yml 自身）
flake8     7.1.1   31 个 *.py       0 条
validate.sh                         通过 26 · 失败 0 · 跳过 1
```

### Phase 1.5 跟进偏差（2026-08-01）

本任务当时沿用的 ROS 测试步骤带 `catkin test ... || echo`，会吞掉真实失败状态。
交接清单要求先在真 ROS 环境测量再决定门禁强度；Phase 1.5 已在 Ubuntu 20.04.6 / Noetic
实验室服务器完成：6 个项目包构建成功，80 个测试、0 error、0 failure，原始退出码 0。

因此按 [ADR-0014](../docs/decisions/ADR-0014.md) 删除 `catkin test` 的 `|| echo`，
并要求 `devel/setup.bash` 必须成功加载。ROS 测试现为真实阻塞门禁。原文中
「不要动已有 build-and-test」是 task-13 当时的范围约束，不再代表当前结论。

### Phase 1.5 分支改名事故（2026-08-03）

工作分支从 `feat/task-XX` 改为 `task-new` 后，GitHub Actions 没有产生任何 CI run。
仓库和 GitHub 公共 API 的证据一致：工作流仍是 active，但 `push.branches` 只允许
`main`、`feat/*` 和 `fix/*`；`task-new` 上已有多个新提交，CI run 数仍为 0。

修复不是把 `task-new` 再补进白名单，而是移除 `push` 的分支过滤，让所有工作分支
都受同一质量门禁。原因有两点：

1. `CONVENTIONS.md` 还允许 `docs/*` 和 `exp/*`，旧白名单本来就与分支规范不一致；
2. 单独追加当前分支名会把故障推迟到下一次改名，不会消除“CI 静默消失”的结构原因。

`scripts/smoke_test_phase1.sh` 同时增加触发契约检查：CI workflow 必须声明 `push`，
且该事件下不得再出现 `branches`/`branches-ignore`。因此今后仅修改分支名不能让 CI
无声失效。`pull_request` 仍只针对 `main`，保持 GitHub Flow 的合并门禁边界。

> ⚠ **yamllint 在 Windows 上会假报 9 条 `wrong new line character`**：
> `.gitattributes` 给 `*.yaml` 只声明了 `text` 没声明 `eol=lf`，工作区是 CRLF
> 而 git blob 是 LF。要按 Linux 侧内容核对，不能直接扫工作区。
>
> 核对 blob 时**不要用 `grep $'\r'`** —— MSYS 的 grep 会静默剥掉 CR，
> 得出「全仓都是 CRLF」的相反结论。用 `validate.sh` §4 写明的「剥 CR 前后字节数差」法。
> 这条坑 task-12 踩过一次并写进了注释，task-13 又原样踩了一次。

---

## ⓘ 优化建议（混元3 评审）

1. **版本注入机制**：CI 构建时自动将 `git rev-parse --short HEAD` 和 `date -u +%Y%m%d-%H%M` 注入固件版本号，写入 `version.h`（`#define FW_COMMIT_HASH "..."` 和 `#define FW_BUILD_TIME "..."`），使 PONG 帧能追溯构建来源。
2. **静态检查维度扩展**：在 `lint-scripts` job 中增加 `cppcheck`（C 代码静态分析）和 `flake8`（Python 代码风格检查），进一步提升代码质量基线。
3. **CI 缓存优化**：ARM GCC 工具链安装使用 `actions/cache` 缓存 apt 包；Docker 构建使用 `type=gha` cache；固件 `make` 使用 `ccache` 加速重复编译。
4. **补充工具链说明**：`gcc-arm-none-eabi` 可满足 MSPM0 编译验证，但 TI 官方推荐 `ti-cgt-armclang`。GCC 用于 CI 快速验证，正式发布固件建议用 TI 官方工具链做最终校验。
5. **ROS 包验证纳入**：在现有 `build-and-test` ROS job 中增加 Phase 0 的 6 个 ROS package 编译 + `catkin test`，保障全代码库持续验证。

---

## 给 Subagent 的执行建议

1. **不要动已有的 `build-and-test` ROS job**：它已经在跑 56 个单元测试 + 33 个 E2E
2. **新增 job 全部设置 `needs: []`**：固件编译不需要 ROS 环境，并行运行可节省时间
3. **gcc-arm-none-eabi 在 ubuntu-latest (22.04/24.04) 上可直接 apt 安装**，不需要手动下载
4. **MSPM0 是 Cortex-M0+（无 FPU）**：CFLAGS 必须加 `-mfloat-abi=soft`，否则链接失败
5. **Docker job 只做 build 验证**（不 push 到 registry），因为 Phase 1 还不需要镜像分发。可增加 `--platform linux/arm64` 非阻塞构建来验证 ARM64 兼容性（qemu-user-static 模拟, 速度较慢）。
6. **shellcheck 首次运行会报大量 warning**：优先修 error 级别，warning/info/style 级别可暂置。验收标准"无 critical 错误"改为"无 error 级别告警"（shellcheck 无 critical 级别）。
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
