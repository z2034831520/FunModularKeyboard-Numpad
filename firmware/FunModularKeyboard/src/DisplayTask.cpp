#include "DisplayTask.h"
#include "SystemTime.h"
#include <WiFi.h>
#include <SPIFFS.h>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

extern "C"
{
    unsigned lodepng_decode32(unsigned char **out,
                              unsigned *w,
                              unsigned *h,
                              const unsigned char *in,
                              size_t insize);
    const char *lodepng_error_text(unsigned code);
}

// int32_t samples32_g[BUFFER_SIZE];
// int16_t samples16_g[BUFFER_SIZE];
int16_t samples[BUFFER_SIZE];

// // 设置单个数字图片
// void SetDigitImage(lv_obj_t *img, char digit) {
//     switch(digit) {
//         case '0':lv_img_set_src(img, &ui_img_zero_png);break;
//         case '1':lv_img_set_src(img, &ui_img_one_png);break;
//         case '2':lv_img_set_src(img, &ui_img_two_png);break;
//         case '3':lv_img_set_src(img, &ui_img_three_png);break;
//         case '4':lv_img_set_src(img, &ui_img_four_png);break;
//         case '5':lv_img_set_src(img, &ui_img_five_png);break;
//         case '6':lv_img_set_src(img, &ui_img_six_png);break;
//         case '7':lv_img_set_src(img, &ui_img_seven_png);break;
//         case '8':lv_img_set_src(img, &ui_img_eight_png);break;
//         case '9':lv_img_set_src(img, &ui_img_nine_png);break;
//         default:break;
//     }
// }

const char *weekdays[] = {
    "SUNDAY",    // tm_wday = 0
    "MONDAY",    // tm_wday = 1
    "TUESDAY",   // tm_wday = 2
    "WEDNESDAY", // tm_wday = 3
    "THURSDAY",  // tm_wday = 4
    "FRIDAY",    // tm_wday = 5
    "SATURDAY"   // tm_wday = 6
};

const char *months_uppercase_abbr[] = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

namespace
{
    struct DecodedProfileIconImage
    {
        std::unique_ptr<uint8_t[]> pixels;
        size_t size = 0;
        uint16_t width = 0;
        uint16_t height = 0;
    };

    struct IndexedSingleColorPreset
    {
        const char *id;
        const char *hex;
    };

    constexpr IndexedSingleColorPreset kIndexedSingleColorPresets[] = {
        {"0", "ff0000"},
        {"1", "ff4000"},
        {"2", "ff8000"},
        {"3", "ffbf00"},
        {"4", "ffff00"},
        {"5", "bfff00"},
        {"6", "80ff00"},
        {"7", "40ff00"},
        {"8", "00ff00"},
        {"9", "00ff40"},
        {"10", "00ff80"},
        {"11", "00ffbf"},
        {"12", "00ffff"},
        {"13", "00bfff"},
        {"14", "0080ff"},
        {"15", "0040ff"},
        {"16", "0000ff"},
        {"17", "4000ff"},
        {"18", "8000ff"},
        {"19", "bf00ff"},
        {"20", "ff00ff"},
        {"21", "ff00bf"},
        {"22", "ff0080"},
        {"23", "ff0040"}};

    const char *ResolveIndexedSingleColorHex(const char *value)
    {
        if (value == nullptr || *value == '\0')
        {
            return "ff0000";
        }

        char *end_ptr = nullptr;
        const long numeric_index = strtol(value, &end_ptr, 10);
        if (end_ptr != value && end_ptr != nullptr && *end_ptr == '\0')
        {
            const long max_index = (long)(sizeof(kIndexedSingleColorPresets) / sizeof(kIndexedSingleColorPresets[0])) - 1;
            if (numeric_index >= 0 && numeric_index <= max_index)
            {
                return kIndexedSingleColorPresets[numeric_index].hex;
            }
        }

        return value;
    }

