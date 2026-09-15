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
    effort_ = CodexEffort::UNKNOWN;
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
    CodexEffort effort = CodexEffort::UNKNOWN;
    if (countSeparator != nullptr)
    {
        char *end = nullptr;
        const unsigned long parsedCount = std::strtoul(countSeparator + 1, &end, 10);
        if (end != countSeparator + 1 && (*end == '\0' || *end == '|') && parsedCount > 0)
        {
            taskCount = static_cast<uint8_t>(parsedCount > 255 ? 255 : parsedCount);
        }
        if (end != nullptr && *end == '|' && end[1] != '\0')
        {
            effort = ParseEffort(end + 1);
        }
    }

    bool recognizedStatus = true;
    if (std::strcmp(statusValue, "READY") == 0)
    {
        SetStatus(CodexStatus::READY, taskCount, effort);
    }
    else if (std::strcmp(statusValue, "CREATING") == 0)
    {
        SetStatus(CodexStatus::CREATING, taskCount, effort);
    }
    else if (std::strcmp(statusValue, "RUNNING") == 0)
    {
        SetStatus(CodexStatus::RUNNING, taskCount, effort);
    }
    else if (std::strcmp(statusValue, "APPROVAL") == 0)
    {
        SetStatus(CodexStatus::WAITING_APPROVAL, taskCount, effort);
    }
    else if (std::strcmp(statusValue, "WAITING") == 0)
    {
        SetStatus(CodexStatus::WAITING_INPUT, taskCount, effort);
    }
    else if (std::strcmp(statusValue, "PAUSING") == 0)
    {
        SetStatus(CodexStatus::PAUSING, taskCount, effort);
    }
    else if (std::strcmp(statusValue, "PAUSED") == 0)
    {
        SetStatus(CodexStatus::PAUSED, taskCount, effort);
    }
    else if (std::strcmp(statusValue, "RESUMING") == 0)
    {
        SetStatus(CodexStatus::RESUMING, taskCount, effort);
    }
    else if (std::strcmp(statusValue, "DONE") == 0)
    {
        SetStatus(CodexStatus::DONE, taskCount, effort);
    }
    else if (std::strcmp(statusValue, "ERROR") == 0)
    {
        SetStatus(CodexStatus::ERROR, taskCount, effort);
    }
    else if (std::strcmp(statusValue, "DISCONNECTED") == 0)
    {
        hostSeen_ = false;
        SetStatus(CodexStatus::DISCONNECTED, taskCount, effort);
    }
    else
    {
        recognizedStatus = false;
    }

    if (recognizedStatus)
    {
        char acknowledgement[56]{};
        const char *effortText = EffortToText(effort_);
        if (effortText[0] != '\0')
        {
            snprintf(acknowledgement, sizeof(acknowledgement), "CX>STATUS|%s|%u|%s", statusValue, taskCount, effortText);
        }
        else
        {
            snprintf(acknowledgement, sizeof(acknowledgement), "CX>STATUS|%s|%u", statusValue, taskCount);
        }
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

void CodexSerialBridge::SetStatus(CodexStatus status, uint8_t task_count, CodexEffort effort)
{
    const CodexEffort effectiveEffort = effort == CodexEffort::UNKNOWN ? effort_ : effort;
    if (status_ == status && taskCountStatus_ == task_count && effort_ == effectiveEffort)
    {
        return;
    }

    status_ = status;
    taskCountStatus_ = task_count;
    effort_ = effectiveEffort;
    if (statusCallback_ != nullptr)
    {
        statusCallback_(status_, taskCountStatus_, effort_, statusContext_);
    }
}

const char *CodexSerialBridge::TaskToLine(CodexTask task)
{
    switch (task)
    {
    case CodexTask::NEW_TASK:
        return "CX>TASK|NEW";
    case CodexTask::PAUSE:
        return "CX>ACTION|PAUSE";
    case CodexTask::RESUME:
        return "CX>ACTION|RESUME";
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
    case CodexTask::EFFORT_NEXT:
        return "CX>ACTION|EFFORT_NEXT";
    case CodexTask::EFFORT_PREVIOUS:
        return "CX>ACTION|EFFORT_PREVIOUS";
    case CodexTask::EFFORT_CURRENT:
        return "CX>ACTION|EFFORT_CURRENT";
    }
    return nullptr;
}

CodexEffort CodexSerialBridge::ParseEffort(const char *value)
{
    if (value == nullptr)
    {
        return CodexEffort::UNKNOWN;
    }
    if (std::strcmp(value, "MINIMAL") == 0)
        return CodexEffort::EFFORT_MINIMAL;
    if (std::strcmp(value, "LOW") == 0)
        return CodexEffort::EFFORT_LOW;
    if (std::strcmp(value, "MEDIUM") == 0)
        return CodexEffort::EFFORT_MEDIUM;
    if (std::strcmp(value, "HIGH") == 0)
        return CodexEffort::EFFORT_HIGH;
    if (std::strcmp(value, "XHIGH") == 0)
        return CodexEffort::EFFORT_XHIGH;
    if (std::strcmp(value, "MAX") == 0)
        return CodexEffort::EFFORT_MAX;
    if (std::strcmp(value, "ULTRA") == 0)
        return CodexEffort::EFFORT_ULTRA;
    return CodexEffort::UNKNOWN;
}

const char *CodexSerialBridge::EffortToText(CodexEffort effort)
{
    switch (effort)
    {
    case CodexEffort::EFFORT_MINIMAL:
        return "MINIMAL";
    case CodexEffort::EFFORT_LOW:
        return "LOW";
    case CodexEffort::EFFORT_MEDIUM:
        return "MEDIUM";
    case CodexEffort::EFFORT_HIGH:
        return "HIGH";
    case CodexEffort::EFFORT_XHIGH:
        return "XHIGH";
    case CodexEffort::EFFORT_MAX:
        return "MAX";
    case CodexEffort::EFFORT_ULTRA:
        return "ULTRA";
    case CodexEffort::UNKNOWN:
    default:
        return "";
    }
}
