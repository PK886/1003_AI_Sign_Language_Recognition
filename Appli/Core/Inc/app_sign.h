/**
 ****************************************************************************************************
 * @file        app_sign.h
 * @brief       Basic sign gesture to English text recognition.
 ****************************************************************************************************
 */

#ifndef __APP_SIGN_H
#define __APP_SIGN_H

#include <stdint.h>
#include "ld.h"

#define APP_SIGN_CUSTOM_SLOT_NB      10U
#define APP_SIGN_TEXT_MAX            32U
#define APP_SIGN_RECORD_MIN_SAMPLES  12U
#define APP_SIGN_RECORD_MAX_SAMPLES  32U

typedef enum {
    APP_SIGN_NONE = 0,
    APP_SIGN_HELLO,
    APP_SIGN_YES,
    APP_SIGN_NO,
    APP_SIGN_HELP,
    APP_SIGN_STOP,
    APP_SIGN_WATER,
    APP_SIGN_OK,
    APP_SIGN_THANKS,
} app_sign_type_t;

typedef enum {
    APP_SIGN_RECORD_OK = 0,
    APP_SIGN_RECORD_BUSY,
    APP_SIGN_RECORD_BAD_SLOT,
    APP_SIGN_RECORD_NO_SAMPLE,
    APP_SIGN_RECORD_NOT_READY,
} app_sign_record_status_t;

typedef struct {
    app_sign_type_t current_sign;
    app_sign_type_t emitted_sign;
    uint8_t current_valid;
    uint8_t emitted_valid;
    uint8_t current_is_custom;
    uint8_t emitted_is_custom;
    uint8_t current_slot;
    uint8_t emitted_slot;
    const char *current_text;
    const char *emitted_text;
    uint8_t custom_recording;
    uint32_t custom_sample_count;
} app_sign_result_t;

typedef struct {
    uint8_t initialized;
    uint32_t last_emit_ms;
    uint32_t candidate_start_ms;
    uint16_t candidate_key;
    uint16_t latched_key;
    ld_point_t prev_center;
} app_sign_state_t;

void app_sign_init(app_sign_state_t *state);
void app_sign_reset(app_sign_state_t *state);
app_sign_result_t app_sign_update(app_sign_state_t *state,
                                  const ld_point_t landmarks[LD_LANDMARKS_NB],
                                  uint32_t now_ms);
const char *app_sign_text(app_sign_type_t sign);
const char *app_sign_description(app_sign_type_t sign);

app_sign_record_status_t app_sign_user_record_begin(uint8_t slot, const char *text);
app_sign_record_status_t app_sign_user_record_sample(const ld_point_t landmarks[LD_LANDMARKS_NB]);
app_sign_record_status_t app_sign_user_record_commit(void);
void app_sign_user_record_cancel(void);
uint8_t app_sign_user_record_is_active(void);
uint8_t app_sign_user_record_is_ready(void);
uint8_t app_sign_user_record_slot(void);
uint32_t app_sign_user_record_sample_count(void);
uint32_t app_sign_user_template_count(void);
uint8_t app_sign_user_slot_is_used(uint8_t slot);
const char *app_sign_user_slot_text(uint8_t slot);

#endif
