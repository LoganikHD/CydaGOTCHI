#include "sniffer.h"
#include "sdstore.h"
#include "settings.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include <ctype.h>
#include <string.h>

Stats g_stats;
ApInfo g_aps[MAX_APS];
portMUX_TYPE g_statsMux = portMUX_INITIALIZER_UNLOCKED;

static QueueHandle_t pktQ;
static uint8_t g_ch = 1;
static bool g_hop = true;
static bool g_lockHop = false;
static uint32_t g_lastHop = 0;
static HsSlot g_slots[MAX_HS_SLOTS];

static bool macEq(const uint8_t* a, const uint8_t* b) {
  return memcmp(a, b, 6) == 0;
}

static void macCpy(uint8_t* d, const uint8_t* s) { memcpy(d, s, 6); }

ApInfo* apFind(const uint8_t bssid[6]) {
  for (int i = 0; i < MAX_APS; ++i) {
    if (g_aps[i].bssid[0] || g_aps[i].bssid[1] || g_aps[i].bssid[5]) {
      if (macEq(g_aps[i].bssid, bssid)) return &g_aps[i];
    }
  }
  return nullptr;
}

static ApInfo* apAlloc(const uint8_t bssid[6]) {
  ApInfo* e = apFind(bssid);
  if (e) return e;
  ApInfo* oldest = &g_aps[0];
  for (int i = 0; i < MAX_APS; ++i) {
    bool empty = !(g_aps[i].bssid[0] | g_aps[i].bssid[1] | g_aps[i].bssid[2] |
                   g_aps[i].bssid[3] | g_aps[i].bssid[4] | g_aps[i].bssid[5]);
    if (empty) {
      memset(&g_aps[i], 0, sizeof(ApInfo));
      macCpy(g_aps[i].bssid, bssid);
      portENTER_CRITICAL(&g_statsMux);
      g_stats.apCount++;
      portEXIT_CRITICAL(&g_statsMux);
      return &g_aps[i];
    }
    if (g_aps[i].lastSeenMs < oldest->lastSeenMs) oldest = &g_aps[i];
  }
  memset(oldest, 0, sizeof(ApInfo));
  macCpy(oldest->bssid, bssid);
  return oldest;
}

static int hdrLen(const uint8_t* p, uint16_t len) {
  if (len < 24) return -1;
  uint16_t fc = p[0] | (p[1] << 8);
  uint8_t type = (fc >> 2) & 0x3;
  uint8_t subtype = (fc >> 4) & 0xF;
  bool tods = fc & 0x0100;
  bool fromds = fc & 0x0200;
  int h = 24;
  if (tods && fromds) h += 6;
  if (type == 2 && (subtype & 0x08)) h += 2;  // QoS
  if (fc & 0x8000) h += 4;                    // HT ctrl / order
  if (h > len) return -1;
  return h;
}

static int findEapol(const uint8_t* p, uint16_t len, int h) {
  if (h < 0 || h + 8 > len) return -1;
  const uint8_t* l = p + h;
  if (l[0] == 0xAA && l[1] == 0xAA && l[2] == 0x03 && l[6] == 0x88 && l[7] == 0x8E)
    return h + 8;
  return -1;
}

// Returns 1..4 for EAPOL-Key messages, 0 otherwise.
static int eapolMsg(const uint8_t* eapol, int elen, bool* pmkid) {
  *pmkid = false;
  if (elen < 7) return 0;
  if (eapol[1] != 0x03) return 0;  // EAPOL-Key
  uint16_t ki = (eapol[5] << 8) | eapol[6];
  bool pairwise = ki & (1 << 3);
  bool install = ki & (1 << 6);
  bool ack = ki & (1 << 7);
  bool mic = ki & (1 << 8);
  bool secure = ki & (1 << 9);
  if (!pairwise) return 0;

  // Key data starts at offset 99 from 802.1X version byte for standard RSN
  if (elen >= 101) {
    uint16_t kdl = (eapol[97] << 8) | eapol[98];
    if (kdl >= 20 && !mic) *pmkid = true;
  }

  if (ack && !mic && !install && !secure) return 1;
  if (!ack && mic && !install && !secure) return 2;
  if (ack && mic && install && secure) return 3;
  if (!ack && mic && !install && secure) return 4;
  if (mic) return 2;
  return 1;
}

static HsSlot* slotGet(const uint8_t* bssid, const uint8_t* sta) {
  HsSlot* free = nullptr;
  HsSlot* oldest = &g_slots[0];
  for (int i = 0; i < MAX_HS_SLOTS; ++i) {
    if (g_slots[i].used && macEq(g_slots[i].bssid, bssid) && macEq(g_slots[i].sta, sta))
      return &g_slots[i];
    if (!g_slots[i].used && !free) free = &g_slots[i];
    if (g_slots[i].lastMs < oldest->lastMs) oldest = &g_slots[i];
  }
  HsSlot* s = free ? free : oldest;
  memset(s, 0, sizeof(HsSlot));
  s->used = true;
  macCpy(s->bssid, bssid);
  macCpy(s->sta, sta);
  return s;
}

