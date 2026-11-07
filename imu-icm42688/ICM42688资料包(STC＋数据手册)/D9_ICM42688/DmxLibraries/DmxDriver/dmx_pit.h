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
 * @file       dmx_pit.h
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

#ifndef _DMX_PIT_H_
#define _DMX_PIT_H_

// 定时器中断通道枚举
typedef enum
{
    PIT0,
    PIT1,
    PIT2,
    PIT3,
    PIT4,
} PIT_num_enum;

/**
*
* @brief    定时器中断初始化
* @param    PIT_num_enum	选择的定时器中断通道,根据dmx_pit.h中枚举查看
* @param    time_ms				中断时间(单位:ms)
* @return   void
* @notes    定时器中断2,3,4模式固定为16位自动重装初值模式,硬件自动清零
* Example:  init_pit_ms(PIT1,5);	// STC32G的定时器1初始化,中断时间为5ms
*
**/
void init_pit_ms(PIT_num_enum num, unsigned int time_ms);

#endif