/**
 ****************************************************************************************************
 * @file        app_key.c
 * @brief       Debounced board-key events for sign recording menus.
 ****************************************************************************************************
 */

#include "app_key.h"
#include "stm32n6xx_hal.h"

#define APP_KEY_COUNT             4U
#define APP_KEY_DEBOUNCE_MS       35U

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    GPIO_PinState active_state;
    uint8_t event;
    GPIO_PinState stable_state;
    GPIO_PinState last_raw_state;
    uint32_t last_change_ms;
} app_key_item_t;

static app_key_item_t keys[APP_KEY_COUNT] = {
    {GPIOC, GPIO_PIN_6,  GPIO_PIN_RESET, APP_KEY_EVENT_MODE, GPIO_PIN_SET,   GPIO_PIN_SET,   0U},
    {GPIOD, GPIO_PIN_1,  GPIO_PIN_RESET, APP_KEY_EVENT_NEXT, GPIO_PIN_SET,   GPIO_PIN_SET,   0U},
    {GPIOG, GPIO_PIN_11, GPIO_PIN_RESET, APP_KEY_EVENT_PREV, GPIO_PIN_SET,   GPIO_PIN_SET,   0U},
    {GPIOC, GPIO_PIN_13, GPIO_PIN_SET,   APP_KEY_EVENT_OK,   GPIO_PIN_RESET, GPIO_PIN_RESET, 0U},
};

void app_key_init(void)
{
    uint8_t i;

    for (i = 0U; i < APP_KEY_COUNT; i++)
    {
        keys[i].stable_state = HAL_GPIO_ReadPin(keys[i].port, keys[i].pin);
        keys[i].last_raw_state = keys[i].stable_state;
        keys[i].last_change_ms = HAL_GetTick();
    }
}

uint8_t app_key_update(uint32_t now_ms)
{
    uint8_t events = 0U;
    uint8_t i;

    for (i = 0U; i < APP_KEY_COUNT; i++)
    {
        GPIO_PinState raw_state = HAL_GPIO_ReadPin(keys[i].port, keys[i].pin);

        if (raw_state != keys[i].last_raw_state)
        {
            keys[i].last_raw_state = raw_state;
            keys[i].last_change_ms = now_ms;
        }

        if (((now_ms - keys[i].last_change_ms) >= APP_KEY_DEBOUNCE_MS) &&
            (raw_state != keys[i].stable_state))
        {
            keys[i].stable_state = raw_state;
            if (raw_state == keys[i].active_state)
            {
                events |= keys[i].event;
            }
        }
    }

    return events;
}
