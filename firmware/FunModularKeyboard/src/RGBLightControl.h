#pragma once
#include <Arduino.h>
#include <FastLED.h>

#define LED_VCC_CTRL     36
#define LED_PIN          6
#define NUM_LEDS         16
#define LED_TYPE         WS2812B
#define COLOR_ORDER      GRB



class RGBLightControl {
public:
    RGBLightControl(uint8_t brightness = 50);
    ~RGBLightControl();
    void SetBrightness(uint8_t brightness);
    void SetLED(uint8_t index, CRGB color);
    void TurnOffLED(uint8_t index);
    void SetLEDColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
    void SetLEDHSV(uint8_t index, uint8_t h, uint8_t s, uint8_t v);

    void SetAllLEDColor(uint8_t r, uint8_t g, uint8_t b);
    void TurnOffAllLED();
    void Rainbow();
    void RainbowWave();
    void ColorCycle();
    void Meteor();
    void Fire();
    void Pulse(uint8_t r, uint8_t g, uint8_t b);

private:
    CRGB leds[NUM_LEDS];

    uint8_t _brightness{50};
};