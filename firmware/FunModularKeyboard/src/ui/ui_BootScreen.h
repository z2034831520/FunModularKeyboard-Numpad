#ifndef UI_BOOT_SCREEN_H
#define UI_BOOT_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

void ui_BootScreen_screen_init(void);
void ui_BootScreen_screen_destroy(void);

extern lv_obj_t *ui_BootScreen;
extern lv_obj_t *ui_BootGif;

#ifdef __cplusplus
}
#endif

#endif
