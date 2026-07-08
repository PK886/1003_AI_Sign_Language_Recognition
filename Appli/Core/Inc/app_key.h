/**
 ****************************************************************************************************
 * @file        app_key.h
 * @brief       Debounced board-key events for sign recording menus.
 ****************************************************************************************************
 */

#ifndef __APP_KEY_H
#define __APP_KEY_H

#include <stdint.h>

#define APP_KEY_EVENT_MODE       0x01U
#define APP_KEY_EVENT_NEXT       0x02U
#define APP_KEY_EVENT_PREV       0x04U
#define APP_KEY_EVENT_OK         0x08U

void app_key_init(void);
uint8_t app_key_update(uint32_t now_ms);

#endif
