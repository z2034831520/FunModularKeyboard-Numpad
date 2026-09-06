#include "BLEKeyboardImpl.h"
#include <BleKeyboard.h>
#include "LogManager.h"

BLEKeyboardImpl::BLEKeyboardImpl() : keyboard_(nullptr), is_initialized_(false), last_connection_state_(false)
{
}

bool BLEKeyboardImpl::begin()
{
    if (!keyboard_)
    {
        keyboard_.reset(new BleKeyboard());
        if (!keyboard_)
        {
            LOG_ERROR("BLE", "Failed to create BleKeyboard instance");
            return false;
        }
    }

    keyboard_->begin();
    is_initialized_ = true;

    return true;
}

void BLEKeyboardImpl::press(uint8_t key)
{
    if (keyboard_ && isConnected())
    {
        keyboard_->press(key);
        LOG_DEBUG("BLE", "Pressed key: 0x%02X", key);
    }
    else if (!isConnected())
    {
        LOG_WARNING("BLE", "Attempted to press key 0x%02X but BLE not connected", key);
    }
}

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

void BLEKeyboardImpl::press(String key)
{
    LOG_DEBUG("BLE", "BLEKeyboardImpl Pressed key: %s", key.c_str());
    if (keyboard_ && isConnected())
    {
        if (key.equals("KEY_MEDIA_POWER"))
        {
            // keyboard_->press(KEY_MEDIA_POWER);
        }
        else if (key.equals("KEY_MEDIA_RESET"))
        {
            // keyboard_->press(KEY_MEDIA_RESET);
        }
        else if (key.equals("KEY_MEDIA_SLEEP"))
        {
            // keyboard_->press(KEY_MEDIA_SLEEP);
        }
        else if (key.equals("KEY_MEDIA_BRIGHTNESS_UP"))
        {
            // keyboard_->press(KEY_MEDIA_BRIGHTNESS_UP);
        }
        else if (key.equals("KEY_MEDIA_BRIGHTNESS_DOWN"))
        {
            // keyboard_->press(KEY_MEDIA_BRIGHTNESS_DOWN);
        }
        else if (key.equals("KEY_MEDIA_PLAY_PAUSE"))
        {
            keyboard_->press(KEY_MEDIA_PLAY_PAUSE);
        }
        else if (key.equals("KEY_MEDIA_NEXT_TRACK"))
        {
            keyboard_->press(KEY_MEDIA_NEXT_TRACK);
        }
        else if (key.equals("KEY_MEDIA_PREVIOUS_TRACK"))
        {
            keyboard_->press(KEY_MEDIA_PREVIOUS_TRACK);
        }
        else if (key.equals("KEY_MEDIA_STOP"))
        {
            keyboard_->press(KEY_MEDIA_STOP);
        }
        else if (key.equals("KEY_MEDIA_MUTE"))
        {
            keyboard_->press(KEY_MEDIA_MUTE);
        }
        else if (key.equals("KEY_MEDIA_VOLUME_UP"))
        {
            keyboard_->press(KEY_MEDIA_VOLUME_UP);
        }
        else if (key.equals("KEY_MEDIA_VOLUME_DOWN"))
        {
            keyboard_->press(KEY_MEDIA_VOLUME_DOWN);
        }
        else if (key.equals("KEY_MEDIA_EMAIL_READER"))
        {
            keyboard_->press(KEY_MEDIA_EMAIL_READER);
        }
        else if (key.equals("KEY_MEDIA_CALCULATOR"))
        {
            keyboard_->press(KEY_MEDIA_CALCULATOR);
        }
        else if (key.equals("KEY_MEDIA_LOCAL_MACHINE_BROWSER"))
        {
            keyboard_->press(KEY_MEDIA_LOCAL_MACHINE_BROWSER);
        }
        else if (key.equals("KEY_MEDIA_WWW_SEARCH"))
        {
            keyboard_->press(KEY_MEDIA_WWW_SEARCH);
        }
        else if (key.equals("KEY_MEDIA_WWW_HOME"))
        {
            keyboard_->press(KEY_MEDIA_WWW_HOME);
        }
        else if (key.equals("KEY_MEDIA_WWW_BACK"))
        {
            keyboard_->press(KEY_MEDIA_WWW_BACK);
        }
        else if (key.equals("KEY_MEDIA_WWW_FORWARD"))
        {
            // keyboard_->press(KEY_MEDIA_WWW_FORWARD);
            keyboard_->press(KEY_LEFT_ALT);
            keyboard_->press(KEY_RIGHT_ARROW);
        }
        else if (key.equals("KEY_MEDIA_WWW_STOP"))
        {
            keyboard_->press(KEY_MEDIA_WWW_STOP);
        }
        else if (key.equals("KEY_MEDIA_WWW_REFRESH"))
        {
            // keyboard_->press(KEY_MEDIA_WWW_REFRESH);
            keyboard_->press(KEY_F5);
        }
    }
    else if (!isConnected())
    {
        LOG_WARNING("BLE", "Attempted to press key %s but BLE not connected", key.c_str());
    }
}

void BLEKeyboardImpl::release(uint8_t key)
{
    if (keyboard_ && isConnected())
    {
        keyboard_->release(key);
        LOG_DEBUG("BLE", "Released key: 0x%02X", key);
    }
    else if (!isConnected())
    {
        LOG_WARNING("BLE", "Attempted to release key 0x%02X but BLE not connected", key);
    }
}

