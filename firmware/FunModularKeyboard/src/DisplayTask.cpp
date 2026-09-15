#include "DisplayTask.h"
#include "SystemTime.h"
#include <WiFi.h>

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

    const char *CodexEffortText(CodexEffort effort)
    {
        switch (effort)
        {
        case CodexEffort::EFFORT_MINIMAL:
            return "MINIMAL";
        case CodexEffort::EFFORT_LOW:
            return "LOW";
        case CodexEffort::EFFORT_MEDIUM:
            return "MEDIUM";
        case CodexEffort::EFFORT_HIGH:
            return "HIGH";
        case CodexEffort::EFFORT_XHIGH:
            return "XHIGH";
        case CodexEffort::EFFORT_MAX:
            return "MAX";
        case CodexEffort::EFFORT_ULTRA:
            return "ULTRA";
        case CodexEffort::UNKNOWN:
        default:
            return "";
        }
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

void DisplayTask::ResetCodexRgbEffect(CodexStatus status)
{
    codex_status_ = status;
    codex_effect_started_ms_ = millis();
    codex_effect_last_step_ms_ = 0;
    codex_effect_step_ = 0;
    codex_effect_on_ = false;

    if (status == CodexStatus::DISCONNECTED)
    {
        configured_rgb_needs_refresh_ = true;
    }
}

void DisplayTask::UpdateConfiguredRgb()
{
    switch (disp_setting_.rgb_mode)
    {
    case Configuration::RGB_MODE::RGB_NONE_MODE:
        if (configured_rgb_needs_refresh_)
        {
            rgbLightControl_.TurnOffAllLED();
        }
        break;

    case Configuration::RGB_MODE::RGB_SINGLE_MODE:
    {
        uint8_t r, g, b;
        HexToRGB(disp_setting_.rgb_single_color, r, g, b);
        rgbLightControl_.SetAllLEDColor(r, g, b);
        break;
    }

    case Configuration::RGB_MODE::RGB_RAINBOW_MODE:
        rgbLightControl_.Rainbow();
        break;

    case Configuration::RGB_MODE::RGB_RAINBOWWARE_MODE:
        rgbLightControl_.RainbowWave();
        break;

    case Configuration::RGB_MODE::RGB_COLORCYCLE_MODE:
        rgbLightControl_.ColorCycle();
        break;

    case Configuration::RGB_MODE::RGB_METER_MODE:
        rgbLightControl_.Meteor();
        break;

    case Configuration::RGB_MODE::RGB_FIRE_MODE:
        rgbLightControl_.Fire();
        break;

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

    configured_rgb_needs_refresh_ = false;
}

void DisplayTask::UpdateCodexRgb(uint32_t now_ms)
{
    switch (codex_status_)
    {
    case CodexStatus::READY:
    {
        if (codex_effect_last_step_ms_ != 0 && now_ms - codex_effect_last_step_ms_ < 30)
        {
            return;
        }
        codex_effect_last_step_ms_ = now_ms;

        const uint16_t phase = ((now_ms - codex_effect_started_ms_) / 8U) & 0xFFU;
        const uint16_t wave = phase < 128U ? phase * 2U : (255U - phase) * 2U;
        const uint8_t level = (uint8_t)(24U + wave * 96U / 255U);
        rgbLightControl_.SetAllLEDColor(0, level, (uint8_t)(level + 60U));
        break;
    }

    case CodexStatus::CREATING:
    case CodexStatus::RUNNING:
    case CodexStatus::PAUSING:
    case CodexStatus::RESUMING:
    {
        if (codex_effect_last_step_ms_ != 0 && now_ms - codex_effect_last_step_ms_ < 60)
        {
            return;
        }
        codex_effect_last_step_ms_ = now_ms;

        for (uint8_t i = 0; i < NUM_LEDS; ++i)
        {
            rgbLightControl_.TurnOffLED(i);
        }

        const uint8_t lead = codex_effect_step_ % NUM_LEDS;
        rgbLightControl_.SetLED(lead, CRGB(30, 120, 255));
        rgbLightControl_.SetLED((lead + NUM_LEDS - 1) % NUM_LEDS, CRGB(0, 45, 120));
        rgbLightControl_.SetLED((lead + NUM_LEDS - 2) % NUM_LEDS, CRGB(0, 15, 45));
        rgbLightControl_.Show();
        codex_effect_step_ = (codex_effect_step_ + 1) % NUM_LEDS;
        break;
    }

    case CodexStatus::WAITING_APPROVAL:
        if (codex_effect_last_step_ms_ == 0 || now_ms - codex_effect_last_step_ms_ >= 400)
        {
            codex_effect_last_step_ms_ = now_ms;
            codex_effect_on_ = !codex_effect_on_;
            if (codex_effect_on_)
            {
                rgbLightControl_.SetAllLEDColor(255, 100, 0);
            }
            else
            {
                rgbLightControl_.TurnOffAllLED();
            }
        }
        break;

    case CodexStatus::WAITING_INPUT:
    {
        if (codex_effect_last_step_ms_ != 0 && now_ms - codex_effect_last_step_ms_ < 30)
        {
            return;
        }
        codex_effect_last_step_ms_ = now_ms;

        const uint16_t phase = ((now_ms - codex_effect_started_ms_) / 16U) & 0xFFU;
        const uint16_t wave = phase < 128U ? phase * 2U : (255U - phase) * 2U;
        const uint8_t level = (uint8_t)(18U + wave * 82U / 255U);
        rgbLightControl_.SetAllLEDColor(level, 0, (uint8_t)(level + 80U));
        break;
    }

    case CodexStatus::PAUSED:
    {
        const uint16_t phase = (uint16_t)((now_ms - codex_effect_started_ms_) % 1600U);
        const bool should_be_on = phase < 120U || (phase >= 240U && phase < 360U);
        if (codex_effect_last_step_ms_ == 0 || should_be_on != codex_effect_on_)
        {
            codex_effect_last_step_ms_ = now_ms;
            codex_effect_on_ = should_be_on;
            if (should_be_on)
            {
                rgbLightControl_.SetAllLEDColor(255, 160, 0);
            }
            else
            {
                rgbLightControl_.TurnOffAllLED();
            }
        }
        break;
    }

    case CodexStatus::DONE:
        if (codex_effect_step_ == 0)
        {
            rgbLightControl_.SetAllLEDColor(0, 180, 40);
            codex_effect_step_ = 1;
        }
        break;

    case CodexStatus::ERROR:
        if (codex_effect_last_step_ms_ == 0 || now_ms - codex_effect_last_step_ms_ >= 180)
        {
            codex_effect_last_step_ms_ = now_ms;
            codex_effect_on_ = !codex_effect_on_;
            if (codex_effect_on_)
            {
                rgbLightControl_.SetAllLEDColor(255, 0, 0);
            }
            else
            {
                rgbLightControl_.TurnOffAllLED();
            }
        }
        break;

    case CodexStatus::DISCONNECTED:
    default:
        break;
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
    // Display an unknown value until the first real ADC reading arrives.
    status_bar_set_battery_level(255);
    status_bar_set_wifi_strength(-200);
    // Draw the boot screen and the first GIF frame before revealing the LCD.
    lv_refr_now(nullptr);
    lvgl_enable_panel_output();
    lvgl_set_backlight_brightness(100);
    // 麦克风
    while (1)
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
                // LOG_DEBUG("Log", "bands start:");
                // for (int i = 0; i < BANDS; i++) {
                //     LOG_DEBUG("Log", "%.2f ", bands[i]);
                // }
                // LOG_DEBUG("Log", "bands end");
        // LOG_DEBUG("Log", "Free stack: %u bytes\n", uxTaskGetStackHighWaterMark(NULL));

        if (codex_status_ == CodexStatus::DISCONNECTED)
        {
            UpdateConfiguredRgb();
        }
        else
        {
            UpdateCodexRgb(millis());
        }

        static uint32_t last_wifi_check_ms = 0;
        const uint32_t now_ms = millis();
        if (now_ms - last_wifi_check_ms >= 3000)
        {
            last_wifi_check_ms = now_ms;
            CheckWiFiStatus();
            status_bar_set_wifi_strength(wifi_rssi_);
        }

        // A short timeout keeps LVGL and non-blocking RGB effects responsive.
        if (xQueueReceive(message_queue_, &msg_, pdMS_TO_TICKS(20)))
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
    case MainCommand::KEY_INPUT:
    {
        if (codex_status_ == CodexStatus::DISCONNECTED &&
            disp_setting_.rgb_click_mode == Configuration::RGB_CLICK_MODE::CLICK_SINGLE_COLOR_MODE)
        {
            uint8_t r, g, b;
            HexToRGB(disp_setting_.rgb_single_color, r, g, b);
            uint32_t temp_key_value = msg.key_value;
            for (int i = 0; i < PHYSICAL_KEY_NUM; i++)
            {
                if (temp_key_value & 0x01)
                {
                    rgbLightControl_.SetLED(i, CRGB(r, g, b));
                }
                else
                {
                    rgbLightControl_.TurnOffLED(i);
                }
                temp_key_value = temp_key_value >> 1;
            }
            rgbLightControl_.Show();
        }
        break;
    }
    case MainCommand::SETTING_UPDATE:
    {
        disp_setting_ = msg.setting;
        configured_rgb_needs_refresh_ = true;
        LOG_DEBUG("Log", "updateDisplay SETTING_UPDATE: rgb_single_color=%s", disp_setting_.rgb_single_color);
        LOG_DEBUG("Log", "updateDisplay SETTING_UPDATE: rgb_mode=%d", disp_setting_.rgb_mode);
        LOG_DEBUG("Log", "updateDisplay SETTING_UPDATE: rgb_click_mode=%d", disp_setting_.rgb_click_mode);
        LOG_DEBUG("Log", "updateDisplay SETTING_UPDATE: tft_brightness=%d", disp_setting_.tft_brightness);
        LOG_DEBUG("Log", "updateDisplay SETTING_UPDATE: rgb_brightness=%d", disp_setting_.rgb_brightness);
        LOG_DEBUG("Log", "updateDisplay SETTING_UPDATE: work_mode=%d", disp_setting_.work_mode);
        LOG_DEBUG("Log", "updateDisplay SETTING_UPDATE: device_volume=%d", disp_setting_.device_volume);

        // 清除下LED上次遗留的显示
        if (codex_status_ == CodexStatus::DISCONNECTED &&
            disp_setting_.rgb_mode == Configuration::RGB_MODE::RGB_NONE_MODE)
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

    case MainCommand::CODEX_STATUS_UPDATE:
    {
        const unsigned task_count = msg.codex_task_count == 0 ? 1U : msg.codex_task_count;
        const char *effort_text = CodexEffortText(msg.codex_effort);
        char status_text[32]{};

        if (codex_status_ != msg.codex_status)
        {
            ResetCodexRgbEffect(msg.codex_status);
        }

        switch (msg.codex_status)
        {
        case CodexStatus::READY:
            snprintf(status_text, sizeof(status_text), "CODEX READY%s%s", effort_text[0] ? " " : "", effort_text);
            ui_MainScreen_set_codex_status(status_text, 0x55D6E8);
            break;
        case CodexStatus::CREATING:
            snprintf(status_text, sizeof(status_text), "CREATING%s%s", effort_text[0] ? " " : "", effort_text);
            ui_MainScreen_set_codex_status(status_text, 0x4A90E2);
            break;
        case CodexStatus::RUNNING:
            snprintf(status_text, sizeof(status_text), "RUNNING x%u%s%s", task_count, effort_text[0] ? " " : "", effort_text);
            ui_MainScreen_set_codex_status(status_text, 0x4A90E2);
            break;
        case CodexStatus::WAITING_APPROVAL:
            snprintf(status_text, sizeof(status_text), "APPROVAL x%u%s%s", task_count, effort_text[0] ? " " : "", effort_text);
            ui_MainScreen_set_codex_status(status_text, 0xFFAA33);
            break;
        case CodexStatus::WAITING_INPUT:
            snprintf(status_text, sizeof(status_text), "WAITING x%u%s%s", task_count, effort_text[0] ? " " : "", effort_text);
            ui_MainScreen_set_codex_status(status_text, 0xB266FF);
            break;
        case CodexStatus::PAUSING:
            snprintf(status_text, sizeof(status_text), "PAUSING%s%s", effort_text[0] ? " " : "", effort_text);
            ui_MainScreen_set_codex_status(status_text, 0xFFC040);
            break;
        case CodexStatus::PAUSED:
            snprintf(status_text, sizeof(status_text), "PAUSED x%u%s%s", task_count, effort_text[0] ? " " : "", effort_text);
            ui_MainScreen_set_codex_status(status_text, 0xFFC040);
            break;
        case CodexStatus::RESUMING:
            snprintf(status_text, sizeof(status_text), "RESUMING%s%s", effort_text[0] ? " " : "", effort_text);
            ui_MainScreen_set_codex_status(status_text, 0x4A90E2);
            break;
        case CodexStatus::DONE:
            ui_MainScreen_set_codex_status("CODEX DONE", 0x55CC77);
            break;
        case CodexStatus::ERROR:
            ui_MainScreen_set_codex_status("CODEX ERROR", 0xFF5555);
            break;
        case CodexStatus::DISCONNECTED:
        default:
            ui_MainScreen_set_codex_status("CODEX OFF", 0x808080);
            break;
        }
        break;
    }

    default:
        break;
    }
}