    bool decodeProfileIconFromLvglPath(const char *lvglPath, DecodedProfileIconImage &decodedImage)
    {
        decodedImage = DecodedProfileIconImage{};

        if (lvglPath == nullptr || lvglPath[0] == '\0')
        {
            return false;
        }

        String spiffsPath = String(lvglPath);
        if (spiffsPath.startsWith("S:"))
        {
            spiffsPath = spiffsPath.substring(2);
        }
        if (!spiffsPath.startsWith("/"))
        {
            spiffsPath = String("/") + spiffsPath;
        }

        File file = SPIFFS.open(spiffsPath, FILE_READ);
        if (!file)
        {
            LOG_WARNING("KeymapUI", "failed to open icon png via SPIFFS path=%s", spiffsPath.c_str());
            return false;
        }

        const size_t pngSize = static_cast<size_t>(file.size());
        if (pngSize == 0)
        {
            file.close();
            LOG_WARNING("KeymapUI", "icon png empty path=%s", spiffsPath.c_str());
            return false;
        }

        std::unique_ptr<uint8_t[]> pngData(new uint8_t[pngSize]);
        const size_t expectedSize = pngSize;
        const size_t bytesRead = file.read(pngData.get(), pngSize);
        file.close();
        if (bytesRead != expectedSize)
        {
            LOG_WARNING("KeymapUI", "icon png read short path=%s read=%u expected=%u",
                        spiffsPath.c_str(),
                        static_cast<unsigned>(bytesRead),
                        static_cast<unsigned>(expectedSize));
            return false;
        }

        unsigned char *rgbaData = nullptr;
        unsigned decodedWidth = 0;
        unsigned decodedHeight = 0;
        const unsigned decodeResult = lodepng_decode32(&rgbaData,
                                                       &decodedWidth,
                                                       &decodedHeight,
                                                       pngData.get(),
                                                       pngSize);
        if (decodeResult != 0 || rgbaData == nullptr || decodedWidth == 0 || decodedHeight == 0)
        {
            LOG_WARNING("KeymapUI", "png decode failed path=%s error=%u message=%s",
                        spiffsPath.c_str(),
                        decodeResult,
                        lodepng_error_text(decodeResult));
            if (rgbaData != nullptr)
            {
                free(rgbaData);
            }
            return false;
        }

        const size_t pixelCount = static_cast<size_t>(decodedWidth) * static_cast<size_t>(decodedHeight);
        const size_t lvglImageSize = static_cast<size_t>(LV_IMG_BUF_SIZE_TRUE_COLOR_ALPHA(decodedWidth, decodedHeight));
        decodedImage.pixels.reset(new uint8_t[lvglImageSize]);
        decodedImage.size = lvglImageSize;
        decodedImage.width = static_cast<uint16_t>(decodedWidth);
        decodedImage.height = static_cast<uint16_t>(decodedHeight);

        for (size_t index = 0; index < pixelCount; ++index)
        {
            const uint8_t red = rgbaData[index * 4 + 0];
            const uint8_t green = rgbaData[index * 4 + 1];
            const uint8_t blue = rgbaData[index * 4 + 2];
            const uint8_t alpha = rgbaData[index * 4 + 3];
            const lv_color_t color = lv_color_make(red, green, blue);
            uint8_t *dest = decodedImage.pixels.get() + (index * LV_IMG_PX_SIZE_ALPHA_BYTE);
            memcpy(dest, &color, sizeof(lv_color_t));
            dest[LV_IMG_PX_SIZE_ALPHA_BYTE - 1] = alpha;
        }

        free(rgbaData);
        LOG_INFO("KeymapUI", "decoded icon path=%s png=%ux%u lvgl_bytes=%u",
                 spiffsPath.c_str(),
                 decodedWidth,
                 decodedHeight,
                 static_cast<unsigned>(decodedImage.size));
        return true;
    }
}

// 获取当前时间并格式化
bool GetCurrentTime(char *date_buf, char *week_buf, char *time_buf, char *second_buf)
{
    struct tm timeInfo{};
    if (!SystemTime::GetLocalTime(&timeInfo))
    {
        snprintf(date_buf, 12, "%s", "SYNCING");
        snprintf(week_buf, 12, "%s", "");
        snprintf(time_buf, 12, "%s", "--:--");
        snprintf(second_buf, 3, "%s", "--");
        return false;
    }

    // 格式化日期（年份后两位）-月-日
    // strftime(date_buf, date_size, "%m-%d", timeinfo);  // 示例: "23-08-20"
    snprintf(date_buf, 12, "%s %.2d", months_uppercase_abbr[timeInfo.tm_mon], timeInfo.tm_mday);

    // 格式化时间 时:分:秒
    // strftime(time_buf, time_size, "%H:%M:%S", timeinfo);  // 示例: "14:30:45"
    // strftime(time_buf, time_size, "%H:%M", timeinfo);  // 示例: "14:30"
    // strftime(second_buf, 3, "%S", timeinfo);  //秒
    snprintf(time_buf, 12, "%.2d:%.2d", timeInfo.tm_hour, timeInfo.tm_min);
    snprintf(second_buf, 3, "%.2d", timeInfo.tm_sec);

    // 周
    snprintf(week_buf, 12, "%s", weekdays[timeInfo.tm_wday]);
    return true;
}

