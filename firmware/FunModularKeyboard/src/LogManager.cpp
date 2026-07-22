#include "LogManager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ANSI 颜色代码
#define ANSI_COLOR_RED     "\033[1;31m"
#define ANSI_COLOR_GREEN   "\033[1;32m"
#define ANSI_COLOR_YELLOW  "\033[1;33m"
#define ANSI_COLOR_BLUE    "\033[1;34m"
#define ANSI_COLOR_MAGENTA "\033[1;35m"
#define ANSI_COLOR_CYAN    "\033[1;36m"
#define ANSI_COLOR_RESET   "\033[0m"

// 格式化缓冲区大小
static const size_t FORMAT_BUFFER_SIZE = 256;
static const size_t MIN_HEAP_FOR_HISTORY = 24 * 1024;

namespace {
const char* levelToCStr(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}
}

LogManager& LogManager::getInstance() {
    static LogManager instance;
    return instance;
}

LogManager::LogManager() {
    // 初始化时记录栈状态
    logHistory_.reserve(maxHistorySize_); // 预分配内存
}

void LogManager::begin(unsigned long baudRate) {
    if (!initialized_) {
        Serial.begin(baudRate);
        
        // 等待串口初始化完成
        unsigned long startTime = millis();
        while (!Serial && (millis() - startTime < 2000)) {
            delay(10);
        }
        
        initialized_ = true;
        
        // // 直接使用Serial输出初始信息，避免递归调用
        // Serial.println("[LogManager] Log system initialized");
        // Serial.printf("[LogManager] Baud rate: %lu", baudRate);
        // Serial.printf("[LogManager] Free stack: %u bytes", getFreeStackSpace());
        
        // 使用内部日志记录初始化完成
        internalLog(LogLevel::INFO, "LogManager", "Log system ready");
    }
}

void LogManager::setLogLevel(LogLevel level) {
    currentLevel_ = level;
    log(LogLevel::INFO, "LogManager", 
        safeFormat("Log level set to: %s", levelToString(level).c_str()));
}

void LogManager::setSerialOutput(bool enabled) {
    serialOutput_ = enabled;
    log(LogLevel::INFO, "LogManager", 
        safeFormat("Serial output %s", (enabled ? "enabled" : "disabled")));
}

void LogManager::addCallback(LogCallback callback) {
    callbacks_.push_back(callback);
}

void LogManager::log(LogLevel level, const String& tag, const String& message) {
    internalLog(level, tag, message);
}

