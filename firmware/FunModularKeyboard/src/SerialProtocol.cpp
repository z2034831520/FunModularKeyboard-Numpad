#include "SerialProtocol.h"
#include <WiFi.h>

namespace
{
    constexpr int32_t kTcpConnectTimeoutMs = 150;
    constexpr uint32_t kTcpIdleDisconnectMs = 30000;
}

SerialProtocol::SerialProtocol()
    : commandCallback_(nullptr), keyEventCallback_(nullptr), tcpEnabled_(false), tcpServerIp_(0, 0, 0, 0), tcpServerPort_(0), lastTcpConnectAttemptMs_(0), lastTcpReceiveMs_(0)
{
}

SerialProtocol::~SerialProtocol()
{
}

void SerialProtocol::begin(unsigned long baudRate)
{
    Serial.begin(baudRate);
    while (!Serial)
    {
        delay(10);
    }

    inputBuffer_.reserve(8192);
    tcpInputBuffer_.reserve(8192);

    // LOG_DEBUG("Log", "Serial Protocol initialized");
    // LOG_DEBUG("Log", "Baud rate: " + String(baudRate));
}

void SerialProtocol::update()
{
    updateSerial();
    updateTcp();
}

void SerialProtocol::configureTcpClient(const IPAddress &serverIp, uint16_t port, bool enable)
{
    if (!enable)
    {
        disableTcpClient();
        return;
    }

    const bool endpointChanged = !tcpEnabled_ || tcpServerIp_ != serverIp || tcpServerPort_ != port;

    tcpEnabled_ = true;
    tcpServerIp_ = serverIp;
    tcpServerPort_ = port;

    if (endpointChanged)
    {
        markTcpDisconnected(true);
    }
}

void SerialProtocol::disableTcpClient()
{
    tcpEnabled_ = false;
    tcpInputBuffer_ = "";
    markTcpDisconnected(true);
}

bool SerialProtocol::isTcpConnected()
{
    return tcpEnabled_ && tcpClient_.connected();
}

void SerialProtocol::markTcpDisconnected(bool resetBackoff)
{
    tcpInputBuffer_ = "";
    tcpClient_.stop();
    tcpClient_ = WiFiClient();
    lastTcpConnectAttemptMs_ = resetBackoff ? 0 : millis();
    lastTcpReceiveMs_ = 0;
}

void SerialProtocol::updateSerial()
{
    while (Serial.available())
    {
        char c = static_cast<char>(Serial.read());
        processIncomingChar(c, inputBuffer_);
    }
}

void SerialProtocol::connectTcpIfNeeded()
{
    if (!tcpEnabled_)
    {
        return;
    }

    if (tcpClient_.connected())
    {
        return;
    }

    if (WiFi.status() != WL_CONNECTED || tcpServerPort_ == 0 || tcpServerIp_ == IPAddress(0, 0, 0, 0))
    {
        return;
    }

    const uint32_t now = millis();
    if (now - lastTcpConnectAttemptMs_ < 3000)
    {
        return;
    }
    lastTcpConnectAttemptMs_ = now;

    tcpClient_.stop();
    tcpClient_ = WiFiClient();
    tcpClient_.setTimeout(10);
    const bool connected = tcpClient_.connect(tcpServerIp_, tcpServerPort_, kTcpConnectTimeoutMs);
    if (!connected)
    {
        LOG_WARNING("Log", "TCP connect failed to %s:%u", tcpServerIp_.toString().c_str(), tcpServerPort_);
        tcpClient_.stop();
        lastTcpReceiveMs_ = 0;
    }
    else
    {
        LOG_INFO("Log", "TCP connected to %s:%u", tcpServerIp_.toString().c_str(), tcpServerPort_);
        lastTcpReceiveMs_ = millis();
    }
}

void SerialProtocol::updateTcp()
{
    connectTcpIfNeeded();

    if (!tcpEnabled_)
    {
        return;
    }

    if (!tcpClient_.connected())
    {
        if (tcpClient_)
        {
            markTcpDisconnected();
        }
        return;
    }

    const uint32_t now = millis();
    if (lastTcpReceiveMs_ != 0 && (now - lastTcpReceiveMs_) > kTcpIdleDisconnectMs)
    {
        LOG_WARNING("Log", "TCP idle timeout after %u ms, schedule reconnect", now - lastTcpReceiveMs_);
        markTcpDisconnected();
        return;
    }

    while (tcpClient_.connected() && tcpClient_.available())
    {
        char c = static_cast<char>(tcpClient_.read());
        lastTcpReceiveMs_ = millis();
        processIncomingChar(c, tcpInputBuffer_);
    }
}

