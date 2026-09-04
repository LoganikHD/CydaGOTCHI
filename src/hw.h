#pragma once

#include <TFT_eSPI.h>
#include "config.h"

extern TFT_eSPI tft;

void hwBegin();
void setBacklight(bool on);
void rgbLed(uint8_t r, uint8_t g, uint8_t b);  // 0-255, active-low hardware
bool bootPressed();