// 定时器回调函数（每秒更新一次）
void UpdateTimeCb(lv_timer_t *timer)
{
    char date_buf[12] = {0};
    char time_buf[12]{0};
    char week_buf[12]{0};
    char second_buf[3]{0};

    GetCurrentTime(date_buf, week_buf, time_buf, second_buf);

    lv_label_set_text(ui_LabelData, date_buf);
    lv_label_set_text(ui_LabelWeek, week_buf);
    lv_label_set_text(ui_LabelTime, time_buf);
    lv_label_set_text(ui_LabelSecond, second_buf);

    // // 更新6个图片组件（分别对应时、分、秒的每一位）
    // SetDigitImage(ui_TimeImage1, time_buf[0]);  // 时的十位
    // SetDigitImage(ui_TimeImage2, time_buf[1]);  // 时的个位
    // SetDigitImage(ui_TimeImage3, time_buf[3]);  // 分的十位
    // SetDigitImage(ui_TimeImage4, time_buf[4]);  // 分的个位
    // SetDigitImage(ui_TimeImage5, time_buf[6]);  // 秒的十位
    // SetDigitImage(ui_TimeImage6, time_buf[7]);  // 秒的个位
}

void TimeShow()
{

    // lv_label_set_text(ui_LabelData, "00/00/00");
    // lv_obj_align(ui_LabelData, LV_ALIGN_TOP_LEFT, 10, 10);  // 左上角

    // 启动定时器（每秒更新一次）
    UpdateTimeCb(nullptr);
    lv_timer_create(UpdateTimeCb, 1000, NULL);
}

DisplayTask::DisplayTask(const uint8_t task_core)
    : Task{"DisplayTask", 8192, 1, task_core},
      message_queue_(nullptr)
{
}

DisplayTask::~DisplayTask() {}

bool DisplayTask::isMusicSpectrumScreenActive() const
{
    return ui_get_active_screen_tag() == UI_SCREEN_MUSIC_SECONDARY;
}

void DisplayTask::updateSpectrumMicState(bool active)
{
#if defined(ENABLE_VOICE_ASR_MAINTASK)
    if (active && !spectrumMicActive_)
    {
        if (!mic_.Begin())
        {
            LOG_WARNING("Display", "Music spectrum mic init failed");
            return;
        }
        for (int i = 0; i < 4; ++i)
        {
            mic_.Read(samples, BUFFER_SIZE);
            delay(5);
        }
        spectrumMicActive_ = true;
        LOG_INFO("Display", "Music spectrum mic acquired");
        return;
    }

    if (!active && spectrumMicActive_)
    {
        mic_.End();
        spectrumMicActive_ = false;
        fftIndex_ = 0;
        LOG_INFO("Display", "Music spectrum mic released");
    }
#else
    if (active)
    {
        spectrumMicActive_ = true;
    }
#endif
}

void DisplayTask::CheckWiFiStatus()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        wifi_rssi_ = WiFi.RSSI();
        // LOG_DEBUG("Log","已连接 | 信号: %d dBm | IP: %s",
        //              wifi_rssi_, WiFi.localIP().toString().c_str());
    }
    else
    {
        //  LOG_DEBUG("Log", "WiFi 已断开!");
        wifi_rssi_ = 0;
    }
}

void DisplayTask::HexToRGB(const char *hexColor, uint8_t &r, uint8_t &g, uint8_t &b)
{
    // 确保字符串以#开头（可选）
    String hex = ResolveIndexedSingleColorHex(hexColor);
    if (hex.startsWith("#"))
    {
        hex = hex.substring(1);
    }

    // 确保长度正确
    if (hex.length() >= 6)
    {
        // 解析十六进制字符串
        char *endptr;

        // 提取R分量
        String rStr = hex.substring(0, 2);
        r = (uint8_t)strtoul(rStr.c_str(), &endptr, 16);

        // 提取G分量
        String gStr = hex.substring(2, 4);
        g = (uint8_t)strtoul(gStr.c_str(), &endptr, 16);

        // 提取B分量
        String bStr = hex.substring(4, 6);
        b = (uint8_t)strtoul(bStr.c_str(), &endptr, 16);
    }
    else
    {
        // 默认值（红色）
        r = 0xFF;
        g = 0x44;
        b = 0x44;
    }
}

