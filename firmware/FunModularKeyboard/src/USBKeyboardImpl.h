#pragma once
#include "IKeyboard.h"
#include <memory>
#include <Arduino.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include <USBHIDConsumerControl.h>

class USBHIDKeyboard;
class USBHIDConsumerControl;

class USBKeyboardImpl : public IKeyboard
{
public:
    USBKeyboardImpl();
    ~USBKeyboardImpl() override = default;

    bool begin() override;
    void press(uint8_t key) override;
    void press(String key) override;
    void release(uint8_t key) override;
    void release(String key) override;
    void releaseAll() override;
    bool isConnected() override;
    void send() override;

private:
    std::unique_ptr<USBHIDKeyboard> keyboard_;
    std::unique_ptr<USBHIDConsumerControl> keyboard_meida_;
    bool is_initialized_{false};
    bool is_connected_{false};
    bool last_connection_state_{false};
};