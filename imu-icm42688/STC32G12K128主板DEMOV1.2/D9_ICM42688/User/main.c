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
 * @file       main.c
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

/**	！！！！！！！！
* 该开源库单片机主频为软件强制设置内部IRC主频,STC-ISP中所选无效
* 如需修改单片机主频应去dmx_board.h中修改MAIN_FOSC宏定义
！！！！！！！！**/

/** ！！！！！！！！
* ICM42688陀螺仪相关文件在该项目下的 -> DmxDevice -> dmx_icm42688.c和dmx_icm42688.h中
* 该例程为ICM42688陀螺仪数据采集,通信协议采用软件模拟SPI
* !!!!!!注意:如下载此程序,则必须正确连接陀螺仪否则会导致陀螺仪初始化过不去,程序陷入while(1)!!!!!!
！！！！！！！！**/

char buff[200];

void main(void)
{ 
		// 初始化芯片
    init_chip();	
	
	  // 以下可放置用户代码段
	
		// 初始化ICM42688陀螺仪
		// 如想使用陀螺仪原始数据,Init_ICM42688();中注释设置陀螺仪低通滤波器带宽和量程的函数
		//Init_ICM42688();
		
		// 初始化ICM42688陀螺仪IIC协议
		// 如想使用陀螺仪原始数据,Init_ICM42688_IIC();中注释设置陀螺仪低通滤波器带宽和量程的函数
		Init_ICM42688_IIC();
		gyroOffset_init();
	
		// 初始化串口1利用定时器2,RXD引脚为P30,TX引脚为P31,波特率为115200(连接TypeC线即可)
		init_uart(UART1,PIT2,UART1_RX_P30_TX_P31,115200);
		// 发送"UART1 TEST!\n"字符串到上位机 
		send_string_uart(UART1,"UART1 TEST!\n");
	
		init_pit_ms(PIT1,10);
	
    while(1)
    {

//				// 获取ICM42688陀螺仪加速度数据
//				Get_Acc_ICM42688();
//				//Get_Acc_ICM42688_IIC();
//				// 获取ICM42688陀螺仪角加速度数据
//				Get_Gyro_ICM42688();
//				//Get_Gyro_ICM42688_IIC();
			
        // 变量转字符串
//        sprintf(buff,"acc_x:%f\r\nacc_y:%f\r\nacc_z:%f\r\ngyro_x:%f\r\ngyro_y:%f\r\ngyro_z:%f\r\n",icm42688_acc_x,icm42688_acc_y,icm42688_acc_z,icm42688_gyro_x,icm42688_gyro_y,icm42688_gyro_z);
				sprintf(buff,"%f,%f\n",AngleX,AngleY);
        // 串口发送该字符串
        send_string_uart(UART1,buff);
//				// 延时100ms
//				delay_ms(100);
			
    }
}