#include "portal.h"
#include "settings.h"
#include "sniffer.h"
#include "ble_scan.h"
#include "sdstore.h"
#include "timesync.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_wifi.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <errno.h>

static WebServer server(80);
static DNSServer dns;
static IPAddress apIP(192, 168, 4, 1);
static bool disconnectHomePending = false;
static uint32_t disconnectHomeAt = 0;
static bool autoJoinHome = true;
static bool connectingHome = false;
static bool wasHomeConnected = false;
static bool portalReady = false;
static uint32_t lastHomeAttempt = 0;
static constexpr uint32_t HOME_CONNECT_TIMEOUT_MS = 20000;
static constexpr uint32_t HOME_RETRY_MS = 60000;

static bool portalFormatUtc(char* out, size_t outLen) {
  if (!out || outLen < 21) return false;
  time_t now = time(nullptr);
  if (now < 1577836800) return false;  // 2020-01-01 UTC
  struct tm utc;
  if (!gmtime_r(&now, &utc)) return false;
  return strftime(out, outLen, "%Y-%m-%dT%H:%M:%SZ", &utc) > 0;
}

static bool portalSetFromPhone(const char* epochMsText) {
  if (!epochMsText || !epochMsText[0]) return false;
  errno = 0;
  char* end = nullptr;
  unsigned long long epochMs = strtoull(epochMsText, &end, 10);
  if (errno || !end || *end != '\0') return false;
  unsigned long long seconds = epochMs / 1000ULL;
  if (seconds < 1577836800ULL || seconds > 4102444800ULL) return false;
  struct timeval tv;
  tv.tv_sec = static_cast<time_t>(seconds);
  tv.tv_usec = static_cast<suseconds_t>((epochMs % 1000ULL) * 1000ULL);
  return settimeofday(&tv, nullptr) == 0;
}

