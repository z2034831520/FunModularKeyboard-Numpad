#include "BatteryMonitor.h"

namespace
{
    constexpr uint8_t kBatteryAdcPin = 4;
    constexpr uint8_t kSampleCount = 32;
    constexpr float kDividerRatio = 2.0f;

    // Compare the calculated voltage with a multimeter and adjust this value
    // when calibrating a specific board.
    constexpr float kCalibrationFactor = 1.0f;

    struct BatteryCurvePoint
    {
        uint16_t voltage_mv;
        uint8_t percent;
    };

    constexpr BatteryCurvePoint kBatteryCurve[] = {
        {4200, 100},
        {4100, 90},
        {4000, 80},
        {3900, 65},
        {3800, 45},
        {3700, 25},
        {3600, 10},
        {3450, 3},
        {3300, 0},
    };
}

void BatteryMonitor::begin()
{
    pinMode(kBatteryAdcPin, INPUT);
    analogReadResolution(12);
    analogSetPinAttenuation(kBatteryAdcPin, ADC_11db);

    // R5/R10 and C13 need several RC time constants before the first sample.
    delay(250);
}

BatteryReading BatteryMonitor::read()
{
    BatteryReading reading;
    reading.voltage_mv = readVoltageMv();
    reading.percent = voltageToPercent(reading.voltage_mv);
    return reading;
}

uint16_t BatteryMonitor::readVoltageMv()
{
    uint32_t totalAdcMv = 0;

    // Discard the first conversion after selecting the ADC input.
    analogRead(kBatteryAdcPin);
    delayMicroseconds(200);

    for (uint8_t i = 0; i < kSampleCount; ++i)
    {
        totalAdcMv += analogReadMilliVolts(kBatteryAdcPin);
        delayMicroseconds(200);
    }

    const float averageAdcMv = totalAdcMv / static_cast<float>(kSampleCount);
    const uint32_t measuredBatteryMv = static_cast<uint32_t>(
        averageAdcMv * kDividerRatio * kCalibrationFactor + 0.5f);

    if (!hasFilteredReading_)
    {
        filteredVoltageMv_ = measuredBatteryMv;
        hasFilteredReading_ = true;
    }
    else
    {
        // 75% previous value + 25% current value reduces icon flicker.
        filteredVoltageMv_ = (filteredVoltageMv_ * 3U + measuredBatteryMv + 2U) / 4U;
    }

    return filteredVoltageMv_ > UINT16_MAX
               ? UINT16_MAX
               : static_cast<uint16_t>(filteredVoltageMv_);
}

uint8_t BatteryMonitor::voltageToPercent(uint16_t voltageMv)
{
    if (voltageMv >= kBatteryCurve[0].voltage_mv)
    {
        return 100;
    }

    constexpr size_t curveSize = sizeof(kBatteryCurve) / sizeof(kBatteryCurve[0]);
    for (size_t i = 0; i + 1 < curveSize; ++i)
    {
        const BatteryCurvePoint &high = kBatteryCurve[i];
        const BatteryCurvePoint &low = kBatteryCurve[i + 1];

        if (voltageMv >= low.voltage_mv)
        {
            const uint32_t voltageOffset = voltageMv - low.voltage_mv;
            const uint32_t voltageRange = high.voltage_mv - low.voltage_mv;
            const uint32_t percentRange = high.percent - low.percent;
            return static_cast<uint8_t>(
                low.percent + voltageOffset * percentRange / voltageRange);
        }
    }

    return 0;
}
