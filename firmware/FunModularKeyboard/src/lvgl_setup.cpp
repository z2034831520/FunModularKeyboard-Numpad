#include "lvgl_setup.h"
#include <Arduino.h>
#include <lvgl.h>
#include <SPIFFS.h>
#include <TFT_eSPI.h> // 根据你的显示驱动修改

static TFT_eSPI tft; // 显示驱动实例

namespace {
#if defined(TFT_BL)
constexpr uint8_t kTftBacklightPwmChannel = 7;
// Keep the LEDC configuration inside the ESP32-S3 timing budget.
constexpr uint32_t kTftBacklightPwmFrequency = 5000;
constexpr uint8_t kTftBacklightPwmResolution = 12;
bool s_backlight_pwm_ready = false;
#endif
bool s_tft_ready = false;
bool s_spiffs_fs_registered = false;

String lvgl_spiffs_real_path(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return String("/");
    }
    if (path[0] == '/') {
        return String(path);
    }
    return String("/") + String(path);
}

void* lvgl_spiffs_open_cb(lv_fs_drv_t* drv, const char* path, lv_fs_mode_t mode) {
    LV_UNUSED(drv);
    const char* openMode = (mode == LV_FS_MODE_WR) ? FILE_WRITE : FILE_READ;
    File file = SPIFFS.open(lvgl_spiffs_real_path(path), openMode);
    if (!file) {
        return nullptr;
    }
    return new File(file);
}

lv_fs_res_t lvgl_spiffs_close_cb(lv_fs_drv_t* drv, void* file_p) {
    LV_UNUSED(drv);
    if (file_p == nullptr) {
        return LV_FS_RES_INV_PARAM;
    }
    File* file = static_cast<File*>(file_p);
    file->close();
    delete file;
    return LV_FS_RES_OK;
}

lv_fs_res_t lvgl_spiffs_read_cb(lv_fs_drv_t* drv, void* file_p, void* buf, uint32_t btr, uint32_t* br) {
    LV_UNUSED(drv);
    if (file_p == nullptr || buf == nullptr || br == nullptr) {
        return LV_FS_RES_INV_PARAM;
    }
    File* file = static_cast<File*>(file_p);
    *br = file->read(static_cast<uint8_t*>(buf), btr);
    return LV_FS_RES_OK;
}

lv_fs_res_t lvgl_spiffs_seek_cb(lv_fs_drv_t* drv, void* file_p, uint32_t pos, lv_fs_whence_t whence) {
    LV_UNUSED(drv);
    if (file_p == nullptr) {
        return LV_FS_RES_INV_PARAM;
    }
    File* file = static_cast<File*>(file_p);
    if (whence == LV_FS_SEEK_SET) {
        return file->seek(pos, SeekSet) ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
    }
    if (whence == LV_FS_SEEK_CUR) {
        return file->seek(file->position() + pos, SeekSet) ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
    }
    return file->seek(file->size() + pos, SeekSet) ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

lv_fs_res_t lvgl_spiffs_tell_cb(lv_fs_drv_t* drv, void* file_p, uint32_t* pos_p) {
    LV_UNUSED(drv);
    if (file_p == nullptr || pos_p == nullptr) {
        return LV_FS_RES_INV_PARAM;
    }
    File* file = static_cast<File*>(file_p);
    *pos_p = file->position();
    return LV_FS_RES_OK;
}

void lvgl_write_panel_brightness(uint8_t brightness_percent) {
    if (!s_tft_ready) {
        return;
    }

    const uint8_t level = (uint8_t)((uint32_t)brightness_percent * 255UL / 100UL);
    tft.startWrite();
    tft.writecommand(0x53);
    tft.writedata(level == 0 ? 0x24 : 0x2C);
    tft.writecommand(0x51);
    tft.writedata(level);
    tft.endWrite();
}
}

void lvgl_set_backlight_brightness(uint8_t brightness_percent) {
    const uint8_t clamped = brightness_percent > 100 ? 100 : brightness_percent;

    lvgl_write_panel_brightness(clamped);

#if defined(TFT_BL)
    const uint32_t max_duty = (1UL << kTftBacklightPwmResolution) - 1UL;
    const uint32_t duty = (uint32_t)clamped * max_duty / 100UL;
    const uint32_t applied_duty =
#if defined(TFT_BACKLIGHT_ON) && (TFT_BACKLIGHT_ON == LOW)
        max_duty - duty;
#else
        duty;
#endif

    if (!s_backlight_pwm_ready) {
        pinMode(TFT_BL, OUTPUT);
        const double configured_frequency = ledcSetup(kTftBacklightPwmChannel,
                                                      kTftBacklightPwmFrequency,
                                                      kTftBacklightPwmResolution);
        if (configured_frequency > 0.0) {
            ledcAttachPin(TFT_BL, kTftBacklightPwmChannel);
            s_backlight_pwm_ready = true;
        }
    }

    if (s_backlight_pwm_ready) {
        ledcWrite(kTftBacklightPwmChannel, applied_duty);
    } else {
        const uint8_t on_level =
#if defined(TFT_BACKLIGHT_ON)
            TFT_BACKLIGHT_ON;
#else
            HIGH;
#endif
        const uint8_t off_level = on_level == HIGH ? LOW : HIGH;
        digitalWrite(TFT_BL, clamped == 0 ? off_level : on_level);
    }
#else
    LV_UNUSED(clamped);
#endif
}

void lvgl_register_spiffs_fs() {
    if (s_spiffs_fs_registered) {
        return;
    }

    static lv_fs_drv_t fs_drv;
    lv_fs_drv_init(&fs_drv);
    fs_drv.letter = 'S';
    fs_drv.open_cb = lvgl_spiffs_open_cb;
    fs_drv.close_cb = lvgl_spiffs_close_cb;
    fs_drv.read_cb = lvgl_spiffs_read_cb;
    fs_drv.seek_cb = lvgl_spiffs_seek_cb;
    fs_drv.tell_cb = lvgl_spiffs_tell_cb;
    lv_fs_drv_register(&fs_drv);
    s_spiffs_fs_registered = true;
}

void lvgl_setup() {
    // 1. 初始化显示驱动
    tft.begin();
    tft.setRotation(1); // 根据需要调整
    s_tft_ready = true;
    lvgl_set_backlight_brightness(100);
    
    // 2. 初始化 LVGL
    lv_init();
    lvgl_register_spiffs_fs();
    
    // 3. 设置显示缓冲区
    static lv_color_t buf1[DISP_BUF_SIZE];
    static lv_color_t buf2[DISP_BUF_SIZE];
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, DISP_BUF_SIZE);
    
    // 4. 注册显示驱动
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &draw_buf;
    disp_drv.flush_cb = my_disp_flush; // 实现这个函数
    disp_drv.hor_res = 428; // 根据你的屏幕修改
    disp_drv.ver_res = 142;
    lv_disp_drv_register(&disp_drv);
    
    // 5. 如果需要触摸屏，注册输入设备
    // static lv_indev_drv_t indev_drv;
    // lv_indev_drv_init(&indev_drv);
    // indev_drv.type = LV_INDEV_TYPE_POINTER;
    // indev_drv.read_cb = my_touchpad_read; // 实现这个函数
    // lv_indev_drv_register(&indev_drv);
}

// 显示刷新回调函数
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)&color_p->full, w * h, true);
    tft.endWrite();
    
    lv_disp_flush_ready(disp);
}

// 触摸屏读取回调函数 (可选)
// bool my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
//     // 实现触摸屏读取逻辑
//     return false;
// }