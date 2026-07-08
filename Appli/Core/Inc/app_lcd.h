/**
 ****************************************************************************************************
 * @file        app_lcd.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2025-01-13
 * @brief       app_lcd.h文件
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

#ifndef __APP_LCD_H
#define __APP_LCD_H

#include "stm32n6xx_hal.h"
#include "stm32_lcd.h"
#include "stm32_lcd_ex.h"

typedef struct
{
    uint32_t x_pos;
    uint32_t y_pos;
    uint32_t width;
    uint32_t height;
} app_rectangle_t;

void app_lcd_init(void);
app_rectangle_t *app_lcd_get_bg_area(void);
app_rectangle_t *app_lcd_get_fg_area(void);
uint8_t *app_lcd_get_bg_buffer_by_index(uint8_t index);
uint8_t *app_lcd_get_bg_buffer(void);
void app_lcd_switch_bg_buffer(void);
void app_lcd_draw_area_update(void);
void app_lcd_draw_area_commit(void);

#endif
