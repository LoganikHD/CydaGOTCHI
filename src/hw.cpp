#include "hw.h"

TFT_eSPI tft;

static bool blOn = true;

void hwBegin() {
  pinMode(PIN_BOOT, INPUT_PULLUP);
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  rgbLed(0, 0, 0);

  pinMode(CYD_BL_PIN, OUTPUT);
  pinMode(CYD_BL_PIN_ALT, OUTPUT);
  digitalWrite(CYD_BL_PIN, HIGH);
  digitalWrite(CYD_BL_PIN_ALT, HIGH);

  tft.init();
  tft.setRotation(0);  // portrait 240x320
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
}

void setBacklight(bool on) {
  blOn = on;
  digitalWrite(CYD_BL_PIN, on ? HIGH : LOW);
  digitalWrite(CYD_BL_PIN_ALT, on ? HIGH : LOW);
}

void rgbLed(uint8_t r, uint8_t g, uint8_t b) {
  // CYD RGB is active LOW
  digitalWrite(PIN_LED_R, r ? LOW : HIGH);
  digitalWrite(PIN_LED_G, g ? LOW : HIGH);
  digitalWrite(PIN_LED_B, b ? LOW : HIGH);
}

bool bootPressed() {
  return digitalRead(PIN_BOOT) == LOW;
}
