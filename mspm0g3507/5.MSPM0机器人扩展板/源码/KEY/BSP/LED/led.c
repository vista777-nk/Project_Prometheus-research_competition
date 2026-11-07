#include "led.h"

void LED_Toggle(void)
{
    DL_GPIO_togglePins(LED_PORT, LED_D1_PIN);
}

void LED_ON(void)
{
    DL_GPIO_setPins(LED_PORT,LED_D1_PIN);
}

void LED_OFF(void)
{
    DL_GPIO_clearPins(LED_PORT,LED_D1_PIN);
}

