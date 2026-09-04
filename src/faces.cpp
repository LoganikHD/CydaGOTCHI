#include "faces.h"
#include "hw.h"

// Classic pwnagotchi look: two eyes + a mouth, drawn with primitives
// so it stays readable on the CYD without Unicode fonts.

void drawGotchiFace(int x, int y, Mood mood, uint32_t now) {
  tft.fillRect(x, y, FACE_W, FACE_H, TFT_BLACK);

  int look = 0;
  int blink = 0;
  int eyeH = 28;
  int eyeW = 32;
  uint16_t col = 0xFFFF;
  bool happy = false;
  bool sleepy = false;
  bool sad = false;
  bool intense = false;

  switch (mood) {
    case Mood::Idle:
    case Mood::Scanning:
      look = ((now / 900) % 4 == 0) ? -5 : (((now / 900) % 4 == 2) ? 5 : 0);
      blink = ((now / 180) % 22 == 0);
      break;
    case Mood::Happy:
    case Mood::Excited:
      happy = true;
      col = 0x07E0;
      look = ((now / 500) % 2) ? -4 : 4;
      eyeH = 30;
      break;
    case Mood::Intense:
      intense = true;
      col = 0xFDA0;
      eyeH = 22;
      eyeW = 36;
      break;
    case Mood::Bored:
      eyeH = 16;
      look = 6;
      break;
    case Mood::Sleepy:
      sleepy = true;
      eyeH = 8;
      col = 0x7BEF;
      break;
    case Mood::Sad:
      sad = true;
      col = 0xF81F;
      eyeH = 18;
      break;
    default:
      break;
  }

  int lx = x + 22;
  int rx = x + FACE_W - 22 - eyeW;
  int ey = y + 14;
  if (blink || sleepy) {
    tft.fillRoundRect(lx, ey + eyeH / 2 - 2, eyeW, 4, 2, col);
    tft.fillRoundRect(rx, ey + eyeH / 2 - 2, eyeW, 4, 2, col);
  } else {
    tft.fillRoundRect(lx, ey, eyeW, eyeH, 8, col);
    tft.fillRoundRect(rx, ey, eyeW, eyeH, 8, col);
    int pr = intense ? 6 : 8;
    int py = ey + eyeH / 2 - pr / 2 + (sad ? 3 : 0);
    tft.fillCircle(lx + eyeW / 2 + look, py + pr / 2, pr, TFT_BLACK);
    tft.fillCircle(rx + eyeW / 2 + look, py + pr / 2, pr, TFT_BLACK);
  }

  int mx = x + FACE_W / 2;
  int my = y + 58;
  if (happy) {
    tft.drawLine(mx - 18, my, mx - 8, my + 8, col);
    tft.drawLine(mx - 8, my + 8, mx + 8, my + 8, col);
    tft.drawLine(mx + 8, my + 8, mx + 18, my, col);
  } else if (sad) {
    tft.drawLine(mx - 16, my + 8, mx - 6, my, col);
    tft.drawLine(mx - 6, my, mx + 6, my, col);
    tft.drawLine(mx + 6, my, mx + 16, my + 8, col);
  } else if (sleepy) {
    tft.drawLine(mx - 10, my + 4, mx + 10, my + 4, col);
  } else if (intense) {
    tft.fillTriangle(mx - 8, my, mx + 8, my, mx, my + 10, col);
  } else {
    tft.drawLine(mx - 14, my + 2, mx + 14, my + 2, col);
  }
}

const char* faceGlyph(Mood mood) {
  switch (mood) {
    case Mood::Excited: return "(^_^)";
    case Mood::Happy: return "(^o^)";
    case Mood::Intense: return "(>_<)";
    case Mood::Bored: return "(-_-)";
    case Mood::Sleepy: return "(u_u)";
    case Mood::Sad: return "(;_;)";
    default: return "(o_o)";
  }
}