void BLEKeyboardImpl::release(String key)
{
    LOG_DEBUG("BLE", "BLEKeyboardImpl release key: %s", key.c_str());
    if (keyboard_ && isConnected())
    {
        // //多媒体按键释放
        // MediaKeyReport zeroReport = {0, 0};
        // keyboard_->write(zeroReport);

        if (key.equals("KEY_MEDIA_POWER"))
        {
            // keyboard_->release(KEY_MEDIA_POWER);
        }
        else if (key.equals("KEY_MEDIA_RESET"))
        {
            // keyboard_->release(KEY_MEDIA_RESET);
        }
        else if (key.equals("KEY_MEDIA_SLEEP"))
        {
            // keyboard_->release(KEY_MEDIA_SLEEP);
        }
        else if (key.equals("KEY_MEDIA_BRIGHTNESS_UP"))
        {
            // keyboard_->release(KEY_MEDIA_BRIGHTNESS_UP);
        }
        else if (key.equals("KEY_MEDIA_BRIGHTNESS_DOWN"))
        {
            // keyboard_->release(KEY_MEDIA_BRIGHTNESS_DOWN);
        }
        else if (key.equals("KEY_MEDIA_PLAY_PAUSE"))
        {
            keyboard_->release(KEY_MEDIA_PLAY_PAUSE);
        }
        else if (key.equals("KEY_MEDIA_NEXT_TRACK"))
        {
            keyboard_->release(KEY_MEDIA_NEXT_TRACK);
        }
        else if (key.equals("KEY_MEDIA_PREVIOUS_TRACK"))
        {
            keyboard_->release(KEY_MEDIA_PREVIOUS_TRACK);
        }
        else if (key.equals("KEY_MEDIA_STOP"))
        {
            keyboard_->release(KEY_MEDIA_STOP);
        }
        else if (key.equals("KEY_MEDIA_MUTE"))
        {
            keyboard_->release(KEY_MEDIA_MUTE);
        }
        else if (key.equals("KEY_MEDIA_VOLUME_UP"))
        {
            keyboard_->release(KEY_MEDIA_VOLUME_UP);
        }
        else if (key.equals("KEY_MEDIA_VOLUME_DOWN"))
        {
            keyboard_->release(KEY_MEDIA_VOLUME_DOWN);
        }
        else if (key.equals("KEY_MEDIA_EMAIL_READER"))
        {
            keyboard_->release(KEY_MEDIA_EMAIL_READER);
        }
        else if (key.equals("KEY_MEDIA_CALCULATOR"))
        {
            keyboard_->release(KEY_MEDIA_CALCULATOR);
        }
        else if (key.equals("KEY_MEDIA_LOCAL_MACHINE_BROWSER"))
        {
            keyboard_->release(KEY_MEDIA_LOCAL_MACHINE_BROWSER);
        }
        else if (key.equals("KEY_MEDIA_WWW_SEARCH"))
        {
            keyboard_->release(KEY_MEDIA_WWW_SEARCH);
        }
        else if (key.equals("KEY_MEDIA_WWW_HOME"))
        {
            keyboard_->release(KEY_MEDIA_WWW_HOME);
        }
        else if (key.equals("KEY_MEDIA_WWW_BACK"))
        {
            keyboard_->release(KEY_MEDIA_WWW_BACK);
        }
        else if (key.equals("KEY_MEDIA_WWW_FORWARD"))
        {
            // keyboard_->press(KEY_MEDIA_WWW_FORWARD);
            keyboard_->release(KEY_LEFT_ALT);
            keyboard_->release(KEY_RIGHT_ARROW);
        }
        else if (key.equals("KEY_MEDIA_WWW_STOP"))
        {
            keyboard_->release(KEY_MEDIA_WWW_STOP);
        }
        else if (key.equals("KEY_MEDIA_WWW_REFRESH"))
        {
            // keyboard_->press(KEY_MEDIA_WWW_REFRESH);
            keyboard_->release(KEY_F5);
        }
    }
    else if (!isConnected())
    {
        LOG_WARNING("BLE", "Attempted to release key %s but BLE not connected", key.c_str());
    }
}

void BLEKeyboardImpl::releaseAll()
{
    if (keyboard_ && isConnected())
    {
        // 普通按键释放
        keyboard_->releaseAll();

        // 多媒体按键释放
        MediaKeyReport zeroReport = {0, 0};
        keyboard_->write(zeroReport);

        LOG_DEBUG("BLE", "Released all keys");
    }
    else if (!isConnected())
    {
        LOG_WARNING("BLE", "Attempted to release all keys but BLE not connected");
    }
}

bool BLEKeyboardImpl::isConnected()
{
    if (!is_initialized_)
    {
        LOG_ERROR("BLE", "BLE keyboard not initialized");
        return false;
    }

    bool connected = keyboard_ ? keyboard_->isConnected() : false;

    // 检测连接状态变化并记录日志
    if (connected != last_connection_state_)
    {
        if (connected)
        {
            LOG_INFO("BLE", "BLE keyboard connected to host");
        }
        else
        {
            LOG_WARNING("BLE", "BLE keyboard disconnected from host");
        }
        last_connection_state_ = connected;
    }

    return connected;
}

void BLEKeyboardImpl::send()
{
    // BLE键盘不需要手动send
    if (keyboard_ && isConnected())
    {
        LOG_DEBUG("BLE", "Manual send called (auto-send is enabled)");
    }
}