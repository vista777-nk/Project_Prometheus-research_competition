# =============================================================================
# MSPM0G3507 差速底盘固件 —— 构建规则
#
#   make                    交叉编译 (默认 PROFILE=ci-link，**不可烧录**，见 README §2)
#   make PROFILE=driverlib  交叉编译真实硬件固件 (需要 TI MSPM0 SDK)
#   make test               用宿主机 gcc 编译并运行单元测试 (不需要 ARM 工具链)
#   make size               打印固件各段占用
#   make flash              烧录 (需要 TI UniFlash / dslite)
#   make clean              清理 build/
#
# 目标工具链: arm-none-eabi-gcc (Cortex-M0+, 软浮点)
# 共享代码来自 ../common/，与 task-10 的 STM32 固件同源，改一处两边都变。
# =============================================================================

CROSS_COMPILE ?= arm-none-eabi-
CC      = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy
SIZE    = $(CROSS_COMPILE)size

# 宿主机编译器，仅用于单元测试
HOST_CC ?= gcc

# --- 构建剖面 ---
# ci-link   : 移植层为空实现。可编译、可链接、可产出 .bin，但**不能驱动硬件**。
#             CI 用它验证整个构建图与代码语义。理由见 src/mspm0_conf.h。
# driverlib : 真实硬件。需要 MSPM0_SDK 指向 TI MSPM0 SDK 根目录。
PROFILE ?= ci-link

TARGET  = mspm0_diff-$(PROFILE)
BUILD   = build
COMMON  = ../common

# --- MCU 相关编译选项 ---
# Cortex-M0+ 没有 FPU，也没有硬件除法指令 —— 浮点与整数除法全是库调用。
# 代码里因此刻意避开热路径上的除法 (见 kinematics.c / encoder.c 的注释)。
CPU     = -mcpu=cortex-m0plus
MCUOPTS = $(CPU) -mthumb -mfloat-abi=soft

# 版本溯源：CI 会把真实的 commit 与构建时间注入进来。
# 本地编译时 git 不可用也不会失败，退化为 "local"。
FW_COMMIT ?= $(shell git rev-parse --short HEAD 2>/dev/null || echo local)
FW_TIME   ?= $(shell date -u +%Y%m%d-%H%M 2>/dev/null || echo unknown)
VERSION_FLAGS = -DFW_COMMIT_HASH=\"$(FW_COMMIT)\" -DFW_BUILD_TIME=\"$(FW_TIME)\"

CFLAGS  = $(MCUOPTS) -std=c99 -O2 -g3
CFLAGS += -Wall -Wextra -Wshadow -Wundef -Wdouble-promotion
CFLAGS += -ffunction-sections -fdata-sections -fno-common
CFLAGS += -I src -I $(COMMON)
CFLAGS += $(VERSION_FLAGS)

LDSCRIPT = linker/MSPM0G3507.ld

# -nostartfiles: 启动代码由 src/startup_mspm0g3507.c 自己提供
# --specs=nano.specs: 用 newlib-nano，128KB Flash 上这几十 KB 很关键
LDFLAGS  = $(MCUOPTS) -T $(LDSCRIPT)
LDFLAGS += --specs=nano.specs -nostartfiles
LDFLAGS += -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map
LDFLAGS += -lc -lm -lnosys

