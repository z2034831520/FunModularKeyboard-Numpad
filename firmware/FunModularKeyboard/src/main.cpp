#include <Arduino.h>
#include "MainTask.h"
#include "Configuration.h"
#include "DisplayTask.h"
#include "SPIFFS.h"
#include "LogManager.h"
#include "SystemTime.h"

// 配置
Configuration config;

// 创建消息队列
QueueHandle_t displayQueue = xQueueCreate(10, sizeof(DisplayMessage));

static DisplayTask display_task(0);
static MainTask main_task(1, config);

// 打印系统时间
void printSystemTime()
{
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
        LOG_DEBUG("Log", "获取系统时间失败");
        return;
    }

    char timeString[64];
    strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
    LOG_DEBUG("Log", "系统时间: ");
    LOG_DEBUG("Log", timeString);
}

void setup()
{
    SystemTime::ConfigureChinaTimeZone();

    // Serial.begin(115200);

    // 初始化日志系统
    LogManager::getInstance().begin(115200);
    // 设置日志级别（只显示DEBUG及以上级别）
    LogManager::getInstance().setLogLevel(LogLevel::DEBUG);
    // 添加自定义回调
    LogManager::getInstance().addCallback([](const LogEntry &entry)
                                          {
                                              // 这里可以添加其他输出方式，如SD卡、网络等
                                              // 示例：同时输出到第二个串口（如果可用）
                                              // Serial2.println(LogManager::formatLogEntry(entry));
                                          });

    // LOG_DEBUG("Log","Flash总大小: %.2f \rMB\n", ESP.getFlashChipSize() / 1024.0 / 1024);
    // LOG_DEBUG("Log","APP分区: %.2f MB", ESP.getSketchSize() / 1024.0 / 1024);
    // LOG_DEBUG("Log","Free Space:%.2f MB", ESP.getFreeSketchSpace() / 1024.0 / 1024);
    // LOG_DEBUG("Log","SPIFFS: %.2f MB", SPIFFS.totalBytes() / 1024.0 / 1024);
    // LOG_DEBUG("Log","SPIFFS Total: %d bytes, Used: %d bytes",
    //     SPIFFS.totalBytes(), SPIFFS.usedBytes());
    // LOG_WARNING("Test", "This is a debug message");

    // // 连接 WiFi
    // if (connectToWiFi()) {
    //     // 同步 NTP 时间到系统时钟
    //     if (syncTimeFromNTP()) {
    //     LOG_DEBUG("Log", "系统时钟同步成功");
    //     }
    // }

    // // // 断开 WiFi 以节省功耗（如果需要）
    // WiFi.disconnect(true);
    // WiFi.mode(WIFI_OFF);

    // 初始化SPIFFS
    config.InitSPIFFS();

    if (!SPIFFS.begin(true))
    {
        LOG_DEBUG("Log", "SPIFFS Mount Failed!");
        return;
    }

    // 打印SPIFFS中存在的文件
    File root = SPIFFS.open("/");
    LOG_DEBUG("Log", "SPIFFS file:");
    while (File file = root.openNextFile())
    {
        LOG_DEBUG("Log", file.name());
    }

    // 加载配置文件
    if (config.load())
    {
        LOG_DEBUG("Log", "Config loaded:");
        LOG_DEBUG("Log", "Device: " + config.getDeviceName());
    }

    // 设置消息队列
    display_task.setMessageQueue(displayQueue);
    main_task.setMessageQueue(displayQueue);

    // 启动任务
    //  先启动主任务，给 BLE/WiFi 初始化预留更多可用内存。
    main_task.begin();

    // BLE mode may briefly use WiFi to obtain NTP time. Keep LVGL from taking
    // internal heap until the keyboard stack has finished initialization.
    const uint32_t startupWaitStartedMs = millis();
    constexpr uint32_t kMainTaskStartupTimeoutMs = 22000;
    while (!main_task.IsStartupReady() && millis() - startupWaitStartedMs < kMainTaskStartupTimeoutMs)
    {
        delay(20);
    }
    display_task.begin();
}

void loop()
{
    // Arduino主循环通常为空，因为任务在FreeRTOS中运行
    vTaskDelay(portMAX_DELAY);
}
