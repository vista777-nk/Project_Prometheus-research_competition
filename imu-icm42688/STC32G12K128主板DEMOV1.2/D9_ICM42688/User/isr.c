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
 * @file       dmx_isr.c
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

#include "dmx_all.h"

// 外部中断0
void INT0_ISR(void) interrupt 0
{
    if(P32)	// 上升沿触发
    {

    }
    else		// 下降沿触发
    {
			
    }
}

// 外部中断1
void INT1_ISR(void) interrupt 2
{
    if(P33)	// 上升沿触发
    {

    }
    else		// 下降沿触发
    {

    }
}

// 外部中断2
void INT2_ISR(void) interrupt 10
{
    // 下降沿触发

}

// 外部中断3
void INT3_ISR(void) interrupt 11
{
    // 下降沿触发

}

// 外部中断4
void INT4_ISR(void) interrupt 16
{
    // 下降沿触发,P30作为RXD引脚时不可使用该中断

}
// 定时器0中断
void TIME0_ISR(void) interrupt 1
{
		
}


float	Angle_gx=0,Angle_gy=0,Angle_gz=0;		//由角速度计算的角速率(角度制)
float	Angle_ax=0,Angle_ay=0,Angle_az=0;		//由加速度计算的加速度(弧度制)
int g_x=0,g_y=0,g_z=0;					//陀螺仪矫正参数
// 定时器1中断
void TIME1_ISR(void) interrupt 3
{
	


				// 获取ICM42688陀螺仪加速度数据
				//Get_Acc_ICM42688();
				Get_Acc_ICM42688_IIC();
				// 获取ICM42688陀螺仪角加速度数据
				//Get_Gyro_ICM42688();
				Get_Gyro_ICM42688_IIC();
	
	
	Angle_ax = (icm42688_acc_x) / 8192.0;	//加速度处理	结果单位是 +- g
	Angle_ay = (icm42688_acc_y) / 8192.0;	//转换关系	8192 LSB/g, 1g对应读数8192
	Angle_az = (icm42688_acc_z) / 8192.0;	//加速度量程 +-4g/S

	Angle_gx = ((float)(icm42688_gyro_x - g_x)) / 65.5;	//陀螺仪处理	结果单位是 +-度
	Angle_gy = ((float)(icm42688_gyro_y - g_y)) / 65.5;	//陀螺仪量程 +-500度/S, 1度/秒 对应读数 65.536
	Angle_gz = ((float)(icm42688_gyro_z - g_z)) / 65.5;	//转换关系65.5 LSB/度

	IMUupdate(Angle_gx*0.0174533f, Angle_gy*0.0174533f, Angle_gz*0.0174533f, Angle_ax,Angle_ay,Angle_az);
}

// 定时器2中断
void TIME2_ISR(void) interrupt 12
{

}

// 定时器3中断
void TIME3_ISR(void) interrupt 19
{

}

// 定时器4中断
void TIME4_ISR(void) interrupt 20
{

}



// 串口1中断
void UART1_ISR(void) interrupt 4
{
    if(TI)		// 获取串口1发送标志
    {
        TI = 0;		// 清理串口1发送标志
        TX_BUSY[1] = 0;
    }
    if(RI)		// 获取串口1接收标志
    {
        RI = 0;		// 清理串口1接收标志

    }
}

// 串口2中断
void UART2_ISR(void) interrupt 8
{
    if(S2TI)	// 获取串口2发送标志
    {
        S2TI = 0;	// 清理串口2发送标志
        TX_BUSY[2] = 0;
    }
    if(S2RI)	// 获取串口2接收标志
    {
        S2RI = 0;	// 清理串口2接收标志

    }
}

// 串口3中断
void UART3_ISR(void) interrupt 17
{
    if(S3TI)	// 获取串口3发送标志
    {
        S3TI = 0;	// 清理串口3发送标志
        TX_BUSY[3] = 0;
    }
    if(S3RI)	// 获取串口3接收标志
    {
        S3RI = 0;	// 清理串口3接收标志

    }
}

// 串口4中断
void UART4_ISR(void) interrupt 18
{
    if(S4TI)	// 获取串口4发送标志
    {
        S4TI = 0;	// 清理串口4发送标志
        TX_BUSY[4] = 0;
    }
    if(S4RI)	// 获取串口4接收标志
    {
        S4RI = 0;	// 清理串口4接收标志

    }
}

