#pragma once

#include <Arduino.h>
#include "CodexProtocol.h"

class CodexSerialBridge
{
public:
    using StatusCallback = void (*)(CodexStatus status, uint8_t task_count, CodexEffort effort, void *context);

    void Begin();
    void Loop();
    bool QueueTask(CodexTask task);
    void SetStatusCallback(StatusCallback callback, void *context);
    bool IsHostConnected() const;

private:
    static constexpr size_t kReceiveBufferSize = 96;
    static constexpr size_t kTaskQueueSize = 6;
    static constexpr uint32_t kHostTimeoutMs = 6500;

    void ReadIncoming();
    void ProcessLine(const char *line);
    void FlushOneTask();
    void SendLine(const char *line);
    void SetStatus(CodexStatus status, uint8_t task_count = 1, CodexEffort effort = CodexEffort::UNKNOWN);
    static const char *TaskToLine(CodexTask task);
    static CodexEffort ParseEffort(const char *value);
    static const char *EffortToText(CodexEffort effort);

    StatusCallback statusCallback_{nullptr};
    void *statusContext_{nullptr};
    char receiveBuffer_[kReceiveBufferSize]{};
    size_t receiveLength_{0};
    CodexTask taskQueue_[kTaskQueueSize]{};
    size_t taskHead_{0};
    size_t taskTail_{0};
    size_t taskCount_{0};
    uint32_t lastHostMessageMs_{0};
    CodexStatus status_{CodexStatus::DISCONNECTED};
    CodexEffort effort_{CodexEffort::UNKNOWN};
    uint8_t taskCountStatus_{1};
    bool hostSeen_{false};
};
