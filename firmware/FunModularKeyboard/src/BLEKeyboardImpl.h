#pragma once
#include "IKeyboard.h"
#include <memory>

class BleKeyboard;

class BLEKeyboardImpl : public IKeyboard
{
public:
    BLEKeyboardImpl();
    ~BLEKeyboardImpl() override = default;

    bool begin() override;
    void press(uint8_t key) override;
    void press(String key) override;
    void release(uint8_t key) override;
    void release(String key) override;
    void releaseAll() override;
    bool isConnected() override;
    void send() override;

private:
    std::unique_ptr<BleKeyboard> keyboard_;
    bool is_initialized_{false};
    bool last_connection_state_{false};
};