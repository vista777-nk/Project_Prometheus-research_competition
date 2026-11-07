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
 * @file       dmx_gpio.c
 * @brief      呆萌侠STC32G12K128开源库
 * @company    合肥呆萌侠智能科技有限公司
 * @author     呆萌侠科技（QQ：2453520483）
 * @MCUcore    STC32G12K128
 * @Software   Keil5 C251
 * @version    查看说明文档内version版本说明
 * @Taobao     https://daimxa.taobao.com/
 * @Openlib    https://gitee.com/daimxa
 * @date       2023-11-10
****************************************************************************************/

#include "stc32g.h"
#include "dmx_gpio.h"

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
void init_gpio(GPIO_pin_enum pin, GPIO_mode_enum mode, unsigned char up_enable)
{
    switch(mode)
    {
    // 上拉准双向口
    case IO_PU:
        switch(pin / 8)
        {
        case 0:
            P0M1 &= ~(0x01 << pin % 8);
            P0M0 &= ~(0x01 << pin % 8);
            if(up_enable) P0PU |= (0x01 << pin % 8);
            else P0PU &= ~(0x01 << pin % 8);
            break;
        case 1:
            P1M1 &= ~(0x01 << pin % 8);
            P1M0 &= ~(0x01 << pin % 8);
            if(up_enable) P1PU |= (0x01 << pin % 8);
            else P1PU &= ~(0x01 << pin % 8);
            break;
        case 2:
            P2M1 &= ~(0x01 << pin % 8);
            P2M0 &= ~(0x01 << pin % 8);
            if(up_enable) P2PU |= (0x01 << pin % 8);
            else P2PU &= ~(0x01 << pin % 8);
            break;
        case 3:
            P3M1 &= ~(0x01 << pin % 8);
            P3M0 &= ~(0x01 << pin % 8);
            if(up_enable) P3PU |= (0x01 << pin % 8);
            else P3PU &= ~(0x01 << pin % 8);
            break;
        case 4:
            P4M1 &= ~(0x01 << pin % 8);
            P4M0 &= ~(0x01 << pin % 8);
            if(up_enable) P4PU |= (0x01 << pin % 8);
            else P4PU &= ~(0x01 << pin % 8);
            break;
        case 5:
            P5M1 &= ~(0x01 << pin % 8);
            P5M0 &= ~(0x01 << pin % 8);
            if(up_enable) P5PU |= (0x01 << pin % 8);
            else P5PU &= ~(0x01 << pin % 8);
            break;
        case 6:
            P6M1 &= ~(0x01 << pin % 8);
            P6M0 &= ~(0x01 << pin % 8);
            if(up_enable) P6PU |= (0x01 << pin % 8);
            else P6PU &= ~(0x01 << pin % 8);
            break;
        case 7:
            P7M1 &= ~(0x01 << pin % 8);
            P7M0 &= ~(0x01 << pin % 8);
            if(up_enable) P7PU |= (0x01 << pin % 8);
            else P7PU &= ~(0x01 << pin % 8);
            break;
        }
        break;
    // 浮空输入
    case IN_HIZ:
        switch(pin / 8)
        {
        case 0:
            P0M1 |=  (0x01 << pin % 8);
            P0M0 &= ~(0x01 << pin % 8);
            if(up_enable) P0PU |= (0x01 << pin % 8);
            else P0PU &= ~(0x01 << pin % 8);
            break;
        case 1:
            P1M1 |=  (0x01 << pin % 8);
            P1M0 &= ~(0x01 << pin % 8);
            if(up_enable) P1PU |= (0x01 << pin % 8);
            else P1PU &= ~(0x01 << pin % 8);
            break;
        case 2:
            P2M1 |=  (0x01 << pin % 8);
            P2M0 &= ~(0x01 << pin % 8);
            if(up_enable) P2PU |= (0x01 << pin % 8);
            else P2PU &= ~(0x01 << pin % 8);
            break;
        case 3:
            P3M1 |=  (0x01 << pin % 8);
            P3M0 &= ~(0x01 << pin % 8);
            if(up_enable) P3PU |= (0x01 << pin % 8);
            else P3PU &= ~(0x01 << pin % 8);
            break;
        case 4:
            P4M1 |=  (0x01 << pin % 8);
            P4M0 &= ~(0x01 << pin % 8);
            if(up_enable) P4PU |= (0x01 << pin % 8);
            else P4PU &= ~(0x01 << pin % 8);
            break;
        case 5:
            P5M1 |=  (0x01 << pin % 8);
            P5M0 &= ~(0x01 << pin % 8);
            if(up_enable) P5PU |= (0x01 << pin % 8);
            else P5PU &= ~(0x01 << pin % 8);
            break;
        case 6:
            P6M1 |=  (0x01 << pin % 8);
            P6M0 &= ~(0x01 << pin % 8);
            if(up_enable) P6PU |= (0x01 << pin % 8);
            else P6PU &= ~(0x01 << pin % 8);
            break;
        case 7:
            P7M1 |=  (0x01 << pin % 8);
            P7M0 &= ~(0x01 << pin % 8);
            if(up_enable) P7PU |= (0x01 << pin % 8);
            else P7PU &= ~(0x01 << pin % 8);
            break;
        }
        break;
    // 开漏输出
    case OUT_OD:
        switch(pin / 8)
        {
        case 0:
            P0M1 |=  (0x01 << pin % 8);
            P0M0 |=  (0x01 << pin % 8);
            if(up_enable) P0PU |= (0x01 << pin % 8);
            else P0PU &= ~(0x01 << pin % 8);
            break;
        case 1:
            P1M1 |=  (0x01 << pin % 8);
            P1M0 |=  (0x01 << pin % 8);
            if(up_enable) P1PU |= (0x01 << pin % 8);
            else P1PU &= ~(0x01 << pin % 8);
            break;
        case 2:
            P2M1 |=  (0x01 << pin % 8);
            P2M0 |=  (0x01 << pin % 8);
            if(up_enable) P2PU |= (0x01 << pin % 8);
            else P2PU &= ~(0x01 << pin % 8);
            break;
        case 3:
            P3M1 |=  (0x01 << pin % 8);
            P3M0 |=  (0x01 << pin % 8);
            if(up_enable) P3PU |= (0x01 << pin % 8);
            else P3PU &= ~(0x01 << pin % 8);
            break;
        case 4:
            P4M1 |=  (0x01 << pin % 8);
            P4M0 |=  (0x01 << pin % 8);
            if(up_enable) P4PU |= (0x01 << pin % 8);
            else P4PU &= ~(0x01 << pin % 8);
            break;
        case 5:
            P5M1 |=  (0x01 << pin % 8);
            P5M0 |=  (0x01 << pin % 8);
            if(up_enable) P5PU |= (0x01 << pin % 8);
            else P5PU &= ~(0x01 << pin % 8);
            break;
        case 6:
            P6M1 |=  (0x01 << pin % 8);
            P6M0 |=  (0x01 << pin % 8);
            if(up_enable) P6PU |= (0x01 << pin % 8);
            else P6PU &= ~(0x01 << pin % 8);
            break;
        case 7:
            P7M1 |=  (0x01 << pin % 8);
            P7M0 |=  (0x01 << pin % 8);
            if(up_enable) P7PU |= (0x01 << pin % 8);
            else P7PU &= ~(0x01 << pin % 8);
            break;
        }
        break;
    // 推挽输出
    case OUT_PP:
        switch(pin / 8)
        {
        case 0:
            P0M1 &= ~(0x01 << pin % 8);
            P0M0 |=  (0x01 << pin % 8);
            if(up_enable) P0PU |= (0x01 << pin % 8);
            else P0PU &= ~(0x01 << pin % 8);
            break;
        case 1:
            P1M1 &= ~(0x01 << pin % 8);
            P1M0 |=  (0x01 << pin % 8);
            if(up_enable) P1PU |= (0x01 << pin % 8);
            else P1PU &= ~(0x01 << pin % 8);
            break;
        case 2:
            P2M1 &= ~(0x01 << pin % 8);
            P2M0 |=  (0x01 << pin % 8);
            if(up_enable) P2PU |= (0x01 << pin % 8);
            else P2PU &= ~(0x01 << pin % 8);
            break;
        case 3:
            P3M1 &= ~(0x01 << pin % 8);
            P3M0 |=  (0x01 << pin % 8);
            if(up_enable) P3PU |= (0x01 << pin % 8);
            else P3PU &= ~(0x01 << pin % 8);
            break;
        case 4:
            P4M1 &= ~(0x01 << pin % 8);
            P4M0 |=  (0x01 << pin % 8);
            if(up_enable) P4PU |= (0x01 << pin % 8);
            else P4PU &= ~(0x01 << pin % 8);
            break;
        case 5:
            P5M1 &= ~(0x01 << pin % 8);
            P5M0 |=  (0x01 << pin % 8);
            if(up_enable) P5PU |= (0x01 << pin % 8);
            else P5PU &= ~(0x01 << pin % 8);
            break;
        case 6:
            P6M1 &= ~(0x01 << pin % 8);
            P6M0 |=  (0x01 << pin % 8);
            if(up_enable) P6PU |= (0x01 << pin % 8);
            else P6PU &= ~(0x01 << pin % 8);
            break;
        case 7:
            P7M1 &= ~(0x01 << pin % 8);
            P7M0 |=  (0x01 << pin % 8);
            if(up_enable) P7PU |= (0x01 << pin % 8);
            else P7PU &= ~(0x01 << pin % 8);
            break;
        }
        break;
    }
}