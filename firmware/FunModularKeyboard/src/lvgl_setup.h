#ifndef LVGL_SETUP_H
#define LVGL_SETUP_H

#include <lvgl.h>

#define DISP_BUF_SIZE (428 * 142 / 10) // 调整缓冲区大小

void lvgl_setup();
void lvgl_set_backlight_brightness(uint8_t brightness_percent);
void lvgl_register_spiffs_fs();
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
// bool my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data);

#endif