#pragma once

#include <Arduino.h>
#include "Configuration.h"
#include "CodexProtocol.h"

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
    CodexStatus codex_status{CodexStatus::DISCONNECTED};
    CodexEffort codex_effort{CodexEffort::UNKNOWN};
    uint8_t codex_task_count{1};
    bool codex_effort_mode{false};
    DisplaySettingsInfo setting;
};

enum class MainCommand
{
    KEY_INPUT = 1,
    SETTING_UPDATE,
    SYSTEM_RESET,
    ASR_RECORDING_STATE,
    CODEX_STATUS_UPDATE,
};
