#ifndef	__BUZZER_H__
#define __BUZZER_H__

#include "ti_msp_dl_config.h"
#include "delay.h"

extern volatile uint32_t bee_time;           //the sound_time of beep(the unit is ms)


void PWM_Buzzer_Init(void);
void Buzzer_ON(void);
void Buzzer_OFF(void);
void Buzzer_Toggle(void);

#endif