/**
*
* @brief    中断优先级设置
* @param    isr_nvic				中断名,根据isr.h中枚举查看
* @param    priority				优先级(0~3,3级为最高级)
* @return   void
* @notes    串口发送数据时,串口中断优先级要高于定时器中断优先级
* Example:  init_nvic(TIME1_PRIORITY,3);	// 定时器1中断优先级设置为最高级别3级
*
**/
void init_nvic(ISR_nvic_enum isr_nvic, unsigned char priority)
{
    switch(isr_nvic)
    {
    // 外部中断优先级分配
    case INT0_PRIORITY:
        switch(priority)
        {
        case 0:
            PX0H = 0;
            PX0 = 0;
            break;
        case 1:
            PX0H = 0;
            PX0 = 1;
            break;
        case 2:
            PX0H = 1;
            PX0 = 0;
            break;
        case 3:
            PX0H = 1;
            PX0 = 1;
            break;
        }
        break;
    case INT1_PRIORITY:
        switch(priority)
        {
        case 0:
            PX1H = 0;
            PX1 = 0;
            break;
        case 1:
            PX1H = 0;
            PX1 = 1;
            break;
        case 2:
            PX1H = 1;
            PX1 = 0;
            break;
        case 3:
            PX1H = 1;
            PX1 = 1;
            break;
        }
        break;
    case INT4_PRIORITY:
        switch(priority)
        {
        case 0:
            PX4H = 0;
            PX4 = 0;
            break;
        case 1:
            PX4H = 0;
            PX4 = 1;
            break;
        case 2:
            PX4H = 1;
            PX4 = 0;
            break;
        case 3:
            PX4H = 1;
            PX4 = 1;
            break;
        }
        break;
    // 定时器中断优先级分配
    case TIME0_PRIORITY:
        switch(priority)
        {
        case 0:
            PT0H = 0;
            PT0 = 0;
            break;
        case 1:
            PT0H = 0;
            PT0 = 1;
            break;
        case 2:
            PT0H = 1;
            PT0 = 0;
            break;
        case 3:
            PT0H = 1;
            PT0 = 1;
            break;
        }
        break;
    case TIME1_PRIORITY:
        switch(priority)
        {
        case 0:
            PT1H = 0;
            PT1 = 0;
            break;
        case 1:
            PT1H = 0;
            PT1 = 1;
            break;
        case 2:
            PT1H = 1;
            PT1 = 0;
            break;
        case 3:
            PT1H = 1;
            PT1 = 1;
            break;
        }
        break;
    // 串口中断优先级分配
    case UART1_PRIORITY:
        switch(priority)
        {
        case 0:
            PSH = 0;
            PS = 0;
            break;
        case 1:
            PSH = 0;
            PS = 1;
            break;
        case 2:
            PSH = 1;
            PS = 0;
            break;
        case 3:
            PSH = 1;
            PS = 1;
            break;
        }
        break;
    case UART2_PRIORITY:
        switch(priority)
        {
        case 0:
            PS2H = 0;
            PS2 = 0;
            break;
        case 1:
            PS2H = 0;
            PS2 = 1;
            break;
        case 2:
            PS2H = 1;
            PS2 = 0;
            break;
        case 3:
            PS2H = 1;
            PS2 = 1;
            break;
        }
        break;
    case UART3_PRIORITY:
        switch(priority)
        {
        case 0:
            PS3H = 0;
            PS3 = 0;
            break;
        case 1:
            PS3H = 0;
            PS3 = 1;
            break;
        case 2:
            PS3H = 1;
            PS3 = 0;
            break;
        case 3:
            PS3H = 1;
            PS3 = 1;
            break;
        }
        break;
    case UART4_PRIORITY:
        switch(priority)
        {
        case 0:
            PS4H = 0;
            PS4 = 0;
            break;
        case 1:
            PS4H = 0;
            PS4 = 1;
            break;
        case 2:
            PS4H = 1;
            PS4 = 0;
            break;
        case 3:
            PS4H = 1;
            PS4 = 1;
            break;
        }
        break;

    }
}