void DisplayTask::run()
{
    // LOG_DEBUG("Log", "Start DisplayTask!");
    // 初始化 LVGL、显示和触摸（需自己实现）
    lvgl_setup();
    // 加载 SquareLine Studio 生成的 UI
    ui_init();
    // 主界面时间显示
    TimeShow();

    // 状态栏接口测试
    status_bar_set_working_mode(WIRED_KEYBOARD_MODE);
    status_bar_set_recording_state(false);
    status_bar_set_volume(5);
    // Display an empty icon until the first real ADC reading arrives.
    status_bar_set_battery_level(0);
    status_bar_set_wifi_strength(-200);
    // 麦克风
#if !defined(ENABLE_VOICE_ASR_MAINTASK)
    delay(100);
    if (!mic_.Begin())
    {
        LOG_DEBUG("Log", "Mic Init Error!");
        /// delay(1000);
        /// ESP.restart();  // 重启设备
    }
    else
    {
        LOG_DEBUG("Log", "Mic Init Sucess!");
        spectrumMicActive_ = true;
    }
    delay(100);
    // 丢弃前几帧MIC数据（可能包含噪声）
    for (int i = 0; i < 10; i++)
    {
        mic_.Read(samples, BUFFER_SIZE);
        delay(10);
    }
#else
    LOG_INFO("ASR", "DisplayTask mic analyzer disabled (ENABLE_VOICE_ASR_MAINTASK)");
#endif

    // 初始化音频分析器
    audioAnalyzer_.begin();

    while (1)
    {
        const bool spectrumScreenActive = isMusicSpectrumScreenActive();
        updateSpectrumMicState(spectrumScreenActive);

        // 处理音频数据
        if (spectrumScreenActive && spectrumMicActive_)
        {
            size_t samples_read = mic_.Read(samples, BUFFER_SIZE);
            if (samples_read >= FFT_SIZE)
            {
                // //提取有效数据
                // for (int i = 0; i < BUFFER_SIZE; i++) {
                //     // INMP441: 有效数据在 [31:8]
                //     int32_t sample24 = samples32_g[i] >> 8;
                //     // 符号扩展到 32bit
                //     if (sample24 & 0x00800000) {
                //         sample24 |= 0xFF000000;
                //     }
                //     // 转为16bit
                //     samples16_g[i] = sample24 >> 8;
                // }

                // LOG_DEBUG("Log", "DATA start");
                // for (int i = 0; i < 64; i++) {
                //     LOG_DEBUG("Log","%d ",samples[i]);  // 示例：打印PCM数据
                // }
                // LOG_DEBUG("Log", "DATA end");

                // //检测数据是否异常
                // for (int i = 0; i < 32; i++) {
                //     if (samples[i] >= 20000) {
                //         LOG_ERROR("Log", "Error samples[%d]=%d", i, samples[i]);
                //         i2s_stop(I2S_NUM_0);
                //         delay(10);
                //         i2s_start(I2S_NUM_0);
                //         //mic_.Reset();
                //         break;
                //     }
                // }
                // 频谱显示
                audioAnalyzer_.process(samples, samples_read);
                fftIndex_ = 0;
                const float *bands = audioAnalyzer_.getBands();
                // LOG_DEBUG("Log", "bands start:");
                // for (int i = 0; i < BANDS; i++) {
                //     LOG_DEBUG("Log", "%.2f ", bands[i]);
                // }
                // LOG_DEBUG("Log", "bands end");
                ui_MusicScreen_drawAudioBandsCool(bands);
                // ui_MusicScreen_drawAudioBands(bands);
            }
        }

        // LOG_DEBUG("Log", "Free stack: %u bytes\n", uxTaskGetStackHighWaterMark(NULL));

        switch (disp_setting_.rgb_mode)
        {
        case Configuration::RGB_MODE::RGB_NONE_MODE:
        {
            break;
        }
        case Configuration::RGB_MODE::RGB_SINGLE_MODE:
        {
            uint8_t r, g, b;
            HexToRGB(disp_setting_.rgb_single_color, r, g, b);
            rgbLightControl_.SetAllLEDColor(r, g, b);
            break;
        }
        case Configuration::RGB_MODE::RGB_RAINBOW_MODE:
        {
            rgbLightControl_.Rainbow();
            break;
        }
        case Configuration::RGB_MODE::RGB_RAINBOWWARE_MODE:
        {
            rgbLightControl_.RainbowWave();
            break;
        }
        case Configuration::RGB_MODE::RGB_COLORCYCLE_MODE:
        {
            rgbLightControl_.ColorCycle();
            break;
        }
        case Configuration::RGB_MODE::RGB_METER_MODE:
        {
            rgbLightControl_.Meteor();
            break;
        }
        case Configuration::RGB_MODE::RGB_FIRE_MODE:
        {
            rgbLightControl_.Fire();
            break;
        }
        case Configuration::RGB_MODE::RGB_PULSE_MODE:
        {
            uint8_t r, g, b;
            HexToRGB(disp_setting_.rgb_single_color, r, g, b);
            rgbLightControl_.Pulse(r, g, b);
            break;
        }
        default:
            break;
        }

        static int wifi_check_count = 0;
        wifi_check_count++;
        // LOG_DEBUG("Log","wifi_check_count = %d",wifi_check_count);
        if (wifi_check_count > 30)
        { // 3s
            wifi_check_count = 0;
            CheckWiFiStatus();
            status_bar_set_wifi_strength(wifi_rssi_);
        }

        // 等待消息，超时设置为100ms以便定期刷新显示
        if (xQueueReceive(message_queue_, &msg_, pdMS_TO_TICKS(100)))
        {
            // 收到新消息，更新显示
            UpdateDisplay(msg_);
            last_message_ = msg_;
        }

        // menuManager_.Loop();
        lv_timer_handler(); // LVGL 主循环

        usleep(1000);
    }
}

