#ifndef FUN_MODULAR_KEYBOARD_UI_HELPERS_H
#define FUN_MODULAR_KEYBOARD_UI_HELPERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

void _ui_screen_change(lv_obj_t **target,
                       lv_scr_load_anim_t animation,
                       int speed,
                       int delay,
                       void (*target_init)(void));

#ifdef __cplusplus
}
#endif

#endif
