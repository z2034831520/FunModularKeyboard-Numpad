#pragma once
#include <Arduino.h>

class IKeyboard {
public:
    virtual ~IKeyboard() = default;
    virtual bool begin() = 0;
    virtual void press(uint8_t key) = 0;
    virtual void press(String key) = 0;
    virtual void release(uint8_t key) = 0;
    virtual void release(String key) = 0;
    virtual void releaseAll() = 0;
    virtual bool isConnected() = 0;
    virtual void send() = 0;
};