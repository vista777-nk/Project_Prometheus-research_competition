/****************************************************************************************
 *     COPYRIGHT NOTICE
 *     Copyright (C) 2023,AS DAIMXA
 *     copyright Copyright (C) 呆萌侠DAIMXA,2023
 *     All rights reserved.
 *     技术讨论QQ群：710026750
 *
 *     除注明出处外，以下所有内容版权均属呆萌侠智能科技所有，未经允许，不得用于商业用途，
 *     修改内容时必须保留呆萌智能侠科技的版权声明。
 *      ____    _    ___ __  ____  __    _    
 *     |  _ \  / \  |_ _|  \/  \ \/ /   / \   
 *     | | | |/ _ \  | || |\/| |\  /   / _ \  
 *     | |_| / ___ \ | || |  | |/  \  / ___ \ 
 *     |____/_/   \_\___|_|  |_/_/\_\/_/   \_\
 *
 * @file       dmx_gpio.h
 * @brief      呆萌侠STC32F12K54开源库
 * @company    合肥呆萌侠智能科技有限公司
 * @author     呆萌侠科技（QQ：2453520483）
 * @MCUcore    STC32F12K54
 * @Software   Keil5 C251
 * @version    查看说明文档内version版本说明
 * @Taobao     https://daimxa.taobao.com/
 * @Openlib    https://gitee.com/daimxa
 * @date       2023-11-10
****************************************************************************************/

#ifndef _DMX_GPIO_H_
#define _DMX_GPIO_H_

// GPIO引脚枚举
typedef enum
{
    // P0端口
    GPIO_P00,
    GPIO_P01,
    GPIO_P02,
    GPIO_P03,
    GPIO_P04,
    GPIO_P05,
    GPIO_P06,
    GPIO_P07,
    // P1端口
    GPIO_P10,
    GPIO_P11,
    GPIO_P12,
    GPIO_P13,
    GPIO_P14,
    GPIO_P15,
    GPIO_P16,
    GPIO_P17,
    // P2端口
    GPIO_P20,
    GPIO_P21,
    GPIO_P22,
    GPIO_P23,
    GPIO_P24,
    GPIO_P25,
    GPIO_P26,
    GPIO_P27,
    // P3端口
    GPIO_P30,
    GPIO_P31,
    GPIO_P32,
    GPIO_P33,
    GPIO_P34,
    GPIO_P35,
    GPIO_P36,
    GPIO_P37,
    // P4端口
    GPIO_P40,
    GPIO_P41,
    GPIO_P42,
    GPIO_P43,
    GPIO_P44,
    GPIO_P45,
    GPIO_P46,
    GPIO_P47,
    // P5端口
    GPIO_P50,
    GPIO_P51,
    GPIO_P52,
    GPIO_P53,
    GPIO_P54,
    GPIO_P55,
    GPIO_P56,
    GPIO_P57,
    // P6端口
    GPIO_P60,	// 仅作占位使用,STC32F12K54无此引脚
    GPIO_P61,	// 仅作占位使用,STC32F12K54无此引脚
    GPIO_P62,	// 仅作占位使用,STC32F12K54无此引脚
    GPIO_P63,	// 仅作占位使用,STC32F12K54无此引脚
    GPIO_P64,	// 仅作占位使用,STC32F12K54无此引脚
    GPIO_P65,	// 仅作占位使用,STC32F12K54无此引脚
    GPIO_P66,	// 仅作占位使用,STC32F12K54无此引脚
    GPIO_P67,	// 仅作占位使用,STC32F12K54无此引脚
    // P7端口
    GPIO_P70,	// 仅作占位使用,STC32F12K54无此引脚
    GPIO_P71,	// 仅作占位使用,STC32F12K54无此引脚
    GPIO_P72,	// 仅作占位使用,STC32F12K54无此引脚
    GPIO_P73,	// 仅作占位使用,STC32F12K54无此引脚
    GPIO_P74,	// 仅作占位使用,STC32F12K54无此引脚
    GPIO_P75,	// 仅作占位使用,STC32F12K54无此引脚
    GPIO_P76,	// 仅作占位使用,STC32F12K54无此引脚
    GPIO_P77,	// 仅作占位使用,STC32F12K54无此引脚
} GPIO_pin_enum;

// GPIO端口模式枚举
typedef enum
{
    IO_PU,	// 准双向口
    IN_HIZ,	// 高阻输入
    OUT_OD, // 漏极开路
    OUT_PP, // 推挽输出
} GPIO_mode_enum;

/**
*
* @brief    GPIO初始化
* @param    pin					选择的引脚,根据dmx_gpio.h中枚举引脚
* @param    mode				引脚工作模式
* @param    up_enable 	上拉设置(0:上拉禁止;0:上拉使能)
* @return   void
* @notes
* Example:  init_gpio(GPIO_P00,OUT_PP ,0);	// STC32G的P00引脚初始化为推挽输出模式,上拉禁止
*
**/
void init_gpio(GPIO_pin_enum pin, GPIO_mode_enum mode, unsigned char up_enable);

#endif