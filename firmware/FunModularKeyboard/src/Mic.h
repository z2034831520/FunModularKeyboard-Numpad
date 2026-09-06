#pragma once
#include <Arduino.h>
#include <driver/i2s.h>

#define MIC_I2S_BCLK 11
#define MIC_II2S_WS 17
#define MIC_II2S_DATA 18

// #define MIC_I2S_BCLK    4
// #define MIC_II2S_WS     3
// #define MIC_II2S_DATA   2

#define I2S_NUM I2S_NUM_0
#define SAMPLE_RATE 16000
#define BUFFER_SIZE 512

class Mic
{
public:
    Mic();
    ~Mic();

    void startDummyTX();
    bool Begin();
    size_t Read(int16_t *buffer, size_t samples_count);
    // size_t Read(int32_t* buffer, size_t samples_count);
    void End();
    bool Reset();

private:
    bool _initialized = false;
    i2s_config_t i2s_config_;
    i2s_pin_config_t pin_config_;
};