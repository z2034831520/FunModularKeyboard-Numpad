#pragma once
#include <Arduino.h>
#include <SPIFFS.h>
#include <SimpleIni.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "MatrixScanner.h"
#include "LogManager.h"

#define CONFIG_ALL_KEY_NUM   26   //包含其他模块的按键总数量
#define CONFIG_SPECIAL_INPUT_NUM 15
#define CONFIG_PROFILE_COUNT 8


struct KeyMapping {
    String function_key{};
    uint8_t normal_key[6]; // Up to 6 normal keys
    uint8_t macros_key[5]; // Up to 5 macros keys    
    uint8_t normal_key_count = 0;
    uint8_t macros_key_count = 0;
};

struct DeviceSettings {
    bool wifi_switch = 0;//0关闭wifi，1连接wifi
    bool connect_host = 1;//0不连接上位机，1周期连接上位机TCP服务端
    String wifi_ssid = "";
    String wifi_password = "";
    int work_mode = 0;//enum WORKMODE
    int rgb_mode = 0;//enum RGB_MODE，为0，则代表关闭LED背景显示，不影响按键点击显示效果，否则按照模式显示rgb
    String rgb_single_colar = "0";
    int rgb_click_mode = 0;//enum RGB_CLICK_MODE,单个按键点击后配置的显示颜色
    int rgb_brightness = 80;//0~100
    int tft_theme = 1;//1~3
    int tft_brightness = 80;//5~100
    int device_volume = 50;//0~100
    int power_mode = 0;//enum POWER_MODE

    bool voice_enable = 1;
    int voice_trigger_key = 16;     // 1-based physical key index
    int voice_max_record_ms = 6000; // max hold duration
    int voice_auto_enter = 1;
    int voice_dev_pid = 1537;
    String voice_cuid = "FunModularKeyboard";
    String voice_baidu_api_key = "";
    String voice_baidu_secret_key = "";
    int pc_status_mask = 0;
    uint8_t active_keymap_profile = 0;
};

 enum PLUGIN_MODULE {
    MODA = 0,
    MODB,
};   

struct MODULESTATUS {
    PLUGIN_MODULE mod_type;
    bool status;
};

class Configuration {
public:
    enum KEY_TYPE {
        NORMAL_KEY = 0,
        MACROS_KEY,
        FUNCTION_KEY,
    };

    enum WORK_MODE {
        WIRED_KEYBOARD_MODE = 0,
        BLUETOOTH_KEYBOARD_MODE,
        WIRELESS_2_4G_KEYBOARD_MODE,
        NONE_MODE,
    };

    enum RGB_MODE {
        RGB_NONE_MODE = 0,
        RGB_SINGLE_MODE,
        RGB_RAINBOW_MODE,
        RGB_RAINBOWWARE_MODE,
        RGB_COLORCYCLE_MODE,
        RGB_METER_MODE,
        RGB_FIRE_MODE,
        RGB_PULSE_MODE,
    };

    enum RGB_CLICK_MODE {
        CLICK_NONE_COLOR_MODE = 0,
        CLICK_SINGLE_COLOR_MODE,
        CLICK_WARE_COLOR_MODE,
    };

    enum POWER_MODE {
        NORMAL_POWER_MODE = 0,//正常模式
        LOW_POWER_MODE,//低功耗模式:关闭rgb和屏幕等耗电模块
        LIGHT_POWER_MODE,//浅休眠模式：短暂暂停功能，可快速恢复
        DEEPSLEEP_POWER_MODE,//深度休眠模式：适合长时间待机
    };
public:
    Configuration();
    ~Configuration();

    bool load(const char* path = "/config.ini");
    bool SaveKeyMapping(const char* path = "/config.ini");
    bool SaveSetting(const char* path = "/config.ini");
    bool loadActiveProfileKeyMapping(const char* fallbackPath = "/config.ini");
    bool switchActiveProfile(uint8_t profileIndex, const char* fallbackPath = "/config.ini");
    static String getProfileConfigPath(uint8_t profileIndex);
    static String getProfileIconPath(uint8_t profileIndex);
    static const char* getProfileDisplayName(uint8_t profileIndex);
    
    void InitSPIFFS();
    String getDeviceName() const { return device_name_; }
    DeviceSettings& getSettings() { return settings_; }
    KeyMapping getKeyMapping(uint8_t physical_key) const;
    KeyMapping getSpecialInputMapping(const String& input_id) const;
    KeyMapping* getMutableSpecialInputMapping(const String& input_id);
    static int getSpecialInputIndex(const String& input_id);
    static const char* getSpecialInputId(uint8_t index);

    bool parseKeyMapping(Configuration::KEY_TYPE key_type, const String& value, KeyMapping& mapping);
    bool writeSection(CSimpleIniA& ini, const char* section);

    // 辅助函数：加载配置但不解析，只用于保存
    bool loadRawIni(CSimpleIniA& ini, const char* path);
    // 辅助函数：保存INI到文件
    bool saveIniToFile(CSimpleIniA& ini, const char* path);

    SemaphoreHandle_t mutex_;
    String device_name_;
    String device_id_;
    String firmware_version_;
    KeyMapping key_mappings_[CONFIG_ALL_KEY_NUM];
    KeyMapping special_key_mappings_[CONFIG_SPECIAL_INPUT_NUM];
    DeviceSettings settings_;

    int all_key_num_{16}; 
};
