#include "CodexSerialBridge.h"

#include <cstring>
#include <cstdlib>

namespace
{
    constexpr const char *kStatusPrefix = "CX<STATUS|";
    constexpr size_t kStatusPrefixLength = 10;
    constexpr size_t kMaxBytesPerLoop = 48;
}

void CodexSerialBridge::Begin()
{
    receiveLength_ = 0;
    taskHead_ = 0;
    taskTail_ = 0;
    taskCount_ = 0;
    lastHostMessageMs_ = 0;
    hostSeen_ = false;
    SetStatus(CodexStatus::DISCONNECTED);
}

void CodexSerialBridge::Loop()
{
    ReadIncoming();

    if (hostSeen_ && static_cast<uint32_t>(millis() - lastHostMessageMs_) > kHostTimeoutMs)
    {
        hostSeen_ = false;
        taskHead_ = 0;
        taskTail_ = 0;
        taskCount_ = 0;
        SetStatus(CodexStatus::DISCONNECTED);
    }

    FlushOneTask();
}

bool CodexSerialBridge::QueueTask(CodexTask task)
{
    if (!IsHostConnected() || taskCount_ >= kTaskQueueSize)
    {
        return false;
    }

    taskQueue_[taskTail_] = task;
    taskTail_ = (taskTail_ + 1) % kTaskQueueSize;
    ++taskCount_;
    return true;
}

void CodexSerialBridge::SetStatusCallback(StatusCallback callback, void *context)
{
    statusCallback_ = callback;
    statusContext_ = context;
}

bool CodexSerialBridge::IsHostConnected() const
{
    return hostSeen_ && static_cast<uint32_t>(millis() - lastHostMessageMs_) <= kHostTimeoutMs;
}

void CodexSerialBridge::ReadIncoming()
{
    size_t processed = 0;
    while (Serial.available() > 0 && processed < kMaxBytesPerLoop)
    {
        const int value = Serial.read();
        if (value < 0)
        {
            break;
        }
        ++processed;

        const char ch = static_cast<char>(value);
        if (ch == '\r')
        {
            continue;
        }
        if (ch == '\n')
        {
            if (receiveLength_ > 0)
            {
                receiveBuffer_[receiveLength_] = '\0';
                ProcessLine(receiveBuffer_);
                receiveLength_ = 0;
            }
            continue;
        }

        if (receiveLength_ + 1 < kReceiveBufferSize)
        {
            receiveBuffer_[receiveLength_++] = ch;
        }
        else
        {
            receiveLength_ = 0;
        }
    }
}

void CodexSerialBridge::ProcessLine(const char *line)
{
    if (line == nullptr || std::strncmp(line, "CX<", 3) != 0)
    {
        return;
    }

    hostSeen_ = true;
    lastHostMessageMs_ = millis();

    if (std::strcmp(line, "CX<PING") == 0)
    {
        if (status_ == CodexStatus::DISCONNECTED)
        {
            SetStatus(CodexStatus::READY);
        }
        SendLine("CX>PONG");
        return;
    }

    if (std::strncmp(line, kStatusPrefix, kStatusPrefixLength) != 0)
    {
        return;
    }

    const char *value = line + kStatusPrefixLength;
    const char *countSeparator = std::strchr(value, '|');
    const size_t valueLength = countSeparator == nullptr
                                   ? std::strlen(value)
                                   : static_cast<size_t>(countSeparator - value);
    if (valueLength == 0 || valueLength >= 16)
    {
        return;
    }

    char statusValue[16]{};
    std::memcpy(statusValue, value, valueLength);

    uint8_t taskCount = 1;
    if (countSeparator != nullptr)
    {
        char *end = nullptr;
        const unsigned long parsedCount = std::strtoul(countSeparator + 1, &end, 10);
        if (end != countSeparator + 1 && *end == '\0' && parsedCount > 0)
        {
            taskCount = static_cast<uint8_t>(parsedCount > 255 ? 255 : parsedCount);
        }
    }

    bool recognizedStatus = true;
    if (std::strcmp(statusValue, "READY") == 0)
    {
        SetStatus(CodexStatus::READY, taskCount);
    }
    else if (std::strcmp(statusValue, "RUNNING") == 0)
    {
        SetStatus(CodexStatus::RUNNING, taskCount);
    }
    else if (std::strcmp(statusValue, "APPROVAL") == 0)
    {
        SetStatus(CodexStatus::WAITING_APPROVAL, taskCount);
    }
    else if (std::strcmp(statusValue, "WAITING") == 0)
    {
        SetStatus(CodexStatus::WAITING_INPUT, taskCount);
    }
    else if (std::strcmp(statusValue, "PAUSED") == 0)
    {
        SetStatus(CodexStatus::PAUSED, taskCount);
    }
    else if (std::strcmp(statusValue, "DONE") == 0)
    {
        SetStatus(CodexStatus::DONE, taskCount);
    }
    else if (std::strcmp(statusValue, "ERROR") == 0)
    {
        SetStatus(CodexStatus::ERROR, taskCount);
    }
    else if (std::strcmp(statusValue, "DISCONNECTED") == 0)
    {
        hostSeen_ = false;
        SetStatus(CodexStatus::DISCONNECTED, taskCount);
    }
    else
    {
        recognizedStatus = false;
    }

    if (recognizedStatus)
    {
        char acknowledgement[40]{};
        snprintf(acknowledgement, sizeof(acknowledgement), "CX>STATUS|%s|%u", statusValue, taskCount);
        SendLine(acknowledgement);
    }
}

void CodexSerialBridge::FlushOneTask()
{
    if (taskCount_ == 0 || !IsHostConnected())
    {
        return;
    }

    const char *line = TaskToLine(taskQueue_[taskHead_]);
    if (line == nullptr)
    {
        taskHead_ = (taskHead_ + 1) % kTaskQueueSize;
        --taskCount_;
        return;
    }

    const size_t required = std::strlen(line) + 2;
    if (Serial.availableForWrite() < static_cast<int>(required))
    {
        return;
    }

    Serial.println(line);
    taskHead_ = (taskHead_ + 1) % kTaskQueueSize;
    --taskCount_;
}

void CodexSerialBridge::SendLine(const char *line)
{
    if (line == nullptr)
    {
        return;
    }

    const size_t required = std::strlen(line) + 2;
    if (Serial.availableForWrite() >= static_cast<int>(required))
    {
        Serial.println(line);
    }
}

void CodexSerialBridge::SetStatus(CodexStatus status, uint8_t task_count)
{
    if (status_ == status && taskCountStatus_ == task_count)
    {
        return;
    }

    status_ = status;
    taskCountStatus_ = task_count;
    if (statusCallback_ != nullptr)
    {
        statusCallback_(status_, taskCountStatus_, statusContext_);
    }
}

const char *CodexSerialBridge::TaskToLine(CodexTask task)
{
    switch (task)
    {
    case CodexTask::ANALYZE:
        return "CX>TASK|ANALYZE";
    case CodexTask::REVIEW:
        return "CX>TASK|REVIEW";
    case CodexTask::ACCEPT:
        return "CX>ACTION|ACCEPT";
    case CodexTask::DECLINE:
        return "CX>ACTION|DECLINE";
    case CodexTask::INTERRUPT_TURN:
        return "CX>ACTION|INTERRUPT";
    }
    return nullptr;
}
