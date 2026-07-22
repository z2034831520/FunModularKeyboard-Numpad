#include "VoiceRecognizer.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>

#include "LogManager.h"

namespace {
constexpr size_t kAsrTextMaxLen = 256;
constexpr uint32_t kDefaultOneShotRecordMs = 3000;
constexpr uint32_t kTokenRetryCooldownMs = 3000;
constexpr uint32_t kMaxCaptureMs = 8000;

// ESP32-S3 在当前工程内存压力下，TLS握手容易失败，优先使用HTTP端点。
constexpr const char* kTokenUrlPrefix = "http://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials";
constexpr const char* kAsrUrl = "http://vop.baidu.com/server_api";

class ReadOnlyStringStream : public Stream {
public:
    explicit ReadOnlyStringStream(const String& data) : data_(data) {}

    int available() override {
        return static_cast<int>(data_.length() - offset_);
    }

    int read() override {
        if (offset_ >= data_.length()) {
            return -1;
        }
        return static_cast<unsigned char>(data_[offset_++]);
    }

    int peek() override {
        if (offset_ >= data_.length()) {
            return -1;
        }
        return static_cast<unsigned char>(data_[offset_]);
    }

    void flush() override {}

    size_t write(uint8_t) override {
        return 0;
    }

private:
    const String& data_;
    size_t offset_{0};
};
}

VoiceRecognizer::VoiceRecognizer() {}

VoiceRecognizer::~VoiceRecognizer() {
    mic_.End();
}

bool VoiceRecognizer::begin() {
    if (!mic_.Begin()) {
        LOG_ERROR("ASR", "Mic init failed");
        return false;
    }
    return true;
}

void VoiceRecognizer::setConfig(const Config& cfg) {
    config_ = cfg;
    if (config_.maxRecordMs > kMaxCaptureMs) {
        LOG_WARNING("ASR", "maxRecordMs too large (%u), clamp to %u", config_.maxRecordMs, kMaxCaptureMs);
        config_.maxRecordMs = kMaxCaptureMs;
    }
    accessToken_ = "";
    tokenExpireAtMs_ = 0;
    lastTokenFetchAttemptMs_ = 0;
    lastTokenFetchFailMs_ = 0;
}

bool VoiceRecognizer::recognizeOnce(String& outText) {
    outText = "";

    if (WiFi.status() != WL_CONNECTED) {
        LOG_ERROR("ASR", "WiFi is not connected");
        return false;
    }

    if (!ensureToken()) {
        LOG_ERROR("ASR", "Failed to get Baidu access token");
        return false;
    }

    std::vector<int16_t> pcm;
    if (!recordPcmForDuration(pcm, kDefaultOneShotRecordMs)) {
        LOG_ERROR("ASR", "Record PCM failed");
        return false;
    }

    String speechBase64;
    if (!pcmToBase64(pcm.data(), pcm.size(), speechBase64)) {
        LOG_ERROR("ASR", "PCM base64 encode failed");
        return false;
    }

    if (!requestAsr(accessToken_, speechBase64, pcm.size() * sizeof(int16_t), outText)) {
        LOG_ERROR("ASR", "ASR request failed");
        return false;
    }

    if (outText.length() > kAsrTextMaxLen) {
        outText = outText.substring(0, kAsrTextMaxLen);
    }

    LOG_INFO("ASR", "Recognized text: %s", outText.c_str());
    return true;
}

bool VoiceRecognizer::ensureToken() {
    const uint32_t now = millis();
    if (!accessToken_.isEmpty() && now < tokenExpireAtMs_) {
        return true;
    }

    if ((now - lastTokenFetchFailMs_) < kTokenRetryCooldownMs) {
        LOG_WARNING("ASR", "Token retry cooling down");
        return false;
    }

    lastTokenFetchAttemptMs_ = now;

    String token;
    uint32_t expiresInSec = 0;
    if (!fetchToken(token, expiresInSec)) {
        lastTokenFetchFailMs_ = now;
        return false;
    }

    accessToken_ = token;
    // 提前 60 秒刷新 token，避免边界过期。
    const uint32_t safeExpireSec = (expiresInSec > 60) ? (expiresInSec - 60) : expiresInSec;
    tokenExpireAtMs_ = now + safeExpireSec * 1000UL;
    return true;
}

