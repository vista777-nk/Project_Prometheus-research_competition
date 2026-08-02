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
 * @file       dmx_mpu.h
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

#ifndef _DMX_ICM42688_IIC_H_
#define _DMX_ICM42688_IIC_H_

#include "dmx_icm42688.h"

// ICM42688陀螺仪模块管脚宏定义
// ICM42688,SCL和SDA管脚
#define ICM42688_SCL  P40
#define ICM42688_SDA  P41
// ICM42688引脚电平操作
#define ICM42688_SCL_LEVEL(level) 	(ICM42688_SCL=level)
#define ICM42688_SDA_LEVEL(level) 	(ICM42688_SDA=level)

/**
*
* @brief    ICM42688陀螺仪初始化 
* @param
* @return   void
* @notes    用户调用
* Example:  Init_ICM42688_IIC();
*
**/
void Init_ICM42688_IIC(void);

/**
*
* @brief    获得ICM42688陀螺仪加速度
* @param
* @return   void
* @notes    单位:g(m/s^2),用户调用
* Example:  Get_Acc_ICM42688_IIC();
*
**/
void Get_Acc_ICM42688_IIC(void);

/**
*
* @brief    获得ICM42688陀螺仪角加速度
* @param
* @return   void
* @notes    单位为:°/s,用户调用
* Example:  Get_Gyro_ICM42688_IIC();
*
**/
void Get_Gyro_ICM42688_IIC(void);

/**
*
* @brief    设置ICM42688陀螺仪低通滤波器带宽和量程
* @param    afs                 // 加速度计量程,可在dmx_icm42688.h文件里枚举定义中查看
* @param    aodr                // 加速度计输出速率,可在dmx_icm42688.h文件里枚举定义中查看
* @param    gfs                 // 陀螺仪量程,可在dmx_icm42688.h文件里枚举定义中查看
* @param    godr                // 陀螺仪输出速率,可在dmx_icm42688.h文件里枚举定义中查看
* @return   void
* @notes    ICM42688.c文件内部调用,用户无需调用尝试
* Example:  Set_LowpassFilter_Range_ICM42688_IIC(ICM42688_AFS_16G,ICM42688_AODR_32000HZ,ICM42688_GFS_2000DPS,ICM42688_GODR_32000HZ);
*
**/
void Set_LowpassFilter_Range_ICM42688_IIC(enum icm42688_afs afs, enum icm42688_aodr aodr, enum icm42688_gfs gfs, enum icm42688_godr godr);


#endif