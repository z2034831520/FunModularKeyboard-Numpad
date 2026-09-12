#include "ui.h"

lv_obj_t * ui_MainScreen = NULL;
lv_obj_t * ui_LabelTime = NULL;
lv_obj_t * ui_LabelData = NULL;
lv_obj_t * ui_LabelSecond = NULL;
lv_obj_t * ui_LabelWeek = NULL;
lv_obj_t * ui_LabelWorkmode = NULL;

static lv_obj_t * ui_line3 = NULL;

void ui_MainScreen_screen_init(void)
{
    ui_MainScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_MainScreen, LV_OBJ_FLAG_SCROLLABLE);

    ui_LabelTime = lv_label_create(ui_MainScreen);
    lv_obj_set_size(ui_LabelTime, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(ui_LabelTime, -18, -24);
    lv_obj_set_align(ui_LabelTime, LV_ALIGN_CENTER);
    lv_label_set_text(ui_LabelTime, "10:34");
    lv_obj_set_style_text_color(ui_LabelTime, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ui_LabelTime, &ui_font_BebasNeueFont86, 0);

    ui_LabelData = lv_label_create(ui_MainScreen);
    lv_obj_set_size(ui_LabelData, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(ui_LabelData, 58, 36);
    lv_obj_set_align(ui_LabelData, LV_ALIGN_CENTER);
    lv_label_set_text(ui_LabelData, "SEP 05");
    lv_obj_set_style_text_color(ui_LabelData, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ui_LabelData, &ui_font_BebasNeueFont36, 0);

    ui_LabelSecond = lv_label_create(ui_MainScreen);
    lv_obj_set_size(ui_LabelSecond, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(ui_LabelSecond, 79, -12);
    lv_obj_set_align(ui_LabelSecond, LV_ALIGN_CENTER);
    lv_label_set_text(ui_LabelSecond, "56");
    lv_obj_set_style_text_color(ui_LabelSecond, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ui_LabelSecond, &ui_font_BebasNeueFont48, 0);

    ui_LabelWeek = lv_label_create(ui_MainScreen);
    lv_obj_set_size(ui_LabelWeek, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(ui_LabelWeek, -40, 36);
    lv_obj_set_align(ui_LabelWeek, LV_ALIGN_CENTER);
    lv_label_set_text(ui_LabelWeek, "MONDAY");
    lv_obj_set_style_text_color(ui_LabelWeek, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ui_LabelWeek, &ui_font_BebasNeueFont36, 0);

    ui_LabelWorkmode = lv_label_create(ui_MainScreen);
    lv_obj_set_size(ui_LabelWorkmode, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(ui_LabelWorkmode, -137, -39);
    lv_obj_set_align(ui_LabelWorkmode, LV_ALIGN_CENTER);
    lv_label_set_text(ui_LabelWorkmode, "BLT MODE");
    lv_obj_set_style_text_color(ui_LabelWorkmode, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_font(ui_LabelWorkmode, &ui_font_BebasNeueFont24, 0);

    ui_line3 = lv_obj_create(ui_MainScreen);
    lv_obj_set_size(ui_line3, 186, 1);
    lv_obj_set_pos(ui_line3, 3, 13);
    lv_obj_set_align(ui_line3, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_line3, LV_OBJ_FLAG_SCROLLABLE);
}

void ui_MainScreen_set_work_mode(const char *mode)
{
    if (ui_LabelWorkmode != NULL) {
        lv_label_set_text(ui_LabelWorkmode, mode);
    }
}

void ui_MainScreen_screen_destroy(void)
{
    if (ui_MainScreen != NULL) {
        lv_obj_del(ui_MainScreen);
    }

    ui_MainScreen = NULL;
    ui_LabelTime = NULL;
    ui_LabelData = NULL;
    ui_LabelSecond = NULL;
    ui_LabelWeek = NULL;
    ui_LabelWorkmode = NULL;
    ui_line3 = NULL;
}
