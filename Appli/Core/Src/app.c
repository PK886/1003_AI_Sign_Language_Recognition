/**
 ****************************************************************************************************
 * @file        app.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2025-01-13
 * @brief       app.c文件
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 * 
 * 实验平台:正点原子 N647开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 * 
 ****************************************************************************************************
 */

#include "app.h"
#include "app_config.h"
#include "app_utils.h"
#include "app_lcd.h"
#include "app_camera.h"
#include "app_bqueue.h"
#include "app_cpuload.h"
#include "app_postprocess.h"
#include "app_key.h"
#include "app_phrase_bank.h"
#include "app_sign.h"
#include "app_voice.h"
#include "ld.h"
#include "tx_api.h"
#include "cmw_camera.h"
#include "ll_aton_runtime.h"
#include "IPL_resize.h"

typedef struct {
    float cx;
    float cy;
    float w;
    float h;
    float rotation;
} app_roi_t;

typedef struct {
    int is_valid;
    pd_pp_box_t pd_hands;
    app_roi_t roi;
    ld_point_t ld_landmarks[LD_LANDMARKS_NB];
} app_hand_info_t;

typedef enum {
    APP_UI_MODE_RECOGNIZE = 0,
    APP_UI_MODE_PHRASE_MENU,
    APP_UI_MODE_RECORDING,
} app_ui_mode_t;

typedef struct {
    float nn_period_ms;
    uint32_t pd_ms;
    uint32_t hl_ms;
    uint32_t pp_ms;
    uint32_t disp_ms;
    int pd_hand_nb;
    float pd_max_prob;
    app_sign_result_t sign;
    app_sign_type_t last_sign;
    app_ui_mode_t ui_mode;
    uint8_t selected_phrase;
    const char *last_output_text;
    uint32_t sign_count;
    uint32_t template_count;
    uint8_t voice_ready;
    app_hand_info_t hands[PD_MAX_HAND_NB];
} app_display_info_t;

typedef struct {
    TX_SEMAPHORE update;
    TX_MUTEX lock;
    app_display_info_t info;
} app_display_t;

typedef struct {
    uint32_t nn_in_len;
    float *prob_out;
    uint32_t prob_out_len;
    float *boxes_out;
    uint32_t boxes_out_len;
    pd_model_pp_static_param_t static_param;
    pd_postprocess_out_t pd_out;
} app_pd_model_info_t;

typedef struct {
    uint8_t *nn_in;
    uint32_t nn_in_len;
    float *prob_out;
    uint32_t prob_out_len;
    float *landmarks_out;
    uint32_t landmarks_out_len;
} app_hl_model_info_t;

static TX_SEMAPHORE isp_semaphore;

static void app_camera_display_pipe_vsync_cb(void);
static void app_camera_display_pipe_frame_cb(void);
static void app_camera_nn_pipe_frame_cb(void);

static TX_THREAD nn_thread;
static UCHAR nn_thread_stack[4096];
static TX_THREAD dp_thread;
static UCHAR dp_thread_stack[4096];
static TX_THREAD isp_thread;
static UCHAR isp_thread_stack[4096];

static VOID nn_thread_entry(ULONG id);
static VOID dp_thread_entry(ULONG id);
static VOID isp_thread_entry(ULONG id);

static app_display_t display;

static uint8_t nn_input_buffers[2][NN_WIDTH * NN_HEIGHT * NN_BPP] __attribute__((aligned(32))) __attribute__((section(".EXTRAM")));
static app_bqueue_t nn_input_queue;

LL_ATON_DECLARE_NAMED_NN_INSTANCE_AND_INTERFACE(palm_detection);
static app_roi_t app_pd_rois[PD_MAX_HAND_NB];

LL_ATON_DECLARE_NAMED_NN_INSTANCE_AND_INTERFACE(hand_landmarks);

static ld_point_t ld_landmarks[PD_MAX_HAND_NB][LD_LANDMARKS_NB];
static ld_point_t sign_landmarks[LD_LANDMARKS_NB];

static uint32_t frame_event_nb;
static volatile uint32_t frame_event_nb_for_resize;

static app_cpuload_t cpuload;

