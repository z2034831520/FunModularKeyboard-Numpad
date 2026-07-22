#include "RGBLightControl.h"

RGBLightControl::RGBLightControl(uint8_t brightness)
: _brightness(brightness) {

    //开启LED VCC
    pinMode(LED_VCC_CTRL, OUTPUT);
    digitalWrite(LED_VCC_CTRL, LOW);

    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.setBrightness(brightness);
}


RGBLightControl::~RGBLightControl() {}

/**
 * 设置LED亮度
 * @param brightness 亮度值 0~100
 */
void RGBLightControl::SetBrightness(uint8_t brightness) {
    FastLED.setBrightness(brightness * 255 / 100);  
}


/**
 * 设置指定LED的颜色
 * @param index LED索引(0开始)
 * @param color CRGB颜色值
 */
void RGBLightControl::SetLED(uint8_t index, CRGB color) {
  if(index >= NUM_LEDS) return;
  leds[index] = color;
}

/**
 * 关闭指定LED
 * @param index LED索引(0开始)
 */
void RGBLightControl::TurnOffLED(uint8_t index) {
  if(index >= NUM_LEDS) return;
  leds[index] = CRGB::Black;
}

/**
 * 设置指定LED的RGB颜色
 * @param index LED索引(0开始)
 * @param r 红色值(0-255)
 * @param g 绿色值(0-255)
 * @param b 蓝色值(0-255)
 */
void RGBLightControl::SetLEDColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
  if(index >= NUM_LEDS) return;
  leds[index] = CRGB(r, g, b);
  FastLED.show();
}

/**
 * 设置指定LED的HSV颜色
 * @param index LED索引(0开始)
 * @param h 色调(0-255)
 * @param s 饱和度(0-255)
 * @param v 亮度(0-255)
 */
void RGBLightControl::SetLEDHSV(uint8_t index, uint8_t h, uint8_t s, uint8_t v) {
  if(index >= NUM_LEDS) return;
  leds[index] = CHSV(h, s, v);
}


/**
 * 设置所有LED的RGB颜色
 * @param r 红色值(0-255)
 * @param g 绿色值(0-255)
 * @param b 蓝色值(0-255)
 */
void RGBLightControl::SetAllLEDColor(uint8_t r, uint8_t g, uint8_t b) {
  for(int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB(r, g, b);
  }
  FastLED.show();
}

/**
 * 关闭所有LED
 */
void RGBLightControl::TurnOffAllLED() {
  for(int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Black;
  }
  FastLED.show();
}

//彩虹渐变效果
void RGBLightControl::Rainbow() {
  static uint8_t hue = 0;
  fill_rainbow(leds, NUM_LEDS, hue, 7);  // 7是色相增量，控制彩虹宽度
  hue++;
  FastLED.show();
  FastLED.delay(30);
}

//彩虹波浪效果
void RGBLightControl::RainbowWave() {
  static uint8_t hue = 0;
  for(int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CHSV(hue + (i * 10), 255, 255);  // 创建波浪效果
  }
  hue++;
  FastLED.show();
  FastLED.delay(30);
}

//颜色循环效果
void RGBLightControl::ColorCycle() {
  static uint8_t hue = 0;
  fill_solid(leds, NUM_LEDS, CHSV(hue, 255, 255));
  hue++;
  FastLED.show();
  FastLED.delay(50);
}

//流星效果
void RGBLightControl::Meteor() {
  // 淡出所有LED
  fadeToBlackBy(leds, NUM_LEDS, 50);
  
  // 添加流星
  int pos = beatsin16(20, 0, NUM_LEDS-1);  // 使用正弦波创建平滑运动
  leds[pos] = CHSV(beatsin8(10, 0, 255), 255, 255);
  
  FastLED.show();
  FastLED.delay(30);
}

//火焰效果
void RGBLightControl::Fire() {
  // 随机生成火焰效果
  for(int i = 0; i < NUM_LEDS; i++) {
    // 热度计算
    int heat = random(150, 255);
    leds[i] = HeatColor(heat);
  }
  
  //让火焰向上传播
  for(int k = NUM_LEDS - 1; k > 0; k--) {
    leds[k] = leds[k-1];
  }
  
  FastLED.show();
  FastLED.delay(50);
}

//脉冲效果
void RGBLightControl::Pulse(uint8_t r, uint8_t g, uint8_t b) {
  static uint8_t brightness = 0;
  static int8_t fadeAmount = 5;
  
  fill_solid(leds, NUM_LEDS, CRGB(r, g, b).nscale8(brightness));
  
  brightness += fadeAmount;
  
  if(brightness <= 0 || brightness >= 255) {
    fadeAmount = -fadeAmount;
  }
  
  FastLED.show();
  FastLED.delay(30);
}