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
 * @file       dmx_icm20602.h
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

#ifndef _DMX_ICM20602_H_
#define _DMX_ICM20602_H_

#include "dmx_mpu.h"

// IPS屏幕管脚宏定义
// ICM20602,SPC管脚
#define ICM20602_SPC     		P40
// ICM20602,SD1管脚
#define ICM20602_SDI     		P41
// ICM20602,SD0管脚
#define ICM20602_SDO    		P42
// ICM20602,CS管脚
#define ICM20602_CS      		P43

// CS管脚电平操作
#define ICM20602_SPC_LEVEL(level)	(ICM20602_SPC=level)
#define ICM20602_SDI_LEVEL(level)	(ICM20602_SDI=level)
#define ICM20602_CS_LEVEL(level)	(ICM20602_CS=level)

// ICM20602延时
#define ICM20602_DELAY					0
#define ICM20602_DELAY_MS(time)	(delay_ms(time))

extern float icm20602_acc_x, icm20602_acc_y, icm20602_acc_z  ;       // 声明ICM20602加速度计数据
extern float icm20602_gyro_x, icm20602_gyro_y, icm20602_gyro_z ;     // 声明ICM20602角加速度数据

/**
*
* @brief    ICM20602陀螺仪初始化
* @param
* @return   void
* @notes    用户调用
* Example:  Init_ICM20602();
*
**/
void Init_ICM20602(void);

/**
*
* @brief    获得ICM20602陀螺仪加速度
* @param
* @return   void
* @notes    单位:g(m/s^2),用户调用
* Example:  Get_Acc_ICM20602();
*
**/
void Get_Acc_ICM20602(void);

/**
*
* @brief    获得ICM20602陀螺仪角加速度
* @param
* @return   void
* @notes    单位为:°/s,用户调用
* Example:  Get_Gyro_ICM20602();
*
**/
void Get_Gyro_ICM20602(void);

/**
*
* @brief    设置ICM20602陀螺仪低通滤波器带宽和量程
* @param    abw                 // 可在dmx_mpu.h文件里枚举定义中查看
* @param    gbw                 // 可在dmx_mpu.h文件里枚举定义中查看
* @param    afs                 // 可在dmx_mpu.h文件里枚举定义中查看
* @param    gfs                 // 可在dmx_mpu.h文件里枚举定义中查看
* @return   void
* @notes    ICM20602.c文件内部调用,用户无需调用尝试
* Example:  Set_LowpassFilter_Range_ICM20602(MPU_ABW_218, MPU_GBW_176,MPU_FS_4G, MPU_FS_2000DPS);
*
**/
void Set_LowpassFilter_Range_ICM20602(enum mpu_acc_bw abw, enum mpu_gyro_bw gbw, enum mpu_acc_fs afs, enum mpu_gyro_fs gfs);

#endif
