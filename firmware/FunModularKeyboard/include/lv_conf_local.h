#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*
 * Keep the LVGL configuration owned by this project.  Options not defined
 * here are supplied by LVGL's lv_conf_internal.h defaults.
 */
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_48 1

#undef LV_FONT_SIMSUN_16_CJK
#define LV_FONT_SIMSUN_16_CJK 0

#undef LV_FONT_CUSTOM_DECLARE
#define LV_FONT_CUSTOM_DECLARE

#undef LV_FONT_DEFAULT
#define LV_FONT_DEFAULT &lv_font_montserrat_14

// Let LVGL read ESP32's monotonic timer directly. Without a tick source,
// lv_timer_create() callbacks never become due after their initial update.
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "esp_timer.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR ((uint32_t)(esp_timer_get_time() / 1000ULL))

#endif
