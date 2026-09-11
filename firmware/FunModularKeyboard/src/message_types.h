#pragma once

#include <Arduino.h>
#include "Configuration.h"
#include "Mic.h"
#include "ui/ui_settings_types.h"

struct BatteryStatusInfo
{
    uint16_t voltage_mv{0};
    uint8_t percent{0};
};

// 定义显示任务需要的数据结构
struct DisplayMessage
{
    uint8_t type;
    uint8_t action;
    uint32_t key_value; // 所有按下按键状态值
    uint8_t active_profile{0};
    bool asr_recording{false};
    BatteryStatusInfo battery_status;
    ui_settings_snapshot_t setting;
    char profile_name[24]{0};
    char profile_icon[8]{0};
    char profile_icon_path[40]{0};
    char keymap_labels[16][24]{};
    // uint8_t workMode;       // 工作模式
    // float batteryVoltage;   // 电池电压
    // bool isConnected;       // 连接状态
    // 可以添加其他需要显示的字段...
    // struct {
    //     float spectrumBands[16];  // 支持最多16个频带
    //     uint8_t numBands;
    // };
};

// 定义主任务可能需要接收的消息类型
enum class MainCommand
{
    ACTION_INPUT = 1,
    KEY_INPUT,
    SETTING_UPDATE,
    // SPECTRUM_DISPLAY,
    SYSTEM_RESET,
    ASR_RECORDING_STATE,
    KEYMAP_PROFILE_UPDATE,
    BATTERY_STATUS_UPDATE,
};
