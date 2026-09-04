#include "sdstore.h"
#include <SD.h>
#include <SPI.h>

static SPIClass sdSpi(VSPI);
static bool g_sd = false;
static uint32_t g_freeMb = 0;

static void pcapGlobalHeader(File& f) {
  uint8_t hdr[24] = {
    0xd4, 0xc3, 0xb2, 0xa1,  // magic
    0x02, 0x00, 0x04, 0x00,  // v2.4
    0, 0, 0, 0,              // thiszone
    0, 0, 0, 0,              // sigfigs
    0xff, 0xff, 0x00, 0x00,  // snaplen 65535
    0x69, 0x00, 0x00, 0x00   // LINKTYPE_IEEE802_11
  };
  f.write(hdr, sizeof(hdr));
}

static void pcapPacket(File& f, const uint8_t* data, uint16_t len) {
  uint32_t ts = millis();
  uint32_t sec = ts / 1000;
  uint32_t usec = (ts % 1000) * 1000;
  uint32_t n = len;
  uint8_t ph[16];
  memcpy(ph + 0, &sec, 4);
  memcpy(ph + 4, &usec, 4);
  memcpy(ph + 8, &n, 4);
  memcpy(ph + 12, &n, 4);
  f.write(ph, 16);
  f.write(data, len);
}

static void macToName(char* out, size_t n, const uint8_t mac[6], const char* ssid) {
  char safe[SSID_MAX + 1];
  int j = 0;
  if (ssid) {
    for (int i = 0; ssid[i] && j < SSID_MAX; ++i) {
      char c = ssid[i];
      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
        safe[j++] = c;
      else
        safe[j++] = '_';
    }
  }
  safe[j] = 0;
  if (j == 0) strcpy(safe, "hidden");
  snprintf(out, n, "%02X%02X%02X%02X%02X%02X_%s.pcap",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], safe);
}

bool sdBegin() {
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  sdSpi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);

  g_sd = false;
  if (!SD.begin(PIN_SD_CS, sdSpi, 10000000)) {
    Serial.println("[SD] mount failed");
    return false;
  }

  uint8_t card = SD.cardType();
  if (card == CARD_NONE) {
    Serial.println("[SD] no card");
    return false;
  }

  uint64_t used = SD.usedBytes();
  uint64_t total = SD.totalBytes();
  g_freeMb = (uint32_t)((total > used ? (total - used) : 0) / (1024 * 1024));

  SD.mkdir(SD_ROOT);
  SD.mkdir(SD_HANDSHAKES);
  SD.mkdir(SD_PMKID);
  SD.mkdir(SD_LOGS);

  g_sd = true;
  Serial.printf("[SD] ok  %u MB free  type=%u\n", g_freeMb, card);
  sdWriteReadme();
  sdLogEvent("boot");
  return true;
}

bool sdOk() { return g_sd; }
uint32_t sdFreeMb() { return g_freeMb; }

void sdWriteReadme() {
  if (!g_sd) return;
  const char* path = SD_ROOT "/README.txt";
  if (SD.exists(path)) return;
  File f = SD.open(path, FILE_WRITE);
  if (!f) return;
  f.println("CYDgotchi / CIRUSTHAVIRUS SD layout");
  f.println("handshakes/  WPA EAPOL captures (libpcap)");
  f.println("pmkid/       PMKID frames");
  f.println("logs/        aps.csv, ble.csv, events.log, stats.txt");
  f.println("Portal: join SSID CIRUSTHAVIRUS  pass cydgotchi  http://192.168.4.1");
  f.println();
  f.println("Format the card FAT32. Captures are written while the device runs.");
  f.close();
}

void sdLogEvent(const char* msg) {
  if (!g_sd || !msg) return;
  File f = SD.open(SD_LOGS "/events.log", FILE_APPEND);
  if (!f) return;
  f.printf("%lu %s\n", (unsigned long)millis(), msg);
  f.close();
}

void sdLogAp(const ApInfo& ap) {
  if (!g_sd) return;
  const char* path = SD_LOGS "/aps.csv";
  bool neu = !SD.exists(path);
  File f = SD.open(path, FILE_APPEND);
  if (!f) return;
  if (neu) f.println("uptime_ms,bssid,ssid,channel,rssi,crypto,handshake,pmkid");
  f.printf("%lu,%02X:%02X:%02X:%02X:%02X:%02X,%s,%u,%d,%u,%u,%u\n",
           (unsigned long)millis(),
           ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5],
           ap.ssid[0] ? ap.ssid : "hidden",
           ap.channel, ap.rssi, ap.crypto,
           ap.hasHandshake ? 1 : 0, ap.hasPmkid ? 1 : 0);
  f.close();
}

bool sdSaveHandshake(const HsSlot& slot, const char* ssid, bool pmkid) {
  if (!g_sd) return false;
  if (SD.totalBytes() > 0) {
    uint64_t used = SD.usedBytes();
    uint64_t total = SD.totalBytes();
    g_freeMb = (uint32_t)((total > used ? (total - used) : 0) / (1024 * 1024));
    if ((total - used) < MIN_SD_FREE_BYTES) {
      Serial.println("[SD] low space, skip write");
      return false;
    }
  }

  char name[96];
  macToName(name, sizeof(name), slot.bssid, ssid);
  char path[128];
  snprintf(path, sizeof(path), "%s/%s", pmkid ? SD_PMKID : SD_HANDSHAKES, name);

  bool exists = SD.exists(path);
  File f = SD.open(path, exists ? FILE_APPEND : FILE_WRITE);
  if (!f) {
    Serial.printf("[SD] open fail %s\n", path);
    return false;
  }
  if (!exists) pcapGlobalHeader(f);
  for (uint8_t i = 0; i < slot.frames && i < 6; ++i) {
    if (slot.flen[i] > 0) pcapPacket(f, slot.frame[i], slot.flen[i]);
  }
  f.close();
  Serial.printf("[SD] wrote %s (%u frames)\n", path, slot.frames);

  char ev[96];
  snprintf(ev, sizeof(ev), "saved %s", name);
  sdLogEvent(ev);
  return true;
}

void sdSaveStats(const Stats& st) {
  if (!g_sd) return;
  File f = SD.open(SD_LOGS "/stats.txt", FILE_WRITE);
  if (!f) return;
  f.printf("version=%s\n", CYDGOTCHI_VERSION);
  f.printf("uptime_ms=%lu\n", (unsigned long)millis());
  f.printf("aps=%u\n", st.apCount);
  f.printf("packets=%lu\n", (unsigned long)st.packets);
  f.printf("beacons=%lu\n", (unsigned long)st.beacons);
  f.printf("eapol=%lu\n", (unsigned long)st.eapol);
  f.printf("pmkid=%lu\n", (unsigned long)st.pmkid);
  f.printf("pwned=%lu\n", (unsigned long)st.pwned);
  f.printf("sd_free_mb=%lu\n", (unsigned long)st.sdFreeMb);
  f.close();
}
