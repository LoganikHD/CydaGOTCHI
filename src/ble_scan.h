#pragma once

#include "types.h"

extern BleDev g_ble[MAX_BLE];

void bleBegin();
void bleLoop();
void blePause(bool on);
