#pragma once
#include <Arduino.h>
#include <map>
#include <string>
#include <atomic>
#include <memory> // 添加智能指针头文件
// #include <BleKeyboard.h>
#include "MatrixScanner.h"
#include "configuration.h"
#include "task.h"
#include "message_types.h"
#include "RGBLightControl.h"
#include "Speaker.h"
#include "RotaryEncoder.h"
#include "LogManager.h"
#include "IKeyboard.h"
#include "VoiceRecognizer.h"
#include "BatteryMonitor.h"

class MainTask : public Task<MainTask>
{
    friend class Task<MainTask>;

public:
    MainTask(const uint8_t task_core, Configuration &configuration);
    ~MainTask();

    // press()函数中输入的值会做处理，输入的不是实际的HID Usage ID，
    // 原有库的_asciimap表里映射不完整，比如Enter等是没有映射的，
    // 所以需要根据press函数的处理反推应该输入的值最终转为实际的HID Usage ID

    std::map<std::string, uint8_t> keyMapTable = {
        // === 字母键 (使用ASCII字符) ===
        {"a", 'a'},
        {"b", 'b'},
        {"c", 'c'},
        {"d", 'd'},
        {"e", 'e'},
        {"f", 'f'},
        {"g", 'g'},
        {"h", 'h'},
        {"i", 'i'},
        {"j", 'j'},
        {"k", 'k'},
        {"l", 'l'},
        {"m", 'm'},
        {"n", 'n'},
        {"o", 'o'},
        {"p", 'p'},
        {"q", 'q'},
        {"r", 'r'},
        {"s", 's'},
        {"t", 't'},
        {"u", 'u'},
        {"v", 'v'},
        {"w", 'w'},
        {"x", 'x'},
        {"y", 'y'},
        {"z", 'z'},

        // === 大写字母键 (使用ASCII字符，库会自动处理Shift) ===
        {"A", 'A'},
        {"B", 'B'},
        {"C", 'C'},
        {"D", 'D'},
        {"E", 'E'},
        {"F", 'F'},
        {"G", 'G'},
        {"H", 'H'},
        {"I", 'I'},
        {"J", 'J'},
        {"K", 'K'},
        {"L", 'L'},
        {"M", 'M'},
        {"N", 'N'},
        {"O", 'O'},
        {"P", 'P'},
        {"Q", 'Q'},
        {"R", 'R'},
        {"S", 'S'},
        {"T", 'T'},
        {"U", 'U'},
        {"V", 'V'},
        {"W", 'W'},
        {"X", 'X'},
        {"Y", 'Y'},
        {"Z", 'Z'},

        // === 数字键 (使用ASCII字符) ===
        {"NUM_0", '0'},
        {"NUM_1", '1'},
        {"NUM_2", '2'},
        {"NUM_3", '3'},
        {"NUM_4", '4'},
        {"NUM_5", '5'},
        {"NUM_6", '6'},
        {"NUM_7", '7'},
        {"NUM_8", '8'},
        {"NUM_9", '9'},

        // === 符号键 (使用ASCII字符) ===
        {"Space", ' '}, // 空格
        {",", ','},
        {".", '.'},
        {";", ';'},
        {"'", '\''},
        {"[", '['},
        {"]", ']'},
        {"\\", '\\'},
        {"/", '/'},
        {"-", '-'},
        {"=", '='},
        {"`", '`'},

        // === 控制字符 (使用ASCII码) ===
        {"Enter", 0xB0},     // 回车
        {"Backspace", 0xB2}, // 退格
        {"Tab", 0xB3},       // Tab
        {"Esc", 0xB1},       // ESC

        // === 功能键 (需要使用库常量) ===KEY_F1~KEY_F12
        {"F1", 0xC2},
        {"F2", 0xC3},
        {"F3", 0xC4},
        {"F4", 0xC5},
        {"F5", 0xC6},
        {"F6", 0xC7},
        {"F7", 0xC8},
        {"F8", 0xC9},
        {"F9", 0xCA},
        {"F10", 0xCB},
        {"F11", 0xCC},
        {"F12", 0xCD},

        // === 方向键 (需要使用库常量) ===
        {"Up", 0xDA},    // KEY_UP_ARROW
        {"Down", 0xD9},  // KEY_DOWN_ARROW
        {"Left", 0xD8},  // KEY_LEFT_ARROW
        {"Right", 0xD7}, // KEY_RIGHT_ARROW

        // === 编辑键 (需要使用库常量) ===
        {"Insert", 0xD1},   // KEY_INSERT
        {"Delete", 0xD4},   // KEY_DELETE
        {"Home", 0xD2},     // KEY_HOME
        {"End", 0xD5},      // KEY_END
        {"PageUp", 0xD3},   // KEY_PAGE_UP
        {"PageDown", 0xD6}, // KEY_PAGE_DOWN

        // === 锁定键 (需要使用库常量) ===
        {"CapsLock", 0xC1},   // KEY_CAPS_LOCK
        {"NumLock", 0xDB},    // 注意：这个可能需要验证
        {"ScrollLock", 0xCF}, // KEY_SCROLL_LOCK

        // === 其他键 ===
        {"PrintScreen", 0xCE}, // KEY_PRINT_SCREEN
        {"Pause", 0xD0},       // KEY_PAUSE
        {"Menu", 0xED},        // KEY_MENU

        // === 修饰键 (使用库的修饰键常量) ===
        {"Ctrl", 0x80},
        {"Control", 0x80},
        {"Shift", 0x81},
        {"Alt", 0x82},
        {"Win", 0x83},
        {"Windows", 0x83},
        {"Gui", 0x83},

        // 左右修饰键 - 如果需要区分左右，使用正确的库常量
        {"LCtrl", 0x80},
        {"LControl", 0x80},
        {"LeftCtrl", 0x80},
        {"RCtrl", 0x84},
        {"RControl", 0x84},
        {"RightCtrl", 0x84},
        {"LShift", 0x81},
        {"LeftShift", 0x81},
        {"RShift", 0x85},
        {"RightShift", 0x85},
        {"LAlt", 0x82},
        {"LeftAlt", 0x82},
        {"RAlt", 0x86},
        {"RightAlt", 0x86},
        {"LWin", 0x83},
        {"LeftWin", 0x83},
        {"LeftWindows", 0x83},
        {"RWin", 0x87},
        {"RightWin", 0x87},
        {"RightWindows", 0x87}};

