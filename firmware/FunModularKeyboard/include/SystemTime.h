#pragma once

#include <stdlib.h>
#include <time.h>

namespace SystemTime
{
    constexpr const char *kChinaTimeZone = "CST-8";

    // The Unix clock survives a software restart, but the process TZ setting does not.
    // Apply it on every boot so localtime_r() continues to return China Standard Time.
    inline void ConfigureChinaTimeZone()
    {
        setenv("TZ", kChinaTimeZone, 1);
        tzset();
    }

    // Reject the ESP32's unset power-on clock (1970) and obviously stale values.
    constexpr time_t kMinimumValidEpoch = 1704067200; // 2024-01-01 00:00:00 UTC

    inline bool GetLocalTime(struct tm *timeInfo)
    {
        if (timeInfo == nullptr)
        {
            return false;
        }

        time_t now = 0;
        time(&now);
        if (now < kMinimumValidEpoch)
        {
            return false;
        }

        return localtime_r(&now, timeInfo) != nullptr;
    }
}
