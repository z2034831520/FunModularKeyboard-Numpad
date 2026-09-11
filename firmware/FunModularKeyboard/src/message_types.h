#pragma once

#include <Arduino.h>
#include "Configuration.h"
#include "Mic.h"
#include "ui/ui_settings_types.h"

struct MusicPlayerInfo
{
    bool connected{false};
    bool is_playing{false};
    bool is_paused{false};
    bool can_prev{false};
    bool can_next{false};
    uint16_t current_seconds{0};
    uint16_t total_seconds{0};
    char title[96]{0};
    char artist[64]{0};
    char player_name[32]{0};
    char lyric_current[160]{0};
    char lyric_next[160]{0};
};

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
    bool host_connected{false};
    MusicPlayerInfo music_player;
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
    HOST_CONNECTION_UPDATE,
    MUSIC_PLAYER_UPDATE,
    KEYMAP_PROFILE_UPDATE,
    BATTERY_STATUS_UPDATE,
};
