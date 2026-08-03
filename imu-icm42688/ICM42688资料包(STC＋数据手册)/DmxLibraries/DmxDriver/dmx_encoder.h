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
 * @file       dmx_encoder.h
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

#ifndef _DMX_ENCODER_H_
#define _DMX_ENCODER_H_

#include "dmx_gpio.h"

// 编码器脉冲数引脚枚举
typedef enum
{
    ENCODER0_P34,
    ENCODER1_P35,
    ENCODER2_P12,
    ENCODER3_P04,
    ENCODER4_P06,
} ENCODER_pin_enum;

/**
*
* @brief    绝对值编码器初始化
* @param    count_pin			捕捉脉冲数的引脚,根据dmx_encoder.h中枚举查看
* @param    dir_pin				捕捉方向的引脚,普通IO口即可
* @return   void
* @notes    一个捕捉脉冲数的引脚占用一个定时计数器资源,即如果用定时器0就不能用ENCODER0_P34采集编码器脉冲
* @notes    ENCODER0_P34对应定时器0,ENCODER1_P35对应定时器1,ENCODER2_P12对应定时器2
* @notes    ENCODER3_P04对应定时器3,ENCODER4_P06对应定时器4
* Example:  init_encoder(ENCODER0_P34,GPIO_P35);	// 编码器初始化,脉冲数捕捉引脚为ENCODER0_P34,方向引脚为P35
*
**/
void init_encoder(ENCODER_pin_enum count_pin, GPIO_pin_enum dir_pin);

/**
*
* @brief    绝对值编码器数据获取
* @param    count_pin			捕捉脉冲数的引脚,根据dmx_encoder.h中枚举查看
* @return   unsigned int	返回编码器的脉冲数,
* @notes    方向可通过方向引脚的电平判断
* Example:  get_encoder_count(ENCODER0_P34);	// 返回编码器ENCODER0_P34的脉冲数
*
**/
unsigned int get_encoder_count(ENCODER_pin_enum count_pin);

/**
*
* @brief    绝对值编码器计数值清除
* @param    count_pin			捕捉脉冲数的引脚,根据dmx_encoder.h中枚举查看
* @return   void
* @notes    采集完编码器脉冲数后要调用此函数
* Example:  get_encoder_count(ENCODER0_P34);
*
**/
void clean_encoder_count(ENCODER_pin_enum count_pin);

#endif