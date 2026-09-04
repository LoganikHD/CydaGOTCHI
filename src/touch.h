#pragma once

#include <Arduino.h>

struct TouchPoint {
  bool pressed;
  int16_t x;
  int16_t y;
};

void touchBegin();
bool touchRead(TouchPoint& out);
void touchCalibrateInteractive();
