#pragma once

#include <stdint.h>

namespace display_config {

// MainBoard V1.2 LCD connector signals (ESP32-S3 GPIO numbers).
constexpr int8_t kBacklightPin = 37;
constexpr int8_t kChipSelectPin = 40;
constexpr int8_t kClockPin = 41;
constexpr int8_t kDataPin = 42;
constexpr int8_t kDataCommandPin = 45;
constexpr int8_t kResetPin = 46;

// The fitted bar display is a 142x428 NV3007 panel, mounted in landscape.
constexpr int16_t kPanelWidth = 142;
constexpr int16_t kPanelHeight = 428;
constexpr int16_t kDisplayWidth = 428;
constexpr int16_t kDisplayHeight = 142;
constexpr uint8_t kRotation = 1;
constexpr int32_t kSpiFrequency = 20000000;
constexpr uint16_t kLandscapeXOffset = 0;
constexpr uint16_t kLandscapeYOffset = 14;

// The PNP backlight switch on MainBoard V1.2 is active low.
constexpr uint8_t kBacklightOnLevel = 0;
constexpr uint8_t kBacklightOffLevel = 1;

} // namespace display_config
