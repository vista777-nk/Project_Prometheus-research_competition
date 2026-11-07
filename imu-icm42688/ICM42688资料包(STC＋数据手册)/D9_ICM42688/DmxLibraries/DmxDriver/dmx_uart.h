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
 * @file       dmx_uart.h
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

#ifndef _DMX_UART_H_
#define _DMX_UART_H_

#include "dmx_pit.h"

// 串口号枚举
typedef enum
{
    UART1,
    UART2,
    UART3,
    UART4,
} UART_num_enum;

// 串口引脚枚举
typedef enum
{
    UART1_RX_P30_TX_P31,
    UART1_RX_P36_TX_P37,
    UART1_RX_P16_TX_P17,
    UART1_RX_P43_TX_P44,

    UART2_RX_P10_TX_P11,
    UART2_RX_P46_TX_P47,

    UART3_RX_P00_TX_P01,
    UART3_RX_P50_TX_P51,

    UART4_RX_P02_TX_P03,
    UART4_RX_P52_TX_P53,
} UART_pin_enum;

// 声明的变量
extern char TX_BUSY[5];

/**
*
* @brief    串口初始化
* @param    uart_x		选择的串口通道号,根据dmx_uart.h中枚举
* @param    pit_x			选择的定时器通道号,根据dmx_pit.h中枚举
* @param    pin				选择的串口引脚,根据dmx_uart.h中枚举
* @param    baud 			波特率(常用:9600,115200)
* @return   void
* @notes    串口1只可以用定时器1或定时器2;串口2只可以用定时器2;串口3只可以用定时器2或定时器3;串口4只可以用定时器2或定时器4;
* Example:  init_uart(UART1,PIT2,UART1_RX_P36_TX_P37,115200);	// STC32G的串口1利用定时器2,RXD引脚为P36,TX引脚为P37,波特率为115200
*
**/
void init_uart(UART_num_enum uart_x, PIT_num_enum pit_x, UART_pin_enum pin, unsigned long baud);

/**
*
* @brief    串口发送字节
* @param    uart_x		选择的串口通道号,根据dmx_uart.h中枚举
* @param    dat 			需要发送的字节
* @return   void
* @notes
* Example:  send_char_uart(UART1,'A');	// STC32G通过串口1的引脚发送字节'A',引脚既是初始化时选择的引脚
*
**/
void send_char_uart(UART_num_enum uart_x, char dat);

/**
*
* @brief    串口发送字符串
* @param    uart_x		选择的串口通道号,根据dmx_uart.h中枚举
* @param    str 			需要发送的字符串
* @return   void
* @notes
* Example:  send_string_uart(UART1,"UART1 TEST!\n");	// STC32G通过串口1的引脚发送字符串"UART1 TEST!\n",引脚既是初始化时选择的引脚
*
**/
void send_string_uart(UART_num_enum uart_x, char *str);



#endif