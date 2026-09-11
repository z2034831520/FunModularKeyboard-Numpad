#include "MainTask.h"
#include "BLEKeyboardImpl.h"
#include "USBKeyboardImpl.h"
#include "KeycodeCodec.h"
#include "SystemTime.h"
#include "ui/ui.h"
#include "ui/ui_MusicScreenSecondary.h"
#include <ctype.h>
#include <time.h>

#include <WiFi.h>
#include <WiFiUdp.h>
#include <mbedtls/base64.h>
#include "esp_attr.h"
#include "esp_bt.h"
#include "esp_heap_caps.h"
#include "lwip/apps/sntp.h"

#define configCHECK_FOR_STACK_OVERFLOW 2 // 启用 FreeRTOS 堆栈溢出检测

/**************************************************************************/
// 通过网络获取时间
// NTP 配置
RTC_DATA_ATTR uint32_t g_bleTimeSyncRestartMarker = 0;

namespace
{
    constexpr const char *kVoiceTriggerFunctionKey = "KEY_FUNCTION_ASR";
    constexpr uint8_t kBoost5VEnablePin = 3;
    constexpr uint32_t kSettingsUiKey1Bit = (1UL << 0);
    constexpr uint32_t kSettingsUiKey2Bit = (1UL << 1);
    constexpr uint32_t kSettingsUiOverrideMask = kSettingsUiKey1Bit | kSettingsUiKey2Bit;
    constexpr uint32_t kWifiRetryIntervalMs = 5000;
    constexpr uint32_t kWifiConnectTimeoutMs = 10000;
    constexpr uint32_t kTimeSyncCheckIntervalMs = 500;
    constexpr uint32_t kTimeSyncRestartIntervalMs = 30000;
    constexpr uint32_t kBluetoothStartupTimeSyncTimeoutMs = 10000;
    constexpr uint32_t kBleTimeSyncRestartMagic = 0x54494D45; // "TIME"
    constexpr const char *kNtpServerPrimary = "0.cn.pool.ntp.org";
    constexpr const char *kNtpServerSecondary = "1.cn.pool.ntp.org";
    constexpr const char *kNtpServerFallback = "pool.ntp.org";
    constexpr const char *kProfileIcons[CONFIG_PROFILE_COUNT] = {
        LV_SYMBOL_HOME,
        LV_SYMBOL_AUDIO,
        LV_SYMBOL_EDIT,
        LV_SYMBOL_SETTINGS,
        LV_SYMBOL_DIRECTORY,
        LV_SYMBOL_IMAGE,
        LV_SYMBOL_BELL,
        LV_SYMBOL_WIFI};

    String normalizeFunctionKey(String key)
    {
        key.trim();
        if (key.startsWith("F:"))
        {
            key = key.substring(2);
            key.trim();
        }
        return key;
    }

    String buildKeySequenceString(const uint8_t *keys, uint8_t count)
    {
        return buildNamedKeySequence(keys, count);
    }

    bool hasNonAsciiUtf8(const String &text)
    {
        for (size_t i = 0; i < text.length(); ++i)
        {
            if (static_cast<uint8_t>(text[i]) > 0x7F)
            {
                return true;
            }
        }
        return false;
    }

    uint32_t readBigEndian32(const uint8_t *bytes)
    {
        return (static_cast<uint32_t>(bytes[0]) << 24U) |
               (static_cast<uint32_t>(bytes[1]) << 16U) |
               (static_cast<uint32_t>(bytes[2]) << 8U) |
               static_cast<uint32_t>(bytes[3]);
    }

