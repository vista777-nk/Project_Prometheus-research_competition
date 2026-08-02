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
 * @file       dmx_mpu.c
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
#include "dmx_delay.h"
#include "dmx_icm42688_iic.h"

// 静态函数声明,以下函数均为该.c文件内部调用
static void ICM42688_Write_Data(unsigned char addr, unsigned char reg, unsigned char dat);
static void ICM42688_Read_Datas(unsigned char addr, unsigned char reg, unsigned char *dat, unsigned char num);
// 数据转换为实际物理数据的转换系数
static float icm42688_iic_acc_inv = 1, icm42688_iic_gyro_inv = 1;
// 陀螺仪地址
static unsigned char ICM42688_GYROSCOPE_DEV_ADDR;

/**
*
* @brief    ICM42688陀螺仪初始化
* @param
* @return   void
* @notes    用户调用
* Example:  Init_ICM42688_IIC();
*
**/
void Init_ICM42688_IIC(void)
{
    unsigned char model;
    unsigned char slave_addr;
    unsigned char time;

    // 读取陀螺仪型号陀螺仪自检
    model = 0xff;
    slave_addr = 0x69;
    while(1)
    {
        time = 50;
        ICM42688_Read_Datas(slave_addr, ICM42688_WHO_AM_I, &model, 1);
        if(model == 0x47)
        {
            // icm42688,0x47
            ICM42688_GYROSCOPE_DEV_ADDR = 0x69;
            break;
        }
        else
        {
            ICM42688_DELAY_MS(20);
            time--;
            if(time < 0)
            {
                while(1);
                // 卡在这里原因有以下几点
                // ICM42688坏了,如果是新的概率极低
                // 接线错误或者没有接好
                // 可能需要外接上拉电阻,上拉到3.3V
            }
        }
    }
		
    ICM42688_Write_Data(ICM42688_GYROSCOPE_DEV_ADDR, ICM42688_PWR_MGMT0, 0x00);	// 复位设备
    ICM42688_DELAY_MS(20);																											// 操作完PWR—MGMT0寄存器后200us内不能有任何读写寄存器的操作

    // 设置ICM42688加速度计和陀螺仪的量程和输出速率
    Set_LowpassFilter_Range_ICM42688_IIC(ICM42688_AFS_16G, ICM42688_AODR_32000HZ, ICM42688_GFS_2000DPS, ICM42688_GODR_32000HZ);

    ICM42688_Write_Data(ICM42688_GYROSCOPE_DEV_ADDR, ICM42688_PWR_MGMT0, 0x0f); // 设置GYRO_MODE,ACCEL_MODE为低噪声模式
    ICM42688_DELAY_MS(20);
}

/**
*
* @brief    获得ICM42688陀螺仪加速度
* @param
* @return   void
* @notes    单位:g(m/s^2),用户调用
* Example:  Get_Acc_ICM42688_IIC();
*
**/
void Get_Acc_ICM42688_IIC(void)
{
    unsigned char dat[6];
    ICM42688_Read_Datas(ICM42688_GYROSCOPE_DEV_ADDR, ICM42688_ACCEL_DATA_X1, dat, 6);
    icm42688_acc_x = icm42688_iic_acc_inv * (short int)(((short int)dat[0] << 8) | dat[1]);
    icm42688_acc_y = icm42688_iic_acc_inv * (short int)(((short int)dat[2] << 8) | dat[3]);
    icm42688_acc_z = icm42688_iic_acc_inv * (short int)(((short int)dat[4] << 8) | dat[5]);
}

