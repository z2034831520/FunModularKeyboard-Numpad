#include "RotaryEncoder.h"

namespace
{
    constexpr uint8_t kEncoderClkPin = 5;
    constexpr uint8_t kEncoderDtPin = 21;
    constexpr uint8_t kEncoderSwitchPin = 9;
    constexpr int8_t kTransitionsPerDetent = 4;
    constexpr uint32_t kButtonDebounceMs = 25;

    // Quadrature state table: previous AB in bits 3:2, current AB in bits 1:0.
    constexpr int8_t kTransitionTable[16] = {
        0, 1, -1, 0,
        -1, 0, 0, 1,
        1, 0, 0, -1,
        0, -1, 1, 0};
}

void RotaryEncoder::Begin()
{
    pinMode(kEncoderClkPin, INPUT_PULLUP);
    pinMode(kEncoderDtPin, INPUT_PULLUP);
    pinMode(kEncoderSwitchPin, INPUT_PULLUP);

    lastRotationState_ = (static_cast<uint8_t>(digitalRead(kEncoderClkPin)) << 1) |
                         static_cast<uint8_t>(digitalRead(kEncoderDtPin));
    lastRawButtonPressed_ = digitalRead(kEncoderSwitchPin) == LOW;
    stableButtonPressed_ = lastRawButtonPressed_;
    buttonChangedAtMs_ = millis();
    rotationAccumulator_ = 0;
    initialized_ = true;
}

void RotaryEncoder::Loop()
{
    if (!initialized_)
    {
        return;
    }

    CheckRotation();
    CheckButton();
}

void RotaryEncoder::SetCallback(Callback callback, void *context)
{
    callback_ = callback;
    callbackContext_ = context;
}

void RotaryEncoder::CheckRotation()
{
    const uint8_t currentState = (static_cast<uint8_t>(digitalRead(kEncoderClkPin)) << 1) |
                                 static_cast<uint8_t>(digitalRead(kEncoderDtPin));
    if (currentState == lastRotationState_)
    {
        return;
    }

    const uint8_t transition = static_cast<uint8_t>((lastRotationState_ << 2) | currentState);
    lastRotationState_ = currentState;
    rotationAccumulator_ += kTransitionTable[transition];

    if (rotationAccumulator_ >= kTransitionsPerDetent)
    {
        rotationAccumulator_ = 0;
        Emit(RotaryAction::CLOCKWISE);
    }
    else if (rotationAccumulator_ <= -kTransitionsPerDetent)
    {
        rotationAccumulator_ = 0;
        Emit(RotaryAction::COUNTERCLOCKWISE);
    }
}

void RotaryEncoder::CheckButton()
{
    const bool rawPressed = digitalRead(kEncoderSwitchPin) == LOW;
    const uint32_t nowMs = millis();

    if (rawPressed != lastRawButtonPressed_)
    {
        lastRawButtonPressed_ = rawPressed;
        buttonChangedAtMs_ = nowMs;
    }

    if (rawPressed != stableButtonPressed_ &&
        static_cast<uint32_t>(nowMs - buttonChangedAtMs_) >= kButtonDebounceMs)
    {
        stableButtonPressed_ = rawPressed;
        if (stableButtonPressed_)
        {
            Emit(RotaryAction::CLICK);
        }
    }
}

void RotaryEncoder::Emit(RotaryAction action)
{
    if (callback_ != nullptr)
    {
        callback_(action, callbackContext_);
    }
}