static void slotStore(HsSlot* s, const uint8_t* frame, uint16_t len) {
  if (s->frames >= 6) return;
  uint16_t n = len > PKT_MAX_LEN ? PKT_MAX_LEN : len;
  memcpy(s->frame[s->frames], frame, n);
  s->flen[s->frames] = n;
  s->frames++;
}

static bool ssidLooksIot(const char* s) {
  if (!s || !s[0]) return false;
  char n[SSID_MAX + 1];
  int i = 0;
  for (; s[i] && i < SSID_MAX; i++) n[i] = (char)tolower((unsigned char)s[i]);
  n[i] = 0;
  static const char* keys[] = {
    "esp_", "esp32", "esp8266", "shelly", "tasmota", "wled", "hue", "ikea",
    "direct-", "hp-print", "canon", "sonos", "roku", "nest", "ring", "echo",
    "firetv", "xiaomi", "miwifi", "iot", "camera", "cam-", "smartplug",
    "tplink", "tuya", "wyze", "arlo", "blink", "govee", "kasa", 0
  };
  for (int k = 0; keys[k]; k++) if (strstr(n, keys[k])) return true;
  return false;
}

static void parseBeacon(const uint8_t* p, uint16_t len, int8_t rssi, uint8_t ch) {
  if (len < 36) return;
  const uint8_t* bssid = p + 16;
  ApInfo* ap = apAlloc(bssid);
  ap->rssi = rssi;
  ap->channel = ch;
  ap->beacons++;
  ap->lastSeenMs = millis();

  // tagged params start at 36
  int i = 36;
  while (i + 2 <= len) {
    uint8_t tag = p[i];
    uint8_t tlen = p[i + 1];
    if (i + 2 + tlen > len) break;
    if (tag == 0 && tlen <= SSID_MAX) {
      memcpy(ap->ssid, p + i + 2, tlen);
      ap->ssid[tlen] = 0;
      bool was = ap->iot;
      ap->iot = ssidLooksIot(ap->ssid);
      if (ap->iot && !was) {
        portENTER_CRITICAL(&g_statsMux);
        g_stats.iotCount++;
        portEXIT_CRITICAL(&g_statsMux);
      }
    } else if (tag == 48) {
      ap->crypto |= 0x02;  // RSN / WPA2
    } else if (tag == 221 && tlen >= 6 && p[i + 2] == 0x00 && p[i + 3] == 0x50 && p[i + 4] == 0xf2 && p[i + 5] == 0x01) {
      ap->crypto |= 0x01;  // WPA
    }
    i += 2 + tlen;
  }
}

static void handleEapol(const uint8_t* p, uint16_t len, int8_t rssi, uint8_t ch, int eoff) {
  uint16_t fc = p[0] | (p[1] << 8);
  bool tods = fc & 0x0100;
  bool fromds = fc & 0x0200;
  const uint8_t *a1 = p + 4, *a2 = p + 10, *a3 = p + 16;
  const uint8_t *bssid, *sta;
  if (fromds && !tods) { bssid = a2; sta = a1; }
  else if (tods && !fromds) { bssid = a1; sta = a2; }
  else { bssid = a3; sta = a2; }

  bool pmkid = false;
  int msg = eapolMsg(p + eoff, len - eoff, &pmkid);
  if (msg == 0) return;

  ApInfo* ap = apAlloc(bssid);
  ap->rssi = rssi;
  ap->channel = ch;
  ap->eapols++;
  ap->lastSeenMs = millis();

  HsSlot* s = slotGet(bssid, sta);
  s->lastMs = millis();
  if (pmkid) s->pmkid = true;
  if (msg >= 1 && msg <= 4) s->msgs |= (1 << (msg - 1));
  slotStore(s, p, len);

  portENTER_CRITICAL(&g_statsMux);
  g_stats.eapol++;
  if (pmkid) g_stats.pmkid++;
  strncpy(g_stats.lastSsid, ap->ssid[0] ? ap->ssid : "hidden", SSID_MAX);
  portEXIT_CRITICAL(&g_statsMux);

  bool complete = (s->msgs & 0x03) == 0x03;  // M1+M2
  if (complete && !ap->hasHandshake) {
    ap->hasHandshake = true;
    if (sdSaveHandshake(*s, ap->ssid, false)) {
      portENTER_CRITICAL(&g_statsMux);
      g_stats.pwned++;
      snprintf(g_stats.status, sizeof(g_stats.status), "got %s", ap->ssid[0] ? ap->ssid : "hidden");
      portEXIT_CRITICAL(&g_statsMux);
    }
  }
  if (pmkid && !ap->hasPmkid) {
    ap->hasPmkid = true;
    sdSaveHandshake(*s, ap->ssid, true);
    portENTER_CRITICAL(&g_statsMux);
    snprintf(g_stats.status, sizeof(g_stats.status), "pmkid %s", ap->ssid[0] ? ap->ssid : "hidden");
    portEXIT_CRITICAL(&g_statsMux);
  }
}

