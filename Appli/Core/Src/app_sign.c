/**
 ****************************************************************************************************
 * @file        app_sign.c
 * @brief       Basic sign gesture to English text recognition.
 ****************************************************************************************************
 */

#include "app_sign.h"
#include <math.h>
#include <string.h>

#define SIGN_MIN_PALM_SCALE          20.0f
#define SIGN_HOLD_MS                 500U
#define SIGN_COOLDOWN_MS             350U
#define SIGN_MOTION_RATIO            0.35f
#define SIGN_FINGER_EXT_RATIO        1.08f
#define SIGN_THUMB_EXT_RATIO         1.10f
#define SIGN_PINCH_RATIO             0.42f
#define SIGN_SPREAD_RATIO            0.25f
#define SIGN_TOGETHER_RATIO          0.15f
#define SIGN_THUMB_DIR_RATIO         1.25f

#define SIGN_FINGER_THUMB            0x01U
#define SIGN_FINGER_INDEX            0x02U
#define SIGN_FINGER_MIDDLE           0x04U
#define SIGN_FINGER_RING             0x08U
#define SIGN_FINGER_PINKY            0x10U

typedef struct {
    const char *text;
    const char *description;
} app_sign_entry_t;

typedef struct {
    uint8_t active;
    uint8_t slot;
    char text[APP_SIGN_TEXT_MAX];
    uint32_t sample_count;
} app_sign_record_context_t;

static const app_sign_entry_t sign_table[] = {
    [APP_SIGN_NONE]   = {"---",       "No stable sign"},
    [APP_SIGN_HELLO]  = {"Hello",     "Five fingers open and spread"},
    [APP_SIGN_YES]    = {"Yes",       "Index finger up"},
    [APP_SIGN_NO]     = {"No",        "Thumb down"},
    [APP_SIGN_HELP]   = {"Help",      "Closed fist"},
    [APP_SIGN_STOP]   = {"Stop",      "Open palm with fingers together"},
    [APP_SIGN_WATER]  = {"Water",     "Index, middle and ring fingers up"},
    [APP_SIGN_OK]     = {"OK",        "Thumb and index pinch with other fingers open"},
    [APP_SIGN_THANKS] = {"Thanks",    "Thumb up"},
};

static app_sign_record_context_t record_context;

static float sign_distance(ld_point_t a, ld_point_t b)
{
    float dx = a.x - b.x;
    float dy = a.y - b.y;

    return sqrtf(dx * dx + dy * dy);
}

static float sign_palm_scale(const ld_point_t landmarks[LD_LANDMARKS_NB])
{
    float scale = sign_distance(landmarks[0], landmarks[9]);

    if (scale < SIGN_MIN_PALM_SCALE)
    {
        scale = SIGN_MIN_PALM_SCALE;
    }

    return scale;
}

static ld_point_t sign_center(const ld_point_t landmarks[LD_LANDMARKS_NB])
{
    ld_point_t center;

    center.x = (landmarks[0].x + landmarks[9].x) * 0.5f;
    center.y = (landmarks[0].y + landmarks[9].y) * 0.5f;

    return center;
}

static uint8_t sign_finger_extended(const ld_point_t landmarks[LD_LANDMARKS_NB],
                                    uint8_t tip,
                                    uint8_t pip)
{
    float tip_distance = sign_distance(landmarks[0], landmarks[tip]);
    float pip_distance = sign_distance(landmarks[0], landmarks[pip]);

    return (uint8_t)(tip_distance > (pip_distance * SIGN_FINGER_EXT_RATIO));
}

static uint8_t sign_thumb_extended(const ld_point_t landmarks[LD_LANDMARKS_NB])
{
    float tip_distance = sign_distance(landmarks[0], landmarks[4]);
    float mcp_distance = sign_distance(landmarks[0], landmarks[2]);

    return (uint8_t)(tip_distance > (mcp_distance * SIGN_THUMB_EXT_RATIO));
}

static uint8_t sign_finger_mask(const ld_point_t landmarks[LD_LANDMARKS_NB])
{
    uint8_t mask = 0U;

    if (sign_thumb_extended(landmarks) != 0U)
    {
        mask |= SIGN_FINGER_THUMB;
    }
    if (sign_finger_extended(landmarks, 8U, 6U) != 0U)
    {
        mask |= SIGN_FINGER_INDEX;
    }
    if (sign_finger_extended(landmarks, 12U, 10U) != 0U)
    {
        mask |= SIGN_FINGER_MIDDLE;
    }
    if (sign_finger_extended(landmarks, 16U, 14U) != 0U)
    {
        mask |= SIGN_FINGER_RING;
    }
    if (sign_finger_extended(landmarks, 20U, 18U) != 0U)
    {
        mask |= SIGN_FINGER_PINKY;
    }

    return mask;
}

