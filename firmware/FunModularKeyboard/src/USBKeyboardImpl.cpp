#include "USBKeyboardImpl.h"
#include "LogManager.h"

extern "C"
{
    bool tud_mounted(void);
}

USBKeyboardImpl::USBKeyboardImpl() : keyboard_(nullptr), keyboard_meida_(nullptr), is_initialized_(false)
{
}

bool USBKeyboardImpl::begin()
{
    if (!keyboard_)
    {
        keyboard_.reset(new USBHIDKeyboard());
        if (!keyboard_)
        {
            LOG_ERROR("USB", "Failed to create USBHIDKeyboard instance");
            return false;
        }
    }

    if (!keyboard_meida_)
    {
        keyboard_meida_.reset(new USBHIDConsumerControl());
        if (!keyboard_meida_)
        {
            LOG_ERROR("USB", "Failed to create USBHIDKeyboard instance");
            return false;
        }
    }

    USB.begin();
    keyboard_->begin();
    keyboard_meida_->begin();

    // // 使用TinyUSB原生API检查连接状态
    // while (!isUSBMounted()) {
    //     delay(100);
    // }

    is_initialized_ = true;
    LOG_INFO("USB", "USB keyboard initialized successfully");
    return true;
}

bool USBKeyboardImpl::isConnected()
{
    return tud_mounted();
}

void USBKeyboardImpl::press(uint8_t key)
{
    if (keyboard_ && isConnected())
    {
        keyboard_->press(key);
        LOG_DEBUG("USB", "Pressed key: 0x%02X", key);
    }
}

