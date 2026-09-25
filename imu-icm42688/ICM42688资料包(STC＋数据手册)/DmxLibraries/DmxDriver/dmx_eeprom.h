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

#ifndef _DMX_EEPROM_H_
#define _DMX_EEPROM_H_

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
void write_eeprom(unsigned long addr,unsigned char *dat,unsigned int num);

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
void read_eeprom(unsigned long addr,unsigned char *dat,unsigned int num);

/**
*
* @brief    删除指定eeprom地址的扇区
* @param    addr				eeprom地址					
* @return   void
* @notes   	dat数组类型必须为unsigned char
* Example:  erase_sector(0x00); // 把指定地址的EEPROM扇区擦除
*
**/
void erase_sector(unsigned long addr);

#endif