void SerialProtocol::processIncomingChar(char c, String &buffer)
{
    if (c == '\n')
    {
        if (buffer.length() > 0)
        {
            processCommand(buffer);
            buffer = "";
        }
        return;
    }

    if (c == '\r')
    {
        return;
    }

    buffer += c;
    if (buffer.length() >= 20480)
    {
        buffer = "";
    }
}

void SerialProtocol::processCommand(const String &command)
{
    // LOG_WARNING("Log", "Received: " + command);

    const size_t capacity = command.length() + 2048;
    DynamicJsonDocument doc(capacity);
    DeserializationError error = deserializeJson(doc, command);

    if (error)
    {
        // LOG_ERROR("Log", "SerialProtocol processCommand error!");
        sendErrorResponse("JSON parse error: " + String(error.c_str()));
        return;
    }

    parseAndDispatch(doc);
}

void SerialProtocol::parseAndDispatch(DynamicJsonDocument &doc)
{
    if (!doc.containsKey("cmd"))
    {
        sendErrorResponse("Missing 'cmd' field");
        return;
    }

    int cmd = doc["cmd"];
    int seq = doc["seq"] | 0;

    // LOG_WARNING("Log", "Processing command: " + String(cmd) + ", seq: " + String(seq));

    // 检查是否是响应命令
    if (isResponseCommand(cmd))
    {
        // 响应命令处理（如果需要可以在这里添加）
        // LOG_ERROR("Log", "Response command received: " + String(cmd));
        return;
    }

    // 获取数据对象
    JsonObject data = doc.as<JsonObject>();

    // 分发命令到回调函数
    if (commandCallback_)
    {
        commandCallback_(cmd, seq, data);
    }
    else
    {
        // LOG_ERROR("Log", "No command callback set");
    }

    // 特殊命令处理
    switch (cmd)
    {
    case CMD_KEY_EVENT:
        if (keyEventCallback_ && doc.containsKey("physical_key") && doc.containsKey("logical_key"))
        {
            int physicalKey = doc["physical_key"];
            int logicalKey = doc["logical_key"];
            bool pressed = doc["pressed"] | false;
            keyEventCallback_(physicalKey, logicalKey, pressed);
        }
        break;

    case CMD_HEARTBEAT:
        // 自动回复心跳
        sendHeartbeatResponse(seq);
        break;
    }
}

void SerialProtocol::sendDeviceInfo(JsonObject deviceinfo, int seq)
{
    DynamicJsonDocument doc(512);
    doc["cmd"] = CMD_DEVICE_INFO_GET | 0x80;
    doc["seq"] = seq;
    doc["status"] = 0;
    doc["device_info"] = deviceinfo;
    sendJsonResponse(doc);
}

void SerialProtocol::sendKeymap(JsonArray keymap, int seq)
{
    constexpr size_t kKeymapResponseCapacity = 12288;
    DynamicJsonDocument doc(kKeymapResponseCapacity);
    doc["cmd"] = CMD_KEYMAP_GET | 0x80;
    doc["seq"] = seq;
    doc["status"] = 0;
    doc["keymap"] = keymap;
    sendJsonResponse(doc);
}

void SerialProtocol::sendConfig(JsonObject config, int seq)
{
    DynamicJsonDocument doc(6144);
    doc["cmd"] = CMD_CONFIG_GET | 0x80;
    doc["seq"] = seq;
    doc["status"] = 0;
    doc["config"] = config;
    sendJsonResponse(doc);
}

void SerialProtocol::sendKeyEvent(int physicalKey, int logicalKey, bool pressed, int seq)
{
    DynamicJsonDocument doc(256);
    doc["cmd"] = CMD_KEY_EVENT;
    doc["seq"] = seq;
    doc["physical_key"] = physicalKey;
    doc["logical_key"] = logicalKey;
    doc["normal_key"] = logicalKey;
    doc["pressed"] = pressed;
    doc["timestamp"] = millis();
    sendJsonResponse(doc);
}

void SerialProtocol::sendInputActivity(int physicalKey, const String &inputId, bool pressed, int seq)
{
    DynamicJsonDocument doc(256);
    doc["cmd"] = CMD_KEY_EVENT;
    doc["seq"] = seq;
    doc["physical_key"] = physicalKey;
    doc["input_id"] = inputId;
    doc["pressed"] = pressed;
    doc["timestamp"] = millis();
    sendJsonResponse(doc);
}