    // enum FUNCTION_KEY {
    //     KEY_MEDIA_NEXT_TRACK = 1,
    //     KEY_MEDIA_PREVIOUS_TRACK,
    //     KEY_MEDIA_STOP,
    //     KEY_MEDIA_PLAY_PAUSE,
    //     KEY_MEDIA_MUTE,
    //     KEY_MEDIA_VOLUME_UP,
    //     KEY_MEDIA_VOLUME_DOWN,
    //     KEY_MEDIA_WWW_HOME,
    //     KEY_MEDIA_LOCAL_MACHINE_BROWSER, // Opens "My Computer" on Windows
    //     KEY_MEDIA_CALCULATOR,
    //     KEY_MEDIA_WWW_BOOKMARKS,
    //     KEY_MEDIA_WWW_SEARCH,
    //     KEY_MEDIA_WWW_STOP,
    //     KEY_MEDIA_WWW_BACK,
    //     KEY_MEDIA_CONSUMER_CONTROL_CONFIGURATION,// Media Selection
    //     KEY_MEDIA_EMAIL_READER,
    // };

    // 设置消息队列
    void setMessageQueue(QueueHandle_t queue)
    {
        message_queue_ = queue;
    }

    // 转换函数
    uint8_t stringToKeycode(const std::string &keyStr)
    {
        auto it = keyMapTable.find(keyStr);
        if (it != keyMapTable.end())
        {
            return it->second;
        }

        if (keyStr.empty() || keyStr == "0")
        {
            return 0;
        }

        char *end = nullptr;
        unsigned long value = 0;

        if (keyStr.size() > 2 && keyStr[0] == '0' && (keyStr[1] == 'x' || keyStr[1] == 'X'))
        {
            value = strtoul(keyStr.c_str() + 2, &end, 16);
            if (end != nullptr && *end == '\0' && value <= 0xFF)
            {
                return static_cast<uint8_t>(value);
            }
        }

        value = strtoul(keyStr.c_str(), &end, 16);
        if (end != nullptr && *end == '\0' && value <= 0xFF)
        {
            return static_cast<uint8_t>(value);
        }

        value = strtoul(keyStr.c_str(), &end, 10);
        if (end != nullptr && *end == '\0' && value <= 0xFF)
        {
            return static_cast<uint8_t>(value);
        }

        return 0; // 返回0表示未找到
    }

