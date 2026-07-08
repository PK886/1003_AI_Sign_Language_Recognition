/**
 ****************************************************************************************************
 * @file        app_voice.c
 * @brief       Reserved voice output module interface.
 ****************************************************************************************************
 */

#include "app_voice.h"

void app_voice_init(void)
{
}

uint8_t app_voice_is_ready(void)
{
    return 0U;
}

app_voice_status_t app_voice_say_text(const char *text)
{
    (void)text;

    return APP_VOICE_NOT_READY;
}
