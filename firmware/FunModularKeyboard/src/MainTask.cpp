#include "MainTask.h"
#include "BLEKeyboardImpl.h"
#include "USBKeyboardImpl.h"
#include "KeycodeCodec.h"
#include "SystemTime.h"
#include "ui/ui.h"
#include <ctype.h>
#include <time.h>

#include <WiFi.h>
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
}

void MainTask::onWiFiConnected()
{
    LOG_INFO("Log", "WiFi connected, IP=%s, RSSI=%d", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    wifiConnectAttemptStartedMs_ = 0;
    wifiNextRetryAtMs_ = 0;
    StartTimeSync();
    reconcileVoiceRuntimeState();
}

void MainTask::processWiFiReconnect(uint32_t nowMs)
{
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
            onWiFiConnected();
        }
        return;
    }

    if (wifiWasConnected_)
    {
        wifiWasConnected_ = false;
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

    // 屏幕反馈不能阻塞 HID 输入路径。
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

    if (hasNonAsciiUtf8(recognizedText))
    {
        LOG_WARNING("ASR", "Non-ASCII voice text cannot be typed without a host bridge");
    }

    // 保留ASCII直打能力作兜底：若上位机未连接且是ASCII文本，尝试HID输入。
    else if (!sendAsciiTextToHost(recognizedText))
    {
        LOG_WARNING("ASR", "Voice text HID output failed");
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

bool MainTask::profileIconExists(uint8_t profileIndex) const
{
    return SPIFFS.exists(Configuration::getProfileIconPath(profileIndex));
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

void MainTask::run()
{

    logHeapSnapshot("run:start");

    applyPowerMode(static_cast<Configuration::POWER_MODE>(configuration_.settings_.power_mode));

    batteryMonitor_.begin();

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
    while (1)
    {
        isNeedUpdateDisplay = 0;

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

        // 板载EC11用于界面导航。
        rotaryEncoder_.Loop();

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
        xQueueSend(message_queue_, &msg, 0);
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
        xQueueSend(message_queue_, &msg, 0);
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
        if (persist)
        {
            configuration_.SaveSetting();
        }
    }
    else if (persist)
    {
        configuration_.SaveSetting();
    }
    if (keymapProfileChanged)
    {
        SendKeyMappedProfileUi();
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