    // 设置工作模式（可在运行时调用）
    void setWorkMode(Configuration::WORK_MODE mode);
    void setBoost5VEnabled(bool enabled);
    bool IsStartupReady() const
    {
        return startupReady_.load(std::memory_order_acquire);
    }

protected:
    void run();

private:
    void applyPowerMode(Configuration::POWER_MODE mode);
    void handleKeyEvent(uint32_t key_value, uint32_t pressed_edges);
    void launchWindowsTarget(const char *target);
    bool hasMappedOutput(const KeyMapping &mapping) const;
    void triggerMappedInput(const KeyMapping &mapping);
    void updateVoiceTriggerBitFromKeymap();
    void startVoiceCapture();
    void finishVoiceCapture();
    void applyVoiceConfig();
    bool sendAsciiTextToHost(const String &text);
    void sendMediaKey(const char *key);
    static void HandleRotaryAction(RotaryAction action, void *context);
    void SendDisplayKeyInput(uint32_t key_value);
    void SendAsrRecordingState(bool isRecording);
    void SendBatteryStatusUpdate();
    void SendDisplaySetting(const DeviceSettings &setting);
    // void SendSpectrumDisplay(float* bands, int numBands);
    // void sendDisplayUpdate();
    bool ConnectToWiFi(const String &ssid, const String &password);
    void scheduleWiFiConnectAttempt(bool immediate = false);
    void stopWiFiReconnect();
    void processWiFiReconnect(uint32_t nowMs);
    void onWiFiConnected();
    void StartTimeSync();
    void ProcessTimeSync(uint32_t nowMs);
    bool SyncTimeBeforeBluetoothStart();
    void reconcileVoiceRuntimeState();
    MatrixScanner scanner_;
    BatteryMonitor batteryMonitor_;
    std::unique_ptr<IKeyboard> currentKeyboard_{nullptr};
    Configuration::WORK_MODE currentWorkMode_;
    Configuration &configuration_;
    QueueHandle_t message_queue_;
    Speaker speaker_;
    RotaryEncoder rotaryEncoder_;
    // Mic mic_;
    bool isNeedUpdateDisplay{0};
    uint32_t lastStableKeyState_{0};
    uint32_t lastBatteryStatusMs_{0};
    bool voiceRecognitionBusy_{false};
    bool voiceCaptureActive_{false};
    bool voiceRecognizerStarted_{false};
    uint32_t voiceTriggerBit_{1UL << 15};
    bool wifiReconnectActive_{false};
    bool wifiWasConnected_{false};
    uint32_t wifiConnectAttemptStartedMs_{0};
    uint32_t wifiNextRetryAtMs_{0};
    bool timeSyncPending_{false};
    uint32_t timeSyncStartedMs_{0};
    uint32_t timeSyncNextCheckMs_{0};
    std::atomic<bool> startupReady_{false};
    bool boost5VPinInitialized_{false};
    bool boost5VEnabled_{true};
    VoiceRecognizer voiceRecognizer_;
};
