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
 * @file       dmx_pwm.h
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

#ifndef _DMX_PWM_H_
#define _DMX_PWM_H_

#define PWM_DUTY_MAX 10000

typedef enum
{
    // 第一组PWM:PWMA
    // PWMA的的PWM不同通道间可以同时输出,但每个通道只能输出一个PWM
    // 通道1
    PWMA1_P_P10, PWMA1_N_P11,
    PWMA1_P_P20, PWMA1_N_P21,
    PWMA1_P_P60, PWMA1_N_P61,	// 仅作占位使用,STC32F12K54无此引脚
    // 通道2
    PWMA2_P_P54, PWMA2_N_P13,
    PWMA2_P_P22, PWMA2_N_P23,
    PWMA2_P_P62, PWMA2_N_P63,
    // 通道3
    PWMA3_P_P14, PWMA3_N_P15,
    PWMA3_P_P24, PWMA3_N_P25,
    PWMA3_P_P64, PWMA3_N_P65,	// 仅作占位使用,STC32F12K54无此引脚
    // 通道4
    PWMA4_P_P16, PWMA4_N_P17,
    PWMA4_P_P26, PWMA4_N_P27,
    PWMA4_P_P66, PWMA4_N_P67,	// 仅作占位使用,STC32F12K54无此引脚
    PWMA4_P_P34, PWMA4_N_P33,

    // 第二组PWM:PWMB
    // PWMB的PWM不同通道间可以同时输出,但每个通道只能输出一个PWM
    // 通道1
    PWMB1_P20,
    PWMB1_P17,
    PWMB1_P00,
    PWMB1_P74,	// 仅作占位使用,STC32F12K54无此引脚
    // 通道2
    PWMB2_P21,
    PWMB2_P54,
    PWMB2_P01,
    PWMB2_P75,	// 仅作占位使用,STC32F12K54无此引脚
    // 通道3
    PWMB3_P22,
    PWMB3_P33,
    PWMB3_P02,
    PWMB3_P76,	// 仅作占位使用,STC32F12K54无此引脚
    // 通道4
    PWMB4_P23,
    PWMB4_P34,
    PWMB4_P03,
    PWMB4_P77,	// 仅作占位使用,STC32F12K54无此引脚
} PWM_pin_enum;

/**
*
* @brief    pwm初始化
* @param    pin     		选择需要初始化pwm引脚(dmx_pwm.h文件里已枚举定义)
* @param    freq     		初始化pwm引脚的频率
* @param    duty     		初始化pwm引脚的初始占空比
* @return   void
* @notes    调用此函数前可查看dmx_pwm.h文件里枚举的可用引脚
* Example:  init_pwm(PWMA1_P_P20,100,1000);	// 初始化PWMA1_P_P20脚,频率为100HZ,占空比为1000,即占空比为10%
*
**/
void init_pwm(PWM_pin_enum pin, unsigned int freq, unsigned int duty);

/**
*
* @brief    pwm发送占空比
* @param    pin     		设置需要发送占空比的pwm引脚(dmx_pwm.h文件里已枚举定义)
* @param    duty     		设置占空比
* @return   void
* @notes    调用此函数前pwm引脚需初始化
* Example:  set_duty_pwm(PWMA1_P_P20,1600);	// 发送PWMA1_P_P20脚,占空比为1600,即占空比为16%
*
**/
void set_duty_pwm(PWM_pin_enum pin, unsigned int duty);





#endif