static uint8_t app_clamp_point_with_margin(int *x, int *y, int margin);
static void app_display_hand(app_hand_info_t *hand);
static void app_display_network_output(app_display_info_t *display_info);
static void app_decode_ld_landmarks(app_roi_t *roi, ld_point_t *lm, ld_point_t *decoded);
static void app_ld_to_roi(ld_point_t lm[LD_LANDMARKS_NB], app_roi_t *roi, pd_pp_box_t *next_pd);
static void app_compute_next_roi(app_roi_t *src, ld_point_t lm_in[LD_LANDMARKS_NB], app_roi_t *next, pd_pp_box_t *next_pd);
static void app_copy_pd_box(pd_pp_box_t *dst, pd_pp_box_t *src);
static void app_cvt_pd_coord_to_screen_coord(pd_pp_box_t *box);
static float app_pd_normalize_angle(float angle);
static float app_pd_cook_rotation(float angle);
static float app_pd_compute_rotation(pd_pp_box_t *box);
static void app_roi_shift_and_scale(app_roi_t *roi, float shift_x, float shift_y, float scale_x, float scale_y);
static void app_pd_box_to_roi(pd_pp_box_t *box, app_roi_t *roi);
static void app_palm_detection_init(app_pd_model_info_t *info);
static uint32_t app_palm_detection_run(uint8_t *buffer, app_pd_model_info_t *info, uint32_t *pd_exec_time);
static void app_rotate_point(float pt[2], float rotation);
static void app_roi_to_corners(app_roi_t *roi, float corners[4][2]);
static uint8_t app_clamp_point(int *x, int *y);
static uint8_t app_clamp_corners(float corners_in[4][2], int corners_out[4][2]);
static int app_hand_landmarks_prepare_input(uint8_t *buffer, app_roi_t *roi, app_hl_model_info_t *info);
static void app_hand_landmarks_init(app_hl_model_info_t *info);
static uint8_t app_hand_landmarks_run(uint8_t *buffer, app_hl_model_info_t *info, app_roi_t *roi, ld_point_t ld_landmarks[LD_LANDMARKS_NB]);

void app_run(void)
{
    app_lcd_init();
    app_bqueue_init(&nn_input_queue, 2, (uint8_t *[2]){nn_input_buffers[0], nn_input_buffers[1]});
    app_cpuload_init(&cpuload);
    app_key_init();
    app_voice_init();
    app_camera_init(app_camera_display_pipe_vsync_cb, app_camera_display_pipe_frame_cb, NULL, app_camera_nn_pipe_frame_cb);

    tx_semaphore_create(&isp_semaphore, NULL, 0);
    tx_semaphore_create(&display.update, NULL, 0);
    tx_mutex_create(&display.lock, NULL, TX_INHERIT);

    app_camera_display_pipe_start(app_lcd_get_bg_buffer(), CMW_MODE_CONTINUOUS);

    tx_thread_create(&nn_thread, "NN Thread", nn_thread_entry, 0, nn_thread_stack, sizeof(nn_thread_stack), TX_MAX_PRIORITIES - 3, TX_MAX_PRIORITIES - 3, 10, TX_AUTO_START);
    tx_thread_create(&dp_thread, "DP Thread", dp_thread_entry, 0, dp_thread_stack, sizeof(dp_thread_stack), TX_MAX_PRIORITIES - 2, TX_MAX_PRIORITIES - 2, 10, TX_AUTO_START);
    tx_thread_create(&isp_thread, "ISP Thread", isp_thread_entry, 0, isp_thread_stack, sizeof(isp_thread_stack), TX_MAX_PRIORITIES - 4, TX_MAX_PRIORITIES - 4, 10, TX_AUTO_START);
}

static void app_camera_display_pipe_vsync_cb(void)
{
    tx_semaphore_put(&isp_semaphore);
}

static void app_camera_display_pipe_frame_cb(void)
{
    app_lcd_switch_bg_buffer();
    app_camera_display_pipe_set_address(app_lcd_get_bg_buffer());
    frame_event_nb++;
}

static void app_camera_nn_pipe_frame_cb(void)
{
    uint8_t *buffer;

    buffer = app_bqueue_get_free(&nn_input_queue, 0);
    if (buffer != NULL)
    {
        app_camera_nn_pipe_set_address(buffer);
        app_bqueue_put_ready(&nn_input_queue);
        frame_event_nb_for_resize = frame_event_nb - 1;
    }
}

