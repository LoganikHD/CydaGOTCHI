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
  // In stealth mode the backlight must stay dark from the first GPIO setup.
  setBacklight(!STEALTH_MODE);

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
#if STEALTH_MODE
  // Stealth mode: ignore all UI colour requests and keep the LED off.
  (void)r;
  (void)g;
  (void)b;
  digitalWrite(PIN_LED_R, HIGH);
  digitalWrite(PIN_LED_G, HIGH);
  digitalWrite(PIN_LED_B, HIGH);
#else
  digitalWrite(PIN_LED_R, r ? LOW : HIGH);
  digitalWrite(PIN_LED_G, g ? LOW : HIGH);
  digitalWrite(PIN_LED_B, b ? LOW : HIGH);
#endif
}

bool bootPressed() {
  return digitalRead(PIN_BOOT) == LOW;
}
