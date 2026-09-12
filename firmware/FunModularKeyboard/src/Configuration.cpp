#include "Configuration.h"
#include "KeycodeCodec.h"

namespace
{
    const char *const kProfileDisplayNames[CONFIG_PROFILE_COUNT] = {
        "APP 1",
        "APP 2",
        "APP 3",
        "APP 4",
        "APP 5",
        "APP 6",
        "APP 7",
        "APP 8"};

    String getLegacyProfileConfigPath(uint8_t profileIndex)
    {
        if (profileIndex >= CONFIG_PROFILE_COUNT)
        {
            profileIndex = 0;
        }
        return String("/config_profile_") + String(profileIndex) + String(".ini");
    }

    void resetKeyMappingState(KeyMapping &mapping)
    {
        mapping.function_key = "";
        mapping.normal_key_count = 0;
        mapping.macros_key_count = 0;
        memset(mapping.normal_key, 0, sizeof(mapping.normal_key));
        memset(mapping.macros_key, 0, sizeof(mapping.macros_key));
    }

    String buildKeySequence(const uint8_t *keys, uint8_t count)
    {
        return buildNamedKeySequence(keys, count);
    }
}

String Configuration::getProfileConfigPath(uint8_t profileIndex)
{
    if (profileIndex >= CONFIG_PROFILE_COUNT)
    {
        profileIndex = 0;
    }
    return String("/keymap") + String(profileIndex + 1) + String(".ini");
}

String Configuration::getProfileIconPath(uint8_t profileIndex)
{
    if (profileIndex >= CONFIG_PROFILE_COUNT)
    {
        profileIndex = 0;
    }
    return String("/profile_icon_") + String(profileIndex) + String(".png");
}

const char *Configuration::getProfileDisplayName(uint8_t profileIndex)
{
    if (profileIndex >= CONFIG_PROFILE_COUNT)
    {
        profileIndex = 0;
    }
    return kProfileDisplayNames[profileIndex];
}

Configuration::Configuration()
{
    mutex_ = xSemaphoreCreateMutex();
}

Configuration::~Configuration()
{
    if (mutex_)
        vSemaphoreDelete(mutex_);
}

void Configuration::InitSPIFFS()
{
    if (!SPIFFS.begin(true))
    {
        LOG_ERROR("Log", "SPIFFS mount failed, attempting low-level format...");
        SPIFFS.format();
        ESP.restart();
    }
}

