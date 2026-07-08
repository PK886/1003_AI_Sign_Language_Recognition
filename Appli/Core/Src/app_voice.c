/**
 ****************************************************************************************************
 * @file        app_voice.c
 * @brief       USB UART text output module interface.
 ****************************************************************************************************
 */

#include "app_voice.h"
#include "main.h"
#include <string.h>

#define APP_VOICE_UART_TIMEOUT_MS  50U

extern UART_HandleTypeDef huart1;

static uint8_t voice_initialized;

static app_voice_status_t app_voice_uart_write(const char *text)
{
    size_t len;

    if ((voice_initialized == 0U) || (text == NULL))
    {
        return APP_VOICE_NOT_READY;
    }

    len = strlen(text);
    if (len == 0U)
    {
        return APP_VOICE_OK;
    }

    if (HAL_UART_Transmit(&huart1,
                          (uint8_t *)text,
                          (uint16_t)len,
                          APP_VOICE_UART_TIMEOUT_MS) != HAL_OK)
    {
        return APP_VOICE_NOT_READY;
    }

    return APP_VOICE_OK;
}

void app_voice_init(void)
{
    voice_initialized = 1U;
}

uint8_t app_voice_is_ready(void)
{
    return voice_initialized;
}

app_voice_status_t app_voice_say_text(const char *text)
{
    if (text == NULL)
    {
        return APP_VOICE_NOT_READY;
    }

    if (app_voice_uart_write("SIGN: ") != APP_VOICE_OK)
    {
        return APP_VOICE_NOT_READY;
    }

    if (app_voice_uart_write(text) != APP_VOICE_OK)
    {
        return APP_VOICE_NOT_READY;
    }

    return app_voice_uart_write("\r\n");
}
