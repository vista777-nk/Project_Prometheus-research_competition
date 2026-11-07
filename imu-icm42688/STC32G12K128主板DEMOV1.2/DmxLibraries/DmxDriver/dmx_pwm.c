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
 * @file       dmx_pwm.c
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

#include "dmx_board.h"
#include "dmx_gpio.h"
#include "dmx_pit.h"
#include "dmx_pwm.h"

/**
*
* @brief    pwm初始化
* @param    pin     		选择需要初始化pwm引脚(dmx_pwm.h文件里已枚举定义)
* @param    freq     		初始化pwm引脚的频率
* @param    duty     		初始化pwm引脚的初始占空比
* @return   void
* @notes    调用此函数前可查看dmx_pwm.h文件里枚举的可用引脚
* Example:  init_pwm(PWMA1_P_P20,100,1000);	// 初始化PWMA1_P_P20脚,频率为100HZ,占空比为1000,即占空比为10%
*
**/
void init_pwm(PWM_pin_enum pin, unsigned int freq, unsigned int duty)
{
    unsigned long match_temp;
    unsigned int freq_div = 0;
    unsigned long period_temp;
    switch(pin)
    {
    case PWMA1_P_P10:
        init_gpio(GPIO_P10, OUT_PP, 0);
        break;
    case PWMA1_N_P11:
        init_gpio(GPIO_P11, OUT_PP, 0);
        break;
    case PWMA1_P_P20:
        init_gpio(GPIO_P20, OUT_PP, 0);
        break;
    case PWMA1_N_P21:
        init_gpio(GPIO_P21, OUT_PP, 0);
        break;
    case PWMA1_P_P60:
        init_gpio(GPIO_P60, OUT_PP, 0);
        break;
    case PWMA1_N_P61:
        init_gpio(GPIO_P61, OUT_PP, 0);
        break;
    case PWMA2_P_P54:
        init_gpio(GPIO_P54, OUT_PP, 0);
        break;
    case PWMA2_N_P13:
        init_gpio(GPIO_P13, OUT_PP, 0);
        break;
    case PWMA2_P_P22:
        init_gpio(GPIO_P22, OUT_PP, 0);
        break;
    case PWMA2_N_P23:
        init_gpio(GPIO_P23, OUT_PP, 0);
        break;
    case PWMA2_P_P62:
        init_gpio(GPIO_P62, OUT_PP, 0);
        break;
    case PWMA2_N_P63:
        init_gpio(GPIO_P63, OUT_PP, 0);
        break;
    case PWMA3_P_P14:
        init_gpio(GPIO_P14, OUT_PP, 0);
        break;
    case PWMA3_N_P15:
        init_gpio(GPIO_P15, OUT_PP, 0);
        break;
    case PWMA3_P_P24:
        init_gpio(GPIO_P24, OUT_PP, 0);
        break;
    case PWMA3_N_P25:
        init_gpio(GPIO_P25, OUT_PP, 0);
        break;
    case PWMA3_P_P64:
        init_gpio(GPIO_P64, OUT_PP, 0);
        break;
    case PWMA3_N_P65:
        init_gpio(GPIO_P65, OUT_PP, 0);
        break;
    case PWMA4_P_P16:
        init_gpio(GPIO_P16, OUT_PP, 0);
        break;
    case PWMA4_N_P17:
        init_gpio(GPIO_P17, OUT_PP, 0);
        break;
    case PWMA4_P_P26:
        init_gpio(GPIO_P26, OUT_PP, 0);
        break;
    case PWMA4_N_P27:
        init_gpio(GPIO_P27, OUT_PP, 0);
        break;
    case PWMA4_P_P66:
        init_gpio(GPIO_P66, OUT_PP, 0);
        break;
    case PWMA4_N_P67:
        init_gpio(GPIO_P67, OUT_PP, 0);
        break;
    case PWMA4_P_P34:
        init_gpio(GPIO_P34, OUT_PP, 0);
        break;
    case PWMA4_N_P33:
        init_gpio(GPIO_P33, OUT_PP, 0);
        break;
    case PWMB1_P20:
        init_gpio(GPIO_P20, OUT_PP, 0);
        break;
    case PWMB1_P17:
        init_gpio(GPIO_P17, OUT_PP, 0);
        break;
    case PWMB1_P00:
        init_gpio(GPIO_P00, OUT_PP, 0);
        break;
    case PWMB1_P74:
        init_gpio(GPIO_P74, OUT_PP, 0);
        break;
    case PWMB2_P21:
        init_gpio(GPIO_P21, OUT_PP, 0);
        break;
    case PWMB2_P54:
        init_gpio(GPIO_P54, OUT_PP, 0);
        break;
    case PWMB2_P01:
        init_gpio(GPIO_P01, OUT_PP, 0);
        break;
    case PWMB2_P75:
        init_gpio(GPIO_P75, OUT_PP, 0);
        break;
    case PWMB3_P22:
        init_gpio(GPIO_P22, OUT_PP, 0);
        break;
    case PWMB3_P33:
        init_gpio(GPIO_P33, OUT_PP, 0);
        break;
    case PWMB3_P02:
        init_gpio(GPIO_P02, OUT_PP, 0);
        break;
    case PWMB3_P76:
        init_gpio(GPIO_P76, OUT_PP, 0);
        break;
    case PWMB4_P23:
        init_gpio(GPIO_P23, OUT_PP, 0);
        break;
    case PWMB4_P34:
        init_gpio(GPIO_P34, OUT_PP, 0);
        break;
    case PWMB4_P03:
        init_gpio(GPIO_P03, OUT_PP, 0);
        break;
    case PWMB4_P77:
        init_gpio(GPIO_P77, OUT_PP, 0);
        break;
    }

		freq_div = (MAIN_FOSC / freq) >> 16;
    period_temp = (MAIN_FOSC / freq) / (freq_div + 1);
    if(duty >= PWM_DUTY_MAX)
        match_temp = period_temp;
    else
        match_temp = period_temp * ((float)duty / PWM_DUTY_MAX);

    if(pin <= 25)
    {
        switch(pin / 6 + 1)
        {
        case 1:
            PWMA_CCMR1 = 0x60;
            PWMA_CCER1 |= (0x01 << ((pin % 2) * 2));
            PWMA_CCR1H = match_temp >> 8;
            PWMA_CCR1L = match_temp;
            PWMA_ENO |= 0x01 << (pin % 2);
            PWMA_PS |= (pin / 2) << 0;
            break;
        case 2:
            PWMA_CCMR2 = 0x60;
            PWMA_CCER1 |= (0x10 << ((pin % 2) * 2));
            PWMA_CCR2H = match_temp >> 8;
            PWMA_CCR2L = match_temp;
            PWMA_ENO |= 0x04 << (pin % 2);
            PWMA_PS |= (pin / 2 - 3) << 2;
            break;
        case 3:
            PWMA_CCMR3 = 0x60;
            PWMA_CCER2 |= (0x01 << ((pin % 2) * 2));
            PWMA_CCR3H = match_temp >> 8;
            PWMA_CCR3L = match_temp;
            PWMA_ENO |= 0x10 << (pin % 2);
            PWMA_PS |= (pin / 2 - 6) << 4;
            break;
        case 4:
            PWMA_CCMR4 = 0x60;
            PWMA_CCER2 |= (0x10 << ((pin % 2) * 2));
            PWMA_CCR4H = match_temp >> 8;
            PWMA_CCR4L = match_temp;
            PWMA_ENO |= 0x40 << (pin % 2);
            PWMA_PS |= (pin / 2 - 9) << 6;
            break;
        case 5:
            PWMA_CCMR4 = 0x60;
            PWMA_CCER2 |= (0x10 << ((pin % 2) * 2));
            PWMA_CCR4H = match_temp >> 8;
            PWMA_CCR4L = match_temp;
            PWMA_ENO |= 0x40 << (pin % 2);
            PWMA_PS |= ((0x03) << 6);
            break;
        }

        PWMA_PSCRH = freq_div >> 8;
        PWMA_PSCRL = freq_div;
        PWMA_ARRH = period_temp >> 8;
        PWMA_ARRL = period_temp;
        PWMA_BKR = 0x80;
        PWMA_CR1 = 0x01;
    }
    else
    {
				pin=(pin - 26);
        switch( pin/ 4 + 1)
        {
        case 1:
            PWMB_CCMR1 = 0x60;
            PWMB_CCER1 |= 0x01;
            PWMB_CCR5H = match_temp >> 8;
            PWMB_CCR5L = match_temp;
            PWMB_ENO |= 0x01;
            PWMB_PS |= (pin % 4) << 0;
            break;
        case 2:
            PWMB_CCMR2 = 0x60;
            PWMB_CCER1 |= 0x10;
            PWMB_CCR6H = match_temp >> 8;
            PWMB_CCR6L = match_temp;
            PWMB_ENO |= 0x04;
            PWMB_PS |= (pin % 4) << 2;
            break;
        case 3:
            PWMB_CCMR3 = 0x60;
            PWMB_CCER2 |= 0x01;
            PWMB_CCR7H = match_temp >> 8;
            PWMB_CCR7L = match_temp;
            PWMB_ENO |= 0x10;
            PWMB_PS |= (pin % 4) << 4;
            break;
        case 4:
            PWMB_CCMR4 = 0x60;
            PWMB_CCER2 |= 0x10;
            PWMB_CCR8H = match_temp >> 8;
            PWMB_CCR8L = match_temp;
            PWMB_ENO |= 0x40;
            PWMB_PS |= (pin % 4) << 6;
            break;
        }
        PWMB_PSCRH = freq_div >> 8;
        PWMB_PSCRL = freq_div;
        PWMB_ARRH = period_temp >> 8;
        PWMB_ARRL = period_temp;
        PWMB_BKR = 0x80;
        PWMB_CR1 = 0x01;
    }
}