/**
*
* @brief    获得ICM42688陀螺仪角加速度
* @param
* @return   void
* @notes    单位为:°/s,用户调用
* Example:  Get_Gyro_ICM42688_IIC();
*
**/
void Get_Gyro_ICM42688_IIC(void)
{
    unsigned char dat[6];
    ICM42688_Read_Datas(ICM42688_GYROSCOPE_DEV_ADDR, ICM42688_GYRO_DATA_X1, dat, 6);
    icm42688_gyro_x = icm42688_iic_gyro_inv * (short int)(((short int)dat[0] << 8) | dat[1]);
    icm42688_gyro_y = icm42688_iic_gyro_inv * (short int)(((short int)dat[2] << 8) | dat[3]);
    icm42688_gyro_z = icm42688_iic_gyro_inv * (short int)(((short int)dat[4] << 8) | dat[5]);
}

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
void Set_LowpassFilter_Range_ICM42688_IIC(enum icm42688_afs afs, enum icm42688_aodr aodr, enum icm42688_gfs gfs, enum icm42688_godr godr)
{
    ICM42688_Write_Data(ICM42688_GYROSCOPE_DEV_ADDR,ICM42688_ACCEL_CONFIG0, (afs << 5) | (aodr + 1));   // 初始化ACCEL量程和输出速率(p77)
    ICM42688_Write_Data(ICM42688_GYROSCOPE_DEV_ADDR,ICM42688_GYRO_CONFIG0, (gfs << 5) | (godr + 1));    // 初始化GYRO量程和输出速率(p76)

    switch(afs)
    {
    case ICM42688_AFS_2G:
        icm42688_iic_acc_inv = 2000 / 32768.0f;             // 加速度计量程为:±2g
        break;
    case ICM42688_AFS_4G:
        icm42688_iic_acc_inv = 4000 / 32768.0f;             // 加速度计量程为:±4g
        break;
    case ICM42688_AFS_8G:
        icm42688_iic_acc_inv = 8000 / 32768.0f;             // 加速度计量程为:±8g
        break;
    case ICM42688_AFS_16G:
        icm42688_iic_acc_inv = 16000 / 32768.0f;            // 加速度计量程为:±16g
        break;
    default:
        icm42688_iic_acc_inv = 1;                           // 不转化为实际数据
        break;
    }
    switch(gfs)
    {
    case ICM42688_GFS_15_625DPS:
        icm42688_iic_gyro_inv = 15.625f / 32768.0f;         // 陀螺仪量程为:±15.625dps
        break;
    case ICM42688_GFS_31_25DPS:
        icm42688_iic_gyro_inv = 31.25f / 32768.0f;          // 陀螺仪量程为:±31.25dps
        break;
    case ICM42688_GFS_62_5DPS:
        icm42688_iic_gyro_inv = 62.5f / 32768.0f;           // 陀螺仪量程为:±62.5dps
        break;
    case ICM42688_GFS_125DPS:
        icm42688_iic_gyro_inv = 125.0f / 32768.0f;          // 陀螺仪量程为:±125dps
        break;
    case ICM42688_GFS_250DPS:
        icm42688_iic_gyro_inv = 250.0f / 32768.0f;          // 陀螺仪量程为:±250dps
        break;
    case ICM42688_GFS_500DPS:
        icm42688_iic_gyro_inv = 500.0f / 32768.0f;          // 陀螺仪量程为:±500dps
        break;
    case ICM42688_GFS_1000DPS:
        icm42688_iic_gyro_inv = 1000.0f / 32768.0f;         // 陀螺仪量程为:±1000dps
        break;
    case ICM42688_GFS_2000DPS:
        icm42688_iic_gyro_inv = 2000.0f / 32768.0f;         // 陀螺仪量程为:±2000dps
        break;
    default:
        icm42688_iic_gyro_inv = 1;                          // 不转化为实际数据
        break;
    }
}        

/**
*
* @brief    ICM42688软件IIC开始
* @param    void
* @return   void
* @notes    内部调用
* Example:  ICM42688_Start();
*
**/
static void ICM42688_Start(void)
{
    ICM42688_SCL_LEVEL(1);
    ICM42688_SDA_LEVEL(1);
    ICM42688_DELAY_MS(ICM42688_DELAY);
    ICM42688_SDA_LEVEL(0);
    ICM42688_DELAY_MS(ICM42688_DELAY);
    ICM42688_SCL_LEVEL(0);
}

/**
*
* @brief    ICM42688软件IIC停止
* @param    void
* @return   void
* @notes    内部调用
* Example:  ICM42688_Stop();
*
**/
static void ICM42688_Stop(void)
{
    ICM42688_SCL_LEVEL(0);
    ICM42688_SDA_LEVEL(0);
    ICM42688_DELAY_MS(ICM42688_DELAY);
    ICM42688_SCL_LEVEL(1);
    ICM42688_DELAY_MS(ICM42688_DELAY);
    ICM42688_SDA_LEVEL(1);
    ICM42688_DELAY_MS(ICM42688_DELAY);
}

