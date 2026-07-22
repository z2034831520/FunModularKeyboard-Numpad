#ifndef PROJECT_LV_CONF_LOCAL_H
#define PROJECT_LV_CONF_LOCAL_H

#include "../.pio/libdeps/esp32s3minin4r2/lvgl/lv_conf.h"

#undef LV_FONT_SIMSUN_16_CJK
#define LV_FONT_SIMSUN_16_CJK 0

#undef LV_FONT_CUSTOM_DECLARE
#define LV_FONT_CUSTOM_DECLARE

#undef LV_FONT_DEFAULT
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#endif