static String htmlEscape(const String& s) {
  String o;
  o.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

static const char PAGE_CSS[] = R"CSS(
body{background:#050505;color:#ddd;font-family:ui-monospace,Consolas,monospace;margin:0}
header{background:#000;border-bottom:1px solid #3a2a00;padding:14px 16px}
h1{color:#fda000;font-size:18px;margin:0}
h1 span{color:#07e0ff;font-size:12px;margin-left:8px}
nav{display:flex;gap:8px;padding:10px 12px;border-bottom:1px solid #222}
nav a{color:#07e0ff;text-decoration:none;padding:6px 8px;border:1px solid #144}
.card{margin:12px;padding:12px;border:1px solid #222;background:#0b0b0b}
label{display:block;color:#888;font-size:12px;margin:8px 0 4px}
input,select{width:100%;box-sizing:border-box;background:#111;color:#eee;border:1px solid #333;padding:8px}
button,.btn{background:#2a4000;color:#fff;border:0;padding:10px 14px;margin-top:10px}
table{width:100%;border-collapse:collapse;font-size:12px}
td,th{border-bottom:1px solid #222;padding:6px 4px;text-align:left}
.ok{color:#0f0}.bad{color:#f55}.muted{color:#888}
)CSS";

static String wrap(const String& body) {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  String s;
  s.reserve(body.length() + 900);
  s += "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>";
  s += "<title>"; s += PORTAL_TITLE; s += "</title><style>";
  s += PAGE_CSS;
  s += "</style></head><body><header><h1>CIRUSTHAVIRUS <span>TERMINAL // CYDgotchi ";
  s += CYDGOTCHI_VERSION;
  s += "</span></h1></header><nav>";
  s += "<a href='/'>status</a><a href='/wifi'>wifi</a><a href='/time'>time</a><a href='/settings'>settings</a><a href='/air'>air</a>";
  s += "</nav>";
  s += body;
  s += R"JS(<script>
async function syncPhoneClock() {
  const el = document.getElementById('clock-result');
  if (el) el.textContent = 'Synchronizing…';
  try {
    const response = await fetch('/time', {method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:'epoch_ms='+encodeURIComponent(Date.now()), cache:'no-store'});
    if (!response.ok) throw new Error('HTTP '+response.status);
    if (el) el.textContent = 'Phone time sent (UTC). Reload to check.';
  } catch (e) {
    if (el) el.textContent = 'Time transfer failed: '+e.message;
  }
}
</script>)JS";
  s += "</body></html>";
  return s;
}

static void handleRoot() {
  char body[1500];
  char utc[24];
  bool hasTime = portalFormatUtc(utc, sizeof(utc));
  snprintf(body, sizeof(body),
    "<div class=card>"
    "<div>mood <b>%s</b></div>"
    "<div>channel <b>%u</b> <b>%s</b></div>"
    "<div>APs <b>%u</b> IoT <b>%u</b> BLE <b>%u</b></div>"
    "<div>EAPOL <b>%lu</b> pwned <b>%lu</b></div>"
    "<div>SD <b class='%s'>%s</b></div>"
    "<div>portal clients <b>%u</b></div>"
    "<div>home wifi <b class='%s'>%s %s</b></div>"
    "<div>clock UTC <b class='%s'>%s</b></div>"
    "<p class=muted>SSID <b>%s</b> &nbsp; pass <b>%s</b> &nbsp; http://192.168.4.1</p>"
    "</div>",
    g_stats.status,
    g_stats.channel, g_stats.hopping ? "HOP" : "HOLD",
    g_stats.apCount, g_stats.iotCount, g_stats.bleCount,
    (unsigned long)g_stats.eapol, (unsigned long)g_stats.pwned,
    g_stats.sdOk ? "ok" : "bad", g_stats.sdOk ? "ready" : "missing",
    g_stats.portalClients,
    g_stats.staOk ? "ok" : "bad",
    g_stats.staOk ? "connected" : connectingHome ? "connecting" : "not connected",
    g_stats.staOk ? g_stats.staIp : "",
    hasTime ? "ok" : "bad", hasTime ? utc : "not synchronized",
    PORTAL_SSID, PORTAL_PASS);
  String page = body;
  if (g_cfg.homeSsid[0]) {
    page += "<div class=card><b>Home-Wi-Fi: ";
    page += g_stats.staOk ? "connected" : connectingHome ? "connecting" : "not connected";
    page += "</b>";
    if (g_stats.staOk || connectingHome) {
      page += "<form method=post action='/wifi/disconnect'><button type=submit>";
      page += connectingHome ? "Cancel connection" : "Disconnect / pause home Wi-Fi";
      page += "</button></form>";
    } else {
      page += "<form method=post action='/wifi/reconnect'>"
              "<button type=submit>Reconnect saved Wi-Fi</button></form>";
    }
    page += "</div>";
  }
  if (!hasTime) {
    page += "<div class=card>Waiting for clock sync… <span id=clock-result></span>";
    page += "<p class=muted>Open this page on your phone to send its clock, even without internet.</p></div>";
    page += "<script>window.addEventListener('load',syncPhoneClock)</script>";
  }
  server.send(200, "text/html", wrap(page));
}

static void handleTime() {
  char utc[24];
  String page = "<div class=card><h3>Clock (UTC)</h3><p>";
  page += portalFormatUtc(utc, sizeof(utc)) ? utc : "Not synchronized yet";
  page += "</p><button onclick='syncPhoneClock()'>Send phone time now</button>";
  page += "<p id=clock-result></p>";
  page += "<p class=muted>With home Wi-Fi and internet, NTP updates the clock automatically. ";
  page += "Without internet, the connected phone can set it here. "
          "Time must be set again after a power cycle.</p></div>";
  server.send(200, "text/html", wrap(page));
}

static void handleTimePost() {
  if (!portalSetFromPhone(server.arg("epoch_ms").c_str())) {
    server.send(400, "text/plain", "Invalid epoch_ms");
    return;
  }
  sdLogEvent("time synced from phone");
  server.send(200, "text/plain", "OK");
}

static void handleWifi() {
  bool homeConnected = WiFi.status() == WL_CONNECTED;
  if (connectingHome && !homeConnected) {
    // Do not scan while STA is joining/associated; APSTA scans can otherwise
    // return ESP_ERR_WIFI_STATE or an empty list on this ESP32/Arduino stack.
    connectingHome = false;
    autoJoinHome = false;
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    delay(150);
    homeConnected = false;
  }
  snifferSetPromisc(false);
  delay(50);
  int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
  if (n <= 0) {
    WiFi.scanDelete();
    delay(150);
    n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
  }
  Serial.printf("[WIFI] portal scan found %d networks\n", n);
  String b = "<div class=card><h3>join local wifi</h3>";
  if (g_cfg.homeSsid[0]) {
    b += "<p>Saved network: <b>";
    b += htmlEscape(g_cfg.homeSsid);
    b += "</b> (";
    b += homeConnected ? "connected" : connectingHome ? "connecting" : "disconnected";
    b += ")</p>";
    if (homeConnected || connectingHome) {
      b += "<form method=post action='/wifi/disconnect'><button type=submit>";
      b += connectingHome ? "Cancel connection" : "Disconnect home Wi-Fi";
      b += "</button></form>";
    } else {
      b += "<form method=post action='/wifi/reconnect'>"
           "<button type=submit>Reconnect saved Wi-Fi</button></form>";
    }
    b += "<p class=muted>Disconnect pauses automatic connection until reboot or manual reconnection; the saved password stays.</p>";
  }
  b += "<form method=post action='/wifi'>";
  b += "<label>network</label><select name=ssid>";
  for (int i = 0; i < n; i++) {
    String ss = WiFi.SSID(i);
    if (!ss.length()) continue;
    b += "<option>";
    b += htmlEscape(ss);
    b += "</option>";
  }
  if (n < 0) b += "<option value=''>Scan failed - enter SSID below</option>";
  else if (n == 0) b += "<option value=''>No networks found - enter SSID below</option>";
  b += "</select><label>or type SSID</label><input name=ssid2 placeholder='optional'>";
  if (n <= 0) b += "<p class=muted><a href='/wifi'>Scan again</a> or type your SSID.</p>";
  b += "<label>password</label><input name=pass type=password>";
  b += "<button>connect</button></form>";
  b += "<p class=muted>Joining home wifi parks channel-hop so the radio can stay associated.</p></div>";
  WiFi.scanDelete();
  snifferSetPromisc(true);
  if (!homeConnected) lastHomeAttempt = millis();  // Leave time to choose a network before retrying.
  server.send(200, "text/html", wrap(b));
}

static void handleWifiPost() {
  String ssid = server.arg("ssid2");
  if (!ssid.length()) ssid = server.arg("ssid");
  String pass = server.arg("pass");
  ssid.trim();
  strncpy(g_cfg.homeSsid, ssid.c_str(), sizeof(g_cfg.homeSsid) - 1);
  strncpy(g_cfg.homePass, pass.c_str(), sizeof(g_cfg.homePass) - 1);
  settingsSave();
  server.send(200, "text/html", wrap("<div class=card>saved. connecting… reload status in a few seconds.</div>"));
  portalConnectHome();
}

static void handleWifiDisconnect() {
  // Give the HTTP response time to reach clients using the home-Wi-Fi address.
  server.send(200, "text/html", wrap(
    "<div class=card>Disconnecting home Wi-Fi. The saved network remains available "
    "for reconnecting in the portal or after a reboot. If this page becomes unreachable, "
    "join the device hotspot at http://192.168.4.1/.</div>"));
  disconnectHomePending = true;
  disconnectHomeAt = millis();
}

static void handleWifiReconnect() {
  if (!g_cfg.homeSsid[0]) {
    server.send(400, "text/html", wrap("<div class=card>No saved home Wi-Fi.</div>"));
    return;
  }
  server.send(200, "text/html", wrap(
    "<div class=card>Reconnecting to saved home Wi-Fi. Reload status in a few seconds.</div>"));
  portalConnectHome();
}

static void handleSettings() {
  String b = "<div class=card><form method=post action='/settings'>";
  b += "<label>device name</label><input name=host value='";
  b += htmlEscape(g_cfg.hostname);
  b += "'>";
  b += "<label>channel hop</label><select name=hop>";
  b += g_cfg.hop ? "<option value=1 selected>on</option><option value=0>off</option>"
                 : "<option value=1>on</option><option value=0 selected>off</option>";
  b += "</select><label>bluetooth scan</label><select name=ble>";
  b += g_cfg.ble ? "<option value=1 selected>on</option><option value=0>off</option>"
                 : "<option value=1>on</option><option value=0 selected>off</option>";
  b += "</select><button>save</button></form></div>";
  server.send(200, "text/html", wrap(b));
}

static void handleSettingsPost() {
  strncpy(g_cfg.hostname, server.arg("host").c_str(), sizeof(g_cfg.hostname) - 1);
  g_cfg.hop = server.arg("hop") != "0";
  g_cfg.ble = server.arg("ble") != "0";
  settingsSave();
  snifferSetHopping(g_cfg.hop);
  server.send(200, "text/html", wrap("<div class=card>settings saved.</div>"));
}

static void handleAir() {
  String b = "<div class=card><h3>access points</h3><table><tr><th>ch</th><th>rssi</th><th>ssid</th><th></th></tr>";
  for (int i = 0; i < MAX_APS; i++) {
    if (!(g_aps[i].beacons || g_aps[i].ssid[0] || g_aps[i].bssid[5])) continue;
    b += "<tr><td>";
    b += String(g_aps[i].channel);
    b += "</td><td>";
    b += String(g_aps[i].rssi);
    b += "</td><td>";
    b += htmlEscape(g_aps[i].ssid[0] ? g_aps[i].ssid : "(hidden)");
    b += "</td><td>";
    if (g_aps[i].iot) b += "iot ";
    if (g_aps[i].hasHandshake) b += "pwn ";
    b += "</td></tr>";
  }
  b += "</table></div><div class=card><h3>bluetooth / iot</h3><table><tr><th>rssi</th><th>kind</th><th>name</th></tr>";
  for (int i = 0; i < MAX_BLE; i++) {
    if (!(g_ble[i].addr[0] | g_ble[i].addr[5] | g_ble[i].name[0])) continue;
    b += "<tr><td>";
    b += String(g_ble[i].rssi);
    b += "</td><td>";
    b += htmlEscape(g_ble[i].kind);
    b += "</td><td>";
    b += htmlEscape(g_ble[i].name);
    b += "</td></tr>";
  }
  b += "</table></div>";
  server.send(200, "text/html", wrap(b));
}

static void handleCaptive() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

void portalBegin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(WIFI_PS_MIN_MODEM);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(PORTAL_SSID, PORTAL_PASS, 6, 0, 4);
  portalReady = true;
  dns.start(53, "*", apIP);

  server.on("/", handleRoot);
  server.on("/wifi", HTTP_GET, handleWifi);
  server.on("/time", HTTP_GET, handleTime);
  server.on("/time", HTTP_POST, handleTimePost);
  server.on("/wifi", HTTP_POST, handleWifiPost);
  server.on("/wifi/disconnect", HTTP_POST, handleWifiDisconnect);
  server.on("/wifi/reconnect", HTTP_POST, handleWifiReconnect);
  server.on("/settings", HTTP_GET, handleSettings);
  server.on("/settings", HTTP_POST, handleSettingsPost);
  server.on("/air", handleAir);
  server.on("/generate_204", handleCaptive);
  server.on("/hotspot-detect.html", handleCaptive);
  server.on("/canonical.html", handleCaptive);
  server.on("/ncsi.txt", handleCaptive);
  server.onNotFound(handleCaptive);
  server.begin();
  g_stats.portalUp = true;
  Serial.printf("[PORTAL] SSID %s  http://192.168.4.1  pass %s\n", PORTAL_SSID, PORTAL_PASS);
}

void portalConnectHome() {
  if (!g_cfg.homeSsid[0]) return;
  disconnectHomePending = false;
  autoJoinHome = true;
  connectingHome = true;
  wasHomeConnected = false;
  lastHomeAttempt = millis();
  snifferLockHop(true);
  snifferSetPromisc(false);  // Keep scanning from competing with association.
  WiFi.setAutoReconnect(true);
  WiFi.disconnect(false, false);
  delay(50);
  WiFi.begin(g_cfg.homeSsid, g_cfg.homePass);
  strncpy(g_stats.status, "joining wifi...", sizeof(g_stats.status) - 1);
  Serial.printf("[WIFI] joining %s\n", g_cfg.homeSsid);
}

void portalDisconnectHome() {
  disconnectHomePending = false;
  autoJoinHome = false;
  connectingHome = false;
  wasHomeConnected = false;
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false);  // Keep the CYDgotchi hotspot and saved credentials.
  snifferSetPromisc(true);
  snifferLockHop(false);
  strncpy(g_stats.status, "home wifi disconnected", sizeof(g_stats.status) - 1);
  Serial.println("[WIFI] home Wi-Fi disconnected by user");
}

bool portalHomeConnected() {
  return portalReady && WiFi.status() == WL_CONNECTED;
}

bool portalHasClients() { return WiFi.softAPgetStationNum() > 0; }

void portalLoop() {
  dns.processNextRequest();
  server.handleClient();
  if (disconnectHomePending && millis() - disconnectHomeAt >= 250) {
    disconnectHomePending = false;
    autoJoinHome = false;
    connectingHome = false;
    wasHomeConnected = false;
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);  // Keep the hotspot and stored credentials.
    snifferSetPromisc(true);
    snifferLockHop(false);
    strncpy(g_stats.status, "home wifi disconnected", sizeof(g_stats.status) - 1);
  }
  g_stats.portalClients = (uint8_t)WiFi.softAPgetStationNum();
  bool sta = WiFi.status() == WL_CONNECTED;
  g_stats.staOk = sta;
  if (sta) {
    if (connectingHome) {
      snifferSetPromisc(true);
      strncpy(g_stats.status, "listening", sizeof(g_stats.status) - 1);
    }
    connectingHome = false;
    wasHomeConnected = true;
    strncpy(g_stats.staIp, WiFi.localIP().toString().c_str(), sizeof(g_stats.staIp) - 1);
    snifferLockHop(true);
  } else {
    g_stats.staIp[0] = 0;
    uint32_t now = millis();
    if (wasHomeConnected) {
      wasHomeConnected = false;
      // Retry promptly if a previously working link drops.
      lastHomeAttempt = now - HOME_RETRY_MS;
    }
    if (connectingHome && now - lastHomeAttempt >= HOME_CONNECT_TIMEOUT_MS) {
      connectingHome = false;
      uint8_t wifiStatus = WiFi.status();
      WiFi.disconnect(false, false);
      snifferSetPromisc(true);
      strncpy(g_stats.status, "home wifi retry later", sizeof(g_stats.status) - 1);
      Serial.printf("[WIFI] home connection timed out (status %u)\n", wifiStatus);
    }
    if (autoJoinHome && !connectingHome && g_cfg.homeSsid[0] &&
        now - lastHomeAttempt >= HOME_RETRY_MS) {
      portalConnectHome();
    }
    // The ESP32 has only one 2.4 GHz radio. Keep its channel fixed while a
    // phone is associated with the portal AP, otherwise the portal drops out.
    bool locked = connectingHome || g_stats.portalClients > 0;
    snifferLockHop(locked);
    static bool lastLock = false;
    if (locked != lastLock) {
      lastLock = locked;
      Serial.printf("[WIFI] channel lock=%s reason=%s%s\n",
                    locked ? "on" : "off",
                    connectingHome ? "home-join " : "",
                    g_stats.portalClients > 0 ? "portal-client" : "");
    }
  }
}