static uint8_t sign_fingers_spread(const ld_point_t landmarks[LD_LANDMARKS_NB], float palm_scale)
{
    float d1 = sign_distance(landmarks[8], landmarks[12]);
    float d2 = sign_distance(landmarks[12], landmarks[16]);
    float d3 = sign_distance(landmarks[16], landmarks[20]);
    float avg = (d1 + d2 + d3) / 3.0f;

    return (uint8_t)(avg > (SIGN_SPREAD_RATIO * palm_scale));
}

static uint8_t sign_fingers_together(const ld_point_t landmarks[LD_LANDMARKS_NB], float palm_scale)
{
    float d1 = sign_distance(landmarks[8], landmarks[12]);
    float d2 = sign_distance(landmarks[12], landmarks[16]);
    float d3 = sign_distance(landmarks[16], landmarks[20]);
    float avg = (d1 + d2 + d3) / 3.0f;

    return (uint8_t)(avg < (SIGN_TOGETHER_RATIO * palm_scale));
}

static app_sign_type_t sign_classify(const ld_point_t landmarks[LD_LANDMARKS_NB])
{
    float palm_scale = sign_palm_scale(landmarks);
    float pinch = sign_distance(landmarks[4], landmarks[8]);
    uint8_t mask = sign_finger_mask(landmarks);
    uint8_t thumb_ext = (uint8_t)((mask & SIGN_FINGER_THUMB) != 0U);
    uint8_t index_ext = (uint8_t)((mask & SIGN_FINGER_INDEX) != 0U);
    uint8_t middle_ext = (uint8_t)((mask & SIGN_FINGER_MIDDLE) != 0U);
    uint8_t ring_ext = (uint8_t)((mask & SIGN_FINGER_RING) != 0U);
    uint8_t pinky_ext = (uint8_t)((mask & SIGN_FINGER_PINKY) != 0U);
    uint8_t long_fingers = (uint8_t)(mask & (SIGN_FINGER_INDEX |
                                             SIGN_FINGER_MIDDLE |
                                             SIGN_FINGER_RING |
                                             SIGN_FINGER_PINKY));
    uint8_t all_five = (uint8_t)(thumb_ext && index_ext && middle_ext && ring_ext && pinky_ext);
    float thumb_dy = landmarks[4].y - landmarks[2].y;
    float thumb_dx = fabsf(landmarks[4].x - landmarks[2].x);
    float index_dy = landmarks[8].y - landmarks[5].y;

    if ((pinch < (SIGN_PINCH_RATIO * palm_scale)) && middle_ext && ring_ext && pinky_ext)
    {
        return APP_SIGN_OK;
    }

    if ((long_fingers == 0U) && !thumb_ext)
    {
        return APP_SIGN_HELP;
    }

    if (all_five && (sign_fingers_together(landmarks, palm_scale) != 0U))
    {
        return APP_SIGN_STOP;
    }

    if (all_five && (sign_fingers_spread(landmarks, palm_scale) != 0U))
    {
        return APP_SIGN_HELLO;
    }

    if (index_ext && middle_ext && ring_ext && !thumb_ext && !pinky_ext)
    {
        return APP_SIGN_WATER;
    }

    if (index_ext && !middle_ext && !ring_ext && !pinky_ext && !thumb_ext && (index_dy < 0.0f))
    {
        return APP_SIGN_YES;
    }

    if (thumb_ext && (long_fingers == 0U) &&
        (fabsf(thumb_dy) > (thumb_dx * SIGN_THUMB_DIR_RATIO)) &&
        (thumb_dy < 0.0f))
    {
        return APP_SIGN_THANKS;
    }

    if (thumb_ext && (long_fingers == 0U) &&
        (fabsf(thumb_dy) > (thumb_dx * SIGN_THUMB_DIR_RATIO)) &&
        (thumb_dy > 0.0f))
    {
        return APP_SIGN_NO;
    }

    return APP_SIGN_NONE;
}

