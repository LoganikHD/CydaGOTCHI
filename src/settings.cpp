#include "settings.h"
#include <Preferences.h>
#include <string.h>

Settings g_cfg;

void settingsLoad() {
  memset(&g_cfg, 0, sizeof(g_cfg));
  g_cfg.hop = true;
  g_cfg.ble = true;
  g_cfg.brightness = 255;
  strncpy(g_cfg.hostname, "cydgotchi", sizeof(g_cfg.hostname) - 1);

  Preferences p;
  if (!p.begin("cydg", true)) return;
  p.getString("ssid", g_cfg.homeSsid, sizeof(g_cfg.homeSsid));
  p.getString("pass", g_cfg.homePass, sizeof(g_cfg.homePass));
  p.getString("host", g_cfg.hostname, sizeof(g_cfg.hostname));
  g_cfg.hop = p.getBool("hop", true);
  g_cfg.ble = p.getBool("ble", true);
  g_cfg.brightness = p.getUChar("br", 255);
  if (!g_cfg.hostname[0]) strncpy(g_cfg.hostname, "cydgotchi", sizeof(g_cfg.hostname) - 1);
  p.end();
}

void settingsSave() {
  Preferences p;
  if (!p.begin("cydg", false)) return;
  p.putString("ssid", g_cfg.homeSsid);
  p.putString("pass", g_cfg.homePass);
  p.putString("host", g_cfg.hostname);
  p.putBool("hop", g_cfg.hop);
  p.putBool("ble", g_cfg.ble);
  p.putUChar("br", g_cfg.brightness);
  p.end();
}

void settingsBegin() { settingsLoad(); }
