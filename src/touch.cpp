#include "touch.h"
#include "config.h"
#include "hw.h"
#include <Preferences.h>

static int16_t calX0 = 200, calX1 = 3900, calY0 = 200, calY1 = 3700;
static bool calLoaded = false;

static void tPinOut(int pin, bool v) { pinMode(pin, OUTPUT); digitalWrite(pin, v ? HIGH : LOW); }
static int tPinIn(int pin) { pinMode(pin, INPUT); return digitalRead(pin); }

static void tDelay() { delayMicroseconds(2); }

static uint16_t xptTransfer(uint8_t cmd) {
  uint16_t v = 0;
  tPinOut(PIN_TOUCH_CLK, false);
  for (int i = 7; i >= 0; --i) {
    tPinOut(PIN_TOUCH_MOSI, (cmd >> i) & 1);
    tPinOut(PIN_TOUCH_CLK, true); tDelay();
    tPinOut(PIN_TOUCH_CLK, false); tDelay();
  }
  tDelay();
  for (int i = 11; i >= 0; --i) {
    tPinOut(PIN_TOUCH_CLK, true); tDelay();
    if (tPinIn(PIN_TOUCH_MISO)) v |= (1 << i);
    tPinOut(PIN_TOUCH_CLK, false); tDelay();
  }
  // extra clocks to complete the frame
  for (int i = 0; i < 3; ++i) {
    tPinOut(PIN_TOUCH_CLK, true); tDelay();
    tPinOut(PIN_TOUCH_CLK, false); tDelay();
  }
  return v;
}

static bool xptSample(uint16_t& x, uint16_t& y, uint16_t& z) {
  digitalWrite(PIN_TOUCH_CS, LOW);
  tDelay();
  z = xptTransfer(0xB1);  // Z1
  x = xptTransfer(0xD1);  // X
  y = xptTransfer(0x91);  // Y
  xptTransfer(0x80);      // power down
  digitalWrite(PIN_TOUCH_CS, HIGH);
  return z >= TOUCH_Z_MIN;
}

void touchBegin() {
  pinMode(PIN_TOUCH_CS, OUTPUT);
  digitalWrite(PIN_TOUCH_CS, HIGH);
  pinMode(PIN_TOUCH_CLK, OUTPUT);
  pinMode(PIN_TOUCH_MOSI, OUTPUT);
  pinMode(PIN_TOUCH_MISO, INPUT);
  pinMode(PIN_TOUCH_IRQ, INPUT);

  Preferences prefs;
  if (prefs.begin("touch", true)) {
    if (prefs.getBool("okp", false)) {
      calX0 = prefs.getShort("x0", calX0);
      calX1 = prefs.getShort("x1", calX1);
      calY0 = prefs.getShort("y0", calY0);
      calY1 = prefs.getShort("y1", calY1);
      calLoaded = true;
    }
    prefs.end();
  }
}

bool touchRead(TouchPoint& out) {
  out.pressed = false;
  uint16_t rx, ry, rz;
  if (!xptSample(rx, ry, rz)) return false;

  // average a couple of samples
  uint16_t rx2, ry2, rz2;
  if (!xptSample(rx2, ry2, rz2)) return false;
  rx = (rx + rx2) / 2;
  ry = (ry + ry2) / 2;

  int16_t x = map(rx, calX0, calX1, 0, SCREEN_W - 1);
  int16_t y = map(ry, calY0, calY1, 0, SCREEN_H - 1);
  if (x < 0) x = 0; if (x >= SCREEN_W) x = SCREEN_W - 1;
  if (y < 0) y = 0; if (y >= SCREEN_H) y = SCREEN_H - 1;
  out.pressed = true;
  out.x = x;
  out.y = y;
  return true;
}

static void waitTap(uint16_t& x, uint16_t& y) {
  while (true) {
    uint16_t rx, ry, rz;
    if (xptSample(rx, ry, rz)) {
      delay(30);
      uint16_t ax = 0, ay = 0, n = 0;
      for (int i = 0; i < 8; ++i) {
        if (xptSample(rx, ry, rz)) { ax += rx; ay += ry; n++; }
        delay(8);
      }
      if (n >= 4) {
        x = ax / n;
        y = ay / n;
        while (xptSample(rx, ry, rz)) delay(10);
        delay(200);
        return;
      }
    }
    delay(10);
  }
}

void touchCalibrateInteractive() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("tap the circles", 40, 8);

  auto drawTarget = [](int sx, int sy, uint16_t col) {
    tft.fillCircle(sx, sy, 14, col);
    tft.drawCircle(sx, sy, 18, TFT_WHITE);
  };

  uint16_t r0x, r0y, r1x, r1y;
  drawTarget(20, 20, TFT_RED);
  waitTap(r0x, r0y);
  tft.fillCircle(20, 20, 18, TFT_GREEN);

  drawTarget(SCREEN_W - 21, SCREEN_H - 21, TFT_RED);
  waitTap(r1x, r1y);
  tft.fillCircle(SCREEN_W - 21, SCREEN_H - 21, 18, TFT_GREEN);

  calX0 = r0x;
  calY0 = r0y;
  calX1 = r1x;
  calY1 = r1y;
  if (calX0 == calX1) calX1 = calX0 + 1;
  if (calY0 == calY1) calY1 = calY0 + 1;

  Preferences prefs;
  if (prefs.begin("touch", false)) {
    prefs.putBool("okp", true);
    prefs.putShort("x0", calX0);
    prefs.putShort("x1", calX1);
    prefs.putShort("y0", calY0);
    prefs.putShort("y1", calY1);
    prefs.end();
  }
  calLoaded = true;
  delay(250);
}
