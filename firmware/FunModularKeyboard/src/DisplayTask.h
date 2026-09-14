#pragma once
#include <Arduino.h>
#include "task.h"
#include "message_types.h"
// #include "screens/MenuManager.h"
#include "ui/ui.h"
#include "ui/ui_StatusBar.h"
#include "lvgl_setup.h"
#include "RGBLightControl.h"
#include "LogManager.h"

class DisplayTask : public Task<DisplayTask>
{
    friend class Task<DisplayTask>;

public:
    DisplayTask(const uint8_t task_core);
    ~DisplayTask();

    // 设置消息队列
    void setMessageQueue(QueueHandle_t queue)
    {
        message_queue_ = queue;
    }

protected:
    void run();

private:
    void UpdateDisplay(const DisplayMessage &msg);
    void UpdateConfiguredRgb();
    void UpdateCodexRgb(uint32_t now_ms);
    void ResetCodexRgbEffect(CodexStatus status);
    void CheckWiFiStatus();
    void HexToRGB(const char *hexColor, uint8_t &r, uint8_t &g, uint8_t &b);

    QueueHandle_t message_queue_;
    DisplayMessage last_message_;
    DisplayMessage msg_;
    RGBLightControl rgbLightControl_;
    DisplaySettingsInfo disp_setting_;
    CodexStatus codex_status_{CodexStatus::DISCONNECTED};
    uint32_t codex_effect_started_ms_{0};
    uint32_t codex_effect_last_step_ms_{0};
    uint8_t codex_effect_step_{0};
    bool codex_effect_on_{false};
    bool configured_rgb_needs_refresh_{true};
    int wifi_rssi_{-100};

    // RSSI (dBm)	信号质量	描述
    // -30 到 -50	优秀	信号极强
    // -50 到 -60	很好	信号强
    // -60 到 -70	好	信号良好
    // -70 到 -80	一般	信号可用
    // -80 以下	差	信号弱
};
