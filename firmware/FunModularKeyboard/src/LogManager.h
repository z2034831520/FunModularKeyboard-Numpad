#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <Arduino.h>
#include <vector>
#include <functional>

/*
 * Compile-time log filtering. Production builds keep warnings and errors,
 * while verbose DEBUG/INFO messages and their format strings are omitted.
 */
#ifndef LOG_COMPILE_LEVEL
#define LOG_COMPILE_LEVEL 2
#endif

// 日志级别定义
enum class LogLevel
{
    DEBUG = 0,
    INFO,
    WARNING,
    ERROR,
    CRITICAL
};

// 日志条目结构
struct LogEntry
{
    String message;
    LogLevel level;
    unsigned long timestamp;
    String tag;
};

// 日志回调函数类型
using LogCallback = std::function<void(const LogEntry &)>;

class LogManager
{
public:
    // 获取单例实例
    static LogManager &getInstance();

    // 删除拷贝构造函数和赋值操作符
    LogManager(const LogManager &) = delete;
    LogManager &operator=(const LogManager &) = delete;

    // 初始化
    void begin(unsigned long baudRate = 115200);

    // 设置日志级别
    void setLogLevel(LogLevel level);

    // 设置是否输出到Serial
    void setSerialOutput(bool enabled);

    // 添加日志回调
    void addCallback(LogCallback callback);

    // 日志输出方法（带级别和标签）
    void log(LogLevel level, const String &tag, const String &message);
    void log(LogLevel level, const String &tag, const char *format, ...);

    // 便捷方法
    void debug(const String &tag, const String &message);
    void debug(const String &tag, const char *format, ...);

    void info(const String &tag, const String &message);
    void info(const String &tag, const char *format, ...);

    void warning(const String &tag, const String &message);
    void warning(const String &tag, const char *format, ...);

    void error(const String &tag, const String &message);
    void error(const String &tag, const char *format, ...);

    void critical(const String &tag, const String &message);
    void critical(const String &tag, const char *format, ...);

    // 获取日志历史
    const std::vector<LogEntry> &getLogHistory() const;

    // 清空日志历史
    void clearHistory();

    // 设置历史记录大小
    void setHistorySize(size_t size);

    // 获取日志级别字符串
    static String levelToString(LogLevel level);

    // 获取带颜色的日志级别字符串（用于终端显示）
    static String levelToColorString(LogLevel level);

    // 栈空间检查（新增）
    bool checkStackSpace() const;

    // 获取剩余栈空间（新增）
    size_t getFreeStackSpace() const;

private:
    LogManager();
    ~LogManager() = default;

    // 内部日志实现
    void internalLog(LogLevel level, const String &tag, const String &message);

    // 格式化输出
    String formatLogEntry(const LogEntry &entry, bool useColor = false) const;

    // 安全的格式化函数（新增）
    String safeFormat(const char *format, ...) const;

    bool initialized_ = false;
    bool serialOutput_ = true;
    LogLevel currentLevel_ = LogLevel::DEBUG;
    std::vector<LogEntry> logHistory_;
    std::vector<LogCallback> callbacks_;
    size_t maxHistorySize_ = 80; // 保持较小历史缓存，避免占用过多堆内存
};

// 全局便捷函数（简化栈检查，避免过度保护）
#if LOG_COMPILE_LEVEL <= 0
#define LOG_DEBUG(tag, ...) LogManager::getInstance().debug(tag, __VA_ARGS__)
#else
#define LOG_DEBUG(tag, ...) ((void)0)
#endif

#if LOG_COMPILE_LEVEL <= 1
#define LOG_INFO(tag, ...) LogManager::getInstance().info(tag, __VA_ARGS__)
#else
#define LOG_INFO(tag, ...) ((void)0)
#endif

#define LOG_WARNING(tag, ...) LogManager::getInstance().warning(tag, __VA_ARGS__)
#define LOG_ERROR(tag, ...) LogManager::getInstance().error(tag, __VA_ARGS__)
#define LOG_CRITICAL(tag, ...) LogManager::getInstance().critical(tag, __VA_ARGS__)

#endif // LOG_MANAGER_H