static VOID nn_thread_entry(ULONG id)
{
    app_pd_model_info_t pd_info;
    app_hl_model_info_t hl_info;
    pd_pp_box_t box_next;
    pd_pp_point_t box_next_keypoints[AI_PD_MODEL_PP_NB_KEYPOINTS];
    uint8_t *nn_pipe_dst;
    uint32_t nn_period[2];
    uint32_t nn_period_ms;
    float nn_period_filtered_ms = 0;
    uint8_t *capture_buffer;
    uint32_t idx_for_resize;
    uint8_t is_tracking = 0;
    app_roi_t roi_next;
    uint32_t pd_ms;
    uint32_t hl_ms;
    float pd_filtered_ms = 0;
    float ld_filtered_ms = 0;
    app_sign_state_t sign_state;
    app_sign_result_t sign_result;
    app_sign_type_t last_sign = APP_SIGN_NONE;
    app_ui_mode_t ui_mode = APP_UI_MODE_RECOGNIZE;
    uint8_t selected_phrase = 0U;
    const char *last_output_text = "---";
    uint32_t sign_count = 0U;
    uint8_t key_events;
    uint8_t j;

    app_palm_detection_init(&pd_info);
    app_hand_landmarks_init(&hl_info);
    app_sign_init(&sign_state);
    sign_result = (app_sign_result_t){0};
    sign_result.current_text = app_sign_text(APP_SIGN_NONE);
    sign_result.emitted_text = app_sign_text(APP_SIGN_NONE);

    box_next.pKps = box_next_keypoints;

    nn_period[1] = HAL_GetTick();
    nn_pipe_dst = app_bqueue_get_free(&nn_input_queue, 0);
    app_camera_nn_pipe_start(nn_pipe_dst, CMW_MODE_CONTINUOUS);

    while (1)
    {

        nn_period[0] = nn_period[1];
        nn_period[1] = HAL_GetTick();
        nn_period_ms = nn_period[1] - nn_period[0];
        nn_period_filtered_ms = (15 * nn_period_filtered_ms + nn_period_ms) / 16;
        key_events = app_key_update(nn_period[1]);

        if ((key_events & APP_KEY_EVENT_MODE) != 0U)
        {
            if (ui_mode == APP_UI_MODE_RECORDING)
            {
                app_sign_user_record_cancel();
                ui_mode = APP_UI_MODE_PHRASE_MENU;
            }
            else if (ui_mode == APP_UI_MODE_PHRASE_MENU)
            {
                ui_mode = APP_UI_MODE_RECOGNIZE;
                app_sign_reset(&sign_state);
            }
            else
            {
                ui_mode = APP_UI_MODE_PHRASE_MENU;
                app_sign_reset(&sign_state);
            }
        }

        if (ui_mode == APP_UI_MODE_PHRASE_MENU)
        {
            if ((key_events & APP_KEY_EVENT_NEXT) != 0U)
            {
                selected_phrase = app_phrase_bank_next(selected_phrase);
            }
            if ((key_events & APP_KEY_EVENT_PREV) != 0U)
            {
                selected_phrase = app_phrase_bank_prev(selected_phrase);
            }
            if ((key_events & APP_KEY_EVENT_OK) != 0U)
            {
                if (app_sign_user_record_begin(selected_phrase,
                                               app_phrase_bank_text(selected_phrase)) == APP_SIGN_RECORD_OK)
                {
                    ui_mode = APP_UI_MODE_RECORDING;
                    app_sign_reset(&sign_state);
                }
            }
        }
        else if ((ui_mode == APP_UI_MODE_RECORDING) &&
                 ((key_events & APP_KEY_EVENT_OK) != 0U))
        {
            if (app_sign_user_record_commit() == APP_SIGN_RECORD_OK)
            {
                ui_mode = APP_UI_MODE_RECOGNIZE;
                app_sign_reset(&sign_state);
            }
        }

        capture_buffer = app_bqueue_get_ready(&nn_input_queue);
        idx_for_resize = frame_event_nb_for_resize % DISPLAY_BUFFER_NB;

        if (is_tracking == 0)
        {
            is_tracking = app_palm_detection_run(capture_buffer, &pd_info, &pd_ms);
            box_next.prob = pd_info.pd_out.pOutData[0].prob;
        }
        else
        {
            app_pd_rois[0] = roi_next;
            app_copy_pd_box(&pd_info.pd_out.pOutData[0], &box_next);
            pd_ms = 0;
        }
        pd_filtered_ms = (7 * pd_filtered_ms + pd_ms) / 8;
        app_bqueue_put_free(&nn_input_queue);
    
        if (is_tracking != 0)
        {
            hl_ms = HAL_GetTick();
            is_tracking = app_hand_landmarks_run(app_lcd_get_bg_buffer_by_index(idx_for_resize), &hl_info, &app_pd_rois[0], ld_landmarks[0]);
            SCB_InvalidateDCache_by_Addr(app_lcd_get_bg_buffer_by_index(idx_for_resize), LCD_BG_WIDTH * LCD_BG_HEIGHT * 3);
            if (is_tracking != 0)
            {
                app_compute_next_roi(&app_pd_rois[0], ld_landmarks[0], &roi_next, &box_next);
                for (j = 0; j < LD_LANDMARKS_NB; j++)
                {
                    app_decode_ld_landmarks(&app_pd_rois[0], &ld_landmarks[0][j], &sign_landmarks[j]);
                }

                if (ui_mode == APP_UI_MODE_RECORDING)
                {
                    (void)app_sign_user_record_sample(sign_landmarks);
                    sign_result = (app_sign_result_t){0};
                    sign_result.current_text = app_phrase_bank_text(selected_phrase);
                    sign_result.emitted_text = app_sign_text(APP_SIGN_NONE);
                    sign_result.custom_recording = app_sign_user_record_is_active();
                    sign_result.custom_sample_count = app_sign_user_record_sample_count();
                }
                else if (ui_mode == APP_UI_MODE_RECOGNIZE)
                {
                    sign_result = app_sign_update(&sign_state, sign_landmarks, HAL_GetTick());
                }
                else
                {
                    sign_result = (app_sign_result_t){0};
                    sign_result.current_text = app_phrase_bank_text(selected_phrase);
                    sign_result.emitted_text = app_sign_text(APP_SIGN_NONE);
                    sign_result.custom_recording = app_sign_user_record_is_active();
                    sign_result.custom_sample_count = app_sign_user_record_sample_count();
                }

                if (sign_result.emitted_valid != 0U)
                {
                    last_sign = sign_result.emitted_sign;
                    last_output_text = sign_result.emitted_text;
                    sign_count++;
                    (void)app_voice_say_text(sign_result.emitted_text);
                }
            }
            hl_ms = HAL_GetTick() - hl_ms;
        }
        else
        {
            hl_ms = 0;
            app_sign_reset(&sign_state);
            sign_result = (app_sign_result_t){0};
            sign_result.current_text = (ui_mode == APP_UI_MODE_RECOGNIZE) ?
                                       app_sign_text(APP_SIGN_NONE) :
                                       app_phrase_bank_text(selected_phrase);
            sign_result.emitted_text = app_sign_text(APP_SIGN_NONE);
            sign_result.custom_recording = app_sign_user_record_is_active();
            sign_result.custom_sample_count = app_sign_user_record_sample_count();
        }
        ld_filtered_ms = (7 * ld_filtered_ms + hl_ms) / 8;
    
        tx_mutex_get(&display.lock, TX_WAIT_FOREVER);
        display.info.pd_ms = is_tracking ? 0 : (int)pd_filtered_ms;
        display.info.hl_ms = is_tracking ? (int)ld_filtered_ms : 0;
        display.info.nn_period_ms = nn_period_filtered_ms;
        display.info.pd_hand_nb = is_tracking;
        display.info.pd_max_prob = pd_info.pd_out.pOutData[0].prob;
        display.info.sign = sign_result;
        display.info.last_sign = last_sign;
        display.info.ui_mode = ui_mode;
        display.info.selected_phrase = selected_phrase;
        display.info.last_output_text = last_output_text;
        display.info.sign_count = sign_count;
        display.info.template_count = app_sign_user_template_count();
        display.info.voice_ready = app_voice_is_ready();
        display.info.hands[0].is_valid = is_tracking;
        app_copy_pd_box(&display.info.hands[0].pd_hands, &pd_info.pd_out.pOutData[0]);
        display.info.hands[0].roi = app_pd_rois[0];
        for (j = 0; j < LD_LANDMARKS_NB; j++)
        {
            display.info.hands[0].ld_landmarks[j] = ld_landmarks[0][j];
        }
        tx_mutex_put(&display.lock);
    
        tx_semaphore_ceiling_put(&display.update, 1);
    }
}