/**
*
* @brief    pwm发送占空比
* @param    pin     		设置需要发送占空比的pwm引脚(dmx_pwm.h文件里已枚举定义)
* @param    duty     		设置占空比
* @return   void
* @notes    调用此函数前pwm引脚需初始化
* Example:  set_duty_pwm(PWMA1_P_P20,1600);	// 发送PWMA1_P_P20脚,占空比为1600,即占空比为16%
*
**/
void set_duty_pwm(PWM_pin_enum pin, unsigned int duty)
{
    unsigned long match_temp;
    if(pin <= 25)
    {
        if(duty >= PWM_DUTY_MAX)
            match_temp = (((PWMA_ARRH) << 8) | PWMA_ARRL + 1);
        else
            match_temp = (((PWMA_ARRH) << 8) | PWMA_ARRL + 1) * ((float)duty / PWM_DUTY_MAX);
        switch(pin / 6)
        {
        case 0:
            PWMA_CCR1H = match_temp >> 8;
            PWMA_CCR1L = match_temp;
            break;
        case 1:
            PWMA_CCR2H = match_temp >> 8;
            PWMA_CCR2L = match_temp;
            break;
        case 2:
            PWMA_CCR3H = match_temp >> 8;
            PWMA_CCR3L = match_temp;
            break;
        case 3:
        case 4:
            PWMA_CCR4H = match_temp >> 8;
            PWMA_CCR4L = match_temp;
            break;
        }
    }
    else
    {
        if(duty >= PWM_DUTY_MAX)
            match_temp = (((PWMB_ARRH) << 8) | PWMB_ARRL + 1);
        else
            match_temp = (((PWMB_ARRH) << 8) | PWMB_ARRL + 1) * ((float)duty / PWM_DUTY_MAX);
        switch((pin - 26) / 4)
        {
        case 0:
            PWMB_CCR5H = match_temp >> 8;
            PWMB_CCR5L = match_temp;
            break;
        case 1:
            PWMB_CCR6H = match_temp >> 8;
            PWMB_CCR6L = match_temp;
            break;
        case 2:
            PWMB_CCR7H = match_temp >> 8;
            PWMB_CCR7L = match_temp;
            break;
        case 3:
            PWMB_CCR8H = match_temp >> 8;
            PWMB_CCR8L = match_temp;
            break;
        }
    }
}