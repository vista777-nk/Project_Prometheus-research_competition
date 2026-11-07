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
 * @file       dmx_ips.h
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

#ifndef _DMX_IPS_H_
#define _DMX_IPS_H_

#include "dmx_font.h"

// 通信方式选择,0:软件SPI,1:硬件SPI
#define IPS_HARD_SPI    1
// 屏幕型号选择,0:IPS114,1:IPS130,2:IPS154,3:IPS200
#define IPS_MODEL       0
// 设置屏幕显示方向,0:竖屏,1:竖屏180度,2:横屏,3:横屏180度
#define IPS_SHOW_DIR    2

// IPS屏幕管脚宏定义
#if IPS_HARD_SPI
// 硬件SPI引脚
#define IPS_PIN 				SPI2_SCK_P25_MOSI_P23_MISO_P24
// 硬件SPI速度
#define IPS_SPPED 			SPI_SPEED4
#else
// IPS,SCL管脚
#define IPS_SCL  				P25
// IPS,SDA管脚
#define IPS_SDA  				P23
// SCL,SDA电平操作
#define IPS_SCL_LEVEL(level)	(IPS_SCL=level)
#define IPS_SDA_LEVEL(level)	(IPS_SDA=level)
#endif
// IPS,RES管脚
#define IPS_RES  				P21
// IPS,DC管脚
#define IPS_DC					P24
// IPS,CS管脚
#define IPS_CS   				P27
// IPS,BLK管脚
#define IPS_BLK  				P54

// 对IPS屏幕管脚进行高低电平操作
#define IPS_RES_LEVEL(level)    (IPS_RES=level)
#define IPS_DC_LEVEL(level)     (IPS_DC=level)
#define IPS_CS_LEVEL(level)     (IPS_CS=level)
#define IPS_BLK_LEVEL(level)    (IPS_BLK=level)

// SPI延时10ms
#define IPS_DELAY_MS(time) 			(delay_ms(time))

/**
*
* @brief    初始化IPS屏幕
* @param    void
* @return   void
* @notes    修改管脚可在dmx_ips.h文件里的宏定义管脚进行修改
* Example:  Init_IPS();
*
**/
void Init_IPS(void);

/**
*
* @brief    IPS屏幕设置起始和结束坐标
* @param    x1                x方向起始点(0~240)
* @param    y1                y方向起始点(0~135)
* @param    x2                x方向结束点(0~240)
* @param    y2                y方向结束点(0~135)
* @return   void
* @notes
* Example:  Set_Pos_IPS(0,0,0,0);
*
**/
void Set_Pos_IPS(unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2);

/**
*
* @brief    IPS屏幕在指定位置画点
* @param    x                   x方向坐标(0~240)
* @param    y                   y方向坐标(0~135)
* @param    color               点的颜色(可在dmx_font.h文件里查看)
* @return   void
* @notes
* Example:  Draw_Point_IPS(0,0,BLACK);
*
**/
void Draw_Point_IPS(unsigned int x, unsigned int y, COLOR_enum color);

/**
*
* @brief    IPS屏幕填充指定区域
* @param    x1                  x方向起始点(0~240)
* @param    y1                  y方向起始点(0~135)
* @param    x2                  x方向结束点(0~240)
* @param    y2                  y方向结束点(0~135)
* @param    color               填充颜色(可在dmx_font.h文件里查看)
* @return   void
* @notes
* Example:  Fill_IPS(0,0,240,135,WHITE);    // IPS114屏幕全白
*
**/
void Fill_IPS(unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2, COLOR_enum color);

/**
*
* @brief    IPS屏幕清屏
* @param    color               填充颜色(可在dmx_font.h文件里查看)
* @return   void
* @notes
* Example:  Clean_IPS(WHITE);   // IPS114屏幕全白
*
**/
void Clean_IPS(COLOR_enum color);

/**
*
* @brief    IPS屏幕显示单个字符
* @param    x                   x方向坐标(0~240)
* @param    y                   y方向坐标(0~135)
* @param    data                字符
* @param    fc                  字体颜色(可在dmx_font.h文件里查看)
* @param    bc                  背景颜色(可在dmx_font.h文件里查看)
* @param    fontsize            字体尺寸(可在dmx_font.h文件里查看)
* @return   void
* @notes
* Example:  Show_Char_IPS(0,0,'D',BLACK,WHITE,Show8x16);    // 在左上角显示白色背景黑色的8X16字符'D'
*
**/
void Show_Char_IPS(unsigned int x, unsigned int y, unsigned char dat, COLOR_enum fc, COLOR_enum bc, SHOW_size_enum fontsize);