static VOID dp_thread_entry(ULONG id)
{
    uint32_t disp_ms = 0;
    app_display_info_t info;
    uint32_t time_smap;

    while (1)
    {
        tx_semaphore_get(&display.update, TX_WAIT_FOREVER);

        tx_mutex_get(&display.lock, TX_WAIT_FOREVER);
        info = display.info;
        tx_mutex_put(&display.lock);
        info.disp_ms = disp_ms;

        time_smap = HAL_GetTick();
        app_display_network_output(&info);
        disp_ms = HAL_GetTick() - time_smap;
    }
}

static VOID isp_thread_entry(ULONG id)
{
    while (1)
    {
        tx_semaphore_get(&isp_semaphore, TX_WAIT_FOREVER);

        app_camera_isp_update();
    }
}

static uint8_t app_clamp_point_with_margin(int *x, int *y, int margin)
{
    int xi = *x;
    int yi = *y;

    if (*x < margin)
    {
        *x = margin;
    }

    if (*y < margin)
    {
        *y = margin;
    }

    if (*x >= LCD_BG_WIDTH - margin)
    {
        *x = LCD_BG_WIDTH - margin - 1;
    }

    if (*y >= LCD_BG_HEIGHT - margin)
    {
        *y = LCD_BG_HEIGHT - margin - 1;
    }

    return (xi != *x) || (yi != *y);
}

