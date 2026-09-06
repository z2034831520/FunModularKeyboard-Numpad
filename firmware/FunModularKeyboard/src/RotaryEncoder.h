#ifndef ROTARY_ENCODER_H
#define ROTARY_ENCODER_H

#include <Arduino.h>
#include <ESP32Encoder.h>
#include <OneButton.h>
#include <lvgl.h>
#include <driver/pcnt.h>
#include "LogManager.h"

// 定义编码器引脚
#define ENCODER_CLK 5
#define ENCODER_DT 21
#define ENCODER_SW GPIO_NUM_9

#define ROTATION_TIMEOUT 500 // 毫秒
#define ENCODER_STEP_THRESOLD 2

class RotaryEncoder
{
public:
    using EncoderCallback = std::function<void(uint8_t)>;
    RotaryEncoder();
    ~RotaryEncoder();

    void Begin();
    void Loop();
    void SetCallback(EncoderCallback cb);

private:
    ESP32Encoder encoder_;
    OneButton button_;
    EncoderCallback callback_;

    // 状态变量
    unsigned long lastRotationTime_{0};
    bool rotationActive_{false};
    int lastDirection_{0}; // 0=无方向, 1=右, -1=左
    int accumulatedSteps_{0};
    int lastValue_{0};
    bool encoderEnabled_{false};

    static void HandleClick(void *context);
    static void HandleDoubleClick(void *context);
    void CheckRotation();
};

#endif