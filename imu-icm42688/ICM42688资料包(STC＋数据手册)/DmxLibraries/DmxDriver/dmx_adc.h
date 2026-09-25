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
 * @file       dmx_adc.h
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

#ifndef _DMX_ADC_H_
#define _DMX_ADC_H_

// ADC引脚枚举
typedef enum
{
    // P1端口ADC引脚
    ADC0_P10,
    ADC1_P11,
    ADCX_PXX,
    ADC3_P13,
    ADC4_P14,
    ADC5_P15,
    ADC6_P16,
    ADC7_P17,
    // P0端口ADC引脚
    ADC8_P00,
    ADC9_P01,
    ADC10_P02,
    ADC11_P03,
    ADC12_P04,
    ADC13_P05,
    ADC14_P06,
    ADC15,
} ADC_pin_enum;

// ADC采样频率枚举
typedef enum
{
    ADC_CLK_2,
    ADC_CLK_4,
    ADC_CLK_6,
    ADC_CLK_8,
    ADC_CLK_10,
    ADC_CLK_12,
    ADC_CLK_14,
    ADC_CLK_16,
    ADC_CLK_18,
    ADC_CLK_20,
    ADC_CLK_22,
    ADC_CLK_24,
    ADC_CLK_26,
    ADC_CLK_28,
    ADC_CLK_30,
    ADC_CLK_32,
} ADC_clk_enum;

// ADC分辨率枚举
typedef enum
{
    ADC_8BIT,
    ADC_10BIT,
    ADC_12BIT,
} ADC_res_enum;

/**
*
* @brief    adc初始化
* @param    adc_n     	选择需要初始化adc引脚(dmx_adc.h文件里已枚举定义)
* @return   void
* @notes    调用此函数前可查看dmx_adc.h文件里枚举的可用引脚
* Example:  init_adc(ADC0_P10,ADC_CLK_2);	// 初始化ADC0_P10引脚,时钟频率为(系统时钟/2)
*
**/
void init_adc(ADC_pin_enum adc_n, ADC_clk_enum clk);

/**
*
* @brief    adc转换一次即获取一次ad值
* @param    adc_n     	选择需要读取的adc引脚(dmx_adc.h文件里已枚举定义)
* @param    adc_res     选择adc精度(8bit,10bit,12bit)
* @return   void
* @notes
* Example:  get_adc(ADC0_P10, ADC_12BIT);  // 获取一次ADC0_P10引脚的ad值,精度为12bit即该函数返回值最大为2的十二次方
*
**/
unsigned int get_adc(ADC_pin_enum adc_n, ADC_res_enum adc_res);

#endif