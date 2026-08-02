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
 * @file       dmx_isr.h
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

#ifndef _ISR_H_
#define _ISR_H_

// 中断名枚举
typedef enum
{
    INT0_PRIORITY,
    INT1_PRIORITY,
    INT4_PRIORITY,

    TIME0_PRIORITY,
    TIME1_PRIORITY,

    UART1_PRIORITY,
    UART2_PRIORITY,
    UART3_PRIORITY,
    UART4_PRIORITY,
} ISR_nvic_enum;

/**
*
* @brief    中断优先级设置
* @param    isr_nvic				中断名,根据isr.h中枚举查看
* @param    priority				优先级(0~3,3级为最高级)
* @return   void
* @notes    串口发送数据时,串口中断优先级要高于定时器中断优先级
* Example:  init_nvic(TIME1_PRIORITY,3);	// 定时器1中断优先级设置为最高级别3级
*
**/
void init_nvic(ISR_nvic_enum isr_nvic, unsigned char priority);

#endif