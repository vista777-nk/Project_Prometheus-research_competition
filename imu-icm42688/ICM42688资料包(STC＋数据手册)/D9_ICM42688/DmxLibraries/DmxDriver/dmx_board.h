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

#ifndef _DMX_BOARD_H_
#define _DMX_BOARD_H_

#include "stc32g.h"

// 单片机主频,软件强制设置内部IRC主频,STC-ISP中所选无效
// 可选 22.1184MHz,24MHz,40MHz,45.1584MHz,48MHz,50.8032MHz,52MHz,56MHz,60MHz,64MHz
#define MAIN_FOSC	56000000UL

#define     T22M_ADDR			CHIPID11 	// 22.1184MHz
#define     T24M_ADDR 		CHIPID12 	// 24MHz
#define     T40M_ADDR 		CHIPID13 	// 40MHz
#define     T45M_ADDR 		CHIPID14 	// 45.1584MHz
#define     T48M_ADDR 		CHIPID15 	// 48MHz
#define     T50M_ADDR 		CHIPID16 	// 50.8032MHz
#define     T52M_ADDR 		CHIPID17 	// 52MHz
#define     T56M_ADDR 		CHIPID18 	// 56MHz
#define     T60M_ADDR 		CHIPID19 	// 60MHz
#define     T64M_ADDR 		CHIPID20 	// 64MHz
#define     VRT20M_ADDR 	CHIPID21 	// VRTRIM_20M
#define     VRT24M_ADDR 	CHIPID22 	// VRTRIM_24M
#define     VRT44M_ADDR 	CHIPID23 	// VRTRIM_44M
#define     VRT72M_ADDR 	CHIPID24 	// VRTRIM_64M

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

/**
*
* @brief    开启ICACHE功能
* @param    void
* @return   void
* @notes    程序存储器高速缓存技术,弥补内部程序存储器的速度跟不上内部硬件逻辑和外设的速度
* Example:  open_icache();
*
**/
void open_icache(void);

/**
*
* @brief    关闭ICACHE功能
* @param    void
* @return   void
* @notes    程序存储器高速缓存技术,弥补内部程序存储器的速度跟不上内部硬件逻辑和外设的速度
* Example:  close_icache();
*
**/
void close_icache(void);
    
#endif