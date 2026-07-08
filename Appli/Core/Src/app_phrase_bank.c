/**
 ****************************************************************************************************
 * @file        app_phrase_bank.c
 * @brief       Phrase menu bank for user-defined static signs.
 ****************************************************************************************************
 */

#include "app_phrase_bank.h"

typedef struct {
    const char *text;
    const char *hint;
} app_phrase_bank_entry_t;

static const app_phrase_bank_entry_t phrase_bank[APP_PHRASE_BANK_COUNT] = {
    {"Hello",          "Greeting"},
    {"Thank you",      "Polite response"},
    {"Yes",            "Confirm"},
    {"No",             "Reject"},
    {"Help me",        "Need help"},
    {"Water please",   "Drink request"},
    {"I am hungry",    "Food request"},
    {"Stop",           "Stop action"},
    {"OK",             "Accepted"},
    {"Call family",    "Contact request"},
};

const char *app_phrase_bank_text(uint8_t index)
{
    if (index < APP_PHRASE_BANK_COUNT)
    {
        return phrase_bank[index].text;
    }

    return "---";
}

const char *app_phrase_bank_hint(uint8_t index)
{
    if (index < APP_PHRASE_BANK_COUNT)
    {
        return phrase_bank[index].hint;
    }

    return "";
}

uint8_t app_phrase_bank_next(uint8_t index)
{
    index++;
    if (index >= APP_PHRASE_BANK_COUNT)
    {
        index = 0U;
    }

    return index;
}

uint8_t app_phrase_bank_prev(uint8_t index)
{
    if (index == 0U)
    {
        return APP_PHRASE_BANK_COUNT - 1U;
    }

    return index - 1U;
}
