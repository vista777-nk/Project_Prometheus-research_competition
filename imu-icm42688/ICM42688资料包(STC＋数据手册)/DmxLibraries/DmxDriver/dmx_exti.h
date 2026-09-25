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
 * @file       dmx_exti.h
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

#ifndef _DMX_EXTI_H_
#define _DMX_EXTI_H_

// 外部中断引脚枚举
typedef enum
{
    EXTI0_P32,
    EXTI1_P33,
    EXTI2_P36,
    EXTI3_P37,
    EXTI4_P30,
} EXTI_num_enum;

/**
*
* @brief    外部中断初始化
* @param    EXTI_num_enum	选择的外部中断触发引脚,根据dmx_exti.h中枚举查看
* @param    mode					中断触发方式(0:下降沿触发;1:上升沿和下降沿触发)
* @return   void
* @notes    外部中断2,3,4中断触发方式只能为下降沿触发
* Example:  init_exti(EXTI0_P32,0);	// STC32G的P32引脚初始化为外部中断引脚,下降沿触发
*
**/
void init_exti(EXTI_num_enum num, unsigned char mode);

#endif