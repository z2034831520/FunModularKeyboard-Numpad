#pragma once

#include <stdint.h>

enum class CodexTask : uint8_t
{
    NEW_TASK,
    PAUSE,
    RESUME,
    ANALYZE,
    REVIEW,
    ACCEPT,
    DECLINE,
    INTERRUPT_TURN,
    EFFORT_NEXT,
    EFFORT_PREVIOUS,
    EFFORT_CURRENT,
};

enum class CodexEffort : uint8_t
{
    UNKNOWN,
    EFFORT_MINIMAL,
    EFFORT_LOW,
    EFFORT_MEDIUM,
    EFFORT_HIGH,
    EFFORT_XHIGH,
    EFFORT_MAX,
    EFFORT_ULTRA,
};

enum class CodexStatus : uint8_t
{
    DISCONNECTED,
    READY,
    CREATING,
    RUNNING,
    WAITING_APPROVAL,
    WAITING_INPUT,
    PAUSING,
    PAUSED,
    RESUMING,
    DONE,
    ERROR,
};