bool Configuration::load(const char *path)
{
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        LOG_ERROR("Log", "[ERROR] Failed to acquire mutex");
        return false;
    }

    File file = SPIFFS.open(path);
    if (!file)
    {
        LOG_ERROR("Log", "[ERROR] Failed to open file: %s", path);
        xSemaphoreGive(mutex_);
        return false;
    }

    String content = file.readString();
    // LOG_DEBUG("Log","content: %s", content.c_str());
    file.close();

    CSimpleIniA ini;
    ini.SetUnicode();
    SI_Error rc = ini.LoadData(content.c_str(), content.length());
    if (rc < 0)
    {
        LOG_ERROR("Log", "[ERROR] Failed to parse INI file: %d", rc);
        xSemaphoreGive(mutex_);
        return false;
    }

    // Parse [device] section
    const char *device_name = ini.GetValue("device_info", "device_name", "");
    if (device_name)
    {
        device_name_ = device_name;
    }

    // Parse [settings] section
    const char *wifi_switch_str = ini.GetValue("settings", "wifi_switch", "0");
    settings_.wifi_switch = atoi(wifi_switch_str);

    const char *wifi_ssid_str = ini.GetValue("settings", "wifi_ssid", "");
    settings_.wifi_ssid = wifi_ssid_str;

    const char *wifi_password_str = ini.GetValue("settings", "wifi_password", "");
    settings_.wifi_password = wifi_password_str;

    const char *work_mode_str = ini.GetValue("settings", "work_mode", "0");
    settings_.work_mode = atoi(work_mode_str);

    const char *rgb_mode_str = ini.GetValue("settings", "rgb_mode", "0");
    settings_.rgb_mode = atoi(rgb_mode_str);

    const char *rgb_single_colar_str = ini.GetValue("settings", "rgb_single_colar", "0");
    settings_.rgb_single_colar = rgb_single_colar_str;

    const char *rgb_click_mode_str = ini.GetValue("settings", "rgb_click_mode", "0");
    settings_.rgb_click_mode = atoi(rgb_click_mode_str);

    const char *rgb_brightness_str = ini.GetValue("settings", "rgb_brightness", "80");
    settings_.rgb_brightness = atoi(rgb_brightness_str);

    const char *tft_theme_str = ini.GetValue("settings", "tft_theme", "1");
    settings_.tft_theme = atoi(tft_theme_str);

    const char *tft_brightness_str = ini.GetValue("settings", "tft_brightness", "80");
    settings_.tft_brightness = constrain(atoi(tft_brightness_str), 5, 100);

    const char *device_volume_str = ini.GetValue("settings", "device_volume", "80");
    settings_.device_volume = atoi(device_volume_str);

    const char *power_mode_str = ini.GetValue("settings", "power_mode", "0");
    settings_.power_mode = atoi(power_mode_str);


    const char *voice_enable_str = ini.GetValue("settings", "voice_enable", "1");
    settings_.voice_enable = atoi(voice_enable_str);

    const char *voice_trigger_key_str = ini.GetValue("settings", "voice_trigger_key", "16");
    settings_.voice_trigger_key = atoi(voice_trigger_key_str);

    const char *voice_max_record_ms_str = ini.GetValue("settings", "voice_max_record_ms", "6000");
    settings_.voice_max_record_ms = atoi(voice_max_record_ms_str);

    const char *voice_auto_enter_str = ini.GetValue("settings", "voice_auto_enter", "1");
    settings_.voice_auto_enter = atoi(voice_auto_enter_str);

    const char *voice_dev_pid_str = ini.GetValue("settings", "voice_dev_pid", "1537");
    settings_.voice_dev_pid = atoi(voice_dev_pid_str);

    const char *voice_cuid_str = ini.GetValue("settings", "voice_cuid", "FunModularKeyboard");
    settings_.voice_cuid = voice_cuid_str;

    const char *voice_baidu_api_key_str = ini.GetValue("settings", "voice_baidu_api_key", "");
    settings_.voice_baidu_api_key = voice_baidu_api_key_str;

    const char *voice_baidu_secret_key_str = ini.GetValue("settings", "voice_baidu_secret_key", "");
    settings_.voice_baidu_secret_key = voice_baidu_secret_key_str;

    const char *active_profile_str = ini.GetValue("settings", "active_keymap_profile", "0");
    settings_.active_keymap_profile = static_cast<uint8_t>(atoi(active_profile_str));
    if (settings_.active_keymap_profile >= CONFIG_PROFILE_COUNT)
    {
        settings_.active_keymap_profile = 0;
    }

    // The current hardware has no module A/B keys. Avoid std::stoi here so the
    // firmware does not require the C++ exception runtime for one integer.
    all_key_num_ = atoi(ini.GetValue("key_num", "all_key_num", "16"));
    if (all_key_num_ < 1 || all_key_num_ > CONFIG_ALL_KEY_NUM)
    {
        all_key_num_ = 16;
    }

    for (uint8_t i = 0; i < all_key_num_; i++)
    {
        resetKeyMappingState(key_mappings_[i]);
    }
    xSemaphoreGive(mutex_);
    return loadActiveProfileKeyMapping(path);
}

bool Configuration::loadActiveProfileKeyMapping(const char *fallbackPath)
{
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        LOG_ERROR("Log", "[ERROR] Failed to acquire mutex");
        return false;
    }

    for (uint8_t i = 0; i < all_key_num_; i++)
    {
        resetKeyMappingState(key_mappings_[i]);
    }
    CSimpleIniA ini;
    ini.SetUnicode();

    const String profilePath = getProfileConfigPath(settings_.active_keymap_profile);
    bool loadedProfile = loadRawIni(ini, profilePath.c_str());
    if (!loadedProfile)
    {
        const String legacyProfilePath = getLegacyProfileConfigPath(settings_.active_keymap_profile);
        loadedProfile = loadRawIni(ini, legacyProfilePath.c_str());
    }
    if (!loadedProfile)
    {
        File fallbackFile = SPIFFS.open(fallbackPath);
        if (!fallbackFile)
        {
            LOG_WARNING("Log", "Profile file %s missing and fallback %s unavailable",
                        profilePath.c_str(),
                        fallbackPath);
            xSemaphoreGive(mutex_);
            return false;
        }

        String content = fallbackFile.readString();
        fallbackFile.close();
        if (ini.LoadData(content.c_str(), content.length()) < 0)
        {
            LOG_ERROR("Log", "Failed to parse fallback keymap file: %s", fallbackPath);
            xSemaphoreGive(mutex_);
            return false;
        }
    }

    // Parse [key_mapping] & [key_macro] section
    for (uint8_t i = 0; i < all_key_num_; i++)
    {
        char key[4];
        snprintf(key, sizeof(key), "%d", i + 1);
        const char *mapping_normal_str = ini.GetValue("key_mapping", key, "");
        const char *mapping_mackey_str = ini.GetValue("key_macro", key, "");
        // LOG_WARNING("Log","[%d],mapping_normal_str: %s",i, mapping_normal_str);
        if (mapping_normal_str && strlen(mapping_normal_str) > 0)
        {
            if (isNamedKeySequence(String(mapping_normal_str)))
            {
                LOG_DEBUG("Log", " key %d = NORMAL_KEY, mapping_normal_str=%s", i, mapping_normal_str);
                parseKeyMapping(NORMAL_KEY, mapping_normal_str, key_mappings_[i]);
            }
            else
            {
                LOG_DEBUG("Log", " key %d = FUNCTION_KEY, mapping_normal_str=%s", i, mapping_normal_str);
                parseKeyMapping(FUNCTION_KEY, mapping_normal_str, key_mappings_[i]);
            }
        }
        if (mapping_mackey_str && strlen(mapping_mackey_str) > 0)
        {
            parseKeyMapping(MACROS_KEY, mapping_mackey_str, key_mappings_[i]);
        }
    }

    xSemaphoreGive(mutex_);
    return true;
}

