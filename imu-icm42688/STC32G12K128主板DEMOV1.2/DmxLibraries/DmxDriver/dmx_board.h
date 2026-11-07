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
 * @file       dmx_board.h
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

#ifndef _DMX_BOARD_H_
#define _DMX_BOARD_H_

#include "stc32g.h"

// 单片机主频,软件强制设置内部IRC主频,STC-ISP中所选无效
// 可选 22.1184MHz,24MHz,27MHz,30MHz,30.1776MHz,35MHz,36.864MHz,40MHz,44.2368MHz
#define MAIN_FOSC	35000000UL

#define     T22M_ADDR			CHIPID11 	// 22.1184MHz
#define     T24M_ADDR 		CHIPID12 	// 24MHz
#define     T27M_ADDR 		CHIPID13 	// 27MHz
#define     T30M_ADDR 		CHIPID14 	// 30MHz
#define     T33M_ADDR 		CHIPID15 	// 33.1776MHz
#define     T35M_ADDR 		CHIPID16 	// 35MHz
#define     T36M_ADDR 		CHIPID17 	// 36.864MHz
#define     T40M_ADDR 		CHIPID18 	// 40MHz
#define     T44M_ADDR 		CHIPID19 	// 44.2368MHz
#define     VRT27M_ADDR 	CHIPID23 	// VRTRIM_27M
#define     VRT44M_ADDR 	CHIPID24 	// VRTRIM_44M

/**
*
* @brief    芯片初始化
* @param    void
* @return   void
* @notes    调用此函数前可查看dmx_adc.h文件里枚举的可用引脚
* Example:  init_chip();
*
**/
void init_chip(void);
    
#endif