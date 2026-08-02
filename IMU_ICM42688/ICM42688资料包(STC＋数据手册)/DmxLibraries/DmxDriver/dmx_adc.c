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
 * @file       dmx_adc.c
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
#include "dmx_adc.h"

/**
*
* @brief    adc初始化
* @param    adc_n     	选择需要初始化adc引脚(dmx_adc.h文件里已枚举定义)
* @return   void
* @notes    调用此函数前可查看dmx_adc.h文件里枚举的可用引脚
* Example:  init_adc(ADC0_P10,ADC_CLK_2);	// 初始化ADC0_P10引脚,时钟频率为(系统时钟/2)
*
**/
void init_adc(ADC_pin_enum adc_n, ADC_clk_enum clk)
{
    ADC_POWER = 1;
    ADC_CONTR &= 0xF0;
    ADC_CONTR |= adc_n;
    if(adc_n / 8 == 1)
    {
        P0M1 |=  (0x01 << adc_n % 8);
        P0M0 &= ~(0x01 << adc_n % 8);
    }
    else if(adc_n / 8 == 0)
    {
        P1M1 |=  (0x01 << adc_n % 8);
        P1M0 &= ~(0x01 << adc_n % 8);
    }
    ADCCFG |= 0xF0;
    ADCCFG |= clk;
}

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
unsigned int get_adc(ADC_pin_enum adc_n, ADC_res_enum adc_res)
{
    ADC_CONTR &= 0xF0;
    ADC_CONTR |= adc_n;
    ADC_RES = 0;
    ADC_RESL = 0;
    ADC_START = 1;
    while(ADC_FLAG == 0);
    ADC_FLAG = 0;
    return (((unsigned int)ADC_RES << 8) | ADC_RESL) >> (4 - (adc_res * 2));
}