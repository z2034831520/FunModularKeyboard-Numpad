#pragma once

#include <Arduino.h>

struct BatteryReading
{
    uint16_t voltage_mv{0};
    uint8_t percent{0};
};

class BatteryMonitor
{
public:
    void begin();
    BatteryReading read();

private:
    uint16_t readVoltageMv();
    static uint8_t voltageToPercent(uint16_t voltageMv);

    uint32_t filteredVoltageMv_{0};
    bool hasFilteredReading_{false};
};
