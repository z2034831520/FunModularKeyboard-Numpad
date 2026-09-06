#pragma once
#include <Arduino.h>
#include "Audio.h"
#include "LogManager.h"

// MAX98357

#define I2S_BCLK 16
#define I2S_LRC 39
#define I2S_DOUT 38

class Speaker
{
public:
    Speaker();
    ~Speaker();

    void SetVolume(uint8_t vol);
    void PlayRemoteAudio(String path);
    void PlayLocalAudio(String path);
    bool TogglePauseResume();
    void Stop();
    bool IsRunning();
    uint32_t GetCurrentTime();
    uint32_t GetTotalPlayingTime();
    void Loop();

private:
    Audio _audio{false, 3, I2S_NUM_1};
};