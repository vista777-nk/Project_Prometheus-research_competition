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
 * @file       dmx_encoder.c
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

#include "stc32g.h"
#include "dmx_encoder.h"

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
void init_encoder(ENCODER_pin_enum count_pin, GPIO_pin_enum dir_pin)
{
    init_gpio(dir_pin, IN_HIZ, 0);
    switch(count_pin)
    {
    case ENCODER0_P34:
        init_gpio(GPIO_P34, IN_HIZ, 0);
        T0_CT = 1;
        TL0 = 0xFF;
        TH0 = 0xFF;
        TR0 = 1;
        ET0 = 1;
        break;
    case ENCODER1_P35:
        init_gpio(GPIO_P35, IN_HIZ, 0);
        T1_CT = 1;
        TL1 = 0xFF;
        TH1 = 0xFF;
        TR1 = 1;
        ET1 = 1;
        break;
    case ENCODER2_P12:
        init_gpio(GPIO_P12, IN_HIZ, 0);
        T2_CT = 1;
        T2L = 0xFF;
        T2H = 0xFF;
        T2R = 1;
        ET2 = 1;
        break;
    case ENCODER3_P04:
        init_gpio(GPIO_P04, IN_HIZ, 0);
        T4T3M |= 0x0C;
        T3L = 0xFF;
        T3H = 0xFF;
        ET3 = 1;
        break;
    case ENCODER4_P06:
        init_gpio(GPIO_P06, IN_HIZ, 0);
        T4T3M |= 0xC0;
        T4L = 0xFF;
        T4H = 0xFF;
        ET4 = 1;
        break;
    }
}

/**
*
* @brief    绝对值编码器数据获取
* @param    count_pin			捕捉脉冲数的引脚,根据dmx_encoder.h中枚举查看
* @return   unsigned int	返回编码器的脉冲数,
* @notes    方向可通过方向引脚的电平判断
* Example:  get_encoder_count(ENCODER0_P34);	// 返回编码器ENCODER0_P34的脉冲数
*
**/
unsigned int get_encoder_count(ENCODER_pin_enum count_pin)
{
    unsigned int count;
    switch(count_pin)
    {
    case ENCODER0_P34:
        count = TL0;
        count = TH0 << 8 | count;
        break;
    case ENCODER1_P35:
        count = TL1;
        count = TH1 << 8 | count;
        break;
    case ENCODER2_P12:
        count = T2L;
        count = T2H << 8 | count;
        break;
    case ENCODER3_P04:
        count = T3L;
        count = T3H << 8 | count;
        break;
    case ENCODER4_P06:
        count = T4L;
        count = T4H << 8 | count;
        break;
    }
    return count;
}

/**
*
* @brief    绝对值编码器计数值清除
* @param    count_pin			捕捉脉冲数的引脚,根据dmx_encoder.h中枚举查看
* @return   void
* @notes    采集完编码器脉冲数后要调用此函数
* Example:  get_encoder_count(ENCODER0_P34);
*
**/
void clean_encoder_count(ENCODER_pin_enum count_pin)
{
    switch(count_pin)
    {
    case ENCODER0_P34:
        TR0 = 0;
        TL0 = 0;
        TH0 = 0;
        TR0 = 1;
        break;
    case ENCODER1_P35:
        TR1 = 0;
        TL1 = 0;
        TH1 = 0;
        TR1 = 1;
        break;
    case ENCODER2_P12:
        T2R = 0;
        T2L = 0;
        T2H = 0;
        T2R = 1;
        break;
    case ENCODER3_P04:
        T3R = 0;
        T3L = 0;
        T3H = 0;
        T3R = 1;
        break;
    case ENCODER4_P06:
        T4R = 0;
        T4L = 0;
        T4H = 0;
        T4R = 1;
        break;
    }
}