static void app_display_hand(app_hand_info_t *hand)
{
    const int disk_radius = 2;
    app_roi_t *roi = &hand->roi;
    int x[LD_LANDMARKS_NB];
    int y[LD_LANDMARKS_NB];
    int is_clamped[LD_LANDMARKS_NB];
    ld_point_t decoded;
    uint8_t i;

    for (i = 0; i < LD_LANDMARKS_NB; i++)
    {
        app_decode_ld_landmarks(roi, &hand->ld_landmarks[i], &decoded);
        x[i] = (int)decoded.x;
        y[i] = (int)decoded.y;
        is_clamped[i] = app_clamp_point_with_margin(&x[i], &y[i], disk_radius);
    }

    for (i = 0; i < LD_LANDMARKS_NB; i++)
    {
        if (is_clamped[i] != 0)
        {
            continue;
        }
        UTIL_LCD_FillCircle(x[i], y[i], disk_radius, UTIL_LCD_COLOR_YELLOW);
    }

    for (i = 0; i < LD_BINDING_NB; i++)
    {
        if ((is_clamped[ld_bindings_idx[i][0]] != 0) || (is_clamped[ld_bindings_idx[i][1]] != 0))
        {
            continue;
        }
        UTIL_LCD_DrawLine(x[ld_bindings_idx[i][0]], y[ld_bindings_idx[i][0]], x[ld_bindings_idx[i][1]], y[ld_bindings_idx[i][1]], UTIL_LCD_COLOR_BLACK);
    }
}

static void app_display_network_output(app_display_info_t *display_info)
{
    float cpuload_one_second;
    uint8_t line_nb = 0;
    int32_t i;

    app_lcd_draw_area_update();

    UTIL_LCD_FillRect(0, 0, LCD_FG_WIDTH, LCD_FG_HEIGHT, 0x00000000);

    app_cpuload_update(&cpuload);
    app_cpuload_get_info(&cpuload, NULL, &cpuload_one_second, NULL);

    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "CPU load");
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "%.1f%%", cpuload_one_second);
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "Inference");
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "pd %2ums", display_info->pd_ms);
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "hl %2ums", display_info->hl_ms);
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "FPS");
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "%.1f", 1000.0 / display_info->nn_period_ms);
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "Mode");
    line_nb += 1;
    if (display_info->ui_mode == APP_UI_MODE_RECORDING)
    {
        UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "Record");
    }
    else if (display_info->ui_mode == APP_UI_MODE_PHRASE_MENU)
    {
        UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "Phrase Menu");
    }
    else
    {
        UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "Recognize");
    }
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "Sign Text");
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "%s", display_info->sign.current_text);
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "Last Output");
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "%s", display_info->last_output_text);
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "Count %lu", (unsigned long)display_info->sign_count);
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "Phrase %u/%u",
                        (unsigned int)(display_info->selected_phrase + 1U),
                        (unsigned int)APP_PHRASE_BANK_COUNT);
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "%s",
                        app_phrase_bank_text(display_info->selected_phrase));
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "Slot %s",
                        app_sign_user_slot_is_used(display_info->selected_phrase) ? "Saved" : "Empty");
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "Samples %lu/%u",
                        (unsigned long)display_info->sign.custom_sample_count,
                        (unsigned int)APP_SIGN_RECORD_MIN_SAMPLES);
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "Templates %lu/%u",
                        (unsigned long)display_info->template_count,
                        (unsigned int)APP_SIGN_CUSTOM_SLOT_NB);
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "Voice %s",
                        display_info->voice_ready ? "Ready" : "Reserved");
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "Keys K0/K1/K2/WK");
    line_nb += 1;

    for (i = 0; i < display_info->pd_hand_nb; i++)
    {
        if (display_info->hands[i].is_valid != 0)
        {
            app_display_hand(&display_info->hands[i]);
        }
    }

    app_lcd_draw_area_commit();
}

static void app_decode_ld_landmarks(app_roi_t *roi, ld_point_t *lm, ld_point_t *decoded)
{
    float rotation = roi->rotation;
    float w = roi->w;
    float h = roi->h;

    decoded->x = roi->cx + (lm->x - 0.5) * w * cos(rotation) - (lm->y - 0.5) * h * sin(rotation);
    decoded->y = roi->cy + (lm->x - 0.5) * w * sin(rotation) + (lm->y - 0.5) * h * cos(rotation);
}