void SerialProtocol::sendHeartbeat(JsonObject data, int seq)
{
    DynamicJsonDocument doc(256);
    doc["cmd"] = CMD_HEARTBEAT;
    doc["seq"] = seq;

    JsonObject heartbeatData = doc.createNestedObject("data");
    heartbeatData["uptime"] = millis();
    heartbeatData["free_memory"] = ESP.getFreeHeap();

    // 合并自定义数据
    if (!data.isNull())
    {
        for (JsonPair kv : data)
        {
            heartbeatData[kv.key()] = kv.value();
        }
    }

    sendJsonResponse(doc);
}

void SerialProtocol::sendHeartbeatResponse(int seq)
{
    DynamicJsonDocument doc(128);
    doc["cmd"] = CMD_HEARTBEAT | 0x80;
    doc["seq"] = seq;
    doc["status"] = 0;

    JsonObject data = doc.createNestedObject("data");
    data["timestamp"] = millis();
    data["device"] = "ESP32 Keyboard";

    sendJsonResponse(doc);
}

void SerialProtocol::sendFirmwareInfo(JsonObject firmware, int seq)
{
    DynamicJsonDocument doc(256);
    doc["cmd"] = CMD_FIRMWARE_INFO | 0x80;
    doc["seq"] = seq;
    doc["status"] = 0;

    JsonObject firmwareInfo = doc.createNestedObject("firmware");
    firmwareInfo["version"] = "1.0.0";
    firmwareInfo["device"] = "FunModularKeyboard";
    firmwareInfo["build_date"] = __DATE__;
    firmwareInfo["build_time"] = __TIME__;

    // 合并自定义固件信息
    if (!firmware.isNull())
    {
        for (JsonPair kv : firmware)
        {
            firmwareInfo[kv.key()] = kv.value();
        }
    }

    sendJsonResponse(doc);
}

void SerialProtocol::sendVoiceText(const String &text, int seq)
{
    DynamicJsonDocument doc(768);
    doc["cmd"] = CMD_VOICE_TEXT;
    doc["seq"] = seq;
    doc["text"] = text;
    doc["timestamp"] = millis();
    sendJsonResponse(doc);
}

void SerialProtocol::sendSuccessResponse(int originalCmd, int seq, JsonObject data)
{
    DynamicJsonDocument doc(256);
    doc["cmd"] = originalCmd | 0x80;
    doc["seq"] = seq;
    doc["status"] = 0;

    if (!data.isNull())
    {
        JsonObject responseData = doc.createNestedObject("data");
        for (JsonPair kv : data)
        {
            responseData[kv.key()] = kv.value();
        }
    }

    sendJsonResponse(doc);
}

void SerialProtocol::sendErrorResponse(const String &error, int code, int seq)
{
    DynamicJsonDocument doc(256);
    doc["cmd"] = 0x80;
    doc["seq"] = seq;
    doc["status"] = 1;

    JsonObject errorObj = doc.createNestedObject("error");
    errorObj["message"] = error;
    errorObj["code"] = code;
    errorObj["timestamp"] = millis();

    sendJsonResponse(doc);
}

void SerialProtocol::sendResponse(int cmd, int seq, int status, JsonObject data)
{
    DynamicJsonDocument doc(256);
    doc["cmd"] = cmd;
    doc["seq"] = seq;
    doc["status"] = status;

    if (!data.isNull())
    {
        JsonObject responseData = doc.createNestedObject("data");
        for (JsonPair kv : data)
        {
            responseData[kv.key()] = kv.value();
        }
    }

    sendJsonResponse(doc);
}

void SerialProtocol::sendCustomCommand(int cmd, int seq, JsonObject data)
{
    DynamicJsonDocument doc(3072);
    doc["cmd"] = cmd;
    doc["seq"] = seq;

    if (!data.isNull())
    {
        for (JsonPair kv : data)
        {
            doc[kv.key()] = kv.value();
        }
    }

    sendJsonResponse(doc);
}

void SerialProtocol::sendJsonResponse(DynamicJsonDocument &doc)
{
    String output;
    serializeJson(doc, output);
    Serial.println(output);
    if (tcpClient_.connected())
    {
        const size_t written = tcpClient_.println(output);
        if (written == 0)
        {
            LOG_WARNING("Log", "TCP write failed, schedule reconnect");
            markTcpDisconnected();
        }
    }
    // Serial.printf("output=%s\r\n",output.c_str()); // 重要：添加换行符
    // LOG_WARNING("Log", "Sent: " + output);
}

void SerialProtocol::setCommandCallback(CommandCallback callback)
{
    commandCallback_ = callback;
}

void SerialProtocol::setKeyEventCallback(KeyEventCallback callback)
{
    keyEventCallback_ = callback;
}
