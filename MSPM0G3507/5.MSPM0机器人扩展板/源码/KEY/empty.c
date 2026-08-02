#include "ti_msp_dl_config.h"
#include "usart.h"
#include "key.h"

int main(void)
{
    USART_Init();
    
    //清除定时器中断标志 Clear timer interrupt flag
    NVIC_ClearPendingIRQ(TIMER_0_INST_INT_IRQN);
    //使能定时器中断   Enable Timer Interrupt
    NVIC_EnableIRQ(TIMER_0_INST_INT_IRQN);
    //定时器开始计时   Timer start
    DL_TimerA_startCounter(TIMER_0_INST);
    
    while (1)
    {
        //按键检测设置在<timer.c>中 
        //Keystroke detection settings in <timer.c>
    }
}