/**
*
* @brief    IPS屏幕显示字符串
* @param    x                   x方向坐标(0~240)
* @param    y                   y方向坐标(0~135)
* @param    *p                  要显示的字符串
* @param    fc                  字体颜色(可在dmx_font.h文件里查看)
* @param    bc                  背景颜色(可在dmx_font.h文件里查看)
* @param    fontsize            字体尺寸(可在dmx_font.h文件里查看)
* @return   void
* @notes
* Example:  Show_String_IPS(0,0,"DMX",BLACK,WHITE,Show8x16);    // 在左上角显示白色背景黑色的8X16字符串'DMX'
*
**/
void Show_String_IPS(unsigned int x, unsigned int y, char *p, COLOR_enum fc, COLOR_enum bc, SHOW_size_enum fontsize);

/**
*
* @brief    IPS屏幕显示Int型变量
* @param    x                   x方向坐标(0~240)
* @param    y                   y方向坐标(0~135)
* @param    data                要显示的Int型变量
* @param    fc                  字体颜色(可在dmx_font.h文件里查看)
* @param    bc                  背景颜色(可在dmx_font.h文件里查看)
* @param    fontsize            字体尺寸(可在dmx_font.h文件里查看)
* @return   void
* @notes
* Example:  Show_Int_IPS(0,0,521,BLACK,WHITE,Show8x16);   // 在左上角显示白色背景黑色的8X16数字“521”
*
**/
void Show_Int_IPS (unsigned short x, unsigned short y, const int dat,COLOR_enum fc, COLOR_enum bc, SHOW_size_enum fontsize);

/**
*
* @brief    IPS屏幕显示Float型变量
* @param    x                   x方向坐标(0~240)
* @param    y                   y方向坐标(0~135)
* @param    dat                 要显示的Float型变量
* @param    pointnum            要显示的Float型变量小数点位数
* @param    fc                  字体颜色(可在dmx_font.h文件里查看)
* @param    bc                  背景颜色(可在dmx_font.h文件里查看)
* @param    fontsize            字体尺寸(可在dmx_font.h文件里查看)
* @return   void
* @notes		float类型的精度为小数点后6位
* Example:  Show_Float_IPS(0,0,521.22,2,BLACK,WHITE,Show8x16);    	// 在左上角显示白色背景黑色的8X16数字“521.22”
*
**/
void Show_Float_IPS (unsigned short x, unsigned short y, const float dat,unsigned char pointnum, COLOR_enum fc, COLOR_enum bc, SHOW_size_enum fontsize);

/**
*
* @brief    IPS屏幕显示汉字
* @param    x                   x方向坐标(0~240)
* @param    y                   y方向坐标(0~135)
* @param    data                要显示的汉字所在数组
* @param    startnumber         第startnumber个汉字开始
* @param    number              第number个汉字结束
* @param    fc                  字体颜色(可在dmx_font.h文件里查看)
* @param    bc                  背景颜色(可在dmx_font.h文件里查看)
* @param    size                16
* @return   void
* @notes    使用PCtoLCD2002软件取模,阴码,逐行式,顺向,16*16
* Example:  Show_Chinese16x16_IPS(0,0,Chinese16x16[0],0,3,BLACK,WHITE,16);   // 在左上角显示IPSChinese16x16数组中的第0~3个汉字即“呆萌侠”
*
**/
void Show_Chinese16x16_IPS(unsigned int x, unsigned int y, const unsigned char *dat, unsigned char startnumber, unsigned char number, COLOR_enum fc, COLOR_enum bc, unsigned char fsize);

/**
*
* @brief    IPS屏幕显示彩色图片
* @param    x                   x方向坐标(0~240)
* @param    y                   y方向坐标(0~135)
* @param    width               图片宽度
* @param    heigh               图片高度
* @param    pic                 图片数组
* @return   void
* @notes
* Example:  Show_Picture_IPS(60,7,121,121,Earth121x121); // 显示地球图片
*
**/
void Show_Picture_IPS(unsigned int x, unsigned int y, unsigned int width, unsigned int heigh, const unsigned char pic[]);

/**
*
* @brief    IPS屏幕显示摄像头灰度图像
* @param    x                   x方向坐标(0~240)
* @param    y                   y方向坐标(0~135)
* @param    image               图像数组
* @param    width               图像数组实际宽度
* @param    heigh               图像数组实际高度
* @param    show_width          图像数组显示宽度
* @param    show_height         图像数组显示高度
* @param    threshold           为0时关闭二值化
* @return   void
* @notes
* Example:  Show_Image_IPS(0,0,OriginalImageData[0],188,120,188,135,0);
*
**/
void Show_Image_IPS(unsigned short x, unsigned short y, unsigned char *image, unsigned short width, unsigned short height, unsigned short show_width, unsigned short show_height, unsigned char threshold);

#endif