# --- 按剖面挑选移植层实现 ---
# 两份实现互斥，各自用 #ifdef 把整个文件包住，因此把两个都编进去也不会冲突；
# 但为了让编译错误更清楚 (以及不在 CI 上需要 TI SDK 头文件)，这里显式排除。
ALL_SRCS = $(wildcard src/*.c)

# TI MSPM0 SDK 根目录，只在 PROFILE=driverlib 时用到
MSPM0_SDK ?= $(HOME)/ti/mspm0_sdk

ifeq ($(PROFILE),driverlib)
  CFLAGS  += -DUSE_TI_DRIVERLIB -I $(MSPM0_SDK)/source
  # SysConfig 生成物 (ti_msp_dl_config.c) 与 SDK 的启动文件由使用者提供，
  # 见 README §7。本工程自带的 startup 只服务于 ci-link 剖面。
  SRCS = $(filter-out src/port_stub.c src/startup_mspm0g3507.c,$(ALL_SRCS))
  LDFLAGS += -L $(MSPM0_SDK)/source/ti/driverlib/lib/gcc/m0p/mspm0g1x0x_g3x0x -ldriverlib
else
  SRCS = $(filter-out src/port_driverlib.c,$(ALL_SRCS))
endif

# 共享库源文件。unity.c 是测试框架，绝不进固件。
COMMON_SRCS = $(COMMON)/pid.c $(COMMON)/crc16.c $(COMMON)/protocol_frame.c \
              $(COMMON)/faults.c

OBJS  = $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))
OBJS += $(patsubst $(COMMON)/%.c,$(BUILD)/common/%.o,$(COMMON_SRCS))
DEPS  = $(OBJS:.o=.d)

# --- 单元测试 (宿主机) ---
# 只编译与硬件无关的模块。encoder.c 之所以能进来，是因为它只调用
# common/mcu_port.h 的两个原语，测试里用假编码器替换掉 —— 这正是移植层分离的收益。
# motor.c / uart.c / main.c / port_*.c / startup 不参与。
TEST_SRCS = test/test_main.c test/test_kinematics.c test/test_protocol.c \
            test/test_encoder.c test/test_faults.c test/test_control_loop.c \
            src/kinematics.c src/protocol.c src/encoder.c \
            $(COMMON)/crc16.c $(COMMON)/pid.c $(COMMON)/protocol_frame.c \
            $(COMMON)/faults.c $(COMMON)/unity.c
TEST_CFLAGS = -std=c99 -O1 -g -Wall -Wextra -Wpedantic -DTEST_HOST \
              -I src -I $(COMMON) -I test
TEST_BIN = $(BUILD)/test_runner

.PHONY: all test size flash clean help banner
.DEFAULT_GOAL := all

all: banner $(BUILD)/$(TARGET).bin $(BUILD)/$(TARGET).hex size

# 空实现剖面下每次构建都把话说清楚，避免有人拿着 CI artifact 去烧板子
banner:
ifneq ($(PROFILE),driverlib)
	@echo "=============================================================="
	@echo " PROFILE=ci-link — 链接验证构建，移植层为空实现"
	@echo " 产出的 .bin 可以烧进去，但读不到编码器、也推不动 PWM。"
	@echo " 可烧录固件请用: make PROFILE=driverlib  (需要 TI MSPM0 SDK)"
	@echo " 理由见 src/mspm0_conf.h 与 README 第 2 节。"
	@echo "=============================================================="
endif

$(BUILD)/$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	@mkdir -p $(dir $@)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/common/%.o: $(COMMON)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

size: $(BUILD)/$(TARGET).elf
	@echo "--- Firmware size (Flash 128K / SRAM 32K) ---"
	$(SIZE) $<

# 单元测试不依赖 ARM 工具链，任何装了 gcc/clang 的机器都能跑
test: $(TEST_BIN)
	@echo "--- Running host unit tests ---"
	@./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRCS)
	@mkdir -p $(BUILD)
	$(HOST_CC) $(TEST_CFLAGS) $(TEST_SRCS) -o $@ -lm

# MSPM0 用 XDS110 调试器 + TI dslite (UniFlash 自带)，不是 OpenOCD。
# 没装工具链时给出明确指引，而不是抛一个 command not found。
flash: $(BUILD)/$(TARGET).hex
ifeq ($(PROFILE),driverlib)
	@command -v dslite >/dev/null 2>&1 || { \
	  echo "dslite 未找到。请安装 TI UniFlash 并把 dslite 加入 PATH。"; exit 1; }
	dslite --config=MSPM0G3507.ccxml -f $<
else
	@echo "拒绝烧录：当前是 PROFILE=ci-link，移植层为空实现，烧进去电机不会动。"
	@echo "请用: make PROFILE=driverlib flash"
	@exit 1
endif

clean:
	rm -rf $(BUILD)

help:
	@echo "targets: all (default) | test | size | flash | clean"
	@echo "vars   : PROFILE=$(PROFILE) (ci-link|driverlib)"
	@echo "         CROSS_COMPILE=$(CROSS_COMPILE)  HOST_CC=$(HOST_CC)"
	@echo "         MSPM0_SDK=$(MSPM0_SDK)"

-include $(DEPS)
