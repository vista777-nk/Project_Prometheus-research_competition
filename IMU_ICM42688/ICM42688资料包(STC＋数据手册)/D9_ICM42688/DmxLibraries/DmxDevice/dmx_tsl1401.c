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
 * @file       dmx_tsl1401.c
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
#include "isr.h"
#include "dmx_pit.h"
#include "dmx_adc.h"
#include "dmx_tsl1401.h"

// CCD数据(一维数组即一行图像)
unsigned int ccd_data[128];

/**
*
* @brief    TSL1401线阵CCD初始化
* @param
* @return   void
* @notes    用户调用
* Example:  Init_TSL1401();
*
**/
void Init_TSL1401(void)
{
    init_adc(ADC4_P14, ADC_CLK_2);
    init_pit_ms(PIT1, 1);
    init_nvic(TIME1_PRIORITY, 3);
}

/**
*
* @brief    TSL1401线阵CCD数据获取
* @param
* @return   void
* @notes    用户调用
* Example:  Get_TSL1401();
*
**/
void Get_TSL1401(void)
{
    unsigned int i = 0;
    TCL1401_CLK_LEVEL(0);
	
    TCL1401_CLK_LEVEL(1);
    TCL1401_SI_LEVEL(0);
	
    TCL1401_SI_LEVEL(1);
    TCL1401_CLK_LEVEL(0);
	
    TCL1401_CLK_LEVEL(1);
    TCL1401_SI_LEVEL(0);
	
    for(i = 0; i < 128; i++)
    {
        TCL1401_CLK_LEVEL(0);
        ccd_data[i] = get_adc(TCL1401_AO_PIN, ADC_12BIT);
        TCL1401_CLK_LEVEL(1);
    }
}