#include "ui_BootScreen.h"
#include "ui_MainScreen.h"

#define BOOT_GIF_PATH "S:/chatgpt_icon_rotate_80x80.gif"
#define BOOT_ANIMATION_TIMEOUT_MS 2600
#define BOOT_FADE_DURATION_MS 300

lv_obj_t *ui_BootScreen = NULL;
lv_obj_t *ui_BootGif = NULL;

static lv_timer_t *boot_timeout_timer = NULL;
static bool boot_finished = false;

static void ui_BootScreen_finish(void)
{
    if (boot_finished) {
        return;
    }
    boot_finished = true;

    if (boot_timeout_timer != NULL) {
        lv_timer_del(boot_timeout_timer);
        boot_timeout_timer = NULL;
    }

    if (ui_MainScreen != NULL) {
        lv_scr_load_anim(ui_MainScreen, LV_SCR_LOAD_ANIM_FADE_ON,
                         BOOT_FADE_DURATION_MS, 0, true);
    }

    /* The old screen is deleted by lv_scr_load_anim after the fade. */
    ui_BootScreen = NULL;
    ui_BootGif = NULL;
}

static void ui_BootScreen_gif_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_READY) {
        ui_BootScreen_finish();
    }
}

static void ui_BootScreen_timeout_cb(lv_timer_t *timer)
{
    /* Avoid deleting the currently running timer from ui_BootScreen_finish. */
    boot_timeout_timer = NULL;
    lv_timer_del(timer);
    ui_BootScreen_finish();
}

static bool ui_BootScreen_asset_exists(void)
{
    lv_fs_file_t file;
    if (lv_fs_open(&file, BOOT_GIF_PATH, LV_FS_MODE_RD) != LV_FS_RES_OK) {
        return false;
    }
    lv_fs_close(&file);
    return true;
}

void ui_BootScreen_screen_init(void)
{
    boot_finished = false;

    if (!ui_BootScreen_asset_exists()) {
        lv_disp_load_scr(ui_MainScreen);
        boot_finished = true;
        return;
    }

    ui_BootScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_BootScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_BootScreen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ui_BootScreen, LV_OPA_COVER, 0);

    ui_BootGif = lv_gif_create(ui_BootScreen);
    lv_gif_set_src(ui_BootGif, BOOT_GIF_PATH);
    lv_obj_center(ui_BootGif);
    lv_obj_add_event_cb(ui_BootGif, ui_BootScreen_gif_event_cb,
                        LV_EVENT_READY, NULL);

    lv_disp_load_scr(ui_BootScreen);
    boot_timeout_timer = lv_timer_create(ui_BootScreen_timeout_cb,
                                         BOOT_ANIMATION_TIMEOUT_MS, NULL);
    lv_timer_set_repeat_count(boot_timeout_timer, 1);
}

void ui_BootScreen_screen_destroy(void)
{
    if (boot_timeout_timer != NULL) {
        lv_timer_del(boot_timeout_timer);
        boot_timeout_timer = NULL;
    }

    if (ui_BootScreen != NULL) {
        lv_obj_del(ui_BootScreen);
    }
    ui_BootScreen = NULL;
    ui_BootGif = NULL;
    boot_finished = false;
}
