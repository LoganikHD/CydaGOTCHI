#include "ui.h"
#include "hw.h"
#include "touch.h"
#include "sniffer.h"
#include "sdstore.h"
#include "faces.h"
#include "ble_scan.h"
#include "settings.h"
#include <string.h>

static const uint16_t COL_BG    = 0x0000;
static const uint16_t COL_BRAND = 0xFDA0;
static const uint16_t COL_CYAN  = 0x07FF;
static const uint16_t COL_GREEN = 0x07E0;
static const uint16_t COL_RED   = 0xF800;
static const uint16_t COL_DIM   = 0x7BEF;
static const uint16_t COL_WHITE = 0xFFFF;
static const uint16_t COL_LINE  = 0x2104;
static const uint16_t COL_BTN   = 0x3186;
static const uint16_t COL_BTN_ON= 0x2A60;

static const int HDR_H     = 18;
static const int MASK_X    = (SCREEN_W - FACE_W) / 2;
static const int MASK_Y    = HDR_H + 2;
static const int FOOTER_H  = 36;
static const int FOOTER_Y  = SCREEN_H - FOOTER_H;
static const int PANEL_Y   = MASK_Y + FACE_H + 2;
static const int PANEL_H   = FOOTER_Y - PANEL_Y;
static const int BTN_H     = 28;
static const int BTN_Y     = FOOTER_Y + (FOOTER_H - BTN_H) / 2;
static const int BTN_W     = 72;
static const int BTN_GAP   = 6;
static const int BTN_X0    = 8;

static uint8_t frameIdx = 0;
static uint32_t lastFrame = 0;
static uint32_t lastUi = 0;
static uint32_t lastStatsSave = 0;
static uint32_t lastPwned = 0;
static uint8_t viewMode = 0;  // 0 face  1 aps  2 ble
static bool wasPressed = false;

static uint8_t lastCh = 255;
static bool lastHop = false;
static bool lastList = false;
static uint16_t lastAps = 0xFFFF;
static uint32_t lastEapol = 0xFFFFFFFF;
static uint32_t lastPmkid = 0xFFFFFFFF;
static uint32_t lastPwn = 0xFFFFFFFF;
static uint32_t lastPps = 0xFFFFFFFF;
static uint16_t lastBle = 0xFFFF;
static bool lastSd = true;
static char lastMood[12] = "";
static char lastSsid[SSID_MAX + 1] = "";
static char lastUp[10] = "";

static uint32_t ppsWindowPkts = 0;
static uint32_t ppsWindowAt = 0;
static uint16_t pps = 0;
static uint8_t activity[8] = {0};
static uint8_t activityI = 0;

static const char* moodName(Mood m) {
  switch (m) {
    case Mood::Idle: return "idle";
    case Mood::Scanning: return "scanning";
    case Mood::Intense: return "intense";
    case Mood::Happy: return "happy";
    case Mood::Excited: return "excited";
    case Mood::Bored: return "bored";
    case Mood::Sleepy: return "sleepy";
    case Mood::Sad: return "sad";
  }
  return "idle";
}

static uint16_t frameDelayMs(Mood m) {
  switch (m) {
    case Mood::Excited: return 320;
    case Mood::Happy: return 400;
    case Mood::Intense: return 450;
    case Mood::Scanning: return 600;
    default: return 800;
  }
}

static uint8_t nextFrame(Mood m, uint8_t cur) {
  switch (m) {
    case Mood::Excited:
    case Mood::Happy: {
      const uint8_t seq[] = {0, 4, 1, 4, 2, 4};
      for (uint8_t i = 0; i < 6; i++) if (seq[i] == cur) return seq[(i + 1) % 6];
      return 4;
    }
    case Mood::Sleepy:
    case Mood::Bored: {
      const uint8_t seq[] = {0, 0, 3, 0, 5};
      for (uint8_t i = 0; i < 5; i++) if (seq[i] == cur) return seq[(i + 1) % 5];
      return 0;
    }
    case Mood::Sad:
      return (cur == 3) ? 0 : 3;
    default: {
      const uint8_t seq[] = {0, 1, 0, 2, 0, 3, 0, 5};
      for (uint8_t i = 0; i < 8; i++) if (seq[i] == cur) return seq[(i + 1) % 8];
      return 0;
    }
  }
}