static float app_ld_compute_rotation(ld_point_t lm[LD_LANDMARKS_NB])
{
    float x0, y0, x1, y1;
    float rotation;

    x0 = lm[0].x;
    y0 = lm[0].y;
    x1 = lm[9].x;
    y1 = lm[9].y;

    rotation = M_PI * 0.5 - atan2f(-(y1 - y0), x1 - x0);

    return app_pd_cook_rotation(app_pd_normalize_angle(rotation));
}

static void app_ld_to_roi(ld_point_t lm[LD_LANDMARKS_NB], app_roi_t *roi, pd_pp_box_t *next_pd)
{
    const int pd_to_ld_idx[AI_PD_MODEL_PP_NB_KEYPOINTS] = {0, 5, 9, 13, 17, 1, 2};
    const int indices[] = {0, 1, 2, 3, 5, 6, 9, 10, 13, 14, 17, 18};
    float max_x, max_y, min_x, min_y;
    int i;

    max_x = max_y = -10000;
    min_x = min_y =  10000;

    roi->rotation = app_ld_compute_rotation(lm);

    for (i = 0; i < ARRAY_NB(indices); i++)
    {
        max_x = MAX(max_x, lm[indices[i]].x);
        max_y = MAX(max_y, lm[indices[i]].y);
        min_x = MIN(min_x, lm[indices[i]].x);
        min_y = MIN(min_y, lm[indices[i]].y);
    }

    roi->cx = (max_x + min_x) / 2;
    roi->cy = (max_y + min_y) / 2;
    roi->w = (max_x - min_x);
    roi->h = (max_y - min_y);

    next_pd->x_center = roi->cx;
    next_pd->y_center = roi->cy;
    next_pd->width = roi->w;
    next_pd->height = roi->h;
    for (i = 0; i < AI_PD_MODEL_PP_NB_KEYPOINTS; i++)
    {
        next_pd->pKps[i].x = lm[pd_to_ld_idx[i]].x;
        next_pd->pKps[i].y = lm[pd_to_ld_idx[i]].y;
    }
}

static void app_compute_next_roi(app_roi_t *src, ld_point_t lm_in[LD_LANDMARKS_NB], app_roi_t *next, pd_pp_box_t *next_pd)
{
    const float shift_x = 0;
    const float shift_y = -0.1;
    const float scale = 2.0;
    ld_point_t lm[LD_LANDMARKS_NB];
    app_roi_t roi;
    uint8_t i;

    for (i = 0; i < LD_LANDMARKS_NB; i++)
    {
        app_decode_ld_landmarks(src, &lm_in[i], &lm[i]);
    }

    app_ld_to_roi(lm, &roi, next_pd);
    app_roi_shift_and_scale(&roi, shift_x, shift_y, scale, scale);

    roi.rotation = 0;

    *next = roi;
}

static void app_copy_pd_box(pd_pp_box_t *dst, pd_pp_box_t *src)
{
    uint8_t i;

    dst->prob = src->prob;
    dst->x_center = src->x_center;
    dst->y_center = src->y_center;
    dst->width = src->width;
    dst->height = src->height;
    for (i = 0 ; i < AI_PD_MODEL_PP_NB_KEYPOINTS; i++)
    {
        dst->pKps[i] = src->pKps[i];
    }
}

static void app_cvt_pd_coord_to_screen_coord(pd_pp_box_t *box)
{
    int i;

    box->x_center *= LCD_BG_WIDTH;
    box->y_center *= LCD_BG_WIDTH;
    box->width *= LCD_BG_WIDTH;
    box->height *= LCD_BG_WIDTH;
    for (i = 0; i < AI_PD_MODEL_PP_NB_KEYPOINTS; i++)
    {
        box->pKps[i].x *= LCD_BG_WIDTH;
        box->pKps[i].y *= LCD_BG_WIDTH;
    }
}

static float app_pd_normalize_angle(float angle)
{
    return angle - 2 * M_PI * floorf((angle - (-M_PI)) / (2 * M_PI));
}

static float app_pd_cook_rotation(float angle)
{
    if (angle >= (3 * M_PI) / 4)
    {
        angle = M_PI;
    }
    else if (angle >= (1 * M_PI) / 4)
    {
        angle = M_PI / 2;
    }
    else if (angle >= -(1 * M_PI) / 4)
    {
        angle = 0;
    }
    else if (angle >= -(3 * M_PI) / 4)
    {
        angle = -M_PI / 2;
    }
    else
    {
        angle = -M_PI;
    }

    return angle;
}

