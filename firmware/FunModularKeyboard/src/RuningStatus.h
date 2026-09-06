#pragma once
#include <Arduino.h>

class RuningStatus
{
public:
    RuningStatus();
    ~RuningStatus();

public:
    enum RUNNINGSTATUS
    {
        STATUS_OFF = 0,
        STATUS_STANDY,
        STATUS_ON,
        STATUS_ERROR,
        STATUS_LIGHT_SLEEP,
        STATUS_DEEP_SLEEP,
    };

private:
};