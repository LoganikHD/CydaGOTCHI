#pragma once

#include "config.h"

enum class Mood : uint8_t {
  Idle,
  Scanning,
  Intense,
  Happy,
  Excited,
  Bored,
  Sleepy,
  Sad
};

enum class EvtKind : uint8_t {
  Beacon = 1,
  Eapol = 2
};

struct ApInfo {
  uint8_t bssid[6];
  char ssid[SSID_MAX + 1];
  int8_t rssi;
  uint8_t channel;
  uint8_t crypto;   // bit0=WPA bit1=WPA2 bit2=WEP bit3=open-looking
  uint16_t beacons;
  uint16_t eapols;
  bool hasHandshake;
  bool hasPmkid;
  bool iot;
  uint32_t lastSeenMs;
};

struct BleDev {
  uint8_t addr[6];
  char name[20];
  char kind[12];
  int8_t rssi;
  uint32_t lastSeenMs;
};

struct Settings {
  char homeSsid[33];
  char homePass[65];
  char hostname[17];
  bool hop;
  bool ble;
  uint8_t brightness;
};

struct HsSlot {
  bool used;
  uint8_t bssid[6];
  uint8_t sta[6];
  uint8_t msgs;     // bit0=M1 bit1=M2 bit2=M3 bit3=M4
  bool pmkid;
  uint32_t lastMs;
  uint8_t frames;
  uint16_t flen[6];
  uint8_t frame[6][PKT_MAX_LEN];
};

struct PktEvt {
  EvtKind kind;
  int8_t rssi;
  uint8_t channel;
  uint16_t len;
  uint8_t data[PKT_MAX_LEN];
};

struct Stats {
  uint32_t packets;
  uint32_t beacons;
  uint32_t eapol;
  uint32_t pmkid;
  uint32_t pwned;
  uint32_t dropped;
  uint16_t apCount;
  uint8_t channel;
  bool hopping;
  bool sdOk;
  uint32_t sdFreeMb;
  Mood mood;
  char status[48];
  char lastSsid[SSID_MAX + 1];
  uint16_t bleCount;
  uint16_t iotCount;
  uint8_t portalClients;
  bool staOk;
  bool portalUp;
  char staIp[16];
};
