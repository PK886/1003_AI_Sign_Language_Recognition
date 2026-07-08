/**
 ****************************************************************************************************
 * @file        app_voice.h
 * @brief       Reserved voice output module interface.
 ****************************************************************************************************
 */

#ifndef __APP_VOICE_H
#define __APP_VOICE_H

#include <stdint.h>

typedef enum {
    APP_VOICE_OK = 0,
    APP_VOICE_NOT_READY,
} app_voice_status_t;

void app_voice_init(void);
uint8_t app_voice_is_ready(void);
app_voice_status_t app_voice_say_text(const char *text);

#endif