/**
*
* @brief    主机向从设备发送应答/非应答信号 1/0
* @param    ack            	发送信号
* @return   void
* @notes    内部调用
* Example:  ICM42688_Sendack(1);
*
**/
static void ICM42688_Sendack(unsigned char ack)
{
    ICM42688_SCL_LEVEL(0);
    ICM42688_DELAY_MS(ICM42688_DELAY);
    if(ack)
        ICM42688_SDA_LEVEL(0);
    else
        ICM42688_SDA_LEVEL(1);
    ICM42688_SCL_LEVEL(1);
    ICM42688_DELAY_MS(ICM42688_DELAY);
    ICM42688_SCL_LEVEL(0);
    ICM42688_DELAY_MS(ICM42688_DELAY);
}

/**
*
* @brief    主机等待从设备应答信号
* @param    void
* @return   int
* @notes    内部调用
* Example:  ICM42688_Waitack();
*
**/
static int ICM42688_Waitack(void)
{
    ICM42688_SCL_LEVEL(0);
    ICM42688_DELAY_MS(ICM42688_DELAY);
    ICM42688_SCL_LEVEL(1);
    ICM42688_DELAY_MS(ICM42688_DELAY);
    if(ICM42688_SDA)
    {
        ICM42688_SCL_LEVEL(1);
        return 0;
    }
    ICM42688_SCL_LEVEL(0);
    ICM42688_DELAY_MS(ICM42688_DELAY);
    return 1;
}

/**
*
* @brief    发送单个字节
* @param    ch                  字节
* @return   void
* @notes    内部调用
* Example:  ICM42688_Write_Char(0X66);
*
**/
static void ICM42688_Write_Char(unsigned char ch)
{
    unsigned char i = 8;
    while(i--)
    {
        if(ch & 0x80)
            ICM42688_SDA_LEVEL(1);
        else
            ICM42688_SDA_LEVEL(0);
        ch <<= 1;
        ICM42688_DELAY_MS(ICM42688_DELAY);
        ICM42688_SCL_LEVEL(1);
        ICM42688_DELAY_MS(ICM42688_DELAY);
        ICM42688_SCL_LEVEL(0);
    }
    ICM42688_Waitack();
}

/**
*
* @brief    接受一个字节数据
* @param    ack               	应答信号
* @return   unsigned char       接受字节
* @notes    内部调用
* Example:  ICM42688_Read_Char(0);
*
**/
static unsigned char ICM42688_Read_Char(unsigned char ack)
{
    unsigned char i;
    unsigned char c = 0;
    ICM42688_SCL_LEVEL(0);
    ICM42688_DELAY_MS(ICM42688_DELAY);
    ICM42688_SDA_LEVEL(1);
    for(i = 0; i < 8; i++)
    {
        ICM42688_DELAY_MS(ICM42688_DELAY);
        ICM42688_SCL_LEVEL(0);
        ICM42688_DELAY_MS(ICM42688_DELAY);
        ICM42688_SCL_LEVEL(1);
        ICM42688_DELAY_MS(ICM42688_DELAY);
        c <<= 1;
        if(ICM42688_SDA)
        {
            c += 1;
        }
    }
    ICM42688_SCL_LEVEL(0);
    ICM42688_DELAY_MS(ICM42688_DELAY);
    ICM42688_Sendack(ack);
    return c;
}

/**
*
* @brief    写数据到设备寄存器中
* @param    addr         		设备地址
* @param    reg         		设备寄存器地址
* @param    dat          		数据地址
* @return   void
* @notes		内部调用
* Example:  ICM42688_Write_Data(addr,reg,data)
*
**/
static void ICM42688_Write_Data(unsigned char addr, unsigned char reg, unsigned char dat)
{
    ICM42688_Start();
    ICM42688_Write_Char(addr << 1);
    ICM42688_Write_Char(reg);
    ICM42688_Write_Char(dat);
    ICM42688_Stop();
}

/**
*
* @brief    从设备寄存器读取多字节数据
* @param    addr         		设备地址
* @param    reg         		设备寄存器地址
* @param    dat          		数据地址
* @param    num           	数据长度
* @return   void
* @notes		内部调用
* Example:  ICM42688_Read_Datas(addr,reg,data,num)
*
**/
static void ICM42688_Read_Datas(unsigned char addr, unsigned char reg, unsigned char *dat, unsigned char num)
{
    ICM42688_Start();
    ICM42688_Write_Char(addr << 1 | 0x00);
    ICM42688_Write_Char(reg);
    ICM42688_Start();
    ICM42688_Write_Char((addr << 1) | 0x01);
    while(--num)
    {
        *dat = ICM42688_Read_Char(1);
        dat++;
    }
    *dat = ICM42688_Read_Char(0);
    ICM42688_Stop();
}