    bool isPng48x48(const uint8_t *bytes, size_t length)
    {
        static constexpr uint8_t kPngSignature[8] = {
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

        if (bytes == nullptr || length < 24)
        {
            return false;
        }
        if (memcmp(bytes, kPngSignature, sizeof(kPngSignature)) != 0)
        {
            return false;
        }
        if (memcmp(bytes + 12, "IHDR", 4) != 0)
        {
            return false;
        }

        const uint32_t width = readBigEndian32(bytes + 16);
        const uint32_t height = readBigEndian32(bytes + 20);
        return width == 48 && height == 48;
    }

    void copyUtf8Truncated(char *destination, size_t destinationSize, const String &source)
    {
        if (destination == nullptr || destinationSize == 0)
        {
            return;
        }

        const char *input = source.c_str();
        const size_t inputLength = strlen(input);
        size_t inputIndex = 0;
        size_t outputIndex = 0;

        while (inputIndex < inputLength && outputIndex < destinationSize - 1)
        {
            const uint8_t leadByte = static_cast<uint8_t>(input[inputIndex]);
            size_t charLength = 1;

            if ((leadByte & 0x80U) == 0x00U)
            {
                charLength = 1;
            }
            else if ((leadByte & 0xE0U) == 0xC0U)
            {
                charLength = 2;
            }
            else if ((leadByte & 0xF0U) == 0xE0U)
            {
                charLength = 3;
            }
            else if ((leadByte & 0xF8U) == 0xF0U)
            {
                charLength = 4;
            }

            if (inputIndex + charLength > inputLength || outputIndex + charLength > destinationSize - 1)
            {
                break;
            }

            memcpy(destination + outputIndex, input + inputIndex, charLength);
            outputIndex += charLength;
            inputIndex += charLength;
        }

        destination[outputIndex] = '\0';
    }

    const char *wifiStatusToText(wl_status_t status)
    {
        switch (status)
        {
        case WL_IDLE_STATUS:
            return "IDLE";
        case WL_NO_SSID_AVAIL:
            return "NO_SSID";
        case WL_SCAN_COMPLETED:
            return "SCAN_COMPLETED";
        case WL_CONNECTED:
            return "CONNECTED";
        case WL_CONNECT_FAILED:
            return "CONNECT_FAILED";
        case WL_CONNECTION_LOST:
            return "CONNECTION_LOST";
        case WL_DISCONNECTED:
            return "DISCONNECTED";
        case WL_NO_SHIELD:
            return "NO_SHIELD";
        default:
            return "UNKNOWN";
        }
    }

    void logHeapSnapshot(const char *stage)
    {
        LOG_INFO("Heap", "%s | free=%u min=%u largest=%u psram=%u",
                 stage,
                 ESP.getFreeHeap(),
                 ESP.getMinFreeHeap(),
                 heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                 ESP.getFreePsram());
    }

    String simplifyDisplayToken(const String &token)
    {
        String result = token;
        result.trim();
        if (result.startsWith("KEY_MEDIA_"))
        {
            return result.substring(String("KEY_MEDIA_").length());
        }
        if (result.startsWith("KEY_"))
        {
            return result.substring(String("KEY_").length());
        }
        if (result.startsWith("NUM_") && result.length() == 5)
        {
            return result.substring(4);
        }
        if (result.length() == 1)
        {
            result.toUpperCase();
        }
        return result;
    }

    struct PendingUiSettingsRequest
    {
        bool pending{false};
        bool persist{false};
        ui_settings_snapshot_t snapshot{};
    };

    portMUX_TYPE g_ui_settings_lock = portMUX_INITIALIZER_UNLOCKED;
    PendingUiSettingsRequest g_ui_settings_request{};
    MainTask *g_main_task = nullptr;
}

extern "C" bool ui_settings_request_apply(const ui_settings_snapshot_t *snapshot)
{
    if (snapshot == nullptr || g_main_task == nullptr)
    {
        return false;
    }

    taskENTER_CRITICAL(&g_ui_settings_lock);
    g_ui_settings_request.snapshot = *snapshot;
    g_ui_settings_request.pending = true;
    g_ui_settings_request.persist = false;
    taskEXIT_CRITICAL(&g_ui_settings_lock);
    return true;
}

extern "C" bool ui_settings_request_save(const ui_settings_snapshot_t *snapshot)
{
    if (snapshot == nullptr || g_main_task == nullptr)
    {
        return false;
    }

    taskENTER_CRITICAL(&g_ui_settings_lock);
    g_ui_settings_request.snapshot = *snapshot;
    g_ui_settings_request.pending = true;
    g_ui_settings_request.persist = true;
    taskEXIT_CRITICAL(&g_ui_settings_lock);
    return true;
}

/**************************************************************************/

bool MainTask::hasMappedOutput(const KeyMapping &mapping) const
{
    return !mapping.function_key.isEmpty() || mapping.normal_key_count > 0 || mapping.macros_key_count > 0;
}

void MainTask::reportPhysicalKeyEdges(uint32_t edgeMask, bool pressed)
{
    if (edgeMask == 0)
    {
        return;
    }

    for (int i = 0; i < PHYSICAL_KEY_NUM; ++i)
    {
        if ((edgeMask & (1UL << i)) == 0)
        {
            continue;
        }

        protocol_.sendInputActivity(i + 1, "", pressed);
    }
}

void MainTask::triggerMappedInput(const KeyMapping &mapping)
{
    if (!currentKeyboard_ || !hasMappedOutput(mapping))
    {
        return;
    }

    for (uint8_t i = 0; i < mapping.macros_key_count; ++i)
    {
        currentKeyboard_->press(mapping.macros_key[i]);
    }

    bool functionPressed = false;
    if (!mapping.function_key.isEmpty() && !mapping.function_key.equals(kVoiceTriggerFunctionKey))
    {
        currentKeyboard_->press(mapping.function_key);
        functionPressed = true;
    }
    else
    {
        for (uint8_t i = 0; i < mapping.normal_key_count; ++i)
        {
            currentKeyboard_->press(mapping.normal_key[i]);
        }
    }

    if (functionPressed)
    {
        currentKeyboard_->release(mapping.function_key);
    }
    else
    {
        for (int i = mapping.normal_key_count - 1; i >= 0; --i)
        {
            currentKeyboard_->release(mapping.normal_key[i]);
        }
    }

    for (int i = mapping.macros_key_count - 1; i >= 0; --i)
    {
        currentKeyboard_->release(mapping.macros_key[i]);
    }
}

// 连接 WiFi
bool MainTask::ConnectToWiFi(const String &ssid, const String &password)
{
    String ssidTrimmed = ssid;
    String passwordTrimmed = password;
    ssidTrimmed.trim();
    passwordTrimmed.trim();

    LOG_DEBUG("Log", "start connect WiFi:");
    LOG_DEBUG("Log", ssidTrimmed);

    if (ssidTrimmed.isEmpty())
    {
        LOG_ERROR("Log", "WiFi SSID is empty");
        wifiReconnectActive_ = false;
        return false;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        LOG_DEBUG("Log", "WiFi already connected");
        wifiReconnectActive_ = true;
        wifiConnectAttemptStartedMs_ = 0;
        wifiNextRetryAtMs_ = 0;
        return true;
    }

    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    delay(20);

    const wl_status_t beginStatus = WiFi.begin(ssidTrimmed.c_str(), passwordTrimmed.c_str());
    wifiReconnectActive_ = true;
    wifiConnectAttemptStartedMs_ = millis();
    wifiNextRetryAtMs_ = wifiConnectAttemptStartedMs_ + kWifiRetryIntervalMs;

    if (beginStatus == WL_CONNECT_FAILED || beginStatus == WL_NO_SHIELD)
    {
        LOG_ERROR("Log", "WiFi begin failed immediately, status=%s(%d)", wifiStatusToText(beginStatus), beginStatus);
        WiFi.disconnect(false, false);
        wifiConnectAttemptStartedMs_ = 0;
        return false;
    }

    LOG_INFO("Log", "WiFi connect attempt started: ssid=%s", ssidTrimmed.c_str());
    return true;
}

void MainTask::scheduleWiFiConnectAttempt(bool immediate)
{
    if (!configuration_.settings_.wifi_switch)
    {
        return;
    }

    String ssid = configuration_.settings_.wifi_ssid;
    ssid.trim();
    if (ssid.isEmpty())
    {
        LOG_WARNING("Log", "WiFi switch enabled but SSID is empty, skip reconnect scheduling");
        wifiReconnectActive_ = false;
        wifiConnectAttemptStartedMs_ = 0;
        wifiNextRetryAtMs_ = 0;
        return;
    }

    wifiReconnectActive_ = true;
    wifiConnectAttemptStartedMs_ = 0;
    wifiNextRetryAtMs_ = immediate ? 0 : millis() + kWifiRetryIntervalMs;
}

void MainTask::stopWiFiReconnect()
{
    wifiReconnectActive_ = false;
    wifiWasConnected_ = false;
    wifiConnectAttemptStartedMs_ = 0;
    wifiNextRetryAtMs_ = 0;
    timeSyncPending_ = false;
    timeSyncStartedMs_ = 0;
    timeSyncNextCheckMs_ = 0;
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    protocol_.disableTcpClient();
}

void MainTask::onWiFiConnected()
{
    LOG_INFO("Log", "WiFi connected, IP=%s, RSSI=%d", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    wifiConnectAttemptStartedMs_ = 0;
    wifiNextRetryAtMs_ = 0;
    updateProtocolTcpEndpoint();
    StartTimeSync();
    reconcileVoiceRuntimeState();
}

void MainTask::processWiFiReconnect(uint32_t nowMs)
{
    constexpr uint32_t kTcpRecoveryWifiResetMs = 15000;

    if (!configuration_.settings_.wifi_switch || currentWorkMode_ == Configuration::BLUETOOTH_KEYBOARD_MODE)
    {
        if (wifiWasConnected_ || WiFi.getMode() != WIFI_OFF)
        {
            stopWiFiReconnect();
            if (currentWorkMode_ == Configuration::BLUETOOTH_KEYBOARD_MODE)
            {
                LOG_WARNING("Log", "WiFi disabled in BLE mode due memory limits");
            }
        }
        return;
    }

    const wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED)
    {
        if (!wifiWasConnected_)
        {
            wifiWasConnected_ = true;
            tcpDisconnectedSinceMs_ = 0;
            onWiFiConnected();
        }

        if (configuration_.settings_.connect_host)
        {
            if (protocol_.isTcpConnected())
            {
                tcpDisconnectedSinceMs_ = 0;
            }
            else if (tcpDisconnectedSinceMs_ == 0)
            {
                tcpDisconnectedSinceMs_ = nowMs;
            }
            else if (nowMs - tcpDisconnectedSinceMs_ >= kTcpRecoveryWifiResetMs)
            {
                LOG_WARNING("Log", "TCP remained disconnected for %u ms, forcing WiFi reconnect", kTcpRecoveryWifiResetMs);
                protocol_.disableTcpClient();
                WiFi.disconnect(false, false);
                wifiWasConnected_ = false;
                wifiConnectAttemptStartedMs_ = 0;
                wifiNextRetryAtMs_ = nowMs + kWifiRetryIntervalMs;
                tcpDisconnectedSinceMs_ = 0;
            }
        }
        else
        {
            tcpDisconnectedSinceMs_ = 0;
        }
        return;
    }

    if (wifiWasConnected_)
    {
        wifiWasConnected_ = false;
        protocol_.disableTcpClient();
        tcpDisconnectedSinceMs_ = 0;
        reconcileVoiceRuntimeState();
        LOG_WARNING("Log", "WiFi disconnected, status=%s(%d)", wifiStatusToText(status), status);
    }

    if (!wifiReconnectActive_)
    {
        return;
    }

    if (wifiConnectAttemptStartedMs_ != 0)
    {
        if (status == WL_CONNECT_FAILED || status == WL_NO_SHIELD || status == WL_NO_SSID_AVAIL ||
            nowMs - wifiConnectAttemptStartedMs_ >= kWifiConnectTimeoutMs)
        {
            LOG_WARNING("Log", "WiFi attempt failed, status=%s(%d)", wifiStatusToText(status), status);
            WiFi.disconnect(false, false);
            wifiConnectAttemptStartedMs_ = 0;
            wifiNextRetryAtMs_ = nowMs + kWifiRetryIntervalMs;
        }
        return;
    }

    if (nowMs < wifiNextRetryAtMs_)
    {
        return;
    }

    if (!ConnectToWiFi(configuration_.settings_.wifi_ssid, configuration_.settings_.wifi_password))
    {
        wifiNextRetryAtMs_ = nowMs + kWifiRetryIntervalMs;
    }
}

void MainTask::handleKeyEvent(uint32_t key_value)
{
    if (!currentKeyboard_)
    {
        LOG_ERROR("Log", "No keyboard initialized");
        return;
    }

    // 检查键盘连接状态
    if (!currentKeyboard_->isConnected())
    {
        static uint32_t last_warning_time = 0;
        uint32_t current_time = millis();

        // 限制警告频率，避免日志刷屏
        if (current_time - last_warning_time > 5000)
        {
            if (currentWorkMode_ == Configuration::WIRED_KEYBOARD_MODE)
            {
                LOG_WARNING("Log", "USB keyboard not connected to host - keys ignored");
            }
            else if (currentWorkMode_ == Configuration::BLUETOOTH_KEYBOARD_MODE)
            {
                LOG_WARNING("Log", "BLE keyboard not connected to host - keys ignored");
            }
            last_warning_time = current_time;
        }
        return;
    }

    uint32_t temp_key_value = key_value;

    // 同步给LED进行点击显示
    SendDisplayKeyInput(key_value);

    // 先释放所有按键，然后按新按下的键
    currentKeyboard_->releaseAll();

    // 主键盘按键处理
    for (int i = 0; i < PHYSICAL_KEY_NUM; i++)
    {
        if (temp_key_value & 0x01)
        {
            // 按下按键
            KeyMapping mapping = configuration_.getKeyMapping(i + 1);
            // 先处理宏键
            if (mapping.macros_key_count > 0)
            {
                for (int n = 0; n < mapping.macros_key_count; n++)
                {
                    currentKeyboard_->press(mapping.macros_key[n]);
                    LOG_DEBUG("Log", "[Keyboard] Set Macros Key 0x%02X", mapping.macros_key[n]);
                }
            }

            // 再处理普通键
            if (mapping.function_key.isEmpty())
            {
                if (mapping.normal_key_count > 0)
                {
                    for (int n = 0; n < mapping.normal_key_count; n++)
                    {
                        currentKeyboard_->press(mapping.normal_key[n]);
                        LOG_DEBUG("Log", "[Keyboard] Set Normal Key 0x%02X", mapping.normal_key[n]);
                    }
                }
            }
            else
            {
                if (!mapping.function_key.equals(kVoiceTriggerFunctionKey))
                {
                    currentKeyboard_->press(mapping.function_key);
                    LOG_DEBUG("Log", "[Keyboard] Set Function Key %s", mapping.function_key.c_str());
                }
            }
        }
        temp_key_value = temp_key_value >> 1;
    }

    LOG_DEBUG("Log", "Key event processed successfully (key_value: 0x%08X)", key_value);
}

void MainTask::updateVoiceTriggerBitFromKeymap()
{
    uint32_t triggerMask = 0;
    for (int i = 0; i < PHYSICAL_KEY_NUM; ++i)
    {
        KeyMapping km = configuration_.getKeyMapping(i + 1);
        String functionKey = normalizeFunctionKey(km.function_key);
        if (!functionKey.isEmpty())
        {
            LOG_DEBUG("ASR", "Key%d function=%s", i + 1, functionKey.c_str());
        }
        if (functionKey.equalsIgnoreCase(kVoiceTriggerFunctionKey))
        {
            triggerMask |= (1UL << i);
        }
    }

    voiceTriggerBit_ = triggerMask;
    if (voiceTriggerBit_ != 0)
    {
        LOG_INFO("ASR", "Voice trigger source: %s (mask=0x%08X)", kVoiceTriggerFunctionKey, voiceTriggerBit_);
    }
    else
    {
        LOG_WARNING("ASR", "No %s mapping found in first %d physical keys, ASR trigger disabled",
                    kVoiceTriggerFunctionKey,
                    PHYSICAL_KEY_NUM);
    }
}

void MainTask::applyVoiceConfig()
{
    updateVoiceTriggerBitFromKeymap();

    VoiceRecognizer::Config cfg;
    cfg.baiduApiKey = configuration_.settings_.voice_baidu_api_key;
    cfg.baiduSecretKey = configuration_.settings_.voice_baidu_secret_key;
    cfg.cuid = configuration_.settings_.voice_cuid;
    cfg.devPid = configuration_.settings_.voice_dev_pid;
    int cfgMs = configuration_.settings_.voice_max_record_ms;
    if (cfgMs < 1000)
        cfgMs = 1000;
    if (cfgMs > 8000)
        cfgMs = 8000;
    cfg.maxRecordMs = static_cast<uint32_t>(cfgMs);
    voiceRecognizer_.setConfig(cfg);
}

bool MainTask::isMusicUiActive() const
{
    const ui_screen_tag_t tag = ui_get_active_screen_tag();
    return tag == UI_SCREEN_MUSIC || tag == UI_SCREEN_MUSIC_SECONDARY;
}

void MainTask::updateMusicUiAsrOwnership()
{
    const bool musicUiActive = isMusicUiActive();
    if (musicUiActive && !asrSuspendedForMusic_)
    {
        if (voiceCaptureActive_ || voiceRecognizer_.isCapturing())
        {
            SendAsrRecordingState(false);
            voiceCaptureActive_ = false;
            voiceRecognitionBusy_ = false;
            LOG_INFO("ASR", "Suspend voice capture while music UI is active");
        }
        voiceRecognizer_.suspend();
        asrSuspendedForMusic_ = true;
        return;
    }

    if (!musicUiActive && asrSuspendedForMusic_)
    {
        asrSuspendedForMusic_ = false;
        if (configuration_.settings_.voice_enable &&
            currentWorkMode_ == Configuration::WIRED_KEYBOARD_MODE &&
            WiFi.status() == WL_CONNECTED)
        {
            if (!voiceRecognizer_.resume())
            {
                LOG_WARNING("ASR", "VoiceRecognizer resume failed after leaving music UI");
            }
        }
    }
}

void MainTask::reconcileVoiceRuntimeState()
{
    const bool shouldEnableVoice = configuration_.settings_.voice_enable &&
                                   currentWorkMode_ == Configuration::WIRED_KEYBOARD_MODE &&
                                   WiFi.status() == WL_CONNECTED;

    if (!shouldEnableVoice)
    {
        if (voiceCaptureActive_ || voiceRecognizer_.isCapturing())
        {
            SendAsrRecordingState(false);
            voiceCaptureActive_ = false;
            voiceRecognitionBusy_ = false;
        }
        voiceRecognizer_.suspend();
        return;
    }

    if (asrSuspendedForMusic_)
    {
        return;
    }

    if (!voiceRecognizerStarted_)
    {
        if (!voiceRecognizer_.begin())
        {
            LOG_WARNING("ASR", "VoiceRecognizer init failed, feature disabled");
            return;
        }
        voiceRecognizerStarted_ = true;
    }
    else if (!voiceRecognizer_.resume())
    {
        LOG_WARNING("ASR", "VoiceRecognizer resume failed");
        return;
    }

}

void MainTask::startVoiceCapture()
{
    if (voiceRecognitionBusy_)
    {
        return;
    }

    if (asrSuspendedForMusic_)
    {
        return;
    }

    if (!configuration_.settings_.voice_enable)
    {
        return;
    }

    if (currentWorkMode_ != Configuration::WIRED_KEYBOARD_MODE)
    {
        LOG_WARNING("ASR", "Voice typing requires wired USB mode");
        return;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        LOG_WARNING("ASR", "WiFi is not connected, skip voice recognition");
        return;
    }

    voiceRecognitionBusy_ = true;
    if (!voiceRecognizer_.startCapture())
    {
        LOG_ERROR("ASR", "Voice capture start failed");
        voiceRecognitionBusy_ = false;
        return;
    }

    voiceCaptureActive_ = true;
    SendAsrRecordingState(true);
    LOG_INFO("ASR", "Voice trigger pressed, start capture");
}

void MainTask::finishVoiceCapture()
{
    if (!voiceCaptureActive_)
    {
        return;
    }

    SendAsrRecordingState(false);
    LOG_INFO("ASR", "Voice trigger released, start recognition");

    String recognizedText;
    bool ok = voiceRecognizer_.finishCaptureAndRecognize(recognizedText);
    if (!ok)
    {
        LOG_ERROR("ASR", "Voice recognition failed");
        voiceCaptureActive_ = false;
        voiceRecognitionBusy_ = false;
        return;
    }

    if (!sendUtf8TextToCdc(recognizedText))
    {
        LOG_WARNING("ASR", "CDC output failed for text: %s", recognizedText.c_str());
    }

    // 保留ASCII直打能力作兜底：若上位机未连接且是ASCII文本，尝试HID输入。
    if (!hasNonAsciiUtf8(recognizedText) && (!currentKeyboard_ || !currentKeyboard_->isConnected()))
    {
        sendAsciiTextToHost(recognizedText);
    }

    voiceCaptureActive_ = false;
    voiceRecognitionBusy_ = false;
}

bool MainTask::sendAsciiTextToHost(const String &text)
{
    if (!currentKeyboard_ || !currentKeyboard_->isConnected())
    {
        LOG_WARNING("ASR", "Keyboard is not connected");
        return false;
    }

    bool sentAny = false;
    for (size_t i = 0; i < text.length(); ++i)
    {
        char ch = text[i];
        if (ch < 0x20 || ch > 0x7E)
        {
            // HID keyboard库以按键码为主，先过滤非ASCII字符避免乱码。
            continue;
        }

        currentKeyboard_->press(static_cast<uint8_t>(ch));
        currentKeyboard_->release(static_cast<uint8_t>(ch));
        delay(8);
        sentAny = true;
    }

    if (sentAny)
    {
        if (configuration_.settings_.voice_auto_enter)
        {
            currentKeyboard_->press(static_cast<uint8_t>(0xB0));
            currentKeyboard_->release(static_cast<uint8_t>(0xB0));
        }
        LOG_INFO("ASR", "Voice text typed to host");
    }
    return sentAny;
}

bool MainTask::sendUtf8TextToCdc(const String &text)
{
    if (text.isEmpty())
    {
        return false;
    }

    String sanitized = text;
    sanitized.replace("\r", " ");
    sanitized.replace("\n", " ");

    protocol_.sendVoiceText(sanitized, 0);
    LOG_INFO("ASR", "Voice text forwarded via private protocol");
    return true;
}

void MainTask::setWorkMode(Configuration::WORK_MODE mode)
{
    if (currentWorkMode_ == mode && currentKeyboard_ != nullptr)
    {
        LOG_DEBUG("Log", "Work mode unchanged: %d", mode);
        return; // 模式未改变
    }

    currentKeyboard_.reset(); // 智能指针会自动清理现有实例

    currentWorkMode_ = mode;

    // 根据模式创建对应的键盘实例
    switch (mode)
    {
    case Configuration::BLUETOOTH_KEYBOARD_MODE:
        currentKeyboard_.reset(new BLEKeyboardImpl());
        LOG_INFO("Log", "Creating BLE keyboard instance");
        break;

    case Configuration::WIRED_KEYBOARD_MODE:
        currentKeyboard_.reset(new USBKeyboardImpl());
        LOG_INFO("Log", "Creating USB keyboard instance");
        break;

    case Configuration::WIRELESS_2_4G_KEYBOARD_MODE:
        // TODO: 实现2.4G模式
        LOG_WARNING("Log", "2.4G mode not implemented yet");
        break;

    default:
        LOG_ERROR("Log", "Unknown work mode: %d", mode);
        return;
    }

    // 初始化键盘
    if (currentKeyboard_ && currentKeyboard_->begin())
    {
        LOG_INFO("Log", "Keyboard initialized successfully in mode: %d", mode);

        // 更新配置
        // configuration_.getSettings().work_mode = mode;
    }
    else
    {
        LOG_ERROR("Log", "Failed to initialize keyboard in mode: %d", mode);
        currentKeyboard_.reset(); // 初始化失败时清理
    }

    if (configuration_.settings_.wifi_switch)
    {
        if (mode == Configuration::BLUETOOTH_KEYBOARD_MODE)
        {
            stopWiFiReconnect();
        }
        else
        {
            scheduleWiFiConnectAttempt(true);
        }
    }
}

MainTask::MainTask(const uint8_t task_core, Configuration &configuration)
    : Task("MainTask", 12288, 1, task_core),
      configuration_(configuration),
      message_queue_(nullptr),
      currentWorkMode_(Configuration::NONE_MODE)
{
    g_main_task = this;
}

MainTask::~MainTask()
{
    if (g_main_task == this)
    {
        g_main_task = nullptr;
    }
}

void MainTask::setBoost5VEnabled(bool enabled)
{
    if (!boost5VPinInitialized_)
    {
        pinMode(kBoost5VEnablePin, OUTPUT);
        boost5VPinInitialized_ = true;
    }

    digitalWrite(kBoost5VEnablePin, enabled ? HIGH : LOW);
    if (boost5VEnabled_ != enabled)
    {
        boost5VEnabled_ = enabled;
        LOG_INFO("Power", "GPIO%d 3.3V->5V enable %s", kBoost5VEnablePin, enabled ? "ON" : "OFF");
    }
}

void MainTask::applyPowerMode(Configuration::POWER_MODE mode)
{
    const bool enableBoost5V = (mode == Configuration::NORMAL_POWER_MODE);
    setBoost5VEnabled(enableBoost5V);
}

void MainTask::SendMusicPlayerUpdate(bool force)
{
    const uint32_t nowMs = millis();
    if (!force && (nowMs - lastMusicUiUpdateMs_ < 500))
    {
        return;
    }
    lastMusicUiUpdateMs_ = nowMs;

    DisplayMessage msg{};
    msg.type = uint8_t(MainCommand::MUSIC_PLAYER_UPDATE);
    msg.music_player = musicPlayerState_;

    if (message_queue_ != nullptr)
    {
        if (xQueueSend(message_queue_, &msg, 0) != pdPASS)
        {
            LOG_WARNING("Display", "Drop MUSIC_PLAYER_UPDATE: display queue full");
        }
    }
}

void MainTask::SendBatteryStatusUpdate()
{
    const BatteryReading reading = batteryMonitor_.read();

    DisplayMessage msg{};
    msg.type = uint8_t(MainCommand::BATTERY_STATUS_UPDATE);
    msg.battery_status.voltage_mv = reading.voltage_mv;
    msg.battery_status.percent = reading.percent;

    LOG_DEBUG("Battery", "Voltage: %u mV, level: %u%%",
              (unsigned)reading.voltage_mv,
              (unsigned)reading.percent);

    if (message_queue_ != nullptr && xQueueSend(message_queue_, &msg, 0) != pdPASS)
    {
        LOG_WARNING("Display", "Drop BATTERY_STATUS_UPDATE: display queue full");
    }
}

void MainTask::updateLocalMusicProgress(uint32_t nowMs)
{
    if (!musicPlayerState_.connected)
    {
        lastMusicProgressTickMs_ = 0;
        return;
    }

    if (!musicPlayerState_.is_playing || musicPlayerState_.is_paused || musicPlayerState_.total_seconds == 0)
    {
        lastMusicProgressTickMs_ = nowMs;
        return;
    }

    if (lastMusicProgressTickMs_ == 0)
    {
        lastMusicProgressTickMs_ = nowMs;
        return;
    }

    const uint32_t elapsedMs = nowMs - lastMusicProgressTickMs_;
    if (elapsedMs < 1000)
    {
        return;
    }

    const uint32_t advancedSeconds = elapsedMs / 1000;
    const uint32_t nextSeconds = min<uint32_t>(musicPlayerState_.total_seconds,
                                               static_cast<uint32_t>(musicPlayerState_.current_seconds) + advancedSeconds);
    musicPlayerState_.current_seconds = static_cast<uint16_t>(nextSeconds);
    lastMusicProgressTickMs_ += advancedSeconds * 1000;

    if (musicPlayerState_.current_seconds >= musicPlayerState_.total_seconds)
    {
        lastMusicProgressTickMs_ = nowMs;
    }
}

void MainTask::SendMusicControlCommand(const char *action)
{
    if (!action || action[0] == '\0')
    {
        return;
    }

    DynamicJsonDocument controlDoc(128);
    JsonObject control = controlDoc.createNestedObject("music_control");
    control["action"] = action;
    protocol_.sendCustomCommand(CMD_MUSIC_CONTROL, 0, controlDoc.as<JsonObject>());
}

String MainTask::formatKeyMappingDisplay(const KeyMapping &mapping, uint8_t physicalKey) const
{
    String display = String("K") + String(physicalKey);

    if (!mapping.function_key.isEmpty())
    {
        return display + ":" + simplifyDisplayToken(mapping.function_key);
    }

    String combo;
    for (uint8_t i = 0; i < mapping.macros_key_count; ++i)
    {
        if (!combo.isEmpty())
        {
            combo += "+";
        }
        combo += simplifyDisplayToken(lookupKeyNameByCode(mapping.macros_key[i]));
    }
    for (uint8_t i = 0; i < mapping.normal_key_count; ++i)
    {
        if (!combo.isEmpty())
        {
            combo += "+";
        }
        combo += simplifyDisplayToken(lookupKeyNameByCode(mapping.normal_key[i]));
    }

    if (combo.isEmpty())
    {
        combo = "--";
    }

    return display + ":" + combo;
}

void MainTask::SendKeyMappedProfileUi()
{
    DisplayMessage msg{};
    const uint8_t activeProfile = configuration_.settings_.active_keymap_profile;
    msg.type = uint8_t(MainCommand::KEYMAP_PROFILE_UPDATE);
    msg.active_profile = activeProfile;
    strncpy(msg.profile_name,
            Configuration::getProfileDisplayName(activeProfile),
            sizeof(msg.profile_name) - 1);
    strncpy(msg.profile_icon,
            kProfileIcons[5],
            sizeof(msg.profile_icon) - 1);

    if (profileIconExists(activeProfile))
    {
        strncpy(msg.profile_icon,
                kProfileIcons[activeProfile % CONFIG_PROFILE_COUNT],
                sizeof(msg.profile_icon) - 1);
        const String spiffsPath = Configuration::getProfileIconPath(activeProfile);
        const String lvglPath = String("S:") + spiffsPath;
        File iconFile = SPIFFS.open(spiffsPath, FILE_READ);
        if (iconFile)
        {
            LOG_INFO("KeymapUI", "profile=%u icon file ready path=%s size=%u",
                     (unsigned)(activeProfile + 1),
                     spiffsPath.c_str(),
                     (unsigned)iconFile.size());
            iconFile.close();
        }
        else
        {
            LOG_WARNING("KeymapUI", "profile=%u icon file missing path=%s",
                        (unsigned)(activeProfile + 1),
                        spiffsPath.c_str());
        }
        copyUtf8Truncated(msg.profile_icon_path,
                          sizeof(msg.profile_icon_path),
                          lvglPath);
    }
    else
    {
        LOG_INFO("KeymapUI", "profile=%u using fallback symbol %s",
                 (unsigned)(activeProfile + 1),
                 msg.profile_icon);
    }

    for (uint8_t i = 0; i < 16; ++i)
    {
        const String label = formatKeyMappingDisplay(configuration_.key_mappings_[i], i + 1);
        strncpy(msg.keymap_labels[i], label.c_str(), sizeof(msg.keymap_labels[i]) - 1);
    }

    if (message_queue_ != nullptr)
    {
        xQueueSend(message_queue_, &msg, portMAX_DELAY);
    }
}

void MainTask::sendCurrentProfileState(int seq)
{
    DynamicJsonDocument profileDoc(512);
    JsonObject profileState = profileDoc.to<JsonObject>();
    const uint8_t activeProfile = configuration_.settings_.active_keymap_profile;
    const bool hasCustomIcon = profileIconExists(activeProfile);

    JsonObject state = profileState.createNestedObject("profile_state");
    state["active_profile"] = activeProfile;
    state["profile_number"] = activeProfile + 1;
    state["profile_name"] = Configuration::getProfileDisplayName(activeProfile);
    state["has_custom_icon"] = hasCustomIcon;
    if (hasCustomIcon)
    {
        state["icon_path"] = Configuration::getProfileIconPath(activeProfile);
    }

    protocol_.sendCustomCommand(CMD_PROFILE_STATE, seq, profileState);
}

void MainTask::sendCurrentKeymapSnapshot(int seq)
{
    constexpr size_t kKeymapDocCapacity = 12288;
    DynamicJsonDocument keymapDoc(kKeymapDocCapacity);
    JsonArray keymapArray = keymapDoc.to<JsonArray>();

    for (int i = 0; i < CONFIG_ALL_KEY_NUM; i++)
    {
        auto function_key_str = configuration_.key_mappings_[i].function_key;
        String normal_key_str;
        String macro_str;
        if (function_key_str.isEmpty())
        {
            normal_key_str = buildKeySequenceString(configuration_.key_mappings_[i].normal_key,
                                                    configuration_.key_mappings_[i].normal_key_count);
            macro_str = buildKeySequenceString(configuration_.key_mappings_[i].macros_key,
                                               configuration_.key_mappings_[i].macros_key_count);
        }

        JsonObject key = keymapArray.createNestedObject();
        key["physical"] = i + 1;
        key["normal"] = normal_key_str;
        key["macro"] = macro_str;
        key["function"] = function_key_str;
    }

    protocol_.sendKeymap(keymapArray, seq);
}

void MainTask::sendCurrentConfigSnapshot(int seq)
{
    DynamicJsonDocument configDoc(4096);
    JsonObject config = configDoc.to<JsonObject>();
    const uint8_t activeProfile = configuration_.settings_.active_keymap_profile;
    config["wifi_switch"] = configuration_.settings_.wifi_switch;
    config["connect_host"] = configuration_.settings_.connect_host;
    config["wifi_ssid"] = configuration_.settings_.wifi_ssid;
    config["wifi_password"] = configuration_.settings_.wifi_password;
    config["work_mode"] = configuration_.settings_.work_mode;
    config["rgb_mode"] = configuration_.settings_.rgb_mode;
    config["rgb_single_colar"] = configuration_.settings_.rgb_single_colar;
    config["rgb_click_mode"] = configuration_.settings_.rgb_click_mode;
    config["rgb_brightness"] = configuration_.settings_.rgb_brightness;
    config["tft_theme"] = configuration_.settings_.tft_theme;
    config["tft_brightness"] = configuration_.settings_.tft_brightness;
    config["device_volume"] = configuration_.settings_.device_volume;
    config["power_mode"] = configuration_.settings_.power_mode;
    config["voice_enable"] = configuration_.settings_.voice_enable;
    config["voice_trigger_key"] = configuration_.settings_.voice_trigger_key;
    config["voice_max_record_ms"] = configuration_.settings_.voice_max_record_ms;
    config["voice_auto_enter"] = configuration_.settings_.voice_auto_enter;
    config["voice_dev_pid"] = configuration_.settings_.voice_dev_pid;
    config["voice_cuid"] = configuration_.settings_.voice_cuid;
    config["voice_baidu_api_key"] = configuration_.settings_.voice_baidu_api_key;
    config["voice_baidu_secret_key"] = configuration_.settings_.voice_baidu_secret_key;
    config["active_keymap_profile"] = activeProfile;
    config["active_profile_name"] = Configuration::getProfileDisplayName(activeProfile);
    config["active_profile_has_custom_icon"] = profileIconExists(activeProfile);
    protocol_.sendConfig(config, seq);
}

bool MainTask::profileIconExists(uint8_t profileIndex) const
{
    return SPIFFS.exists(Configuration::getProfileIconPath(profileIndex));
}

bool MainTask::removeProfileIcon(uint8_t profileIndex)
{
    const String path = Configuration::getProfileIconPath(profileIndex);
    if (!SPIFFS.exists(path))
    {
        return true;
    }
    return SPIFFS.remove(path);
}

bool MainTask::saveProfileIconFromBase64(uint8_t profileIndex, const String &pngBase64, String &errorMessage)
{
    if (pngBase64.isEmpty())
    {
        errorMessage = "png_base64 is empty";
        return false;
    }

    if (pngBase64.length() > 14336)
    {
        errorMessage = "png_base64 too large";
        return false;
    }

    size_t decodedLength = 0;
    const unsigned char *encoded = reinterpret_cast<const unsigned char *>(pngBase64.c_str());
    const size_t encodedLength = pngBase64.length();
    int ret = mbedtls_base64_decode(nullptr, 0, &decodedLength, encoded, encodedLength);
    if (ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL || decodedLength == 0)
    {
        errorMessage = "base64 length invalid";
        return false;
    }

    std::unique_ptr<uint8_t[]> decoded(new uint8_t[decodedLength]);
    if (!decoded)
    {
        errorMessage = "icon buffer alloc failed";
        return false;
    }

    ret = mbedtls_base64_decode(decoded.get(), decodedLength, &decodedLength, encoded, encodedLength);
    if (ret != 0)
    {
        errorMessage = "base64 decode failed";
        return false;
    }
    if (!isPng48x48(decoded.get(), decodedLength))
    {
        errorMessage = "only 48x48 PNG is supported";
        return false;
    }

    const String path = Configuration::getProfileIconPath(profileIndex);
    File file = SPIFFS.open(path, FILE_WRITE);
    if (!file)
    {
        errorMessage = "open icon file failed";
        return false;
    }

    const size_t written = file.write(decoded.get(), decodedLength);
    file.close();
    if (written != decodedLength)
    {
        SPIFFS.remove(path);
        errorMessage = "write icon file failed";
        return false;
    }

    return true;
}

bool MainTask::switchKeymapProfile(int delta)
{
    int profile = static_cast<int>(configuration_.settings_.active_keymap_profile) + delta;
    if (profile < 0)
    {
        profile = CONFIG_PROFILE_COUNT - 1;
    }
    else if (profile >= CONFIG_PROFILE_COUNT)
    {
        profile = 0;
    }

    if (!configuration_.switchActiveProfile(static_cast<uint8_t>(profile)))
    {
        return false;
    }

    applyVoiceConfig();
    SendKeyMappedProfileUi();
    sendCurrentProfileState(0);
    sendCurrentConfigSnapshot(0);
    sendCurrentKeymapSnapshot(0);
    return true;
}

// 从 NTP 同步时间到系统时钟
void MainTask::StartTimeSync()
{
    // SNTP keeps the Unix clock in UTC. localtime_r() applies the POSIX TZ rule.
    configTzTime(SystemTime::kChinaTimeZone,
                 kNtpServerPrimary,
                 kNtpServerSecondary,
                 kNtpServerFallback);

    timeSyncPending_ = true;
    timeSyncStartedMs_ = millis();
    timeSyncNextCheckMs_ = timeSyncStartedMs_;
    LOG_INFO("Time", "NTP sync started (timezone=Asia/Shanghai, UTC+8)");
}

void MainTask::ProcessTimeSync(uint32_t nowMs)
{
    if (!timeSyncPending_ || WiFi.status() != WL_CONNECTED)
    {
        return;
    }

    if (static_cast<int32_t>(nowMs - timeSyncNextCheckMs_) < 0)
    {
        return;
    }

    struct tm timeInfo{};
    if (SystemTime::GetLocalTime(&timeInfo))
    {
        char formattedTime[32] = {0};
        strftime(formattedTime, sizeof(formattedTime), "%Y-%m-%d %H:%M:%S", &timeInfo);
        LOG_INFO("Time", "NTP sync complete: %s", formattedTime);
        timeSyncPending_ = false;
        return;
    }

    if (nowMs - timeSyncStartedMs_ >= kTimeSyncRestartIntervalMs)
    {
        LOG_WARNING("Time", "NTP sync timed out; restarting SNTP");
        StartTimeSync();
        return;
    }

    timeSyncNextCheckMs_ = nowMs + kTimeSyncCheckIntervalMs;
}

bool MainTask::SyncTimeBeforeBluetoothStart()
{
    struct tm timeInfo{};
    if (SystemTime::GetLocalTime(&timeInfo))
    {
        return true;
    }

    String ssid = configuration_.settings_.wifi_ssid;
    String password = configuration_.settings_.wifi_password;
    ssid.trim();
    password.trim();
    if (ssid.isEmpty())
    {
        LOG_WARNING("Time", "Cannot sync time before BLE start: WiFi SSID is empty");
        return false;
    }

    LOG_INFO("Time", "Temporarily connecting WiFi before BLE startup");
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.setSleep(false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    delay(20);
    WiFi.begin(ssid.c_str(), password.c_str());

    const uint32_t wifiStartedMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStartedMs < kWifiConnectTimeoutMs)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    bool synchronized = false;
    if (WiFi.status() == WL_CONNECTED)
    {
        LOG_INFO("Time", "Temporary WiFi connected, IP=%s", WiFi.localIP().toString().c_str());
        StartTimeSync();

        const uint32_t syncStartedMs = millis();
        while (millis() - syncStartedMs < kBluetoothStartupTimeSyncTimeoutMs)
        {
            if (SystemTime::GetLocalTime(&timeInfo))
            {
                char formattedTime[32] = {0};
                strftime(formattedTime, sizeof(formattedTime), "%Y-%m-%d %H:%M:%S", &timeInfo);
                LOG_INFO("Time", "NTP sync complete before BLE start: %s", formattedTime);
                synchronized = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    else
    {
        LOG_WARNING("Time", "Temporary WiFi connection failed, status=%d", WiFi.status());
    }

    if (!synchronized && WiFi.status() == WL_CONNECTED)
    {
        LOG_WARNING("Time", "NTP sync timed out before BLE start");
    }

    timeSyncPending_ = false;
    timeSyncStartedMs_ = 0;
    timeSyncNextCheckMs_ = 0;
    if (sntp_enabled())
    {
        sntp_stop();
    }
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    delay(100);
    g_bleTimeSyncRestartMarker = kBleTimeSyncRestartMagic;
    LOG_INFO("Time", "Temporary WiFi stopped; restarting to reclaim memory before BLE startup");
    delay(50);
    ESP.restart();
    return synchronized;
}

int MainTask::parseKeymapSetCommand(int seq, JsonObject data)
{
    LOG_DEBUG("Log", "Parsing keymap set command");

    if (!data.containsKey("keymap") || !data["keymap"].is<JsonArray>())
    {
        LOG_ERROR("Log", "Invalid keymap data format");
        return -1;
    }

    JsonArray keymaps = data["keymap"].as<JsonArray>();
    int count = 0;

    // 获取互斥锁以确保线程安全
    if (xSemaphoreTake(configuration_.mutex_, portMAX_DELAY) == pdTRUE)
    {
        // // 遍历所有键位映射并重置
        // for (int i = 0; i < CONFIG_ALL_KEY_NUM; i++) {
        //     configuration_.key_mappings_[i].normal_key_count = 0;
        //     configuration_.key_mappings_[i].macros_key_count = 0;
        //     // 清空数组
        //     memset(configuration_.key_mappings_[i].normal_key, 0, sizeof(configuration_.key_mappings_[i].normal_key));
        //     memset(configuration_.key_mappings_[i].macros_key, 0, sizeof(configuration_.key_mappings_[i].macros_key));
        // }

        for (JsonObject keymap : keymaps)
        {
            if (parseSingleKeyMapping(keymap) == 0)
            {
                count++;
            }
        }

        xSemaphoreGive(configuration_.mutex_);
    }

    LOG_DEBUG("Log", "Successfully parsed " + String(count) + " key mappings");

    // 保存配置到持久化存储
    if (count > 0)
    {
        if (configuration_.SaveKeyMapping())
        {
            LOG_DEBUG("Log", "Key mappings saved to persistent storage");
        }
        else
        {
            LOG_ERROR("Log", "Failed to save key mappings to persistent storage");
        }
    }

    applyVoiceConfig();
    return count;
}

bool MainTask::parseKeymapSetValue(Configuration::KEY_TYPE key_type, const String &value, KeyMapping &mapping)
{
    if (key_type == Configuration::FUNCTION_KEY)
    {
        mapping.function_key = normalizeFunctionKey(value);
        LOG_INFO("ASR", "Set function key mapping=%s", mapping.function_key.c_str());
    }
    else
    {
        int pos = 0;
        while (pos < value.length())
        {
            int end_pos = value.indexOf('+', pos);
            if (end_pos == -1)
                end_pos = value.length();

            String code_str = value.substring(pos, end_pos);
            uint8_t keycode = stringToKeycode(code_str.c_str());

            if (key_type == Configuration::NORMAL_KEY)
            {
                if (mapping.normal_key_count >= 6)
                {
                    LOG_ERROR("Log", "Normal key array overflow! Max 6 keys allowed.");
                    break;
                }
                mapping.normal_key[mapping.normal_key_count] = keycode;
                mapping.normal_key_count++;
                mapping.function_key = ""; // 定义了普通键则，清除对应function_key
            }
            else if (key_type == Configuration::MACROS_KEY)
            {
                if (mapping.macros_key_count >= 5)
                {
                    LOG_ERROR("Log", "Macros key array overflow! Max 5 keys allowed.");
                    break;
                }
                mapping.macros_key[mapping.macros_key_count] = keycode;
                mapping.macros_key_count++;
            }
            pos = end_pos + 1;
        }
    }

    return true;
}

int MainTask::parseSingleKeyMapping(JsonObject keyObj)
{
    // if (!keyObj.containsKey("physical_key") || !keyObj.containsKey("normal")
    //     || !keyObj.containsKey("macro") || !keyObj.containsKey("function")) {
    //     LOG_ERROR("Log","Invalid key mapping object");
    //     return -1;
    // }

    KeyMapping *targetMapping = nullptr;

    if (keyObj["physical"].is<int>())
    {
        int physicalKey_index = keyObj["physical"].as<int>() - 1;
        if (physicalKey_index < 0 || physicalKey_index >= CONFIG_ALL_KEY_NUM)
        {
            LOG_ERROR("Log", "Physical key out of range:%d ", physicalKey_index);
            return -1;
        }
        targetMapping = &configuration_.key_mappings_[physicalKey_index];
    }
    else
    {
        LOG_ERROR("Log", "Invalid key mapping target");
        return -1;
    }

    KeyMapping &keymapping = *targetMapping;

    // 清空数组
    keymapping.normal_key_count = 0;
    keymapping.macros_key_count = 0;
    memset(keymapping.normal_key, 0, sizeof(keymapping.normal_key));
    memset(keymapping.macros_key, 0, sizeof(keymapping.macros_key));

    // 解析普通键 (normal)
    String normalStr = keyObj["normal"];
    LOG_ERROR("Log", "parseSingleKeyMapping normalStr:%s ", normalStr.c_str());

    if (normalStr && strlen(normalStr.c_str()) > 0)
    {
        parseKeymapSetValue(Configuration::NORMAL_KEY, normalStr, keymapping);
    }

    // 解析宏键 (macro)
    String macroStr = keyObj["macro"];
    if (macroStr && strlen(macroStr.c_str()) > 0)
    {
        parseKeymapSetValue(Configuration::MACROS_KEY, macroStr, keymapping);
    }

    // 解析功能键 (function)
    String functionStr = keyObj["function"];
    if (functionStr && (strlen(functionStr.c_str()) > 0) && (!functionStr.equals("0")))
    {
        parseKeymapSetValue(Configuration::FUNCTION_KEY, functionStr, keymapping);
    }

    // LOG_WARNING("Log","physicalKey_index=%d,normal_key_count=%d,macros_key_count=%d",
    //     physicalKey_index ,keymapping.normal_key_count,keymapping.macros_key_count);

    return 0;
}

int MainTask::parseConfigSetCommand(int seq, JsonObject data)
{
    LV_UNUSED(seq);

    if (!data.containsKey("config") || !data["config"].is<JsonObject>())
    {
        LOG_ERROR("Log", "Invalid config data format");
        return -1;
    }

    // 获取互斥锁以确保线程安全
    int pendingProfile = -1;
    bool powerModeChanged = false;

    if (xSemaphoreTake(configuration_.mutex_, portMAX_DELAY) == pdTRUE)
    {
        JsonObject config = data["config"];
        bool settingChanged = false;
        bool voiceConfigChanged = false;

        if (config.containsKey("wifi_switch"))
        {
            configuration_.settings_.wifi_switch = config["wifi_switch"];
            settingChanged = true;

            if (configuration_.settings_.wifi_switch == true)
            {
                if (config.containsKey("wifi_ssid") && config.containsKey("wifi_password"))
                {
                    configuration_.settings_.wifi_ssid = config["wifi_ssid"].as<String>();
                    configuration_.settings_.wifi_password = config["wifi_password"].as<String>();
                    settingChanged = true;
                    scheduleWiFiConnectAttempt(true);
                }

                if (WiFi.status() == WL_CONNECTED)
                {
                    updateProtocolTcpEndpoint();
                }
                else
                {
                    scheduleWiFiConnectAttempt(true);
                }
            }
            else
            {
                stopWiFiReconnect();
                reconcileVoiceRuntimeState();
                LOG_DEBUG("Log", "[parseConfigSetCommand] WiFi disconnect!");
            }
        }

        if (config.containsKey("connect_host"))
        {
            configuration_.settings_.connect_host = config["connect_host"].as<int>() != 0;
            settingChanged = true;

            if (!configuration_.settings_.connect_host)
            {
                protocol_.disableTcpClient();
            }
            else if (WiFi.status() == WL_CONNECTED)
            {
                updateProtocolTcpEndpoint();
            }
        }

        if (config.containsKey("work_mode"))
        {
            int new_work_mode = config["work_mode"];
            if (new_work_mode >= Configuration::WIRED_KEYBOARD_MODE &&
                new_work_mode <= Configuration::WIRELESS_2_4G_KEYBOARD_MODE)
            {
                setWorkMode(static_cast<Configuration::WORK_MODE>(new_work_mode));
                configuration_.settings_.work_mode = new_work_mode;
                settingChanged = true;
            }
        }

        if (config.containsKey("rgb_single_colar"))
        {
            configuration_.settings_.rgb_single_colar = config["rgb_single_colar"].as<String>();
            settingChanged = true;
        }

        if (config.containsKey("rgb_mode"))
        {
            int new_rgb_mode = config["rgb_mode"];
            // if (newMode >= Configuration::RGB_CLICK_MODE &&
            //     newMode <= Configuration::RGB_PULSE_MODE) {
            // setWorkMode(static_cast<Configuration::WORK_MODE>(newMode));
            configuration_.settings_.rgb_mode = new_rgb_mode;
            settingChanged = true;
            // }
        }

        if (config.containsKey("rgb_click_mode"))
        {
            int new_rgb_click_mode = config["rgb_click_mode"];
            // if (newMode >= Configuration::RGB_CLICK_MODE &&
            //     newMode <= Configuration::RGB_PULSE_MODE) {
            // setWorkMode(static_cast<Configuration::WORK_MODE>(newMode));
            configuration_.settings_.rgb_click_mode = new_rgb_click_mode;
            settingChanged = true;
            // }
        }

        if (config.containsKey("rgb_brightness"))
        {
            configuration_.settings_.rgb_brightness = config["rgb_brightness"];
            settingChanged = true;
        }

        if (config.containsKey("tft_theme"))
        {
            configuration_.settings_.tft_theme = config["tft_theme"];
            settingChanged = true;
        }

        if (config.containsKey("tft_brightness"))
        {
            configuration_.settings_.tft_brightness = constrain(static_cast<int>(config["tft_brightness"]), 5, 100);
            settingChanged = true;
        }

        if (config.containsKey("device_volume"))
        {
            configuration_.settings_.device_volume = config["device_volume"];
            settingChanged = true;
            // 设置喇叭音量
            speaker_.SetVolume(configuration_.settings_.device_volume / 5); // 0~21
        }

        if (config.containsKey("power_mode"))
        {
            int new_power_mode = constrain(static_cast<int>(config["power_mode"]),
                                           static_cast<int>(Configuration::NORMAL_POWER_MODE),
                                           static_cast<int>(Configuration::DEEPSLEEP_POWER_MODE));
            if (configuration_.settings_.power_mode != new_power_mode)
            {
                configuration_.settings_.power_mode = new_power_mode;
                settingChanged = true;
                powerModeChanged = true;
            }
        }

        if (config.containsKey("voice_enable"))
        {
            configuration_.settings_.voice_enable = config["voice_enable"];
            settingChanged = true;
            voiceConfigChanged = true;
        }
        if (config.containsKey("voice_trigger_key"))
        {
            configuration_.settings_.voice_trigger_key = config["voice_trigger_key"];
            settingChanged = true;
            voiceConfigChanged = true;
        }
        if (config.containsKey("voice_max_record_ms"))
        {
            configuration_.settings_.voice_max_record_ms = config["voice_max_record_ms"];
            settingChanged = true;
            voiceConfigChanged = true;
        }
        if (config.containsKey("voice_auto_enter"))
        {
            configuration_.settings_.voice_auto_enter = config["voice_auto_enter"];
            settingChanged = true;
            voiceConfigChanged = true;
        }
        if (config.containsKey("voice_dev_pid"))
        {
            configuration_.settings_.voice_dev_pid = config["voice_dev_pid"];
            settingChanged = true;
            voiceConfigChanged = true;
        }
        if (config.containsKey("voice_cuid"))
        {
            configuration_.settings_.voice_cuid = config["voice_cuid"].as<String>();
            settingChanged = true;
            voiceConfigChanged = true;
        }
        if (config.containsKey("voice_baidu_api_key"))
        {
            configuration_.settings_.voice_baidu_api_key = config["voice_baidu_api_key"].as<String>();
            settingChanged = true;
            voiceConfigChanged = true;
        }
        if (config.containsKey("voice_baidu_secret_key"))
        {
            configuration_.settings_.voice_baidu_secret_key = config["voice_baidu_secret_key"].as<String>();
            settingChanged = true;
            voiceConfigChanged = true;
        }
        if (config.containsKey("active_keymap_profile"))
        {
            const uint8_t newProfile = config["active_keymap_profile"].as<uint8_t>();
            if (newProfile < CONFIG_PROFILE_COUNT && newProfile != configuration_.settings_.active_keymap_profile)
            {
                settingChanged = true;
                voiceConfigChanged = true;
                pendingProfile = static_cast<int>(newProfile);
            }
        }

        if (voiceConfigChanged)
        {
            applyVoiceConfig();
        }
        if (settingChanged || voiceConfigChanged)
        {
            reconcileVoiceRuntimeState();
        }

        if (settingChanged)
        {
            SendDisplaySetting(configuration_.settings_);
            SendMusicPlayerUpdate(true);
            SendHostConnectionUpdate();
        }
        xSemaphoreGive(configuration_.mutex_);
    }

    if (powerModeChanged)
    {
        applyPowerMode(static_cast<Configuration::POWER_MODE>(configuration_.settings_.power_mode));
    }

    if (pendingProfile >= 0)
    {
        configuration_.switchActiveProfile(static_cast<uint8_t>(pendingProfile));
        applyVoiceConfig();
        SendDisplaySetting(configuration_.settings_);
        SendMusicPlayerUpdate(true);
        SendKeyMappedProfileUi();
        sendCurrentProfileState(0);
        sendCurrentConfigSnapshot(0);
        sendCurrentKeymapSnapshot(0);
        SendHostConnectionUpdate();
    }

    return 0;
}

void MainTask::onCommandReceived(int cmd, int seq, JsonObject data)
{
    // LOG_WARNING("Log", "Command received: " + String(cmd) + ", seq: " + String(seq));

    switch (cmd)
    {
    case CMD_DEVICE_INFO_GET:
    {
        DynamicJsonDocument deviceinfoDoc(128);
        JsonObject deviceinfo = deviceinfoDoc.to<JsonObject>();
        deviceinfo["device_name"] = "FunModularKeyBoard";
        deviceinfo["device_id"] = "FMB001";
        deviceinfo["firmware_version"] = "1.0.1";
        protocol_.sendDeviceInfo(deviceinfo, seq);
        break;
    }
    case CMD_KEYMAP_GET:
    {
        constexpr size_t kKeymapDocCapacity = 12288;
        DynamicJsonDocument keymapDoc(kKeymapDocCapacity);
        JsonArray keymapArray = keymapDoc.to<JsonArray>();
        int keymapEntryCount = 0;

        // JsonObject key1 = keymapArray.createNestedObject();
        // key1["physical"] = 1;
        // key1["logical"] = 'A';

        // JsonObject key2 = keymapArray.createNestedObject();
        // key2["physical"] = 2;
        // key2["logical"] = 'B';

        for (int i = 0; i < CONFIG_ALL_KEY_NUM; i++)
        {
            auto function_key_str = configuration_.key_mappings_[i].function_key;
            String normal_key_str;
            String macro_str;
            if (function_key_str.isEmpty())
            {
                normal_key_str = buildKeySequenceString(configuration_.key_mappings_[i].normal_key,
                                                        configuration_.key_mappings_[i].normal_key_count);
                macro_str = buildKeySequenceString(configuration_.key_mappings_[i].macros_key,
                                                   configuration_.key_mappings_[i].macros_key_count);
            }
            else
            {
                LOG_DEBUG("Log", "[CMD_KEYMAP_GET] physical = %d, function_key_str=%s", i + 1, function_key_str.c_str());
            }

            JsonObject key = keymapArray.createNestedObject();
            if (key.isNull())
            {
                LOG_ERROR("Log", "[CMD_KEYMAP_GET] JSON capacity exhausted at physical key %d (capacity=%u)",
                          i + 1,
                          static_cast<unsigned>(kKeymapDocCapacity));
                break;
            }
            key["physical"] = i + 1;
            key["normal"] = normal_key_str;
            key["macro"] = macro_str;
            key["function"] = function_key_str;
            ++keymapEntryCount;
        }

        LOG_INFO("Log", "[CMD_KEYMAP_GET] sending %d keymap entries", keymapEntryCount);

        protocol_.sendKeymap(keymapArray, seq);
        break;
    }

    case CMD_KEYMAP_SET:
        if (data.containsKey("keymap"))
        {
            // 处理按键映射设置
            if (parseKeymapSetCommand(seq, data) > 0)
            {
                configuration_.SaveKeyMapping();
                SendKeyMappedProfileUi();
                protocol_.sendSuccessResponse(CMD_KEYMAP_SET, seq, JsonObject());
            }
            else
            {
                LOG_ERROR("Log", "CMD_KEYMAP_SET set error");
            }
        }
        break;

    case CMD_CONFIG_GET:
    {
        sendCurrentConfigSnapshot(seq);
        break;
    }

    case CMD_CONFIG_SET:
        if (data.containsKey("config"))
        {
            if (parseConfigSetCommand(seq, data) >= 0)
            {
                configuration_.SaveSetting();
                protocol_.sendSuccessResponse(CMD_CONFIG_SET, seq, JsonObject());
            }
            else
            {
                LOG_ERROR("Log", "CMD_CONFIG_SET set error");
            }
        }
        break;

    case CMD_PROFILE_STATE:
        sendCurrentProfileState(seq);
        break;

    case CMD_PROFILE_ICON_SET:
    {
        if (!data.containsKey("profile_icon") || !data["profile_icon"].is<JsonObject>())
        {
            protocol_.sendErrorResponse("Missing profile_icon object", 2, seq);
            break;
        }

        JsonObject icon = data["profile_icon"];
        const uint8_t requestedProfile = icon["profile"] | configuration_.settings_.active_keymap_profile;
        if (requestedProfile >= CONFIG_PROFILE_COUNT)
        {
            protocol_.sendErrorResponse("profile out of range", 3, seq);
            break;
        }

        String errorMessage;
        bool success = false;
        const bool clearIcon = icon["clear"] | false;
        if (clearIcon)
        {
            success = removeProfileIcon(requestedProfile);
            if (!success)
            {
                errorMessage = "remove icon failed";
            }
        }
        else if (icon.containsKey("png_base64") && icon["png_base64"].is<const char *>())
        {
            success = saveProfileIconFromBase64(requestedProfile,
                                                icon["png_base64"].as<String>(),
                                                errorMessage);
        }
        else
        {
            errorMessage = "png_base64 missing";
        }

        if (!success)
        {
            protocol_.sendErrorResponse(errorMessage, 4, seq);
            break;
        }

        DynamicJsonDocument responseDoc(384);
        JsonObject response = responseDoc.to<JsonObject>();
        response["profile"] = requestedProfile;
        response["profile_number"] = requestedProfile + 1;
        response["has_custom_icon"] = profileIconExists(requestedProfile);
        response["profile_name"] = Configuration::getProfileDisplayName(requestedProfile);
        protocol_.sendSuccessResponse(CMD_PROFILE_ICON_SET, seq, response);

        if (requestedProfile == configuration_.settings_.active_keymap_profile)
        {
            SendKeyMappedProfileUi();
        }
        sendCurrentProfileState(0);
        break;
    }

    case CMD_FIRMWARE_INFO:
    {
        DynamicJsonDocument firmwareDoc(128);
        JsonObject firmware = firmwareDoc.to<JsonObject>();
        firmware["version"] = "1.0.0";
        firmware["author"] = "Your Name";
        protocol_.sendFirmwareInfo(firmware, seq);
        break;
    }

    case CMD_MUSIC_STATUS:
    {
        if (!data.containsKey("music_status") || !data["music_status"].is<JsonObject>())
        {
            protocol_.sendErrorResponse("Missing music_status object", 2, seq);
            break;
        }

        JsonObject music = data["music_status"];
        musicPlayerState_.connected = music["connected"] | false;
        musicPlayerState_.is_playing = music["is_playing"] | false;
        musicPlayerState_.is_paused = music["is_paused"] | false;
        musicPlayerState_.can_prev = music["can_prev"] | false;
        musicPlayerState_.can_next = music["can_next"] | false;
        musicPlayerState_.current_seconds = (music["position_ms"] | 0) / 1000;
        musicPlayerState_.total_seconds = (music["duration_ms"] | 0) / 1000;

        String title = music["title"] | "WAITING FOR PLAYER";
        String artist = music["artist"] | "";
        String playerName = music["player"] | "PC MUSIC";
        String lyricCurrent = music["lyric_current"] | "";
        String lyricNext = music["lyric_next"] | "";

        copyUtf8Truncated(musicPlayerState_.title, sizeof(musicPlayerState_.title), title);
        copyUtf8Truncated(musicPlayerState_.artist, sizeof(musicPlayerState_.artist), artist);
        copyUtf8Truncated(musicPlayerState_.player_name, sizeof(musicPlayerState_.player_name), playerName);
        copyUtf8Truncated(musicPlayerState_.lyric_current, sizeof(musicPlayerState_.lyric_current), lyricCurrent);
        copyUtf8Truncated(musicPlayerState_.lyric_next, sizeof(musicPlayerState_.lyric_next), lyricNext);

        lastMusicStatusRxMs_ = millis();
        lastMusicProgressTickMs_ = lastMusicStatusRxMs_;
        SendMusicPlayerUpdate(true);
        protocol_.sendSuccessResponse(CMD_MUSIC_STATUS, seq, JsonObject());
        break;
    }
    }
}

void MainTask::onKeyEvent(int physicalKey, int logicalKey, bool pressed)
{
    // LOG_DEBUG("Log", "Key: " + String(physicalKey) + " -> " + String(logicalKey) +
    //                " " + (pressed ? "PRESSED" : "RELEASED"));
}

void MainTask::updateProtocolTcpEndpoint()
{
    constexpr uint16_t kTcpPort = 30000;
    constexpr uint16_t kDiscoveryPort = 30001;
    constexpr uint32_t kDiscoveryTimeoutMs = 800;

    if (!configuration_.settings_.connect_host || WiFi.status() != WL_CONNECTED)
    {
        protocol_.disableTcpClient();
        return;
    }

    // UDP 广播发现服务端 IP
    IPAddress serverIp;
    WiFiUDP udp;
    if (udp.begin(kDiscoveryPort))
    {
        const char kDiscoveryMsg[] = "FUNKEYBOARD_DISCOVER";
        udp.beginPacket(IPAddress(255, 255, 255, 255), kDiscoveryPort);
        udp.write(reinterpret_cast<const uint8_t *>(kDiscoveryMsg), sizeof(kDiscoveryMsg) - 1);
        udp.endPacket();

        uint32_t startMs = millis();
        while (millis() - startMs < kDiscoveryTimeoutMs)
        {
            if (udp.parsePacket() >= 8)
            {
                char buf[64] = {0};
                int len = udp.read(buf, sizeof(buf) - 1);
                if (len > 0 && strstr(buf, "FUNKEYBOARD_HERE"))
                {
                    serverIp = udp.remoteIP();
                    LOG_INFO("Log", "UDP discovery found server at %s", serverIp.toString().c_str());
                    break;
                }
            }
            delay(10);
        }
        udp.stop();
    }

    if (serverIp == IPAddress(0, 0, 0, 0))
    {
        serverIp = IPAddress(192, 168, 31, 1);
        LOG_INFO("Log", "UDP discovery failed, using fallback IP %s", serverIp.toString().c_str());
    }

    protocol_.configureTcpClient(serverIp, kTcpPort, true);
    LOG_INFO("Log", "TCP client target=%s:%u", serverIp.toString().c_str(), kTcpPort);
}


void MainTask::run()
{

    logHeapSnapshot("run:start");

    applyPowerMode(static_cast<Configuration::POWER_MODE>(configuration_.settings_.power_mode));

    batteryMonitor_.begin();

    // 初始化协议
    protocol_.begin(115200);

    // 获取按键的映射
    for (int i = 1; i <= CONFIG_ALL_KEY_NUM; i++)
    {
        KeyMapping km = configuration_.getKeyMapping(i);
        // LOG_DEBUG("Log","Key%d: %d normal_key",i, km.normal_key_count);
        // for (int n = 0; n < configuration_.getKeyMapping(i).normal_key_count; n++) {
        //     LOG_DEBUG("Log"," + 0x%02X", km.normal_key[n]);
        // }
    }
    for (int i = 1; i <= CONFIG_ALL_KEY_NUM; i++)
    {
        KeyMapping km = configuration_.getKeyMapping(i);
        // LOG_DEBUG("Log","Key%d: %d macros_key",i, km.macros_key_count);
        // for (int n = 0; n < configuration_.getKeyMapping(i).macros_key_count; n++) {
        //     LOG_DEBUG("Log"," + 0x%02X", km.macros_key[n]);
        // }
    }

    const bool skipBleStartupTimeSync = g_bleTimeSyncRestartMarker == kBleTimeSyncRestartMagic;
    g_bleTimeSyncRestartMarker = 0;

    // BLE-only mode cannot keep WiFi active due to memory pressure. Sync once before BLE starts.
    if (!skipBleStartupTimeSync &&
        configuration_.settings_.work_mode == Configuration::BLUETOOTH_KEYBOARD_MODE &&
        configuration_.settings_.wifi_switch)
    {
        SyncTimeBeforeBluetoothStart();
    }

    // BLE-only 模式下释放经典蓝牙内存，避免与 WiFi 同时初始化时堆内存不足。
    if (configuration_.settings_.work_mode == Configuration::BLUETOOTH_KEYBOARD_MODE)
    {
        static bool btClassicMemReleased = false;
        if (!btClassicMemReleased)
        {
            esp_err_t err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
            if (err == ESP_OK || err == ESP_ERR_INVALID_STATE)
            {
                btClassicMemReleased = true;
                LOG_INFO("Log", "Released classic BT memory for BLE-only mode");
            }
            else
            {
                LOG_WARNING("Log", "Failed to release classic BT memory, err=%d", err);
            }
        }
    }

    logHeapSnapshot("run:before_setWorkMode");

    Configuration::WORK_MODE desiredMode = static_cast<Configuration::WORK_MODE>(configuration_.settings_.work_mode);

    // 根据配置设置初始工作模式
    setWorkMode(desiredMode);

    logHeapSnapshot("run:after_setWorkMode");
    startupReady_.store(true, std::memory_order_release);

    // 蓝牙初始化
    if (currentWorkMode_ == Configuration::BLUETOOTH_KEYBOARD_MODE)
    {
        LOG_DEBUG("Log", "Starting BLE work!");
        delay(150);
    }
    else
    {
        LOG_DEBUG("Log", "Starting USB work!");
    }

    // 恢复板载EC11原始功能：仅用于本机屏幕/菜单切换，不进入外设按键映射模型。
    rotaryEncoder_.Begin();
    rotaryEncoder_.SetCallback([this](uint8_t key)
                               {
        if (ui_get_active_screen_tag() == UI_SCREEN_MUSIC_SECONDARY) {
            if (key == LV_KEY_LEFT) {
                SendMusicControlCommand("previous");
                return;
            }
            if (key == LV_KEY_RIGHT) {
                SendMusicControlCommand("next");
                return;
            }
            if (key == LV_KEY_ENTER) {
                SendMusicControlCommand("toggle");
                return;
            }
        }

        if (ui_KeyMappedSecondary != NULL && lv_scr_act() == ui_KeyMappedSecondary) {
            if (key == LV_KEY_LEFT) {
                switchKeymapProfile(-1);
                return;
            }
            if (key == LV_KEY_RIGHT) {
                switchKeymapProfile(1);
                return;
            }
        }

        if (ui_get_active_screen_tag() == UI_SCREEN_SETTING_SECONDARY) {
            if (key == LV_KEY_LEFT) {
                this->SendDisplayAction(LV_KEY_UP);
                return;
            }
            if (key == LV_KEY_RIGHT) {
                this->SendDisplayAction(LV_KEY_DOWN);
                return;
            }
        }
        this->SendDisplayAction(key); });

    SendKeyMappedProfileUi();
    sendCurrentProfileState(0);
    snprintf(musicPlayerState_.title, sizeof(musicPlayerState_.title), "%s", "WAITING FOR PLAYER");
    snprintf(musicPlayerState_.player_name, sizeof(musicPlayerState_.player_name), "%s", "PC MUSIC");
    SendMusicPlayerUpdate(true);

    if (configuration_.settings_.wifi_switch == true)
    {
        logHeapSnapshot("run:before_wifi_connect");
        if (desiredMode == Configuration::BLUETOOTH_KEYBOARD_MODE)
        {
            stopWiFiReconnect();
            LOG_WARNING("Log", "WiFi disabled in BLE mode due memory limits");
        }
        else
        {
            scheduleWiFiConnectAttempt(true);
        }
        logHeapSnapshot("run:after_wifi_phase");
    }
    else
    {
        stopWiFiReconnect();
        LOG_DEBUG("Log", "WiFi off!");
    }

    applyVoiceConfig();
    reconcileVoiceRuntimeState();

    // 显示配置更新
    SendDisplaySetting(configuration_.settings_);
    SendMusicPlayerUpdate(true);
    SendHostConnectionUpdate();
    SendBatteryStatusUpdate();
    lastBatteryStatusMs_ = millis();

    // // 麦克风
    // if (!mic_.Begin()) {
    //     LOG_DEBUG("Log", "Mic Init Error!");
    //     ///delay(1000);
    //     ///ESP.restart();  // 重启设备
    // } else {
    //     LOG_DEBUG("Log", "Mic Init Sucess!");
    // }

    // 设置喇叭音量
    speaker_.SetVolume(configuration_.settings_.device_volume / 5); // 0~21
    speaker_.PlayLocalAudio("/coin2.wav");

    // 初始化和上位机通信协议
    // protocol_.setCommandCallback(onCommandReceived);
    // protocol_.setKeyEventCallback(onKeyEvent);
    // protocol_.setLogCallback(onLogMessage);
    // 初始化和上位机通信协议，设置回调函数
    protocol_.setCommandCallback([this](int cmd, int seq, JsonObject data)
                                 { onCommandReceived(cmd, seq, data); });

    protocol_.setKeyEventCallback([this](int physicalKey, int logicalKey, bool pressed)
                                  { onKeyEvent(physicalKey, logicalKey, pressed); });

    while (1)
    {
        isNeedUpdateDisplay = 0;
        static uint32_t lastHostConnectionStatusMs = 0;
        static uint32_t lastHostConnectKickMs = 0;

        // MIC读取
        //  int16_t samples[BUFFER_SIZE];
        //  size_t samples_read = mic_.Read(samples, BUFFER_SIZE);
        //  // 处理音频数据
        //  if (samples_read > 0) {
        //      audioAnalyzer_.process(samples, samples_read);
        //      // 发送频谱数据到显示任务
        //      SendSpectrumDisplay(audioAnalyzer_.getBands(), NUM_BANDS);
        //  }

        // LOG_DEBUG("Log", "PCM samples_read=%d start: ", samples_read);
        // for (int i = 0; i < samples_read; i++) {
        //     LOG_DEBUG("Log","%x ",samples[i]);  // 示例：打印PCM数据
        // }
        // LOG_DEBUG("Log", "PCM end");

        // 保留本地提示音/短音频播放能力。
        speaker_.Loop();

        const uint8_t musicControlRequest = ui_MusicScreenSecondary_consume_control_request();
        if (musicControlRequest == UI_MUSIC_CONTROL_PREV)
        {
            SendMusicControlCommand("previous");
        }
        else if (musicControlRequest == UI_MUSIC_CONTROL_TOGGLE)
        {
            if (musicPlayerState_.connected)
            {
                updateLocalMusicProgress(millis());
                const bool nextPlayingState = !musicPlayerState_.is_playing;
                musicPlayerState_.is_playing = nextPlayingState;
                musicPlayerState_.is_paused = !nextPlayingState;
                lastMusicStatusRxMs_ = millis();
                lastMusicProgressTickMs_ = lastMusicStatusRxMs_;
                SendMusicPlayerUpdate(true);
            }
            SendMusicControlCommand("toggle");
        }
        else if (musicControlRequest == UI_MUSIC_CONTROL_NEXT)
        {
            SendMusicControlCommand("next");
        }

        // 板载EC11用于界面导航。
        rotaryEncoder_.Loop();
        updateMusicUiAsrOwnership();

        // 更新协议处理
        protocol_.update();

        ui_settings_snapshot_t pendingSettings{};
        bool persistUiSettings = false;
        if (consumeUiSettingsRequest(pendingSettings, persistUiSettings))
        {
            applyUiSettingsSnapshot(pendingSettings, persistUiSettings);
        }

        const uint32_t nowMs = millis();
        if (nowMs - lastBatteryStatusMs_ >= 5000)
        {
            lastBatteryStatusMs_ = nowMs;
            SendBatteryStatusUpdate();
        }
        processWiFiReconnect(nowMs);
        ProcessTimeSync(nowMs);
        updateLocalMusicProgress(nowMs);
        if (configuration_.settings_.connect_host && WiFi.status() == WL_CONNECTED && !protocol_.isTcpConnected())
        {
            if (nowMs - lastHostConnectKickMs >= 3000)
            {
                lastHostConnectKickMs = nowMs;
                updateProtocolTcpEndpoint();
            }
        }
        if (nowMs - lastHostConnectionStatusMs >= 2500)
        {
            lastHostConnectionStatusMs = nowMs;
            SendHostConnectionUpdate();
        }
        if (musicPlayerState_.connected && lastMusicStatusRxMs_ > 0 && (nowMs - lastMusicStatusRxMs_ > 30000))
        {
            musicPlayerState_.connected = false;
            musicPlayerState_.is_playing = false;
            musicPlayerState_.is_paused = false;
            musicPlayerState_.current_seconds = 0;
            musicPlayerState_.total_seconds = 0;
            lastMusicProgressTickMs_ = 0;
            snprintf(musicPlayerState_.title, sizeof(musicPlayerState_.title), "%s", "PLAYER OFFLINE");
            musicPlayerState_.artist[0] = '\0';
            musicPlayerState_.lyric_current[0] = '\0';
            musicPlayerState_.lyric_next[0] = '\0';
            SendMusicPlayerUpdate(true);
        }
        SendMusicPlayerUpdate(false);

        // todo:增加模块设备配置判断

        uint32_t key_value = 0;
        uint32_t changes = scanner_.scan();
        key_value = scanner_.getStableState();
        uint32_t host_key_value = key_value;

        uint32_t pressedEdges = (~lastStableKeyState_) & key_value;
        uint32_t releasedEdges = lastStableKeyState_ & (~key_value);
        if (ui_get_active_screen_tag() == UI_SCREEN_SETTING_SECONDARY)
        {
            if (pressedEdges & kSettingsUiKey1Bit)
            {
                SendDisplayAction(LV_KEY_LEFT);
            }
            if (pressedEdges & kSettingsUiKey2Bit)
            {
                SendDisplayAction(LV_KEY_RIGHT);
            }
            pressedEdges &= ~kSettingsUiOverrideMask;
            releasedEdges &= ~kSettingsUiOverrideMask;
            host_key_value &= ~kSettingsUiOverrideMask;
        }
        reportPhysicalKeyEdges(pressedEdges, true);
        reportPhysicalKeyEdges(releasedEdges, false);
        if (pressedEdges & voiceTriggerBit_)
        {
            startVoiceCapture();
        }

        if (voiceCaptureActive_)
        {
            voiceRecognizer_.feedCapture();
        }

        if (releasedEdges & voiceTriggerBit_)
        {
            finishVoiceCapture();
        }
        lastStableKeyState_ = key_value;

        // 触发键只用于语音，不再透传成普通按键。
        host_key_value &= ~voiceTriggerBit_;
        if (changes && currentKeyboard_)
        {
            // LOG_DEBUG("Log","key_value = %x", key_value);
            handleKeyEvent(host_key_value);
        }

        // 当需要更新显示时
        // if(isNeedUpdateDisplay) {
        // sendDisplayUpdate();
        //}

        usleep(100);
    }
}

void MainTask::SendDisplayAction(uint8_t action)
{
    DisplayMessage msg;
    msg.type = uint8_t(MainCommand::ACTION_INPUT);
    msg.action = action;
    // 发送消息到显示任务
    if (message_queue_ != nullptr)
    {
        xQueueSend(message_queue_, &msg, portMAX_DELAY);
    }
}

void MainTask::SendDisplayKeyInput(uint32_t key_value)
{
    DisplayMessage msg;
    msg.type = uint8_t(MainCommand::KEY_INPUT);
    msg.key_value = key_value;
    // 发送消息到显示任务
    if (message_queue_ != nullptr)
    {
        xQueueSend(message_queue_, &msg, portMAX_DELAY);
    }
}

void MainTask::SendAsrRecordingState(bool isRecording)
{
    DisplayMessage msg;
    msg.type = uint8_t(MainCommand::ASR_RECORDING_STATE);
    msg.asr_recording = isRecording;
    if (message_queue_ != nullptr)
    {
        xQueueSend(message_queue_, &msg, portMAX_DELAY);
    }
}

void MainTask::SendDisplaySetting(const DeviceSettings &setting)
{
    DisplayMessage msg{};
    msg.type = uint8_t(MainCommand::SETTING_UPDATE);
    msg.setting.work_mode = setting.work_mode;
    msg.setting.rgb_mode = setting.rgb_mode;
    msg.setting.rgb_click_mode = setting.rgb_click_mode;
    msg.setting.rgb_brightness = setting.rgb_brightness;
    msg.setting.tft_theme = setting.tft_theme;
    msg.setting.tft_brightness = setting.tft_brightness;
    msg.setting.device_volume = setting.device_volume;
    msg.setting.power_mode = setting.power_mode;
    msg.setting.connect_host = setting.connect_host;
    msg.setting.voice_enable = setting.voice_enable;
    msg.setting.active_keymap_profile = setting.active_keymap_profile;
    snprintf(msg.setting.rgb_single_color, sizeof(msg.setting.rgb_single_color), "%s", setting.rgb_single_colar.c_str());
    // 发送消息到显示任务
    if (message_queue_ != nullptr)
    {
        if (xQueueSend(message_queue_, &msg, 0) != pdPASS)
        {
            LOG_WARNING("Display", "Drop SETTING_UPDATE: display queue full");
        }
    }
}

bool MainTask::consumeUiSettingsRequest(ui_settings_snapshot_t &snapshot, bool &persist)
{
    bool hasPending = false;

    taskENTER_CRITICAL(&g_ui_settings_lock);
    if (g_ui_settings_request.pending)
    {
        snapshot = g_ui_settings_request.snapshot;
        persist = g_ui_settings_request.persist;
        g_ui_settings_request.pending = false;
        g_ui_settings_request.persist = false;
        hasPending = true;
    }
    taskEXIT_CRITICAL(&g_ui_settings_lock);

    return hasPending;
}

void MainTask::applyUiSettingsSnapshot(const ui_settings_snapshot_t &requested, bool persist)
{
    ui_settings_snapshot_t snapshot = requested;
    bool settingChanged = false;
    bool voiceConfigChanged = false;
    bool keymapProfileChanged = false;
    bool powerModeChanged = false;
    const uint8_t requestedProfile = requested.active_keymap_profile >= CONFIG_PROFILE_COUNT
                                         ? 0
                                         : requested.active_keymap_profile;
    const bool shouldSwitchProfile = requestedProfile != configuration_.settings_.active_keymap_profile;

    snapshot.work_mode = constrain(snapshot.work_mode,
                                   static_cast<int32_t>(Configuration::WIRED_KEYBOARD_MODE),
                                   static_cast<int32_t>(Configuration::WIRELESS_2_4G_KEYBOARD_MODE));
    snapshot.rgb_mode = constrain(snapshot.rgb_mode,
                                  static_cast<int32_t>(Configuration::RGB_NONE_MODE),
                                  static_cast<int32_t>(Configuration::RGB_PULSE_MODE));
    snapshot.rgb_click_mode = constrain(snapshot.rgb_click_mode,
                                        static_cast<int32_t>(Configuration::CLICK_NONE_COLOR_MODE),
                                        static_cast<int32_t>(Configuration::CLICK_WARE_COLOR_MODE));
    snapshot.rgb_brightness = constrain(snapshot.rgb_brightness, 0, 100);
    snapshot.tft_theme = constrain(snapshot.tft_theme, 1, 3);
    snapshot.tft_brightness = constrain(snapshot.tft_brightness, 5, 100);
    snapshot.device_volume = constrain(snapshot.device_volume, 0, 100);
    snapshot.power_mode = constrain(snapshot.power_mode,
                                    static_cast<int32_t>(Configuration::NORMAL_POWER_MODE),
                                    static_cast<int32_t>(Configuration::DEEPSLEEP_POWER_MODE));
    snapshot.active_keymap_profile = requestedProfile;
    snapshot.rgb_single_color[sizeof(snapshot.rgb_single_color) - 1] = '\0';

    if (xSemaphoreTake(configuration_.mutex_, portMAX_DELAY) == pdTRUE)
    {
        if (configuration_.settings_.work_mode != snapshot.work_mode)
        {
            setWorkMode(static_cast<Configuration::WORK_MODE>(snapshot.work_mode));
            configuration_.settings_.work_mode = snapshot.work_mode;
            settingChanged = true;
        }
        if (configuration_.settings_.rgb_mode != snapshot.rgb_mode)
        {
            configuration_.settings_.rgb_mode = snapshot.rgb_mode;
            settingChanged = true;
        }
        if (configuration_.settings_.rgb_click_mode != snapshot.rgb_click_mode)
        {
            configuration_.settings_.rgb_click_mode = snapshot.rgb_click_mode;
            settingChanged = true;
        }
        if (!configuration_.settings_.rgb_single_colar.equals(snapshot.rgb_single_color))
        {
            configuration_.settings_.rgb_single_colar = snapshot.rgb_single_color;
            settingChanged = true;
        }
        if (configuration_.settings_.rgb_brightness != snapshot.rgb_brightness)
        {
            configuration_.settings_.rgb_brightness = snapshot.rgb_brightness;
            settingChanged = true;
        }
        if (configuration_.settings_.tft_theme != snapshot.tft_theme)
        {
            configuration_.settings_.tft_theme = snapshot.tft_theme;
            settingChanged = true;
        }
        if (configuration_.settings_.tft_brightness != snapshot.tft_brightness)
        {
            configuration_.settings_.tft_brightness = snapshot.tft_brightness;
            settingChanged = true;
        }
        if (configuration_.settings_.device_volume != snapshot.device_volume)
        {
            configuration_.settings_.device_volume = snapshot.device_volume;
            settingChanged = true;
        }
        if (configuration_.settings_.power_mode != snapshot.power_mode)
        {
            configuration_.settings_.power_mode = snapshot.power_mode;
            settingChanged = true;
            powerModeChanged = true;
        }
        if (configuration_.settings_.connect_host != snapshot.connect_host)
        {
            configuration_.settings_.connect_host = snapshot.connect_host;
            settingChanged = true;
            if (!snapshot.connect_host)
            {
                protocol_.disableTcpClient();
            }
            else if (WiFi.status() == WL_CONNECTED)
            {
                updateProtocolTcpEndpoint();
            }
        }
        if (configuration_.settings_.voice_enable != snapshot.voice_enable)
        {
            configuration_.settings_.voice_enable = snapshot.voice_enable;
            settingChanged = true;
            voiceConfigChanged = true;
        }
        xSemaphoreGive(configuration_.mutex_);
    }

    if (powerModeChanged)
    {
        applyPowerMode(static_cast<Configuration::POWER_MODE>(configuration_.settings_.power_mode));
    }

    if (shouldSwitchProfile && configuration_.switchActiveProfile(requestedProfile))
    {
        settingChanged = true;
        voiceConfigChanged = true;
        keymapProfileChanged = true;
    }

    if (voiceConfigChanged)
    {
        applyVoiceConfig();
    }
    if (settingChanged || voiceConfigChanged)
    {
        reconcileVoiceRuntimeState();
        speaker_.SetVolume(configuration_.settings_.device_volume / 5);
        SendDisplaySetting(configuration_.settings_);
        SendMusicPlayerUpdate(true);
        SendHostConnectionUpdate();
        if (persist)
        {
            configuration_.SaveSetting();
            sendCurrentConfigSnapshot(0);
        }
    }
    else if (persist)
    {
        configuration_.SaveSetting();
        sendCurrentConfigSnapshot(0);
    }
    if (keymapProfileChanged)
    {
        SendKeyMappedProfileUi();
        sendCurrentProfileState(0);
        sendCurrentKeymapSnapshot(0);
    }
}


void MainTask::SendHostConnectionUpdate()
{
    DisplayMessage msg{};
    msg.type = uint8_t(MainCommand::HOST_CONNECTION_UPDATE);
    msg.host_connected = protocol_.isTcpConnected();
    if (message_queue_ != nullptr)
    {
        if (xQueueSend(message_queue_, &msg, 0) != pdPASS)
        {
            LOG_WARNING("Display", "Drop HOST_CONNECTION_UPDATE: display queue full");
        }
    }
}

// // 添加发送频谱数据的函数
// void MainTask::SendSpectrumDisplay(float* bands, int numBands) {
//     DisplayMessage msg;
//     msg.type = uint8_t(MainCommand::SPECTRUM_DISPLAY);
//     memcpy(msg.spectrumBands, bands, sizeof(float) * numBands);
//     msg.numBands = numBands;

//     if (message_queue_ != nullptr) {
//         xQueueSend(message_queue_, &msg, portMAX_DELAY);
//     }
// }

// void MainTask::sendDisplayUpdate() {
//     DisplayMessage msg;
//     msg.workMode = currentWorkMode_;
//     //msg.batteryVoltage = readBatteryVoltage();
//     //msg.isConnected = checkConnectionStatus();

//     // 发送消息到显示任务
//     if(message_queue_ != nullptr) {
//         xQueueSend(message_queue_, &msg, portMAX_DELAY);
//     }
// }
