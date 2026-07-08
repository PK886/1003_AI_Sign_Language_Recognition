/**
 ****************************************************************************************************
 * @file        app_phrase_bank.h
 * @brief       Phrase menu bank for user-defined static signs.
 ****************************************************************************************************
 */

#ifndef __APP_PHRASE_BANK_H
#define __APP_PHRASE_BANK_H

#include <stdint.h>

#define APP_PHRASE_BANK_COUNT      10U

const char *app_phrase_bank_text(uint8_t index);
const char *app_phrase_bank_hint(uint8_t index);
uint8_t app_phrase_bank_next(uint8_t index);
uint8_t app_phrase_bank_prev(uint8_t index);

#endif
