#include "ble_scan.h"
#include "sniffer.h"
#include "settings.h"
#include "sdstore.h"
#include <NimBLEDevice.h>
#include "esp_bt.h"
#include <string.h>
#include <ctype.h>

BleDev g_ble[MAX_BLE];

static bool g_inited = false;
static bool g_paused = false;
static uint32_t g_lastScan = 0;
static bool g_scanning = false;

static bool macEq(const uint8_t* a, const uint8_t* b) { return memcmp(a, b, 6) == 0; }

static void lower(char* s) {
  for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

static void classify(NimBLEAdvertisedDevice* d, char* kind, size_t kn, char* name, size_t nn) {
  strncpy(kind, "ble", kn - 1);
  name[0] = 0;
  if (d->haveName()) {
    strncpy(name, d->getName().c_str(), nn - 1);
  }

  char nlow[24];
  strncpy(nlow, name, sizeof(nlow) - 1);
  nlow[sizeof(nlow) - 1] = 0;
  lower(nlow);

  uint16_t mfg = 0;
  if (d->haveManufacturerData()) {
    std::string md = d->getManufacturerData();
    if (md.size() >= 2) mfg = (uint8_t)md[0] | ((uint8_t)md[1] << 8);
  }

  if (mfg == 0x004C) strncpy(kind, "apple", kn - 1);
  else if (mfg == 0x0006) strncpy(kind, "msft", kn - 1);
  else if (mfg == 0x0075 || mfg == 0x038F || mfg == 0x0157) strncpy(kind, "iot", kn - 1);
  else if (mfg == 0x0059) strncpy(kind, "nordic", kn - 1);

  if (strstr(nlow, "tile") || strstr(nlow, "airtag") || strstr(nlow, "smarttag"))
    strncpy(kind, "tag", kn - 1);
  else if (strstr(nlow, "band") || strstr(nlow, "watch") || strstr(nlow, "mi ") || strstr(nlow, "fitbit"))
    strncpy(kind, "wear", kn - 1);
  else if (strstr(nlow, "jbl") || strstr(nlow, "bose") || strstr(nlow, "airpod") || strstr(nlow, "head") || strstr(nlow, "speaker"))
    strncpy(kind, "audio", kn - 1);
  else if (strstr(nlow, "esp") || strstr(nlow, "shelly") || strstr(nlow, "tasmota") || strstr(nlow, "wled") ||
           strstr(nlow, "hue") || strstr(nlow, "plug") || strstr(nlow, "bulb") || strstr(nlow, "switch") ||
           strstr(nlow, "sensor") || strstr(nlow, "thermo"))
    strncpy(kind, "iot", kn - 1);
  else if (strstr(nlow, "iphone") || strstr(nlow, "galaxy") || strstr(nlow, "pixel") || strstr(nlow, "android"))
    strncpy(kind, "phone", kn - 1);

  if (d->isAdvertisingService(NimBLEUUID((uint16_t)0x181A)) ||
      d->isAdvertisingService(NimBLEUUID((uint16_t)0x181C)) ||
      d->isAdvertisingService(NimBLEUUID((uint16_t)0xFE95)))
    strncpy(kind, "iot", kn - 1);

  if (!name[0]) snprintf(name, nn, "%s", kind);
}

static BleDev* allocDev(const uint8_t* addr) {
  for (int i = 0; i < MAX_BLE; i++) {
    if (macEq(g_ble[i].addr, addr)) return &g_ble[i];
  }
  BleDev* oldest = &g_ble[0];
  for (int i = 0; i < MAX_BLE; i++) {
    bool empty = !(g_ble[i].addr[0] | g_ble[i].addr[1] | g_ble[i].addr[5]);
    if (empty) {
      memset(&g_ble[i], 0, sizeof(BleDev));
      memcpy(g_ble[i].addr, addr, 6);
      portENTER_CRITICAL(&g_statsMux);
      g_stats.bleCount++;
      portEXIT_CRITICAL(&g_statsMux);
      return &g_ble[i];
    }
    if (g_ble[i].lastSeenMs < oldest->lastSeenMs) oldest = &g_ble[i];
  }
  memset(oldest, 0, sizeof(BleDev));
  memcpy(oldest->addr, addr, 6);
  return oldest;
}

class AdvCb : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* d) override {
    NimBLEAddress a = d->getAddress();
    uint8_t addr[6];
    memcpy(addr, a.getNative(), 6);
    BleDev* e = allocDev(addr);
    e->rssi = d->getRSSI();
    e->lastSeenMs = millis();
    classify(d, e->kind, sizeof(e->kind), e->name, sizeof(e->name));
  }
};

static AdvCb g_cb;

void bleBegin() {
  memset(g_ble, 0, sizeof(g_ble));
  if (!g_cfg.ble) return;
  esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_P3);
  NimBLEScan* sc = NimBLEDevice::getScan();
  sc->setAdvertisedDeviceCallbacks(&g_cb, false);
  sc->setActiveScan(true);
  sc->setInterval(134);
  sc->setWindow(80);
  g_inited = true;
  Serial.println("[BLE] scanner ready");
}

void blePause(bool on) { g_paused = on; }

void bleLoop() {
  if (!g_inited || !g_cfg.ble || g_paused) return;
  if (g_stats.portalClients || g_stats.staOk) return;  // radio busy with users
  uint32_t now = millis();
  if (g_scanning) {
    if (!NimBLEDevice::getScan()->isScanning()) {
      g_scanning = false;
      snifferSetPromisc(true);
      g_lastScan = now;
    }
    return;
  }
  if (now - g_lastScan < 14000) return;
  snifferLockHop(true);
  snifferSetPromisc(false);
  NimBLEDevice::getScan()->start(2, nullptr, false);
  g_scanning = true;
  g_lastScan = now;
}