static float app_pd_compute_rotation(pd_pp_box_t *box)
{
    float x0, y0, x1, y1;
    float rotation;

    x0 = box->pKps[0].x;
    y0 = box->pKps[0].y;
    x1 = box->pKps[2].x;
    y1 = box->pKps[2].y;

    rotation = M_PI * 0.5 - atan2f(-(y1 - y0), x1 - x0);

    return app_pd_cook_rotation(app_pd_normalize_angle(rotation));
}

static void app_roi_shift_and_scale(app_roi_t *roi, float shift_x, float shift_y, float scale_x, float scale_y)
{
    float long_side;
    float sx, sy;

    sx = (roi->w * shift_x * cos(roi->rotation) - roi->h * shift_y * sin(roi->rotation));
    sy = (roi->w * shift_x * sin(roi->rotation) + roi->h * shift_y * cos(roi->rotation));

    roi->cx += sx;
    roi->cy += sy;

    long_side = MAX(roi->w, roi->h);
    roi->w = long_side;
    roi->h = long_side;

    roi->w *= scale_x;
    roi->h *= scale_y;
}

static void app_pd_box_to_roi(pd_pp_box_t *box, app_roi_t *roi)
{
    const float shift_x = 0;
    const float shift_y = -0.5;
    const float scale = 2.6;

    roi->cx = box->x_center;
    roi->cy = box->y_center;
    roi->w = box->width;
    roi->h = box->height;
    roi->rotation = app_pd_compute_rotation(box);

    app_roi_shift_and_scale(roi, shift_x, shift_y, scale, scale);

    roi->rotation = 0;
}

static void app_palm_detection_init(app_pd_model_info_t *info)
{
    const LL_Buffer_InfoTypeDef *nn_out_info = LL_ATON_Output_Buffers_Info_palm_detection();
    const LL_Buffer_InfoTypeDef *nn_in_info = LL_ATON_Input_Buffers_Info_palm_detection();

    info->nn_in_len = LL_Buffer_len(&nn_in_info[0]);
    info->prob_out = (float *)LL_Buffer_addr_start(&nn_out_info[0]);
    info->prob_out_len = LL_Buffer_len(&nn_out_info[0]);
    info->boxes_out = (float *)LL_Buffer_addr_start(&nn_out_info[1]);
    info->boxes_out_len = LL_Buffer_len(&nn_out_info[1]);

    app_postprocess_init(&info->static_param);
}

static uint32_t app_palm_detection_run(uint8_t *buffer, app_pd_model_info_t *info, uint32_t *pd_exec_time)
{
    uint32_t time_stamp;
    uint32_t hand_nb;
    uint32_t i;

    time_stamp = HAL_GetTick();

    LL_ATON_Set_User_Input_Buffer_palm_detection(0, buffer, info->nn_in_len);
    LL_ATON_RT_Main(&NN_Instance_palm_detection);
    app_postprocess_run((void * []){info->prob_out, info->boxes_out}, 2, &info->pd_out, &info->static_param);
    hand_nb = MIN(info->pd_out.box_nb, PD_MAX_HAND_NB);

    for (i = 0; i < hand_nb; i++)
    {
        app_cvt_pd_coord_to_screen_coord(&info->pd_out.pOutData[i]);
        app_pd_box_to_roi(&info->pd_out.pOutData[i], &app_pd_rois[i]);
    }

    SCB_InvalidateDCache_by_Addr(info->prob_out, info->prob_out_len);
    SCB_InvalidateDCache_by_Addr(info->boxes_out, info->boxes_out_len);

    *pd_exec_time = HAL_GetTick() - time_stamp;

    return hand_nb;
}

static void app_rotate_point(float pt[2], float rotation)
{
    float x = pt[0];
    float y = pt[1];

    pt[0] = cos(rotation) * x - sin(rotation) * y;
    pt[1] = sin(rotation) * x + cos(rotation) * y;
}

static void app_roi_to_corners(app_roi_t *roi, float corners[4][2])
{
    uint8_t i;

    corners[0][0] = -roi->w / 2;
    corners[0][1] = -roi->h / 2;
    corners[1][0] = roi->w / 2;
    corners[1][1] = -roi->h / 2;
    corners[2][0] = roi->w / 2;
    corners[2][1] = roi->h / 2;
    corners[3][0] = -roi->w / 2;
    corners[3][1] = roi->h / 2;

    for (i = 0; i < 4; i++)
    {
        app_rotate_point(corners[i], roi->rotation);
    }

    for (i = 0; i < 4; i++)
    {
        corners[i][0] += roi->cx;
        corners[i][1] += roi->cy;
    }
}