bool VoiceRecognizer::fetchToken(String& token, uint32_t& expiresInSec) {
    token = "";
    expiresInSec = 0;

    if (config_.baiduApiKey.isEmpty() || config_.baiduSecretKey.isEmpty()) {
        LOG_ERROR("ASR", "Baidu API key/secret is empty");
        return false;
    }

    String url = String(kTokenUrlPrefix) +
                 "&client_id=" + config_.baiduApiKey +
                 "&client_secret=" + config_.baiduSecretKey;

    HTTPClient http;
    if (!http.begin(url)) {
        LOG_ERROR("ASR", "Token HTTP begin failed");
        return false;
    }
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setReuse(false);
    http.setTimeout(6000);

    int code = http.GET();
    String payload = http.getString();

    if (code != HTTP_CODE_OK) {
        if (payload.length() > 180) {
            payload = payload.substring(0, 180) + "...";
        }
        http.end();
        LOG_ERROR("ASR", "Token HTTP error: %d, payload=%s", code, payload.c_str());
        return false;
    }

    if (payload.length() == 0) {
        http.end();
        LOG_ERROR("ASR", "Token HTTP empty payload");
        return false;
    }

    StaticJsonDocument<128> filter;
    filter["access_token"] = true;
    filter["expires_in"] = true;
    filter["error"] = true;
    filter["error_description"] = true;

    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
    http.end();
    if (err) {
        LOG_ERROR("ASR", "Token JSON parse error: %s", err.c_str());
        return false;
    }

    if (!doc.containsKey("access_token") || !doc.containsKey("expires_in")) {
        String err = doc["error"] | "";
        String errDesc = doc["error_description"] | "";
        String payloadPreview = payload;
        if (payloadPreview.length() > 180) {
            payloadPreview = payloadPreview.substring(0, 180) + "...";
        }
        LOG_ERROR("ASR", "Token payload preview: %s", payloadPreview.c_str());
        LOG_ERROR("ASR", "Token response missing fields, error=%s, desc=%s", err.c_str(), errDesc.c_str());
        LOG_ERROR("ASR", "Token response missing fields");
        return false;
    }

    token = doc["access_token"].as<String>();
    expiresInSec = doc["expires_in"].as<uint32_t>();
    return !token.isEmpty() && expiresInSec > 0;
}

bool VoiceRecognizer::recordPcmForDuration(std::vector<int16_t>& pcm, uint32_t durationMs) {
    pcm.clear();

    const size_t targetSamples = (SAMPLE_RATE * durationMs) / 1000;
    pcm.reserve(targetSamples);

    int16_t buffer[BUFFER_SIZE] = {0};
    const uint32_t start = millis();
    while ((millis() - start) < durationMs) {
        size_t readSamples = mic_.Read(buffer, BUFFER_SIZE);
        if (readSamples == 0) {
            continue;
        }

        size_t remain = targetSamples - pcm.size();
        size_t pushCount = (readSamples < remain) ? readSamples : remain;
        pcm.insert(pcm.end(), buffer, buffer + pushCount);

        if (pcm.size() >= targetSamples) {
            break;
        }
    }

    LOG_INFO("ASR", "PCM samples recorded: %u", static_cast<unsigned>(pcm.size()));
    return !pcm.empty();
}

bool VoiceRecognizer::pcmToBase64(const int16_t* samples, size_t sampleCount, String& outBase64) {
    outBase64 = "";
    if (samples == nullptr || sampleCount == 0) {
        return false;
    }

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(samples);
    const size_t inputLen = sampleCount * sizeof(int16_t);
    const size_t outputCap = ((inputLen + 2) / 3) * 4 + 1;

    std::vector<unsigned char> encoded(outputCap, 0);
    size_t actualLen = 0;
    int ret = mbedtls_base64_encode(encoded.data(), encoded.size(), &actualLen, bytes, inputLen);
    if (ret != 0 || actualLen == 0) {
        return false;
    }

    outBase64.reserve(actualLen + 8);
    for (size_t i = 0; i < actualLen; ++i) {
        outBase64 += static_cast<char>(encoded[i]);
    }
    return true;
}