bool Configuration::switchActiveProfile(uint8_t profileIndex, const char *fallbackPath)
{
    if (profileIndex >= CONFIG_PROFILE_COUNT)
    {
        return false;
    }

    settings_.active_keymap_profile = profileIndex;
    if (!loadActiveProfileKeyMapping(fallbackPath))
    {
        return false;
    }

    return SaveSetting(fallbackPath);
}

// 辅助函数：加载INI但不解析
bool Configuration::loadRawIni(CSimpleIniA &ini, const char *path)
{
    File file = SPIFFS.open(path);
    if (!file)
    {
        // 如果文件不存在，返回空INI对象
        return false;
    }

    String content = file.readString();
    file.close();

    SI_Error rc = ini.LoadData(content.c_str(), content.length());
    return (rc >= 0);
}

// 辅助函数：保存INI到文件
bool Configuration::saveIniToFile(CSimpleIniA &ini, const char *path)
{
    std::string content;
    ini.Save(content);

    File file = SPIFFS.open(path, "w");
    if (!file)
    {
        LOG_ERROR("Log", "[ERROR] Failed to create file: %s", path);
        return false;
    }

    file.print(content.c_str());
    file.close();
    return true;
}

bool Configuration::SaveKeyMapping(const char *path)
{
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        LOG_ERROR("Log", "[ERROR] Failed to acquire mutex");
        return false;
    }

    const String savePath = (strcmp(path, "/config.ini") == 0)
                                ? getProfileConfigPath(settings_.active_keymap_profile)
                                : String(path);

    CSimpleIniA ini;
    ini.SetUnicode();

    // 先加载现有的配置文件（如果存在）
    if (!loadRawIni(ini, savePath.c_str()))
    {
        LOG_DEBUG("Log", "Config file not found or empty, creating new one");
    }

    // Write [key_mapping] section
    for (uint8_t i = 0; i < all_key_num_; i++)
    {
        char key[4];
        snprintf(key, sizeof(key), "%d", i + 1);
        String mapping_str = key_mappings_[i].function_key;
        if (mapping_str.isEmpty())
        {
            mapping_str = buildKeySequence(key_mappings_[i].normal_key, key_mappings_[i].normal_key_count);
        }
        ini.SetValue("key_mapping", key, mapping_str.c_str());
    }

    for (uint8_t i = all_key_num_; i < CONFIG_ALL_KEY_NUM; i++)
    {
        char key[4];
        snprintf(key, sizeof(key), "%d", i + 1);
        ini.SetValue("key_mapping", key, "");
    }

    // Write [key_macro] section
    for (uint8_t i = 0; i < all_key_num_; i++)
    {
        String mapping_str = buildKeySequence(key_mappings_[i].macros_key, key_mappings_[i].macros_key_count);

        char key[4];
        snprintf(key, sizeof(key), "%d", i + 1);
        ini.SetValue("key_macro", key, mapping_str.c_str());
    }

    for (uint8_t i = all_key_num_; i < CONFIG_ALL_KEY_NUM; i++)
    {
        char key[4];
        snprintf(key, sizeof(key), "%d", i + 1);
        ini.SetValue("key_macro", key, "");
    }

    // 保存到文件
    bool result = saveIniToFile(ini, savePath.c_str());
    if (result)
    {
        const String legacyPath = getLegacyProfileConfigPath(settings_.active_keymap_profile);
        if (legacyPath != savePath && SPIFFS.exists(legacyPath))
        {
            SPIFFS.remove(legacyPath);
        }
    }

    xSemaphoreGive(mutex_);

    if (result)
    {
        LOG_INFO("Log", "Key mappings saved successfully");
    }
    else
    {
        LOG_ERROR("Log", "Failed to save key mappings");
    }

    return result;
}

