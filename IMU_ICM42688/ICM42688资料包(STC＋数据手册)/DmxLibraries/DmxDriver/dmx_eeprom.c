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
 * @file       dmx_eeprom.h
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

#include "dmx_delay.h"
#include "dmx_board.h"
#include "dmx_eeprom.h"
#include "intrins.h"

/**
*
* @brief    eeprom开启访问
* @param    void					
* @return   void
* @notes    dmx_eeprom.c文件内部调用
* Example:  enable_eeprom();
*
**/
static void enable_eeprom(void)
{
		IAPEN = 1; 
		IAP_TPS = MAIN_FOSC / 1000000;
}

/**
*
* @brief    eeprom禁止访问
* @param    void					
* @return   void
* @notes    dmx_eeprom.c文件内部调用
* Example:  disable_eeprom();
*
**/
static void disable_eeprom(void)
{
		IAP_CONTR = 0;			
		IAP_CMD   = 0;			
		IAP_TRIG  = 0;			
		IAP_ADDRE = 0xff;   
		IAP_ADDRH = 0xff;   
		IAP_ADDRL = 0xff;
}

/**
*
* @brief    触发eeprom操作.
* @param    void					
* @return   void
* @notes    dmx_eeprom.c文件内部调用
* Example:  trig_eeprom();
*
**/
static void trig_eeprom(void)
{
		F0 = EA;    
		EA = 0;     
		IAP_TRIG = 0x5A;
		IAP_TRIG = 0xA5;                    												
		_nop_();   
		_nop_();
		_nop_();
		_nop_();
		EA = F0;
}

/**
*
* @brief    从指定eeprom首地址连续写入n个字节
* @param    addr				eeprom地址					
* @param    dat					写入数据的数组的首地址				
* @param    num					写入字节个数		
* @return   void
* @notes   	dat数组类型必须为unsigned char
* Example:  write_eeprom(0x00,write_data,1); // 把write_data的1个数据写入eeprom中的0x00地址里面
*
**/
void write_eeprom(unsigned long addr,unsigned char *dat,unsigned int num)
{
		close_icache();
		enable_eeprom();                       
		IAP_CMD = 2;                        
		while(num--)
		{
			IAP_ADDRE = (unsigned char)(addr >> 16); 
			IAP_ADDRH = (unsigned char)(addr >> 8);  
			IAP_ADDRL = (unsigned char)addr;        
			IAP_DATA  = *dat++;       
			trig_eeprom(); 
			addr++;                 
		}
		disable_eeprom();    
		open_icache();
}

/**
*
* @brief    从指定eeprom首地址连续读出n个字节
* @param    addr				eeprom地址					
* @param    dat					存放数据的数组的首地址				
* @param    num					读取字节个数		
* @return   void
* @notes   	dat数组类型必须为unsigned char
* Example:  read_eeprom(0x00,read_data,1); // 从eeprom中的0x00地址连续读出1个数据存放在read_data里面
*
**/
void read_eeprom(unsigned long addr,unsigned char *dat,unsigned int num)
{
		close_icache();
		enable_eeprom();                           
		IAP_CMD = 1;                           
		while(num--)
		{
			IAP_ADDRE = (unsigned char)(addr >> 16); 
			IAP_ADDRH = (unsigned char)(addr >> 8);  
			IAP_ADDRL = (unsigned char)addr;         
			trig_eeprom();                      
			*dat++ = IAP_DATA;            
			addr++;
		}
		disable_eeprom();    
		open_icache();
}

/**
*
* @brief    删除指定eeprom地址的扇区
* @param    addr				eeprom地址					
* @return   void
* @notes   	dat数组类型必须为unsigned char
* Example:  erase_sector(0x00); // 把指定地址的EEPROM扇区擦除
*
**/
void erase_sector(unsigned long addr)
{
		close_icache();
		enable_eeprom();                      
		IAP_CMD = 3;                    
		IAP_ADDRE = (unsigned char)(addr >> 16);
		IAP_ADDRH = (unsigned char)(addr >> 8);
		IAP_ADDRL = (unsigned char)addr;         
		trig_eeprom();                     
		disable_eeprom();      
		open_icache();
}
