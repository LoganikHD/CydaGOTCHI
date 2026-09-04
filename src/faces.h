#pragma once

#include "types.h"

#define FACE_W 128
#define FACE_H 76

void drawGotchiFace(int x, int y, Mood mood, uint32_t now);
const char* faceGlyph(Mood mood);
