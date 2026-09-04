#pragma once

#include "types.h"

bool sdBegin();
bool sdOk();
uint32_t sdFreeMb();
bool sdSaveHandshake(const HsSlot& slot, const char* ssid, bool pmkid);
void sdLogEvent(const char* msg);
void sdLogAp(const ApInfo& ap);
void sdWriteReadme();
void sdSaveStats(const Stats& st);
