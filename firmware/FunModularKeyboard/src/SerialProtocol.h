#ifndef SERIALPROTOCOL_H
#define SERIALPROTOCOL_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiClient.h>
#include "LogManager.h"

// 命令定义
enum CommandType
{
    CMD_CONF_VERSION_GET = 0x01,
    CMD_CONF_VERSION_SET = 0x02,
    CMD_DEVICE_INFO_GET = 0x03,
    CMD_DEVICE_INFO_SET = 0x04,
    CMD_KEYMAP_GET = 0x05,
    CMD_KEYMAP_SET = 0x06,
    CMD_CONFIG_GET = 0x07,
    CMD_CONFIG_SET = 0x08,
    CMD_KEY_EVENT = 0x09,
    CMD_HEARTBEAT = 0x0a,
    CMD_FIRMWARE_INFO = 0x0b,
    CMD_VOICE_TEXT = 0x0c,
    CMD_PC_STATUS = 0x0d,
    CMD_MUSIC_STATUS = 0x0e,
    CMD_MUSIC_CONTROL = 0x0f,
    CMD_PROFILE_STATE = 0x10,
    CMD_PROFILE_ICON_SET = 0x11
};
// 回调函数类型定义
// typedef void (*CommandCallback)(int cmd, int seq, JsonObject data);
// typedef void (*KeyEventCallback)(int physicalKey, int logicalKey, bool pressed);

typedef std::function<void(int cmd, int seq, JsonObject data)> CommandCallback;
typedef std::function<void(int physicalKey, int logicalKey, bool pressed)> KeyEventCallback;

class SerialProtocol
{
public:
    SerialProtocol();
    ~SerialProtocol();

    // 初始化
    void begin(unsigned long baudRate = 115200);
    void update();
    void configureTcpClient(const IPAddress &serverIp, uint16_t port, bool enable = true);
    void disableTcpClient();
    bool isTcpConnected();

    // 发送命令
    void sendDeviceInfo(JsonObject deviceinfo, int seq);
    void sendKeymap(JsonArray keymap, int seq = 0);
    void sendConfig(JsonObject config, int seq = 0);
    void sendKeyEvent(int physicalKey, int logicalKey, bool pressed, int seq = 0);
    void sendInputActivity(int physicalKey, const String &inputId, bool pressed, int seq = 0);
    void sendHeartbeat(JsonObject data, int seq = 0);
    void sendFirmwareInfo(JsonObject firmware, int seq = 0);
    void sendVoiceText(const String &text, int seq = 0);
    void sendSuccessResponse(int originalCmd, int seq, JsonObject data);
    void sendErrorResponse(const String &error, int code = 1, int seq = 0);

    // 通用发送方法
    void sendResponse(int cmd, int seq, int status, JsonObject data);
    void sendCustomCommand(int cmd, int seq, JsonObject data);

    // 设置回调函数
    void setCommandCallback(CommandCallback callback);
    void setKeyEventCallback(KeyEventCallback callback);
    void setLogCallback(LogCallback callback);

    // 工具函数
    bool isResponseCommand(int cmd) { return (cmd & 0x80) != 0; }
    int getOriginalCommand(int cmd) { return cmd & 0x7F; }

private:
    // 命令处理
    void processCommand(const String &command);
    void updateSerial();
    void updateTcp();
    void processIncomingChar(char c, String &buffer);
    void connectTcpIfNeeded();
    void markTcpDisconnected(bool resetBackoff = false);
    void parseAndDispatch(DynamicJsonDocument &doc);

    // 响应构建
    void sendJsonResponse(DynamicJsonDocument &doc);
    void sendHeartbeatResponse(int seq);

    // 回调函数
    CommandCallback commandCallback_;
    KeyEventCallback keyEventCallback_;

    // 缓冲区
    String inputBuffer_;
    String tcpInputBuffer_;

    // TCP transport
    bool tcpEnabled_;
    IPAddress tcpServerIp_;
    uint16_t tcpServerPort_;
    uint32_t lastTcpConnectAttemptMs_;
    uint32_t lastTcpReceiveMs_;
    WiFiClient tcpClient_;
};

#endif