bool VoiceRecognizer::requestAsr(const String& token, const String& speechBase64, size_t pcmBytes, String& outText) {
    outText = "";

    HTTPClient http;
    if (!http.begin(kAsrUrl)) {
        LOG_ERROR("ASR", "ASR HTTP begin failed");
        return false;
    }
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setReuse(false);
    http.setTimeout(10000);

    http.addHeader("Content-Type", "application/json");
    String body;
    body.reserve(speechBase64.length() + 256);
    body = "{\"format\":\"pcm\",\"rate\":" + String(SAMPLE_RATE) +
           ",\"channel\":1,\"cuid\":\"" + config_.cuid +
           "\",\"token\":\"" + token +
           "\",\"dev_pid\":" + String(config_.devPid) +
           ",\"len\":" + String(static_cast<unsigned>(pcmBytes)) +
           ",\"speech\":\"" + speechBase64 + "\"}";

    ReadOnlyStringStream bodyStream(body);
    int code = http.sendRequest("POST", &bodyStream, body.length());
    String payload = http.getString();
    http.end();

    if (code != HTTP_CODE_OK) {
        LOG_ERROR("ASR", "ASR HTTP error: %d (%s), pcmBytes=%u, bodyBytes=%u, payload=%s",
                  code,
                  HTTPClient::errorToString(code).c_str(),
                  static_cast<unsigned>(pcmBytes),
                  static_cast<unsigned>(body.length()),
                  payload.c_str());
        return false;
    }

    DynamicJsonDocument resp(4096);
    DeserializationError err = deserializeJson(resp, payload);
    if (err) {
        LOG_ERROR("ASR", "ASR JSON parse error: %s", err.c_str());
        return false;
    }

    int errNo = resp["err_no"] | -1;
    if (errNo != 0) {
        const char* errMsg = resp["err_msg"] | "unknown";
        LOG_ERROR("ASR", "ASR err_no=%d, err_msg=%s", errNo, errMsg);
        return false;
    }

    if (!resp["result"].is<JsonArray>() || resp["result"].size() == 0) {
        LOG_ERROR("ASR", "ASR result empty");
        return false;
    }

    outText = resp["result"][0].as<String>();
    outText.trim();
    return !outText.isEmpty();
}

bool VoiceRecognizer::startCapture() {
    if (capturing_) {
        return true;
    }

    if (WiFi.status() != WL_CONNECTED) {
        LOG_ERROR("ASR", "WiFi is not connected");
        return false;
    }

    // 在内存还未被大块PCM占用前先拿token，降低鉴权失败概率。
    if (!ensureToken()) {
        LOG_ERROR("ASR", "Failed to get Baidu access token before capture");
        return false;
    }

    capturedPcm_.clear();
    const size_t reserveSamples = (SAMPLE_RATE * config_.maxRecordMs) / 1000;
    capturedPcm_.reserve(reserveSamples);
    captureStartMs_ = millis();
    capturing_ = true;
    LOG_INFO("ASR", "Voice capture started");
    return true;
}

void VoiceRecognizer::feedCapture() {
    if (!capturing_) {
        return;
    }

    const uint32_t elapsed = millis() - captureStartMs_;
    if (elapsed >= config_.maxRecordMs) {
        return;
    }

    int16_t buffer[BUFFER_SIZE] = {0};
    size_t readSamples = mic_.Read(buffer, BUFFER_SIZE);
    if (readSamples == 0) {
        return;
    }

    const size_t targetSamples = (SAMPLE_RATE * config_.maxRecordMs) / 1000;
    size_t remain = targetSamples - capturedPcm_.size();
    size_t pushCount = (readSamples < remain) ? readSamples : remain;
    capturedPcm_.insert(capturedPcm_.end(), buffer, buffer + pushCount);
}

bool VoiceRecognizer::finishCaptureAndRecognize(String& outText) {
    outText = "";
    if (!capturing_) {
        return false;
    }
    capturing_ = false;

    if (capturedPcm_.empty()) {
        LOG_WARNING("ASR", "No PCM captured");
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        LOG_ERROR("ASR", "WiFi is not connected");
        return false;
    }

    String speechBase64;
    if (!pcmToBase64(capturedPcm_.data(), capturedPcm_.size(), speechBase64)) {
        LOG_ERROR("ASR", "PCM base64 encode failed");
        return false;
    }

    if (!requestAsr(accessToken_, speechBase64, capturedPcm_.size() * sizeof(int16_t), outText)) {
        LOG_ERROR("ASR", "ASR request failed");
        return false;
    }

    if (outText.length() > kAsrTextMaxLen) {
        outText = outText.substring(0, kAsrTextMaxLen);
    }
    return !outText.isEmpty();
}

void VoiceRecognizer::suspend() {
    capturing_ = false;
    captureStartMs_ = 0;
    capturedPcm_.clear();
    mic_.End();
}

bool VoiceRecognizer::resume() {
    return mic_.Begin();
}