void LogManager::log(LogLevel level, const String& tag, const char* format, ...) {
    char formatBuffer[FORMAT_BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(formatBuffer, FORMAT_BUFFER_SIZE, format, args);
    va_end(args);
    
    internalLog(level, tag, String(formatBuffer));
}

// 便捷方法实现
void LogManager::debug(const String& tag, const String& message) {
    log(LogLevel::DEBUG, tag, message);
}

void LogManager::debug(const String& tag, const char* format, ...) {
    char formatBuffer[FORMAT_BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(formatBuffer, FORMAT_BUFFER_SIZE, format, args);
    va_end(args);
    
    log(LogLevel::DEBUG, tag, String(formatBuffer));
}

void LogManager::info(const String& tag, const String& message) {
    log(LogLevel::INFO, tag, message);
}

void LogManager::info(const String& tag, const char* format, ...) {
    char formatBuffer[FORMAT_BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(formatBuffer, FORMAT_BUFFER_SIZE, format, args);
    va_end(args);
    
    log(LogLevel::INFO, tag, String(formatBuffer));
}

void LogManager::warning(const String& tag, const String& message) {
    log(LogLevel::WARNING, tag, message);
}

void LogManager::warning(const String& tag, const char* format, ...) {
    char formatBuffer[FORMAT_BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(formatBuffer, FORMAT_BUFFER_SIZE, format, args);
    va_end(args);
    
    log(LogLevel::WARNING, tag, String(formatBuffer));
}

void LogManager::error(const String& tag, const String& message) {
    log(LogLevel::ERROR, tag, message);
}

void LogManager::error(const String& tag, const char* format, ...) {
    char formatBuffer[FORMAT_BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(formatBuffer, FORMAT_BUFFER_SIZE, format, args);
    va_end(args);
    
    log(LogLevel::ERROR, tag, String(formatBuffer));
}

void LogManager::critical(const String& tag, const String& message) {
    log(LogLevel::CRITICAL, tag, message);
}

void LogManager::critical(const String& tag, const char* format, ...) {
    char formatBuffer[FORMAT_BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(formatBuffer, FORMAT_BUFFER_SIZE, format, args);
    va_end(args);
    
    log(LogLevel::CRITICAL, tag, String(formatBuffer));
}

void LogManager::internalLog(LogLevel level, const String& tag, const String& message) {
    // 检查日志级别
    if (static_cast<int>(level) < static_cast<int>(currentLevel_)) {
        return;
    }
    
    // 创建日志条目
    LogEntry entry;
    entry.message = message;
    entry.level = level;
    entry.timestamp = millis();
    entry.tag = tag;

    // 低内存时不保存历史，优先保证系统稳定运行。
    if (maxHistorySize_ > 0 && ESP.getFreeHeap() > MIN_HEAP_FOR_HISTORY) {
        if (logHistory_.size() >= maxHistorySize_) {
            logHistory_.clear();
        }
        logHistory_.push_back(entry);
    }
    
    // 输出到Serial
    if (serialOutput_ && initialized_) {
        Serial.printf("[%lu] %s [%s] %s\r\n",
                      entry.timestamp,
                      levelToCStr(entry.level),
                      entry.tag.c_str(),
                      entry.message.c_str());
        
        // 如果是关键错误，立即刷新
        if (level >= LogLevel::ERROR) {
            Serial.flush();
        }
    }
    
    // 调用回调函数
    for (auto& callback : callbacks_) {
        callback(entry);
    }
}

const std::vector<LogEntry>& LogManager::getLogHistory() const {
    return logHistory_;
}

void LogManager::clearHistory() {
    logHistory_.clear();
    logHistory_.shrink_to_fit(); // 释放内存
}

void LogManager::setHistorySize(size_t size) {
    maxHistorySize_ = size;
    // 如果当前历史记录超过新大小，截断
    if (logHistory_.size() > maxHistorySize_) {
        logHistory_.erase(logHistory_.begin(), 
                         logHistory_.begin() + (logHistory_.size() - maxHistorySize_));
    }
}

String LogManager::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

String LogManager::levelToColorString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return ANSI_COLOR_BLUE "DEBUG" ANSI_COLOR_RESET;
        case LogLevel::INFO: return ANSI_COLOR_GREEN "INFO" ANSI_COLOR_RESET;
        case LogLevel::WARNING: return ANSI_COLOR_YELLOW "WARNING" ANSI_COLOR_RESET;
        case LogLevel::ERROR: return ANSI_COLOR_RED "ERROR" ANSI_COLOR_RESET;
        case LogLevel::CRITICAL: return ANSI_COLOR_MAGENTA "CRITICAL" ANSI_COLOR_RESET;
        default: return "UNKNOWN";
    }
}

String LogManager::formatLogEntry(const LogEntry& entry, bool useColor) const {
    String timestamp = String(entry.timestamp);
    String levelStr = useColor ? levelToColorString(entry.level) : levelToString(entry.level);
    
    return "[" + timestamp + "] " + levelStr + " [" + entry.tag + "] " + entry.message;
}

String LogManager::safeFormat(const char* format, ...) const {
    char buffer[FORMAT_BUFFER_SIZE];
    
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, FORMAT_BUFFER_SIZE, format, args);
    va_end(args);
    
    String result(buffer);
    return result;
}

bool LogManager::checkStackSpace() const {
    return getFreeStackSpace() > 256; // 至少256字节空闲栈空间
}

size_t LogManager::getFreeStackSpace() const {
    TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
    if (currentTask) {
        return uxTaskGetStackHighWaterMark(currentTask) * sizeof(StackType_t);
    }
    return 0;
}