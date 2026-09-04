#pragma once

#include "types.h"

void snifferBegin();
void snifferLoop();
void snifferSetHopping(bool on);
void snifferNextChannel();
void snifferLockHop(bool on);
void snifferSetPromisc(bool on);
bool snifferHopping();
uint8_t snifferChannel();

extern Stats g_stats;
extern ApInfo g_aps[MAX_APS];
extern portMUX_TYPE g_statsMux;

ApInfo* apFind(const uint8_t bssid[6]);
