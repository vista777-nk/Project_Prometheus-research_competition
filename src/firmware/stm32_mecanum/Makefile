# =============================================================================
# STM32F407VET6 麦轮底盘固件 —— 构建规则
#
#   make            交叉编译固件，产出 build/stm32_mecanum.{elf,bin,hex}
#   make test       用宿主机 gcc 编译并运行单元测试 (不需要 ARM 工具链)
#   make size       打印固件各段占用
#   make flash      用 OpenOCD 通过 ST-Link 烧录
#   make clean      清理 build/
#
# 目标工具链: arm-none-eabi-gcc (Cortex-M4F)
# 共享代码来自 ../common/，与 task-11 的 MSPM0 固件同源，改一处两边都变。
# =============================================================================

CROSS_COMPILE ?= arm-none-eabi-
CC      = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy
SIZE    = $(CROSS_COMPILE)size

# 宿主机编译器，仅用于单元测试
HOST_CC ?= gcc

TARGET  = stm32_mecanum
BUILD   = build
COMMON  = ../common

# --- MCU 相关编译选项 ---
CPU     = -mcpu=cortex-m4
FPU     = -mfloat-abi=hard -mfpu=fpv4-sp-d16
MCUOPTS = $(CPU) $(FPU) -mthumb

# 版本溯源：CI 会把真实的 commit 与构建时间注入进来 (见 .github/workflows/ci.yml)。
# 本地编译时 git 不可用也不会失败，退化为 "local"。
FW_COMMIT ?= $(shell git rev-parse --short HEAD 2>/dev/null || echo local)
FW_TIME   ?= $(shell date -u +%Y%m%d-%H%M 2>/dev/null || echo unknown)
VERSION_FLAGS = -DFW_COMMIT_HASH=\"$(FW_COMMIT)\" -DFW_BUILD_TIME=\"$(FW_TIME)\"

CFLAGS  = $(MCUOPTS) -std=c99 -O2 -g3
CFLAGS += -Wall -Wextra -Wshadow -Wundef -Wdouble-promotion
CFLAGS += -ffunction-sections -fdata-sections -fno-common
CFLAGS += -DSTM32F407xx
CFLAGS += -I src -I $(COMMON)
CFLAGS += $(VERSION_FLAGS)

# -nostartfiles: 启动代码由 src/startup_stm32f407vetx.c 自己提供
# --specs=nano.specs: 用 newlib-nano，省下几十 KB Flash
# -u _printf_float 之类的浮点 printf 支持刻意不开 —— 固件里不该有 printf
LDFLAGS  = $(MCUOPTS) -T linker/STM32F407VETx_FLASH.ld
LDFLAGS += --specs=nano.specs -nostartfiles
LDFLAGS += -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map
LDFLAGS += -lc -lm -lnosys

# --- 源文件 ---
# 固件自有源文件 (含 HAL/裸机层)
SRCS        = $(wildcard src/*.c)
# 共享库源文件。unity.c 是测试框架，绝不进固件。
COMMON_SRCS = $(COMMON)/pid.c $(COMMON)/crc16.c $(COMMON)/protocol_frame.c \
              $(COMMON)/faults.c

OBJS  = $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))
OBJS += $(patsubst $(COMMON)/%.c,$(BUILD)/common/%.o,$(COMMON_SRCS))
DEPS  = $(OBJS:.o=.d)

# --- 单元测试 (宿主机) ---
# 只编译与硬件无关的模块。encoder.c 之所以能进来，是因为重构后它只调用
# mcu_port.h 的两个原语，测试里用假编码器替换掉 —— 这正是移植层分离的收益。
# motor.c / uart.c / main.c / port_stm32f407.c / startup 不参与。
TEST_SRCS = test/test_main.c test/test_crc16.c test/test_kinematics.c \
            test/test_pid.c test/test_protocol.c test/test_encoder.c \
            src/kinematics.c src/protocol.c src/encoder.c \
            $(COMMON)/crc16.c $(COMMON)/pid.c $(COMMON)/protocol_frame.c \
            $(COMMON)/faults.c $(COMMON)/unity.c
TEST_CFLAGS = -std=c99 -O1 -g -Wall -Wextra -Wpedantic -DTEST_HOST \
              -I src -I $(COMMON) -I test
TEST_BIN = $(BUILD)/test_runner

.PHONY: all test size flash clean help
.DEFAULT_GOAL := all

all: $(BUILD)/$(TARGET).bin $(BUILD)/$(TARGET).hex size

$(BUILD)/$(TARGET).elf: $(OBJS)
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
	@echo "--- Firmware size ---"
	$(SIZE) $<

# 单元测试不依赖 ARM 工具链，任何装了 gcc/clang 的机器都能跑
test: $(TEST_BIN)
	@echo "--- Running host unit tests ---"
	@./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRCS)
	@mkdir -p $(BUILD)
	$(HOST_CC) $(TEST_CFLAGS) $(TEST_SRCS) -o $@ -lm

# ST-Link + OpenOCD 烧录。没有硬件时这条目标用不到，CI 也不会调用。
flash: $(BUILD)/$(TARGET).elf
	openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
	        -c "program $< verify reset exit"

clean:
	rm -rf $(BUILD)

help:
	@echo "targets: all (default) | test | size | flash | clean"
	@echo "vars   : CROSS_COMPILE=$(CROSS_COMPILE)  HOST_CC=$(HOST_CC)"

-include $(DEPS)
