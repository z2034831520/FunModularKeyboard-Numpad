#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*
 * Keep the LVGL configuration owned by this project.  Options not defined
 * here are supplied by LVGL's lv_conf_internal.h defaults.
 */
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_48 0

/*
 * The time-only UI creates base objects and labels.
 * Disable the unused widget families so the default theme cannot pull their
 * implementations into the firmware image.
 */
#define LV_USE_ARC 0
#define LV_USE_BAR 0
#define LV_USE_BTN 0
#define LV_USE_BTNMATRIX 0
#define LV_USE_CANVAS 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMG 0
#define LV_USE_LABEL 1
#define LV_USE_LINE 0
#define LV_USE_ROLLER 0
#define LV_USE_SLIDER 0
#define LV_USE_SWITCH 0
#define LV_USE_TABLE 0
#define LV_USE_TEXTAREA 0

#define LV_USE_ANIMIMG 0
#define LV_USE_CALENDAR 0
#define LV_USE_CHART 0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_METER 0
#define LV_USE_MSGBOX 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

/* All screens use fixed coordinates; no flex/grid layout is generated. */
#define LV_USE_FLEX 0
#define LV_USE_GRID 0

#define LV_USE_BMP 0
#define LV_USE_SJPG 0
#define LV_USE_GIF 0
#define LV_USE_QRCODE 0
#define LV_USE_FREETYPE 0
#define LV_USE_TINY_TTF 0
#define LV_USE_RLOTTIE 0
#define LV_USE_FFMPEG 0
#define LV_USE_SNAPSHOT 0
#define LV_USE_MONKEY 0
#define LV_USE_GRIDNAV 0
#define LV_USE_FRAGMENT 0
#define LV_USE_IMGFONT 0
#define LV_USE_MSG 0
#define LV_USE_IME_PINYIN 0

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