void DisplayTask::UpdateDisplay(const DisplayMessage &msg)
{
    switch (MainCommand(msg.type))
    {
    case MainCommand::ACTION_INPUT:
    {
        // 编码器导航逻辑由各个 screen 的 LV_EVENT_KEY 统一处理。
        lv_obj_t *active_screen = lv_scr_act();
        if (active_screen)
        {
            lv_event_send(active_screen, LV_EVENT_KEY, (void *)(uint32_t)msg.action);
        }
        // LOG_DEBUG("Log","updateDisplay msg.action=%d",msg.action);
        break;
    }
    case MainCommand::KEY_INPUT:
    {
        if (disp_setting_.rgb_click_mode == Configuration::RGB_CLICK_MODE::CLICK_SINGLE_COLOR_MODE)
        {
            uint8_t r, g, b;
            HexToRGB(disp_setting_.rgb_single_color, r, g, b);
            uint32_t temp_key_value = msg.key_value;
            for (int i = 0; i < PHYSICAL_KEY_NUM; i++)
            {
                if (temp_key_value & 0x01)
                {
                    rgbLightControl_.SetLEDColor(i, r, g, b);
                }
                else
                {
                    rgbLightControl_.SetLEDColor(i, 0, 0, 0);
                }
                temp_key_value = temp_key_value >> 1;
            }
        }
        break;
    }
    case MainCommand::SETTING_UPDATE:
    {
        disp_setting_ = msg.setting;
        LOG_DEBUG("Log", "updateDisplay SETTING_UPDATE: rgb_single_color=%s", disp_setting_.rgb_single_color);
        LOG_DEBUG("Log", "updateDisplay SETTING_UPDATE: rgb_mode=%d", disp_setting_.rgb_mode);
        LOG_DEBUG("Log", "updateDisplay SETTING_UPDATE: rgb_click_mode=%d", disp_setting_.rgb_click_mode);
        LOG_DEBUG("Log", "updateDisplay SETTING_UPDATE: tft_brightness=%d", disp_setting_.tft_brightness);
        LOG_DEBUG("Log", "updateDisplay SETTING_UPDATE: rgb_brightness=%d", disp_setting_.rgb_brightness);
        LOG_DEBUG("Log", "updateDisplay SETTING_UPDATE: work_mode=%d", disp_setting_.work_mode);
        LOG_DEBUG("Log", "updateDisplay SETTING_UPDATE: device_volume=%d", disp_setting_.device_volume);

        // 清除下LED上次遗留的显示
        if (disp_setting_.rgb_mode == Configuration::RGB_MODE::RGB_NONE_MODE)
        {
            rgbLightControl_.TurnOffAllLED();
        }

        // 更新图标状态
        status_bar_set_working_mode(disp_setting_.work_mode);
        status_bar_set_volume(disp_setting_.device_volume);

        // RGB LED亮度设置
        rgbLightControl_.SetBrightness(disp_setting_.rgb_brightness);

        // TFT 背光亮度设置
        lvgl_set_backlight_brightness((uint8_t)disp_setting_.tft_brightness);

        // 更新工作模式文字部分
        switch (disp_setting_.work_mode)
        {
        case WIRED_KEYBOARD_MODE:
        {
            ui_MainScreen_set_work_mode("WIR MODE");
            break;
        }
        case BLUETOOTH_KEYBOARD_MODE:
        {
            ui_MainScreen_set_work_mode("BLT MODE");
            break;
        }
        case WIRELESS_2_4G_KEYBOARD_MODE:
        {
            ui_MainScreen_set_work_mode("2.4 MODE");
            break;
        }
        }

        // 更新RGB和TFT亮度显示
        char light[16] = {0};
        sprintf(light, "RGB_LIGHT:%d%", disp_setting_.rgb_brightness);
        ui_MainScreen_set_rgb_light(light);
        sprintf(light, "TFT_LIGHT:%d%", disp_setting_.tft_brightness);
        ui_MainScreen_set_tft_light(light);
        ui_SettingScreenSecondary_set_snapshot(&disp_setting_);

        break;
    }
    case MainCommand::ASR_RECORDING_STATE:
    {
        status_bar_set_recording_state(msg.asr_recording);
        break;
    }

    case MainCommand::BATTERY_STATUS_UPDATE:
    {
        status_bar_set_battery_level(msg.battery_status.percent);
        LOG_DEBUG("Battery", "Display voltage: %u mV, level: %u%%",
                  (unsigned)msg.battery_status.voltage_mv,
                  (unsigned)msg.battery_status.percent);
        break;
    }

    case MainCommand::PC_STATUS_UPDATE:
    {
        char hostBuf[48] = {0};
        char timeBuf[48] = {0};
        char upBuf[48] = {0};
        char downBuf[48] = {0};
        char cpuBuf[48] = {0};
        char cpuTempBuf[48] = {0};
        char memBuf[48] = {0};
        char diskIoBuf[48] = {0};

        snprintf(hostBuf, sizeof(hostBuf), "Locks: C%s N%s S%s",
                 msg.pc_status.caps_lock ? "ON" : "--",
                 msg.pc_status.num_lock ? "ON" : "--",
                 msg.pc_status.scroll_lock ? "ON" : "--");
        snprintf(timeBuf, sizeof(timeBuf), "Network: %s", msg.pc_status.network_connected ? "ONLINE" : "OFFLINE");

        if (msg.pc_status.network_up_kbps >= 0.0f)
        {
            snprintf(upBuf, sizeof(upBuf), "Net Up: %.1f Kbps", msg.pc_status.network_up_kbps);
        }
        else
        {
            snprintf(upBuf, sizeof(upBuf), "Net Up: -- Kbps");
        }

        if (msg.pc_status.network_down_kbps >= 0.0f)
        {
            snprintf(downBuf, sizeof(downBuf), "Net Down: %.1f Kbps", msg.pc_status.network_down_kbps);
        }
        else
        {
            snprintf(downBuf, sizeof(downBuf), "Net Down: -- Kbps");
        }

        if (msg.pc_status.cpu_usage_percent >= 0.0f)
        {
            snprintf(cpuBuf, sizeof(cpuBuf), "CPU: %.1f%%", msg.pc_status.cpu_usage_percent);
        }
        else
        {
            snprintf(cpuBuf, sizeof(cpuBuf), "CPU: --");
        }

        if (msg.pc_status.memory_usage_percent >= 0.0f)
        {
            snprintf(memBuf, sizeof(memBuf), "MEM: %.1f%%", msg.pc_status.memory_usage_percent);
        }
        else
        {
            snprintf(memBuf, sizeof(memBuf), "MEM: --");
        }

        if (msg.pc_status.cpu_temp_c >= 0.0f)
        {
            snprintf(cpuTempBuf, sizeof(cpuTempBuf), "CPU Temp: %.1fC", msg.pc_status.cpu_temp_c);
        }
        else
        {
            snprintf(cpuTempBuf, sizeof(cpuTempBuf), "CPU Temp: N/A");
        }

        if (msg.pc_status.disk_io_percent >= 0.0f)
        {
            snprintf(diskIoBuf, sizeof(diskIoBuf), "Disk IO: %.1f%%", msg.pc_status.disk_io_percent);
        }
        else
        {
            snprintf(diskIoBuf, sizeof(diskIoBuf), "Disk IO: --");
        }

        ui_PcStatusScreen_set_host_status(hostBuf);
        ui_PcStatusScreen_set_time_status(timeBuf);
        ui_PcStatusScreen_set_lock_status(upBuf);
        ui_PcStatusScreen_set_network_status(downBuf);
        ui_PcStatusScreen_set_power_status(cpuBuf);
        ui_PcStatusScreen_set_cpu_temp_status(cpuTempBuf);
        ui_PcStatusScreen_set_perf_status(memBuf);
        ui_PcStatusScreen_set_temp_status(diskIoBuf);
        break;
    }

    case MainCommand::HOST_CONNECTION_UPDATE:
    {
        ui_MainScreen_set_host_connection(msg.host_connected);
        break;
    }

    case MainCommand::MUSIC_PLAYER_UPDATE:
    {
        ui_MusicScreenSecondary_set_player_state(msg.music_player.title,
                                                 msg.music_player.artist,
                                                 msg.music_player.player_name,
                                                 msg.music_player.lyric_current,
                                                 msg.music_player.lyric_next,
                                                 msg.music_player.connected,
                                                 msg.music_player.is_playing,
                                                 msg.music_player.is_paused,
                                                 msg.music_player.can_prev,
                                                 msg.music_player.can_next,
                                                 msg.music_player.current_seconds,
                                                 msg.music_player.total_seconds);
        break;
    }

    case MainCommand::KEYMAP_PROFILE_UPDATE:
    {
        char fileName[32] = {0};
        DecodedProfileIconImage decodedIcon;
        snprintf(fileName, sizeof(fileName), "config_profile_%u.ini", msg.active_profile);
        if (msg.profile_icon_path[0] != '\0')
        {
            lv_img_header_t header;
            const lv_res_t infoResult = lv_img_decoder_get_info(msg.profile_icon_path, &header);
            LOG_INFO("KeymapUI", "display profile=%u icon_src=%s decoder_result=%d size=%ux%u cf=%u",
                     (unsigned)(msg.active_profile + 1),
                     msg.profile_icon_path,
                     (int)infoResult,
                     (unsigned)header.w,
                     (unsigned)header.h,
                     (unsigned)header.cf);
            decodeProfileIconFromLvglPath(msg.profile_icon_path, decodedIcon);
        }
        else
        {
            LOG_INFO("KeymapUI", "display profile=%u icon_src=<fallback:%s>",
                     (unsigned)(msg.active_profile + 1),
                     msg.profile_icon);
        }
        ui_KeyMapped_set_profile_icon_image_data(decodedIcon.pixels.get(),
                                                 decodedIcon.size,
                                                 decodedIcon.width,
                                                 decodedIcon.height,
                                                 msg.profile_icon);
        ui_KeyMappedSecondary_set_profile(msg.profile_icon, msg.profile_name, fileName);
        ui_KeyMappedSecondary_set_profile_icon_image_data(decodedIcon.pixels.get(),
                                                          decodedIcon.size,
                                                          decodedIcon.width,
                                                          decodedIcon.height,
                                                          msg.profile_icon);
        for (unsigned int i = 0; i < 16; ++i)
        {
            ui_KeyMappedSecondary_set_key_label(i, msg.keymap_labels[i]);
        }
        if (ui_get_active_screen_tag() == UI_SCREEN_KEYMAPPED || ui_get_active_screen_tag() == UI_SCREEN_KEYMAPPED_SECONDARY)
        {
            lv_refr_now(NULL);
        }
        break;
    }

    default:
        break;
    }
}
