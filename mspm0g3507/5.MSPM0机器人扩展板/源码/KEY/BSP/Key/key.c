#include "key.h"

// 定义按键句柄 - Define key handle
Key_t key1 = {
    .GPIOx = KEY_PORT,
    .GPIO_Pin = KEY_K1_PIN,
    .state = KEY_STATE_RELEASED,
    .pressTime = 0,
    .debounceTime = 0
};


/* 消抖时间（单位：ms） - Debounce time (ms) */
#define DEBOUNCE_DELAY 20  // 典型值：10-50ms / Typical value: 10-50ms

/**
 * @brief 按键扫描函数（非阻塞式） - Key scan function (non-blocking)
 * @param key         按键句柄指针 / Pointer to KeyHandle
 * @param currentTime 当前系统时间（单位：ms） / Current system time (ms)
 * @param longPressThreshold 长按时间阈值（单位：ms） / Long press threshold (ms)
 * @return KeyEvent   返回按键事件 / Returns key event
 */
KeyEvent Key_Scan(Key_t* key, uint32_t currentTime, uint32_t longPressThreshold) {
    // 读取按键电平：0表示按下，1表示释放 / Read pin level: 0=pressed, 1=released
    uint8_t isPressed = 0;
    
    if(DL_GPIO_readPins(key->GPIOx, key->GPIO_Pin) == 0)
    {
        isPressed = 1;
    }
    
    switch (key->state) {
        /* 状态1：按键释放 - State 1: Key released */
        case KEY_STATE_RELEASED:
            if (isPressed) {
                key->state = KEY_STATE_DEBOUNCE;       // 进入消抖状态 / Enter debounce state
                key->debounceTime = currentTime;      // 记录消抖开始时间 / Record debounce start time
            }
            break;

        /* 状态2：消抖检测 - State 2: Debounce checking */
        case KEY_STATE_DEBOUNCE:
            // 消抖时间到达后检测稳定状态 / Check stable state after debounce delay
            if (currentTime - key->debounceTime >= DEBOUNCE_DELAY) {
                if (isPressed) {
                    key->state = KEY_STATE_PRESSED;    // 确认按下 / Confirm press
                    key->pressTime = currentTime;      // 记录按下时间 / Record press time
                } else {
                    key->state = KEY_STATE_RELEASED;   // 抖动误触发 / False trigger due to bounce
                }
            }
            break;

        /* 状态3：已按下 - State 3: Pressed */
        case KEY_STATE_PRESSED:
            if (!isPressed) {
                key->state = KEY_STATE_RELEASED;       // 释放触发短按 / Release triggers short press
                return KEY_EVENT_SHORT;                // 返回短按事件 / Return short press event
            } else if (currentTime - key->pressTime >= longPressThreshold) {
                key->state = KEY_STATE_LONG;           // 触发长按 / Trigger long press
                return KEY_EVENT_LONG;                 // 返回长按事件 / Return long press event
            }
            break;

        /* 状态4：长按已触发 - State 4: Long press triggered */
        case KEY_STATE_LONG:
            if (!isPressed) {
                key->state = KEY_STATE_RELEASED;       // 释放后重置状态 / Reset state after release
            }
            break;
    }

    return KEY_EVENT_NONE;  // 默认无事件 / Default: no event
}

void Key_Handle(void)
{
    int16_t LongPressThreshold = 700;// 按键扫描时长按的阈值：500ms    Threshold for long press during key scanning: 500ms
    static uint32_t lastTick = 0;  // 初始时间  initial time
    uint32_t currentTick = Get_Time();
    static int task_flag = 1;

    // 每10ms检测一次按键 - Check key every 10ms
    if (currentTick - lastTick >= 10) {
        lastTick = currentTick;

        KeyEvent event = Key_Scan(&key1, currentTick, LongPressThreshold);

        switch (event) {
            case KEY_EVENT_SHORT:
                // 处理短按 Handle short press
//                printf("short press\r\n");
                LED_Toggle();
                break;
            case KEY_EVENT_LONG:
                // 处理长按 Handle long press
                
                break;
            default:
                break;
        }
    }
}