bool Configuration::SaveSetting(const char *path)
{
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        LOG_ERROR("Log", "[ERROR] Failed to acquire mutex");
        return false;
    }

    CSimpleIniA ini;
    ini.SetUnicode();

    // 先加载现有的配置文件（如果存在）
    if (!loadRawIni(ini, path))
    {
        LOG_DEBUG("Log", "Config file not found or empty, creating new one");
    }

    // Write [settings] section
    ini.SetValue("settings", "wifi_switch", String(settings_.wifi_switch, DEC).c_str());
    ini.SetValue("settings", "wifi_ssid", settings_.wifi_ssid.c_str());
    ini.SetValue("settings", "wifi_password", settings_.wifi_password.c_str());
    ini.SetValue("settings", "work_mode", String(settings_.work_mode, DEC).c_str());
    ini.SetValue("settings", "rgb_mode", String(settings_.rgb_mode, DEC).c_str());
    ini.SetValue("settings", "rgb_single_colar", settings_.rgb_single_colar.c_str());
    ini.SetValue("settings", "rgb_click_mode", String(settings_.rgb_click_mode, DEC).c_str());
    ini.SetValue("settings", "rgb_brightness", String(settings_.rgb_brightness, DEC).c_str());
    ini.SetValue("settings", "tft_theme", String(settings_.tft_theme, DEC).c_str());
    settings_.tft_brightness = constrain(settings_.tft_brightness, 5, 100);
    ini.SetValue("settings", "tft_brightness", String(settings_.tft_brightness, DEC).c_str());
    ini.SetValue("settings", "device_volume", String(settings_.device_volume, DEC).c_str());
    ini.SetValue("settings", "power_mode", String(settings_.power_mode, DEC).c_str());
    ini.SetValue("settings", "voice_enable", String(settings_.voice_enable, DEC).c_str());
    ini.SetValue("settings", "voice_trigger_key", String(settings_.voice_trigger_key, DEC).c_str());
    ini.SetValue("settings", "voice_max_record_ms", String(settings_.voice_max_record_ms, DEC).c_str());
    ini.SetValue("settings", "voice_auto_enter", String(settings_.voice_auto_enter, DEC).c_str());
    ini.SetValue("settings", "voice_dev_pid", String(settings_.voice_dev_pid, DEC).c_str());
    ini.SetValue("settings", "voice_cuid", settings_.voice_cuid.c_str());
    ini.SetValue("settings", "voice_baidu_api_key", settings_.voice_baidu_api_key.c_str());
    ini.SetValue("settings", "voice_baidu_secret_key", settings_.voice_baidu_secret_key.c_str());
    ini.SetValue("settings", "active_keymap_profile", String(settings_.active_keymap_profile, DEC).c_str());

    // 保存到文件
    bool result = saveIniToFile(ini, path);

    xSemaphoreGive(mutex_);

    if (result)
    {
        LOG_INFO("Log", "Settings saved successfully");
    }
    else
    {
        LOG_ERROR("Log", "Failed to save settings");
    }

    return result;
}

bool Configuration::parseKeyMapping(KEY_TYPE key_type, const String &value, KeyMapping &mapping)
{
    int pos = 0;
    if (key_type == FUNCTION_KEY)
    {
        mapping.function_key = value;
    }
    else
    {
        while (pos < value.length())
        {
            int end_pos = value.indexOf('+', pos);
            if (end_pos == -1)
                end_pos = value.length();

            String code_str = value.substring(pos, end_pos);
            code_str.trim();
            uint8_t code = 0;
            if (!lookupKeycodeByName(code_str, code) || code == 0)
            {
                pos = end_pos + 1;
                continue;
            }

            if (key_type == NORMAL_KEY)
            {
                if (mapping.normal_key_count >= 6)
                {
                    LOG_ERROR("Log", "Normal key array overflow in config! Max 6 keys.");
                    break;
                }
                mapping.normal_key[mapping.normal_key_count++] = code;
            }
            else if (key_type == MACROS_KEY)
            {
                if (mapping.macros_key_count >= 5)
                {
                    LOG_ERROR("Log", "Macros key array overflow in config! Max 5 keys.");
                    break;
                }
                mapping.macros_key[mapping.macros_key_count++] = code;
            }
            pos = end_pos + 1;
        }
    }
    return true;
}

KeyMapping Configuration::getKeyMapping(uint8_t physical_key) const
{
    if (physical_key >= 1 && physical_key <= CONFIG_ALL_KEY_NUM)
    {
        return key_mappings_[physical_key - 1];
    }
    return KeyMapping();
}
