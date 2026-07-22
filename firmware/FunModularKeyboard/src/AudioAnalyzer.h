#pragma once
#include <Arduino.h>
#include <ArduinoFFT.h>

#define FFT_SIZE 512
#define BANDS    16

class AudioAnalyzer {
public:
    explicit AudioAnalyzer(uint32_t sampleRate);

    void begin();
    void process(const int16_t* samples, size_t count);
    const float* getBands() const;

private:
    uint32_t sampleRate_;

    float vReal_[FFT_SIZE];
    float vImag_[FFT_SIZE];
    float  bands_[BANDS];

    ArduinoFFT<float> fft_;

    void computeFFT();
    void computeBands();
    void applyWindow();
    void removeDC();
};