static uint8_t app_clamp_point(int *x, int *y)
{
    int xi;
    int yi;

    xi = *x;
    yi = *y;

    if (*x < 0)
    {
        *x = 0;
    }

    if (*y < 0)
    {
        *y = 0;
    }

    if (*x >= LCD_BG_WIDTH)
    {
        *x = LCD_BG_WIDTH - 1;
    }

    if (*y >= LCD_BG_HEIGHT)
    {
        *y = LCD_BG_HEIGHT - 1;
    }

    return (xi != *x) || (yi != *y);
}

static uint8_t app_clamp_corners(float corners_in[4][2], int corners_out[4][2])
{
    uint8_t is_clamp = 0;
    uint8_t i;

    for (i = 0; i < 4; i++)
    {
        corners_out[i][0] = (int)corners_in[i][0];
        corners_out[i][1] = (int)corners_in[i][1];
        is_clamp |= app_clamp_point(&corners_out[i][0], &corners_out[i][1]);
    }

    return is_clamp;
}

static int app_hand_landmarks_prepare_input(uint8_t *buffer, app_roi_t *roi, app_hl_model_info_t *info)
{
    float corners_f[4][2];
    int corners[4][2];
    uint8_t* out_data;
    size_t height_out;
    size_t height_in;
    size_t width_out;
    size_t width_in;

    uint8_t is_clamped;
    int offset_x;
    int offset_y;
    uint8_t *in_data;

    out_data = info->nn_in;
    width_out = LD_WIDTH;
    height_out = LD_HEIGHT;

    app_roi_to_corners(roi, corners_f);
    is_clamped = app_clamp_corners(corners_f, corners);

    if (is_clamped != 0)
    {
        memset(info->nn_in, 0, info->nn_in_len);

        offset_x = (int)(((corners[0][0] - corners_f[0][0]) * LD_WIDTH) / (corners_f[2][0] - corners_f[0][0]));
        offset_y = (int)(((corners[0][1] - corners_f[0][1]) * LD_HEIGHT) / (corners_f[2][1] - corners_f[0][1]));
        out_data += offset_y * (int)LD_WIDTH * DISPLAY_BPP + offset_x * DISPLAY_BPP;

        width_out = (int)((corners[2][0] - corners[0][0]) / (corners_f[2][0] - corners_f[0][0]) * LD_WIDTH);
        height_out = (int)((corners[2][1] - corners[0][1]) / (corners_f[2][1] - corners_f[0][1]) * LD_HEIGHT);
    }

    in_data = buffer + corners[0][1] * LCD_BG_WIDTH * DISPLAY_BPP + corners[0][0]* DISPLAY_BPP;
    width_in = corners[2][0] - corners[0][0];
    height_in = corners[2][1] - corners[0][1];

    IPL_resize_bilinear_iu8ou8_with_strides_RGB(in_data, out_data, LCD_BG_WIDTH * DISPLAY_BPP, LD_WIDTH * DISPLAY_BPP, width_in, height_in, width_out, height_out);

    return 0;
}

static void app_hand_landmarks_init(app_hl_model_info_t *info)
{
    const LL_Buffer_InfoTypeDef *nn_out_info = LL_ATON_Output_Buffers_Info_hand_landmarks();
    const LL_Buffer_InfoTypeDef *nn_in_info = LL_ATON_Input_Buffers_Info_hand_landmarks();

    info->nn_in = LL_Buffer_addr_start(&nn_in_info[0]);
    info->nn_in_len = LL_Buffer_len(&nn_in_info[0]);
    info->prob_out = (float *)LL_Buffer_addr_start(&nn_out_info[2]);
    info->prob_out_len = LL_Buffer_len(&nn_out_info[2]);
    info->landmarks_out = (float *)LL_Buffer_addr_start(&nn_out_info[3]);
    info->landmarks_out_len = LL_Buffer_len(&nn_out_info[3]);
}

static uint8_t app_hand_landmarks_run(uint8_t *buffer, app_hl_model_info_t *info, app_roi_t *roi, ld_point_t ld_landmarks[LD_LANDMARKS_NB])
{
    int is_valid;

    app_hand_landmarks_prepare_input(buffer, roi, info);
    SCB_CleanInvalidateDCache_by_Addr(info->nn_in, info->nn_in_len);

    LL_ATON_RT_Main(&NN_Instance_hand_landmarks);

    is_valid = ld_post_process(info->prob_out, info->landmarks_out, ld_landmarks);

    SCB_InvalidateDCache_by_Addr(info->prob_out, info->prob_out_len);
    SCB_InvalidateDCache_by_Addr(info->landmarks_out, info->landmarks_out_len);

    return (is_valid == 0) ? 0 : 1;
}
