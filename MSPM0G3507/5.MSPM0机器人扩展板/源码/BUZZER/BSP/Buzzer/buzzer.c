#include "buzzer.h"

void PWM_Buzzer_Init(void) {
    DL_Timer_startCounter(BUZZER_INST);
}

void Buzzer_Toggle(void)
{
    static int i=0;
    if(i==0)
    {
        Buzzer_ON();
        i=1;
    }
    else
    {
        Buzzer_OFF();
        i=0;
    }
}

void Buzzer_ON(void)
{
    DL_TimerA_setCaptureCompareValue(BUZZER_INST, 30, GPIO_BUZZER_C3_IDX);
}

void Buzzer_OFF(void)
{
    DL_TimerA_setCaptureCompareValue(BUZZER_INST, 0, GPIO_BUZZER_C3_IDX);
}


