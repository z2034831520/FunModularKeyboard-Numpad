#pragma once

#include <Arduino.h>

enum class RotaryAction : uint8_t
{
    CLOCKWISE,
    COUNTERCLOCKWISE,
    CLICK,
};

class RotaryEncoder
{
public:
    using Callback = void (*)(RotaryAction action, void *context);

    void Begin();
    void Loop();
    void SetCallback(Callback callback, void *context);

private:
    void CheckRotation();
    void CheckButton();
    void Emit(RotaryAction action);

    Callback callback_{nullptr};
    void *callbackContext_{nullptr};
    uint8_t lastRotationState_{0};
    int8_t rotationAccumulator_{0};
    bool lastRawButtonPressed_{false};
    bool stableButtonPressed_{false};
    uint32_t buttonChangedAtMs_{0};
    bool initialized_{false};
};
