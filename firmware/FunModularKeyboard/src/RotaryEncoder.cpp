#include "RotaryEncoder.h"

RotaryEncoder::RotaryEncoder()
    : button_(ENCODER_SW, true), // 内部上拉
      callback_(nullptr)
{
}

RotaryEncoder::~RotaryEncoder()
{
}

void RotaryEncoder::Begin()
{

    // ///////////////////////////////////
    // pinMode(GPIO_NUM_19, OUTPUT);
    // digitalWrite(GPIO_NUM_19, LOW);
    // //////////////////////////////////

    // 初始化按钮回调 - 使用静态方法并传递this指针作为上下文
    button_.attachClick(HandleClick, this);
    button_.attachDoubleClick(HandleDoubleClick, this);

    if (ESP.getFreeHeap() < 8 * 1024)
    {
        encoderEnabled_ = false;
        LOG_WARNING("Log", "Skip rotary init due low heap: %u", ESP.getFreeHeap());
        return;
    }

    ESP32Encoder::isrServiceCpuCore = ISR_CORE_USE_DEFAULT;

    // 初始化编码器
    ESP32Encoder::useInternalWeakPullResistors = UP;
    encoder_.attachHalfQuad(ENCODER_CLK, ENCODER_DT);
    encoder_.setFilter(1023); // 设置滤波器值(0-1023)，值越大滤波效果越强
    encoder_.setCount(0);
    encoderEnabled_ = true;
}

void RotaryEncoder::Loop()
{
    button_.tick();
    if (encoderEnabled_)
    {
        CheckRotation();
    }
}

void RotaryEncoder::SetCallback(EncoderCallback cb)
{
    callback_ = cb;
}

void RotaryEncoder::CheckRotation()
{
    // int currentValue = encoder_.getCount();
    // if (currentValue != lastValue_) {
    //     if (callback_) {
    //         //LOG_DEBUG("Log","currentValue - lastEncoderValue_=%d",currentValue - lastValue_);
    //         if (currentValue - lastValue_) {
    //             //LOG_DEBUG("Log","CheckRotation LV_KEY_RIGHT");
    //             callback_(LV_KEY_RIGHT); // 顺时针旋转
    //         } else {
    //             //LOG_DEBUG("Log","CheckRotation LV_KEY_LEFT");
    //             callback_(LV_KEY_LEFT);  // 逆时针旋转
    //         }
    //     }
    //     lastValue_ = currentValue;
    // }

    int currentValue = encoder_.getCount();
    int delta = currentValue - lastValue_;
    unsigned long now = millis();

    if (delta != 0)
    {
        int currentDirection = (delta > 0) ? 1 : -1;
        accumulatedSteps_ += abs(delta);

        if (!rotationActive_ ||
            currentDirection != lastDirection_ ||
            (now - lastRotationTime_) > ROTATION_TIMEOUT ||
            accumulatedSteps_ >= ENCODER_STEP_THRESOLD)
        {
            if (callback_ && accumulatedSteps_ >= ENCODER_STEP_THRESOLD)
            {
                LOG_DEBUG("Log", "CheckRotation currentDirection=%d", currentDirection);
                callback_(currentDirection > 0 ? LV_KEY_RIGHT : LV_KEY_LEFT);
            }

            rotationActive_ = true;
            lastDirection_ = currentDirection;
            accumulatedSteps_ = 0;
        }

        lastRotationTime_ = now;
        lastValue_ = currentValue;
    }
    else if (rotationActive_ && (now - lastRotationTime_) > ROTATION_TIMEOUT)
    {
        rotationActive_ = false;
        accumulatedSteps_ = 0;
    }
}

// 静态点击处理函数
void RotaryEncoder::HandleClick(void *context)
{
    RotaryEncoder *self = static_cast<RotaryEncoder *>(context);
    if (self->callback_)
    {
        LOG_DEBUG("Log", "HandleClick LV_KEY_ENTER");
        self->callback_(LV_KEY_ENTER); // 单击
    }
}

// 静态双击处理函数
void RotaryEncoder::HandleDoubleClick(void *context)
{
    RotaryEncoder *self = static_cast<RotaryEncoder *>(context);
    if (self->callback_)
    {
        LOG_DEBUG("Log", "HandleClick LV_KEY_ESC");
        self->callback_(LV_KEY_ESC); // 双击
    }
}