static void fmtUptime(char* out, size_t n) {
  uint32_t s = millis() / 1000;
  uint32_t h = s / 3600;
  uint32_t m = (s / 60) % 60;
  s %= 60;
  snprintf(out, n, "%02lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)s);
}

static void tickPps(uint32_t now) {
  if (ppsWindowAt == 0) {
    ppsWindowAt = now;
    ppsWindowPkts = g_stats.packets;
    return;
  }
  if (now - ppsWindowAt >= 1000) {
    uint32_t d = g_stats.packets - ppsWindowPkts;
    pps = (uint16_t)d;
    ppsWindowPkts = g_stats.packets;
    ppsWindowAt = now;
    activity[activityI % 8] = (uint8_t)(d > 40 ? 8 : (d / 5));
    activityI++;
  }
}

static void drawMask() {
  drawGotchiFace(MASK_X, MASK_Y, g_stats.mood, millis());
}

static void drawHeader(bool force) {
  if (!force && lastCh == g_stats.channel) return;
  tft.fillRect(0, 0, SCREEN_W, HDR_H, COL_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(COL_BRAND, COL_BG);
  tft.drawString("CIRUSTHAVIRUS", 6, 2);
  char ch[10];
  snprintf(ch, sizeof(ch), "CH%02u", g_stats.channel);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(COL_CYAN, COL_BG);
  tft.drawString(ch, SCREEN_W - 6, 2);
}

static void drawFooterBg() {
  tft.drawFastHLine(0, FOOTER_Y, SCREEN_W, COL_LINE);
  tft.fillRect(0, FOOTER_Y + 1, SCREEN_W, FOOTER_H - 1, COL_BG);
}

static void drawButtons(bool force) {
  if (!force && lastCh == g_stats.channel && lastHop == g_stats.hopping && lastList == (viewMode != 0))
    return;
  lastHop = g_stats.hopping;
  lastList = (viewMode != 0);
  lastCh = g_stats.channel;

  drawFooterBg();
  auto btn = [&](int i, const char* label, bool on) {
    int x = BTN_X0 + i * (BTN_W + BTN_GAP);
    uint16_t bg = on ? COL_BTN_ON : COL_BTN;
    tft.fillRoundRect(x, BTN_Y, BTN_W, BTN_H, 4, bg);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(COL_WHITE, bg);
    tft.drawString(label, x + BTN_W / 2, BTN_Y + BTN_H / 2);
  };
  char ch[10];
  snprintf(ch, sizeof(ch), "CH %u", g_stats.channel);
  btn(0, ch, true);
  btn(1, g_stats.hopping ? "HOP" : "HOLD", g_stats.hopping);
  const char* vlab = (viewMode == 1) ? "APS" : (viewMode == 2) ? "BLE" : "FACE";
  btn(2, vlab, viewMode != 0);
}

static void cell(int x, int y, const char* k, const char* v, uint16_t vc) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.drawString(k, x, y);
  tft.fillRect(x, y + 10, 108, 14, COL_BG);
  tft.setTextFont(2);
  tft.setTextColor(vc, COL_BG);
  tft.drawString(v, x, y + 8);
}

static void drawActivity(int x, int y) {
  tft.fillRect(x, y, 72, 10, COL_BG);
  for (int i = 0; i < 8; i++) {
    uint8_t h = activity[(activityI + i) % 8];
    if (h == 0) h = 1;
    int bx = x + i * 9;
    tft.fillRect(bx, y + (10 - h), 7, h, h > 4 ? COL_CYAN : COL_DIM);
  }
}

static void drawStats(bool force) {
  char up[10];
  fmtUptime(up, sizeof(up));
  bool changed = force ||
                 strcmp(lastMood, moodName(g_stats.mood)) != 0 ||
                 strcmp(lastSsid, g_stats.lastSsid) != 0 ||
                 strcmp(lastUp, up) != 0 ||
                 lastAps != g_stats.apCount || lastEapol != g_stats.eapol ||
                 lastPmkid != g_stats.pmkid || lastPwn != g_stats.pwned ||
                 lastPps != pps || lastSd != g_stats.sdOk || lastHop != g_stats.hopping ||
                 lastBle != g_stats.bleCount;
  if (!changed) return;

  lastAps = g_stats.apCount;
  lastEapol = g_stats.eapol;
  lastPmkid = g_stats.pmkid;
  lastPwn = g_stats.pwned;
  lastPps = pps;
  lastBle = g_stats.bleCount;
  lastSd = g_stats.sdOk;
  strncpy(lastMood, moodName(g_stats.mood), sizeof(lastMood) - 1);
  strncpy(lastSsid, g_stats.lastSsid, sizeof(lastSsid) - 1);
  strncpy(lastUp, up, sizeof(lastUp) - 1);

  tft.fillRect(0, PANEL_Y, SCREEN_W, PANEL_H, COL_BG);
  tft.drawFastHLine(12, PANEL_Y, SCREEN_W - 24, COL_LINE);

  tft.setTextDatum(TC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(COL_WHITE, COL_BG);
  char mood[24];
  snprintf(mood, sizeof(mood), "%s  %s", lastMood, g_stats.hopping ? "hop" : "hold");
  tft.drawString(mood, SCREEN_W / 2, PANEL_Y + 4);

  char b[24];
  int y = PANEL_Y + 22;
  snprintf(b, sizeof(b), "%u", g_stats.apCount);
  cell(10, y, "APS", b, COL_WHITE);
  snprintf(b, sizeof(b), "%lu", (unsigned long)g_stats.pwned);
  cell(126, y, "PWNED", b, g_stats.pwned ? COL_GREEN : COL_WHITE);

  y += 28;
  snprintf(b, sizeof(b), "%lu", (unsigned long)g_stats.eapol);
  cell(10, y, "EAPOL", b, COL_CYAN);
  snprintf(b, sizeof(b), "%lu", (unsigned long)g_stats.pmkid);
  cell(126, y, "PMKID", b, COL_CYAN);

  y += 28;
  snprintf(b, sizeof(b), "%u  iot %u", g_stats.bleCount, g_stats.iotCount);
  cell(10, y, "BLE/IOT", b, COL_CYAN);
  cell(126, y, "SD", g_stats.sdOk ? "ready" : "missing", g_stats.sdOk ? COL_GREEN : COL_RED);

  y += 28;
  tft.setTextFont(1);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.drawString("LAST", 10, y);
  tft.setTextColor(COL_WHITE, COL_BG);
  tft.drawString(g_stats.lastSsid[0] ? g_stats.lastSsid : "--", 42, y);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.drawString("UP", 126, y);
  tft.setTextColor(COL_WHITE, COL_BG);
  tft.drawString(up, 146, y);

  drawActivity(10, y + 12);
}

static void drawBleList() {
  tft.fillRect(0, PANEL_Y, SCREEN_W, PANEL_H, COL_BG);
  tft.drawFastHLine(12, PANEL_Y, SCREEN_W - 24, COL_LINE);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
  tft.setTextColor(COL_BRAND, COL_BG);
  tft.drawString("bluetooth / iot", 10, PANEL_Y + 4);
  int row = 0;
  const int maxRows = (PANEL_H - 18) / 12;
  for (int i = 0; i < MAX_BLE && row < maxRows; ++i) {
    if (!(g_ble[i].addr[0] | g_ble[i].addr[5] | g_ble[i].name[0])) continue;
    int yy = PANEL_Y + 16 + row * 12;
    tft.setTextColor(COL_WHITE, COL_BG);
    char line[42];
    snprintf(line, sizeof(line), "%+4d %-4s %s",
             g_ble[i].rssi, g_ble[i].kind, g_ble[i].name);
    tft.drawString(line, 10, yy);
    row++;
  }
  if (row == 0) {
    tft.setTextColor(COL_DIM, COL_BG);
    tft.drawString("scanning BLE...", 10, PANEL_Y + 18);
  }
}

static void drawApList() {
  tft.fillRect(0, PANEL_Y, SCREEN_W, PANEL_H, COL_BG);
  tft.drawFastHLine(12, PANEL_Y, SCREEN_W - 24, COL_LINE);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
  tft.setTextColor(COL_BRAND, COL_BG);
  tft.drawString("nearby 2.4 GHz", 10, PANEL_Y + 4);
  int row = 0;
  const int maxRows = (PANEL_H - 18) / 12;
  for (int i = 0; i < MAX_APS && row < maxRows; ++i) {
    if (!(g_aps[i].bssid[0] | g_aps[i].bssid[5] | g_aps[i].ssid[0] | g_aps[i].beacons))
      continue;
    int yy = PANEL_Y + 16 + row * 12;
    tft.setTextColor(g_aps[i].hasHandshake ? COL_GREEN : COL_WHITE, COL_BG);
    char line[40];
    snprintf(line, sizeof(line), "%02u %+4d  %s",
             g_aps[i].channel, g_aps[i].rssi,
             g_aps[i].ssid[0] ? g_aps[i].ssid : "(hidden)");
    tft.drawString(line, 10, yy);
    row++;
  }
  if (row == 0) {
    tft.setTextColor(COL_DIM, COL_BG);
    tft.drawString("listening...", 10, PANEL_Y + 18);
  }
}

static void updateMood() {
  uint32_t now = millis();
  Mood m = Mood::Scanning;
  if (!g_stats.sdOk) m = Mood::Sad;
  else if (now - lastPwned < 8000) m = Mood::Excited;
  else if (now - lastPwned < 40000) m = Mood::Happy;
  else if (g_stats.apCount >= 20) m = Mood::Intense;
  else if (g_stats.apCount == 0 && now > 20000) m = Mood::Sleepy;
  else if (now > 120000 && g_stats.pwned == 0) m = Mood::Bored;
  else if (now < 4000) m = Mood::Idle;
  g_stats.mood = m;
}

void uiSplash() {
  tft.fillScreen(COL_BG);
  tft.setTextDatum(TC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(COL_BRAND, COL_BG);
  tft.drawString("CIRUSTHAVIRUS", SCREEN_W / 2, 8);
  drawGotchiFace(MASK_X, 36, Mood::Happy, 0);
  tft.setTextColor(COL_CYAN, COL_BG);
  tft.drawString("CYDgotchi " CYDGOTCHI_VERSION, SCREEN_W / 2, 128);
  tft.setTextFont(1);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.drawString("join  CIRUSTHAVIRUS", SCREEN_W / 2, 150);
  tft.drawString("pass  cydgotchi   http://192.168.4.1", SCREEN_W / 2, 164);
  tft.drawString("hold BOOT to calibrate touch", SCREEN_W / 2, 184);
  delay(1200);
}

void uiBegin(bool calibrate) {
  if (calibrate) touchCalibrateInteractive();
  tft.fillScreen(COL_BG);
  lastCh = 255;
  drawHeader(true);
  drawMask();
  drawStats(true);
  drawButtons(true);
}

void uiLoop() {
  uint32_t now = millis();
  static uint32_t lastPwnCount = 0;
  if (g_stats.pwned > lastPwnCount) {
    lastPwned = now;
    lastPwnCount = g_stats.pwned;
    rgbLed(0, 255, 0);
  }
  tickPps(now);
  updateMood();

  if (now - lastFrame >= frameDelayMs(g_stats.mood)) {
    lastFrame = now;
    frameIdx = nextFrame(g_stats.mood, frameIdx);
    if (viewMode == 0) drawMask();
    switch (g_stats.mood) {
      case Mood::Excited: rgbLed(0, 255, 0); break;
      case Mood::Happy: rgbLed(0, 40, 0); break;
      case Mood::Sad: rgbLed(255, 0, 0); break;
      case Mood::Scanning:
      case Mood::Intense: rgbLed(0, 0, 40); break;
      default: rgbLed(0, 0, 0); break;
    }
  }

  if (now - lastUi >= UI_FPS_MS) {
    lastUi = now;
    g_stats.sdOk = sdOk();
    g_stats.sdFreeMb = sdFreeMb();
    drawHeader(false);
    if (viewMode == 1) drawApList();
    else if (viewMode == 2) drawBleList();
    else drawStats(false);
    drawButtons(false);
  }

  if (now - lastStatsSave > 60000) {
    lastStatsSave = now;
    sdSaveStats(g_stats);
  }

  TouchPoint tp;
  bool down = touchRead(tp);
  if (down && !wasPressed) {
    wasPressed = true;
    if (tp.y >= FOOTER_Y) {
      int slot = (tp.x - BTN_X0) / (BTN_W + BTN_GAP);
      if (slot <= 0) {
        snifferNextChannel();
        drawHeader(true);
        drawButtons(true);
      } else if (slot == 1) {
        snifferSetHopping(!snifferHopping());
        drawButtons(true);
        if (viewMode == 0) drawStats(true);
      } else {
        viewMode = (viewMode + 1) % 3;
        if (viewMode == 1) drawApList();
        else if (viewMode == 2) drawBleList();
        else drawStats(true);
        drawButtons(true);
      }
    }
  }
  if (!down) wasPressed = false;
}

void uiSetMood(Mood m) { g_stats.mood = m; }
