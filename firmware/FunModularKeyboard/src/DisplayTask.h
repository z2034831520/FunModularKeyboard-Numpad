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
#include "Mic.h"
#include "AudioAnalyzer.h"

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
    void CheckWiFiStatus();
    void HexToRGB(const char *hexColor, uint8_t &r, uint8_t &g, uint8_t &b);
    bool isMusicSpectrumScreenActive() const;
    void updateSpectrumMicState(bool active);

    QueueHandle_t message_queue_;
    DisplayMessage last_message_;
    DisplayMessage msg_;
    RGBLightControl rgbLightControl_;
    ui_settings_snapshot_t disp_setting_;
    Mic mic_;
    AudioAnalyzer audioAnalyzer_{SAMPLE_RATE};
    uint16_t fftIndex_{0};
    bool spectrumMicActive_{false};
    int wifi_rssi_{-100};

    // RSSI (dBm)	信号质量	描述
    // -30 到 -50	优秀	信号极强
    // -50 到 -60	很好	信号强
    // -60 到 -70	好	信号良好
    // -70 到 -80	一般	信号可用
    // -80 以下	差	信号弱
};
