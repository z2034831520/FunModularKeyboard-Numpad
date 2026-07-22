#pragma once

#include <Arduino.h>
#include <vector>
#include "Mic.h"

class VoiceRecognizer {
public:
    struct Config {
        String baiduApiKey;
        String baiduSecretKey;
        String cuid{"FunModularKeyboard"};
        int devPid{1537};
        uint32_t maxRecordMs{6000};
    };

    VoiceRecognizer();
    ~VoiceRecognizer();

    bool begin();
    void setConfig(const Config& cfg);
    bool recognizeOnce(String& outText);
    bool startCapture();
    void feedCapture();
    bool finishCaptureAndRecognize(String& outText);
    void suspend();
    bool resume();
    bool isCapturing() const { return capturing_; }

private:
    bool ensureToken();
    bool fetchToken(String& token, uint32_t& expiresInSec);
    bool recordPcmForDuration(std::vector<int16_t>& pcm, uint32_t durationMs);
    bool pcmToBase64(const int16_t* samples, size_t sampleCount, String& outBase64);
    bool requestAsr(const String& token, const String& speechBase64, size_t pcmBytes, String& outText);

private:
    Mic mic_;
    Config config_;
    String accessToken_;
    uint32_t tokenExpireAtMs_{0};
    uint32_t lastTokenFetchAttemptMs_{0};
    uint32_t lastTokenFetchFailMs_{0};
    bool capturing_{false};
    uint32_t captureStartMs_{0};
    std::vector<int16_t> capturedPcm_;
};
