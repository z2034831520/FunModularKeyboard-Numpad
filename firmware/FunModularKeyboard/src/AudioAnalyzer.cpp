#include "AudioAnalyzer.h"
#include <math.h>

AudioAnalyzer::AudioAnalyzer(uint32_t sampleRate)
    : sampleRate_(sampleRate), fft_(vReal_, vImag_, FFT_SIZE, sampleRate)
{
    memset(bands_, 0, sizeof(bands_));
}

void AudioAnalyzer::begin()
{
    // 初始化清零
    memset(vReal_, 0, sizeof(vReal_));
    memset(vImag_, 0, sizeof(vImag_));
}

void AudioAnalyzer::process(const int16_t *samples, size_t count)
{
    // 将 int16_t 转 float, 限制最大 FFT_SIZE
    size_t n = min(count, (size_t)FFT_SIZE);
    for (size_t i = 0; i < n; i++)
    {
        vReal_[i] = (float)samples[i];
        vImag_[i] = 0;
    }

    removeDC();
    applyWindow();
    computeFFT();
    computeBands();
}

void AudioAnalyzer::removeDC()
{
    // 去掉直流分量
    float mean = 0;
    for (int i = 0; i < FFT_SIZE; i++)
        mean += vReal_[i];
    mean /= FFT_SIZE;
    for (int i = 0; i < FFT_SIZE; i++)
        vReal_[i] -= mean;
}

void AudioAnalyzer::applyWindow()
{
    // Hanning 窗，减少低频泄露
    for (int i = 0; i < FFT_SIZE; i++)
    {
        vReal_[i] *= 0.5 * (1 - cos(2 * PI * i / (FFT_SIZE - 1)));
    }
}

void AudioAnalyzer::computeFFT()
{
    // 已经自己在 applyWindow() 做了窗，这里用矩形窗相当于不再额外加窗
    // fft_.windowing(FFT_WIN_TYP_RECTANGLE, FFT_FORWARD);
    fft_.compute(FFT_FORWARD);
    fft_.complexToMagnitude();
}

void AudioAnalyzer::computeBands()
{
    // 将 FFT 结果按 BANDS 分段
    int binsPerBand = (FFT_SIZE / 2) / BANDS;
    for (int i = 0; i < BANDS; i++)
    {
        float sum = 0;
        int start = i * binsPerBand;
        int end = start + binsPerBand;
        for (int j = start; j < end; j++)
        {
            sum += vReal_[j];
        }
        bands_[i] = (float)(sum / binsPerBand);
        bands_[i] *= 0.4f; /// 缩减幅度
    }

    // 可选：让第一个频带不爆高
    bands_[0] *= 0.1f;
    bands_[1] *= 0.1f;
    bands_[2] *= 0.1f;
}

const float *AudioAnalyzer::getBands() const
{
    return bands_;
}