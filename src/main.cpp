#include "config.h"
#include "hw.h"
#include "touch.h"
#include "sdstore.h"
#include "settings.h"
#include "portal.h"
#include "sniffer.h"
#include "ble_scan.h"
#include "ui.h"

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n[BOOT] CIRUSTHAVIRUS CYDgotchi %s\n", CYDGOTCHI_VERSION);

  hwBegin();
  touchBegin();
  settingsBegin();
  rgbLed(0, 0, 255);
  
  uiSplash();
  bool cal = bootPressed();

  if (cal) Serial.println("[TOUCH] BOOT held — calibrating");
  uiBegin(cal);

  if (sdBegin()) {
    g_stats.sdOk = true;
    g_stats.sdFreeMb = sdFreeMb();
    strncpy(g_stats.status, "sd ready", sizeof(g_stats.status) - 1);
  } else {
    g_stats.sdOk = false;
    strncpy(g_stats.status, "no sd card", sizeof(g_stats.status) - 1);
  }

  // BT coexistence aborts if NimBLE starts after Wi-Fi. BLE first.
  bleBegin();
  portalBegin();
  snifferBegin();
  if (g_cfg.homeSsid[0]) portalConnectHome();
  rgbLed(0, 0, 40);
  Serial.println("[BOOT] listening");
}

void loop() {
  snifferLoop();
  portalLoop();
  bleLoop();
  uiLoop();
  delay(5);
}