static void IRAM_ATTR snifferCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;
  auto* rec = (wifi_promiscuous_pkt_t*)buf;
  uint32_t slen = rec->rx_ctrl.sig_len;
  if (slen > 4) slen -= 4;  // FCS
  if (slen < 24 || slen > PKT_MAX_LEN) {
    g_stats.dropped++;
    return;
  }
  PktEvt ev;
  ev.kind = (type == WIFI_PKT_MGMT) ? EvtKind::Beacon : EvtKind::Eapol;
  ev.rssi = rec->rx_ctrl.rssi;
  ev.channel = rec->rx_ctrl.channel;
  ev.len = (uint16_t)slen;
  memcpy(ev.data, rec->payload, slen);
  BaseType_t woken = pdFALSE;
  if (xQueueSendFromISR(pktQ, &ev, &woken) != pdTRUE) g_stats.dropped++;
  if (woken) portYIELD_FROM_ISR();
}

static void processEvt(const PktEvt& ev) {
  portENTER_CRITICAL(&g_statsMux);
  g_stats.packets++;
  portEXIT_CRITICAL(&g_statsMux);

  const uint8_t* p = ev.data;
  uint16_t fc = p[0] | (p[1] << 8);
  uint8_t ftype = (fc >> 2) & 0x3;
  uint8_t subtype = (fc >> 4) & 0xF;
  int h = hdrLen(p, ev.len);

  if (ftype == 0 && (subtype == 8 || subtype == 5)) {
    portENTER_CRITICAL(&g_statsMux);
    g_stats.beacons++;
    portEXIT_CRITICAL(&g_statsMux);
    parseBeacon(p, ev.len, ev.rssi, ev.channel);
    return;
  }
  if (ftype == 2) {
    int eoff = findEapol(p, ev.len, h);
    if (eoff >= 0) handleEapol(p, ev.len, ev.rssi, ev.channel, eoff);
  }
}

void snifferBegin() {
  memset(&g_stats, 0, sizeof(g_stats));
  memset(g_aps, 0, sizeof(g_aps));
  memset(g_slots, 0, sizeof(g_slots));
  g_stats.hopping = true;
  g_stats.channel = 1;
  g_stats.mood = Mood::Idle;
  strncpy(g_stats.status, "waking up...", sizeof(g_stats.status) - 1);

  pktQ = xQueueCreate(PKT_QUEUE_LEN, sizeof(PktEvt));
  g_hop = g_cfg.hop;
  g_stats.hopping = g_hop;
  // ESP32 aborts if WiFi+BT run with WIFI_PS_NONE
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  wifi_promiscuous_filter_t filt = {};
  filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
  esp_wifi_set_promiscuous_filter(&filt);
  esp_wifi_set_promiscuous_rx_cb(&snifferCb);
  esp_wifi_set_promiscuous(true);
  g_ch = 1;
  esp_wifi_set_channel(g_ch, WIFI_SECOND_CHAN_NONE);
  Serial.println("[WIFI] promiscuous on");
}

void snifferSetHopping(bool on) {
  g_hop = on;
  g_cfg.hop = on;
  g_stats.hopping = on;
}

void snifferLockHop(bool on) {
  g_lockHop = on;
  g_stats.hopping = on ? false : g_hop;
}

void snifferSetPromisc(bool on) {
  esp_wifi_set_promiscuous(on);
}

bool snifferHopping() { return g_hop; }
uint8_t snifferChannel() { return g_ch; }

void snifferNextChannel() {
  g_ch++;
  if (g_ch > CHANNEL_MAX) g_ch = CHANNEL_MIN;
  esp_wifi_set_channel(g_ch, WIFI_SECOND_CHAN_NONE);
  g_stats.channel = g_ch;
}

void snifferLoop() {
  PktEvt ev;
  while (xQueueReceive(pktQ, &ev, 0) == pdTRUE) processEvt(ev);

  uint32_t now = millis();
  if (g_hop && !g_lockHop && now - g_lastHop >= HOP_DWELL_MS) {
    g_lastHop = now;
    snifferNextChannel();
  }
  for (int i = 0; i < MAX_HS_SLOTS; ++i) {
    if (g_slots[i].used && now - g_slots[i].lastMs > HS_SLOT_TIMEOUT_MS) {
      if ((g_slots[i].msgs & 0x03) != 0x03) g_slots[i].used = false;
    }
  }
}
