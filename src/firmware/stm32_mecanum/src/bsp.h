/**
 * @file bsp.h
 * @brief 板级支持包：时钟树、SysTick 毫秒计时、GPIO 配置助手、急停与 LED
 *
 * 本文件及其实现只参与交叉编译，不进入 Host 单元测试。
 */
#ifndef STM32_MECANUM_BSP_H
#define STM32_MECANUM_BSP_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32f4xx_conf.h"

/**
 * 初始化最基础的运行环境：
 *   Flash 等待周期 → 电压调节档位 → HSE + PLL 168MHz → 总线分频 → SysTick 1ms
 *   → 打开所有用到的 GPIO 端口时钟 → 配置急停输入与状态 LED
 *
 * 必须在任何外设初始化之前调用。
 */
void bsp_init(void);

/** 自复位以来的毫秒数 (SysTick 中断累加，49.7 天回绕) */
uint32_t bsp_get_tick_ms(void);

/** 忙等延时。仅用于上电初始化阶段，控制循环里禁止调用。 */
void bsp_delay_ms(uint32_t ms);

/**
 * 硬件急停是否被按下。
 * 急停开关按常闭 (NC) 接法：正常时把引脚拉到低电平，按下 / 断线时因内部上拉变高。
 * 这样"线掉了"和"按下了"都会触发急停 —— 失效安全 (fail-safe)。
 */
bool bsp_estop_asserted(void);

/** 状态指示灯 */
void bsp_led_set(bool on);
void bsp_led_toggle(void);

/**
 * 配置一个 GPIO 引脚。
 *
 * @param port GPIOA..GPIOE
 * @param pin  0..15
 * @param mode GPIO_MODE_INPUT / OUTPUT / AF / ANALOG
 * @param af   复用功能号 0..15 (mode != GPIO_MODE_AF 时忽略)
 * @param pull GPIO_PULL_NONE / UP / DOWN
 */
void bsp_gpio_config(GPIO_TypeDef *port, uint32_t pin, uint32_t mode,
                     uint32_t af, uint32_t pull);

#endif /* STM32_MECANUM_BSP_H */
