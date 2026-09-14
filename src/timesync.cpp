#include "timesync.h"
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

static bool g_started = false;
static bool g_valid = false;
static uint32_t g_lastAttemptMs = 0;
static uint32_t g_lastReportMs = 0;

static const char* TZ_DE = "CET-1CEST,M3.5.0/2,M10.5.0/3";
static const char* NTP1 = "pool.ntp.org";
static const char* NTP2 = "time.cloudflare.com";
static const char* NTP3 = "time.nist.gov";

static bool clockLooksValid() {
  time_t now = time(nullptr);
  return now >= 1577836800; // 2020-01-01 UTC
}

static void startNtp() {
  if (WiFi.status() != WL_CONNECTED) return;

  IPAddress dns1 = WiFi.dnsIP(0);
  IPAddress dns2 = WiFi.dnsIP(1);
  Serial.printf("[TIME] WiFi up  IP=%s  DNS1=%s  DNS2=%s\n",
                WiFi.localIP().toString().c_str(),
                dns1.toString().c_str(),
                dns2.toString().c_str());

  // DHCP supplies DNS. Do not override it with a fixed address.
  configTzTime(TZ_DE, NTP1, NTP2, NTP3);
  g_started = true;
  g_lastAttemptMs = millis();
  Serial.printf("[TIME] NTP started: %s, %s, %s\n", NTP1, NTP2, NTP3);
}

void timeSyncBegin() {
  g_started = false;
  g_valid = clockLooksValid();
  g_lastAttemptMs = 0;
  g_lastReportMs = 0;
}

void timeSyncLoop() {
  if (WiFi.status() != WL_CONNECTED) {
    g_started = false;
    g_valid = clockLooksValid();
    return;
  }

  if (!g_started) {
    startNtp();
    return;
  }

  if (clockLooksValid()) {
    if (!g_valid) {
      g_valid = true;
      time_t now = time(nullptr);
      struct tm t;
      localtime_r(&now, &t);
      char buf[32];
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &t);
      Serial.printf("[TIME] NTP synced: %s\n", buf);
    }
    return;
  }

  g_valid = false;
  uint32_t nowMs = millis();

  // Re-arm SNTP if no valid clock arrives within 30 seconds.
  if ((uint32_t)(nowMs - g_lastAttemptMs) >= 30000) {
    Serial.println("[TIME] NTP timeout, retrying");
    startNtp();
  } else if ((uint32_t)(nowMs - g_lastReportMs) >= 5000) {
    g_lastReportMs = nowMs;
    Serial.println("[TIME] waiting for NTP...");
  }
}

bool timeSyncValid() {
  if (!g_valid && clockLooksValid()) g_valid = true;
  return g_valid;
}
