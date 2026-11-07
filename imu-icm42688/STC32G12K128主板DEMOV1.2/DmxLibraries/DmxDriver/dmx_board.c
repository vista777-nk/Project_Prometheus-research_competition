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
 * @file       dmx_board.c
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

#include "dmx_board.h"

/**
*
* @brief    芯片初始化
* @param    void
* @return   void
* @notes    调用此函数前可查看dmx_adc.h文件里枚举的可用引脚
* Example:  init_chip();
*
**/
void init_chip(void)
{
		// 使能访问XFR
    EAXFR = 1;
		// 设置外部数据总线速度最快
    CKCON = 0;
		// 设置WTST初始值
    WTST = 0;
	
		// 设置内部IRC频率
		switch(MAIN_FOSC)
		{
				case 22118400UL:
					CLKDIV = 0x04;
					IRTRIM = T22M_ADDR;
					VRTRIM = VRT27M_ADDR;
					IRCBAND &= ~0x03;
					IRCBAND |= 0x02;
					CLKDIV = 0x00;
					break;
				case 24000000UL:
					CLKDIV = 0x04;
					IRTRIM = T24M_ADDR;
					VRTRIM = VRT27M_ADDR;
					IRCBAND &= ~0x03;
					IRCBAND |= 0x02;
					CLKDIV = 0x00;
					break;
				case 27000000UL:
					CLKDIV = 0x04;
					IRTRIM = T27M_ADDR;
					VRTRIM = VRT27M_ADDR;
					IRCBAND &= ~0x03;
					IRCBAND |= 0x02;
					CLKDIV = 0x00;
					break;
				case 30000000UL:
					CLKDIV = 0x04;
					IRTRIM = T30M_ADDR;
					VRTRIM = VRT27M_ADDR;
					IRCBAND &= ~0x03;
					IRCBAND |= 0x02;
					CLKDIV = 0x00;
					break;
				case 33177600UL:
					CLKDIV = 0x04;
					IRTRIM = T33M_ADDR;
					VRTRIM = VRT27M_ADDR;
					IRCBAND &= ~0x03;
					IRCBAND |= 0x02;
					CLKDIV = 0x00;
					break;
				case 35000000UL:
					CLKDIV = 0x04;
					IRTRIM = T35M_ADDR;
					VRTRIM = VRT44M_ADDR;
					IRCBAND |= 0x03;
					CLKDIV = 0x00;
					break;
				case 36864000UL:
					CLKDIV = 0x04;
					IRTRIM = T36M_ADDR;
					VRTRIM = VRT44M_ADDR;
					IRCBAND |= 0x03;
					CLKDIV = 0x00;
					break;
				case 40000000UL:
					CLKDIV = 0x04;
					IRTRIM = T40M_ADDR;
					VRTRIM = VRT44M_ADDR;
					IRCBAND |= 0x03;
					CLKDIV = 0x00;
					break;
				case 44236800UL:
					CLKDIV = 0x04;
					IRTRIM = T44M_ADDR;
					VRTRIM = VRT44M_ADDR;
					IRCBAND |= 0x03;
					CLKDIV = 0x00;
					WTST = 1;
					break;
				default:
					CLKDIV = 0x04;
					IRTRIM = T35M_ADDR;
					VRTRIM = VRT44M_ADDR;
					IRCBAND |= 0x03;
					CLKDIV = 0x00;
					break;
		}
	
		// 设置P54引脚功能为复位
    RSTCFG = 0x10;

		// 初始化全部IO口为准双向口
    P0M1 = 0x00;
    P0M0 = 0x00;
    P1M1 = 0x00;
    P1M0 = 0x00;
    P2M1 = 0x00;
    P2M0 = 0x00;
    P3M1 = 0x00;
    P3M0 = 0x00;
    P4M1 = 0x00;
    P4M0 = 0x00;
    P5M1 = 0x00;
    P5M0 = 0x00;
    P6M1 = 0x00;
    P6M0 = 0x00;
    P7M1 = 0x00;
    P7M0 = 0x00;
		
		// 初始化寄存器
		ADCCFG = 0;
		AUXR = 0;
		SCON = 0;
		S2CON = 0;
		S3CON = 0;
		S4CON = 0;
		P_SW1 = 0;
		IE2 = 0;
		TMOD = 0;

		// 初始化全局中断
    EA = 1;
}