void app_sign_init(app_sign_state_t *state)
{
    memset(state, 0, sizeof(*state));
}

void app_sign_reset(app_sign_state_t *state)
{
    state->initialized = 0U;
    state->candidate_start_ms = 0U;
    state->candidate = APP_SIGN_NONE;
    state->latched = APP_SIGN_NONE;
}

app_sign_result_t app_sign_update(app_sign_state_t *state,
                                  const ld_point_t landmarks[LD_LANDMARKS_NB],
                                  uint32_t now_ms)
{
    app_sign_result_t result;
    app_sign_type_t sign;
    ld_point_t center = sign_center(landmarks);
    float palm_scale = sign_palm_scale(landmarks);
    float motion;

    memset(&result, 0, sizeof(result));
    result.current_sign = APP_SIGN_NONE;
    result.emitted_sign = APP_SIGN_NONE;
    result.current_text = app_sign_text(APP_SIGN_NONE);
    result.emitted_text = app_sign_text(APP_SIGN_NONE);
    result.custom_recording = app_sign_user_record_is_active();
    result.custom_sample_count = app_sign_user_record_sample_count();

    if (state->initialized == 0U)
    {
        state->initialized = 1U;
        state->prev_center = center;
    }

    motion = sign_distance(center, state->prev_center);
    sign = sign_classify(landmarks);
    result.current_sign = sign;
    result.current_text = app_sign_text(sign);

    if ((motion < (SIGN_MOTION_RATIO * palm_scale)) &&
        ((now_ms - state->last_emit_ms) > SIGN_COOLDOWN_MS))
    {
        if (sign == APP_SIGN_NONE)
        {
            state->candidate = APP_SIGN_NONE;
            state->latched = APP_SIGN_NONE;
            state->candidate_start_ms = now_ms;
        }
        else if (sign != state->candidate)
        {
            state->candidate = sign;
            state->candidate_start_ms = now_ms;
        }
        else if ((sign != state->latched) &&
                 ((now_ms - state->candidate_start_ms) > SIGN_HOLD_MS))
        {
            result.emitted_sign = sign;
            result.emitted_text = app_sign_text(sign);
            state->latched = sign;
            state->last_emit_ms = now_ms;
        }
    }

    state->prev_center = center;

    return result;
}

const char *app_sign_text(app_sign_type_t sign)
{
    if (sign <= APP_SIGN_THANKS)
    {
        return sign_table[sign].text;
    }

    return sign_table[APP_SIGN_NONE].text;
}

const char *app_sign_description(app_sign_type_t sign)
{
    if (sign <= APP_SIGN_THANKS)
    {
        return sign_table[sign].description;
    }

    return sign_table[APP_SIGN_NONE].description;
}

app_sign_record_status_t app_sign_user_record_begin(uint8_t slot, const char *text)
{
    if (slot >= APP_SIGN_CUSTOM_SLOT_NB)
    {
        return APP_SIGN_RECORD_BAD_SLOT;
    }

    if (record_context.active != 0U)
    {
        return APP_SIGN_RECORD_BUSY;
    }

    memset(&record_context, 0, sizeof(record_context));
    record_context.active = 1U;
    record_context.slot = slot;

    if (text != NULL)
    {
        strncpy(record_context.text, text, APP_SIGN_TEXT_MAX - 1U);
    }

    return APP_SIGN_RECORD_OK;
}

app_sign_record_status_t app_sign_user_record_sample(const ld_point_t landmarks[LD_LANDMARKS_NB])
{
    (void)landmarks;

    if (record_context.active == 0U)
    {
        return APP_SIGN_RECORD_NO_SAMPLE;
    }

    record_context.sample_count++;

    return APP_SIGN_RECORD_OK;
}

app_sign_record_status_t app_sign_user_record_commit(void)
{
    if (record_context.active == 0U)
    {
        return APP_SIGN_RECORD_NO_SAMPLE;
    }

    if (record_context.sample_count == 0U)
    {
        app_sign_user_record_cancel();
        return APP_SIGN_RECORD_NO_SAMPLE;
    }

    record_context.active = 0U;

    return APP_SIGN_RECORD_OK;
}

void app_sign_user_record_cancel(void)
{
    memset(&record_context, 0, sizeof(record_context));
}

uint8_t app_sign_user_record_is_active(void)
{
    return record_context.active;
}

uint32_t app_sign_user_record_sample_count(void)
{
    return record_context.sample_count;
}
