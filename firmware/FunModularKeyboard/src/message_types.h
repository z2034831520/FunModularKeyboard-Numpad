#pragma once

#include <Arduino.h>
#include "Configuration.h"

struct BatteryStatusInfo
{
    uint16_t voltage_mv{0};
    uint8_t percent{0};
};

// Only the settings consumed by the remaining time page and status/RGB logic.
struct DisplaySettingsInfo
{
    int32_t work_mode{0};
    int32_t rgb_mode{0};
    int32_t rgb_click_mode{0};
    int32_t rgb_brightness{0};
    int32_t tft_brightness{0};
    int32_t device_volume{0};
    char rgb_single_color[16]{0};
};

struct DisplayMessage
{
    uint8_t type{0};
    uint32_t key_value{0};
    bool asr_recording{false};
    BatteryStatusInfo battery_status;
    DisplaySettingsInfo setting;
};

enum class MainCommand
{
    KEY_INPUT = 1,
    SETTING_UPDATE,
    SYSTEM_RESET,
    ASR_RECORDING_STATE,
    BATTERY_STATUS_UPDATE,
};
