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
 * @file       dmx_hard_spi.h
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

#ifndef _DMX_HARD_SPI_H_
#define _DMX_HARD_SPI_H_

// 硬件SPI引脚枚举
typedef enum
{
    SPI1_SCK_P15_MOSI_P13_MISO_P14,
    SPI2_SCK_P25_MOSI_P23_MISO_P24,
    SPI3_SCK_P43_MOSI_P40_MISO_P41,
    SPI4_SCK_P32_MOSI_P34_MISO_P33,
} SPI_pin_enum;

// 硬件SPI速率枚举
typedef enum
{
    SPI_SPEED4,
    SPI_SPEED8,
    SPI_SPEED16,
    SPI_SPEED2,
} SPI_speed_enum;

/**
*
* @brief    硬件SPI初始化
* @param    pin     	初始化的硬件SPI引脚(dmx_hard_spi.h文件里已枚举定义)
* @param    speed    	初始化的硬件SPI速度(dmx_hard_spi.h文件里已枚举定义)
* @param    mode     	硬件SPI模式选择
* @param    mstr     	硬件SPI主从模式选择(主机模式:0,从机模式:1)
* @return   void
* @notes
* Example:  init_hard_spi(SPI2_SCK_P25_MOSI_P23_MISO_P24,SPI_SPEED16,0,0);	// 初始化硬件SPI:SCK_P25,MOSI_P23,MISO_P24引脚,速度为SPI_SPEED16,主机模式
*
**/
void init_hard_spi(SPI_pin_enum pin, SPI_speed_enum speed, unsigned char mode, unsigned char mstr);

/**
*
* @brief    硬件SPI写8bit数据
* @param    dat             数据
* @return   void
* @notes
* Example:  write_8bit_hard_spi(0x11);
*
**/
void write_8bit_hard_spi(unsigned char dat);

/**
*
* @brief    硬件SPI读8bit数据
* @return   unsigned char  	数据
* @notes
* Example:  read_8bit_hard_spi();
*
**/
unsigned char read_8bit_hard_spi(void);

#endif