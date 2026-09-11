#include "SerialProtocol.h"
SerialProtocol::SerialProtocol()
    : commandCallback_(nullptr)
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

    // LOG_DEBUG("Log", "Serial Protocol initialized");
    // LOG_DEBUG("Log", "Baud rate: " + String(baudRate));
}

void SerialProtocol::update()
{
    updateSerial();
}

void SerialProtocol::updateSerial()
{
    while (Serial.available())
    {
        char c = static_cast<char>(Serial.read());
        processIncomingChar(c, inputBuffer_);
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
    // Serial.printf("output=%s\r\n",output.c_str()); // 重要：添加换行符
    // LOG_WARNING("Log", "Sent: " + output);
}

void SerialProtocol::setCommandCallback(CommandCallback callback)
{
    commandCallback_ = callback;
}