void USBKeyboardImpl::press(String key)
{
    if (keyboard_meida_ && isConnected())
    {
        // if (key.equals("CONSUMER_CONTROL_POWER")) {
        //     keyboard_meida_->press(CONSUMER_CONTROL_POWER);
        // } else if (key.equals("CONSUMER_CONTROL_RESET")) {
        //     keyboard_meida_->press(CONSUMER_CONTROL_RESET);
        // } else if (key.equals("CONSUMER_CONTROL_SLEEP")) {
        //     keyboard_meida_->press(CONSUMER_CONTROL_SLEEP);
        // } else if (key.equals("CONSUMER_CONTROL_BRIGHTNESS_INCREMENT")) {
        //     keyboard_meida_->press(CONSUMER_CONTROL_BRIGHTNESS_INCREMENT);
        // } else if (key.equals("CONSUMER_CONTROL_BRIGHTNESS_DECREMENT")) {
        //     keyboard_meida_->press(CONSUMER_CONTROL_BRIGHTNESS_DECREMENT);
        // } else if (key.equals("CONSUMER_CONTROL_PLAY_PAUSE")) {    //播放和暂停
        //     keyboard_meida_->press(CONSUMER_CONTROL_PLAY_PAUSE);
        // } else if (key.equals("CONSUMER_CONTROL_SCAN_NEXT")) {     //下一个
        //     keyboard_meida_->press(CONSUMER_CONTROL_SCAN_NEXT);
        // } else if (key.equals("CONSUMER_CONTROL_SCAN_PREVIOUS")) { //上一个
        //     keyboard_meida_->press(CONSUMER_CONTROL_SCAN_PREVIOUS);
        // } else if (key.equals("CONSUMER_CONTROL_STOP")) {          //音乐停止
        //     keyboard_meida_->press(CONSUMER_CONTROL_STOP);
        // } else if (key.equals("CONSUMER_CONTROL_MUTE")) {          //静音
        //     keyboard_meida_->press(CONSUMER_CONTROL_MUTE);
        // } else if (key.equals("CONSUMER_CONTROL_VOLUME_INCREMENT")) { //音量增
        //     keyboard_meida_->press(CONSUMER_CONTROL_VOLUME_INCREMENT);
        // } else if (key.equals("CONSUMER_CONTROL_VOLUME_DECREMENT")) { // 音量减
        //     keyboard_meida_->press(CONSUMER_CONTROL_VOLUME_DECREMENT);
        // } else if (key.equals("CONSUMER_CONTROL_EMAIL_READER")) { //打开邮件客户端
        //     keyboard_meida_->press(CONSUMER_CONTROL_EMAIL_READER);
        // } else if (key.equals("CONSUMER_CONTROL_CALCULATOR")) {   //打开计算器
        //     keyboard_meida_->press(CONSUMER_CONTROL_CALCULATOR  );
        // } else if (key.equals("CONSUMER_CONTROL_LOCAL_BROWSER")) { //打开文件
        //     keyboard_meida_->press(CONSUMER_CONTROL_LOCAL_BROWSER);
        // } else if (key.equals("CONSUMER_CONTROL_SEARCH")) {  //激活搜索框
        //     keyboard_meida_->press(CONSUMER_CONTROL_SEARCH);
        // } else if (key.equals("CONSUMER_CONTROL_HOME")) {    //浏览器主页
        //     keyboard_meida_->press(CONSUMER_CONTROL_HOME);
        // } else if (key.equals("CONSUMER_CONTROL_BACK")) {    //后退导航，VSCODE支持，网页也支持
        //     keyboard_meida_->press(CONSUMER_CONTROL_BACK);
        // } else if (key.equals("CONSUMER_CONTROL_FORWARD")) { //前进导航， VSCODE支持，网页也支持
        //     keyboard_meida_->press(CONSUMER_CONTROL_FORWARD);
        // } else if (key.equals("CONSUMER_CONTROL_BR_STOP")) { //停止加载
        //     keyboard_meida_->press(CONSUMER_CONTROL_BR_STOP);
        // } else if (key.equals("CONSUMER_CONTROL_REFRESH")) { //刷新页面，单独网页刷新
        //     keyboard_meida_->press(CONSUMER_CONTROL_REFRESH);
        // }

        if (key.equals("KEY_MEDIA_POWER"))
        {
            keyboard_meida_->press(CONSUMER_CONTROL_POWER);
        }
        else if (key.equals("KEY_MEDIA_RESET"))
        {
            keyboard_meida_->press(CONSUMER_CONTROL_RESET);
        }
        else if (key.equals("KEY_MEDIA_SLEEP"))
        {
            keyboard_meida_->press(CONSUMER_CONTROL_SLEEP);
        }
        else if (key.equals("KEY_MEDIA_BRIGHTNESS_UP"))
        {
            keyboard_meida_->press(CONSUMER_CONTROL_BRIGHTNESS_INCREMENT);
        }
        else if (key.equals("KEY_MEDIA_BRIGHTNESS_DOWN"))
        {
            keyboard_meida_->press(CONSUMER_CONTROL_BRIGHTNESS_DECREMENT);
        }
        else if (key.equals("KEY_MEDIA_PLAY_PAUSE"))
        { // 播放和暂停
            keyboard_meida_->press(CONSUMER_CONTROL_PLAY_PAUSE);
        }
        else if (key.equals("KEY_MEDIA_NEXT_TRACK"))
        { // 下一个
            keyboard_meida_->press(CONSUMER_CONTROL_SCAN_NEXT);
        }
        else if (key.equals("KEY_MEDIA_PREVIOUS_TRACK"))
        { // 上一个
            keyboard_meida_->press(CONSUMER_CONTROL_SCAN_PREVIOUS);
        }
        else if (key.equals("KEY_MEDIA_STOP"))
        { // 音乐停止
            keyboard_meida_->press(CONSUMER_CONTROL_STOP);
        }
        else if (key.equals("KEY_MEDIA_MUTE"))
        { // 静音
            keyboard_meida_->press(CONSUMER_CONTROL_MUTE);
        }
        else if (key.equals("KEY_MEDIA_VOLUME_UP"))
        { // 音量增
            keyboard_meida_->press(CONSUMER_CONTROL_VOLUME_INCREMENT);
        }
        else if (key.equals("KEY_MEDIA_VOLUME_DOWN"))
        { // 音量减
            keyboard_meida_->press(CONSUMER_CONTROL_VOLUME_DECREMENT);
        }
        else if (key.equals("KEY_MEDIA_EMAIL_READER"))
        { // 打开邮件客户端
            keyboard_meida_->press(CONSUMER_CONTROL_EMAIL_READER);
        }
        else if (key.equals("KEY_MEDIA_CALCULATOR"))
        { // 打开计算器
            keyboard_meida_->press(CONSUMER_CONTROL_CALCULATOR);
        }
        else if (key.equals("KEY_MEDIA_LOCAL_MACHINE_BROWSER"))
        { // 打开文件
            keyboard_meida_->press(CONSUMER_CONTROL_LOCAL_BROWSER);
        }
        else if (key.equals("KEY_MEDIA_WWW_SEARCH"))
        { // 激活搜索框
            keyboard_meida_->press(CONSUMER_CONTROL_SEARCH);
        }
        else if (key.equals("KEY_MEDIA_WWW_HOME"))
        { // 浏览器主页
            keyboard_meida_->press(CONSUMER_CONTROL_HOME);
        }
        else if (key.equals("KEY_MEDIA_WWW_BACK"))
        { // 后退导航，VSCODE支持，网页也支持
            keyboard_meida_->press(CONSUMER_CONTROL_BACK);
        }
        else if (key.equals("KEY_MEDIA_WWW_FORWARD"))
        { // 前进导航， VSCODE支持，网页也支持
            keyboard_meida_->press(CONSUMER_CONTROL_FORWARD);
        }
        else if (key.equals("KEY_MEDIA_WWW_STOP"))
        { // 停止加载
            keyboard_meida_->press(CONSUMER_CONTROL_BR_STOP);
        }
        else if (key.equals("KEY_MEDIA_WWW_REFRESH"))
        { // 刷新页面，单独网页刷新
            keyboard_meida_->press(CONSUMER_CONTROL_REFRESH);
        }
    }
}

void USBKeyboardImpl::release(uint8_t key)
{
    if (keyboard_ && isConnected())
    {
        keyboard_->release(key);
        LOG_DEBUG("USB", "Released key: 0x%02X", key);
    }
}

void USBKeyboardImpl::release(String key)
{
    if (keyboard_meida_ && isConnected())
    {
        keyboard_meida_->release();
        LOG_DEBUG("USB", "keyboard_meida Released!");
    }
}

void USBKeyboardImpl::releaseAll()
{
    if (keyboard_ && isConnected())
    {
        keyboard_->releaseAll();
        LOG_DEBUG("USB", "Released normal keys");
    }
    if (keyboard_meida_ && isConnected())
    {
        keyboard_meida_->release();
        LOG_DEBUG("USB", "Released keyboard_meida keys");
    }
}

void USBKeyboardImpl::send()
{
    if (keyboard_ && isConnected())
    {
        LOG_DEBUG("USB", "Manual send called (auto-send is enabled)");
    }

    if (keyboard_meida_ && isConnected())
    {
        LOG_DEBUG("USB", "Manual send called (auto-send is enabled)");
    }
}