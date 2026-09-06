#pragma once
#include <Arduino.h>

#define PHYSICAL_KEY_NUM 16
#define PHYSICAL_KEY_ROW 5  // 修正为实际行数
#define PHYSICAL_KEY_COL 4  // 修正为实际列数
#define DEBOUNCE_TIME_MS 10 // 消抖时间(ms)

class MatrixScanner
{
    const uint8_t row_pins[PHYSICAL_KEY_ROW] = {48, 10, 47, 33, 14}; // 示例引脚
    const uint8_t col_pins[PHYSICAL_KEY_COL] = {35, 34, 7, 13};      // 示例引脚

public:
    MatrixScanner();
    ~MatrixScanner();

    uint32_t scan();            // 扫描并返回变化的状态位
    uint32_t getStableState();  // 获取当前稳定状态
    uint32_t getPressedKeys();  // 获取当前按下的键(消抖后)
    uint32_t getReleasedKeys(); // 获取当前释放的键(消抖后)

private:
    const uint8_t *row_pins_;
    const uint8_t *col_pins_;

    // 每个按键的状态机
    struct KeyState
    {
        enum State
        {
            IDLE,
            DEBOUNCE_PRESS,
            PRESSED,
            DEBOUNCE_RELEASE
        };
        State state;
        unsigned long timer;
    };

    KeyState key_states_[PHYSICAL_KEY_NUM]; // 每个按键独立状态机
    uint32_t stable_state_ = 0;             // 消抖后的稳定状态
    uint32_t pressed_keys_ = 0;             // 当前按下的键
    uint32_t released_keys_ = 0;            // 当前释放的键
};