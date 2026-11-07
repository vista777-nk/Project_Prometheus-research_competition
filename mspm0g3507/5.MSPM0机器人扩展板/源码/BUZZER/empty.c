#include "ti_msp_dl_config.h"
#include "usart.h"
#include "buzzer.h"

int main(void)
{
    USART_Init();
    
    PWM_Buzzer_Init();
    while (1)
    {
        Buzzer_Toggle();
        delay_ms(1000);
    }
}
