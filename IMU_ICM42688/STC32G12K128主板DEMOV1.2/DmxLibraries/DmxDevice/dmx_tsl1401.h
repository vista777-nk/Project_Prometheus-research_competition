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
 * @file       dmx_tsl1401.h
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

#ifndef _DMX_TSL1401_H_
#define _DMX_TSL1401_H_

// CCD曝光时间,单位MS
#define TCL1401_EXPOSURE_TIME		10

// CCD的CLK引脚
#define TCL1401_CLK_PIN					P33
// CCD的SI引脚
#define TCL1401_SI_PIN     			P32
// CCD的AO引脚
#define TCL1401_AO_PIN     			ADC4_P14

// CCD管脚电平操作
#define TCL1401_CLK_LEVEL(level) 	(TCL1401_CLK_PIN = (level))
#define TCL1401_SI_LEVEL(level) 	(TCL1401_SI_PIN  = (level))

extern unsigned int ccd_data[128];	// 声明CCD数据(一维数组即一行图像)

/**
*
* @brief    TSL1401线阵CCD初始化
* @param
* @return   void
* @notes    用户调用
* Example:  Init_TSL1401();
*
**/
void Init_TSL1401(void);

/**
*
* @brief    TSL1401线阵CCD数据获取
* @param
* @return   void
* @notes    用户调用
* Example:  Get_TSL1401();
*
**/
void Get_TSL1401(void);

#endif