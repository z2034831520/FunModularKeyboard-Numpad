#pragma once

#include <stdint.h>

enum class CodexTask : uint8_t
{
    ANALYZE,
    REVIEW,
    ACCEPT,
    DECLINE,
    INTERRUPT_TURN,
};

enum class CodexStatus : uint8_t
{
    DISCONNECTED,
    READY,
    RUNNING,
    WAITING_APPROVAL,
    WAITING_INPUT,
    PAUSED,
    DONE,
    ERROR,
};
