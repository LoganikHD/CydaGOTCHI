#include "portal.h"
#include "settings.h"
#include "sniffer.h"
#include "ble_scan.h"
#include "sdstore.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_wifi.h>

static WebServer server(80);
static DNSServer dns;
static IPAddress apIP(192, 168, 4, 1);

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
  String s;
  s.reserve(body.length() + 900);
  s += "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>";
  s += "<title>"; s += PORTAL_TITLE; s += "</title><style>";
  s += PAGE_CSS;
  s += "</style></head><body><header><h1>CIRUSTHAVIRUS <span>TERMINAL // CYDgotchi ";
  s += CYDGOTCHI_VERSION;
  s += "</span></h1></header><nav>";
  s += "<a href='/'>status</a><a href='/wifi'>wifi</a><a href='/settings'>settings</a><a href='/air'>air</a>";
  s += "</nav>";
  s += body;
  s += "</body></html>";
  return s;
}

static void handleRoot() {
  char body[1200];
  snprintf(body, sizeof(body),
    "<div class=card>"
    "<div>mood <b>%s</b></div>"
    "<div>channel <b>%u</b> hop <b>%s</b></div>"
    "<div>APs <b>%u</b> IoT <b>%u</b> BLE <b>%u</b></div>"
    "<div>EAPOL <b>%lu</b> pwned <b>%lu</b></div>"
    "<div>SD <b class='%s'>%s</b></div>"
    "<div>portal clients <b>%u</b></div>"
    "<div>home wifi <b class='%s'>%s %s</b></div>"
    "<p class=muted>SSID <b>%s</b> &nbsp; pass <b>%s</b> &nbsp; http://192.168.4.1</p>"
    "</div>",
    g_stats.status,
    g_stats.channel, g_cfg.hop ? "on" : "off",
    g_stats.apCount, g_stats.iotCount, g_stats.bleCount,
    (unsigned long)g_stats.eapol, (unsigned long)g_stats.pwned,
    g_stats.sdOk ? "ok" : "bad", g_stats.sdOk ? "ready" : "missing",
    g_stats.portalClients,
    g_stats.staOk ? "ok" : "bad",
    g_stats.staOk ? "connected" : "not connected",
    g_stats.staOk ? g_stats.staIp : "",
    PORTAL_SSID, PORTAL_PASS);
  server.send(200, "text/html", wrap(body));
}

static void handleWifi() {
  snifferSetPromisc(false);
  delay(50);
  int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
  String b = "<div class=card><h3>join local wifi</h3>";
  b += "<form method=post action='/wifi'>";
  b += "<label>network</label><select name=ssid>";
  for (int i = 0; i < n; i++) {
    String ss = WiFi.SSID(i);
    if (!ss.length()) continue;
    b += "<option>";
    b += htmlEscape(ss);
    b += "</option>";
  }
  b += "</select><label>or type SSID</label><input name=ssid2 placeholder='optional'>";
  b += "<label>password</label><input name=pass type=password>";
  b += "<button>connect</button></form>";
  b += "<p class=muted>Joining home wifi parks channel-hop so the radio can stay associated.</p></div>";
  WiFi.scanDelete();
  snifferSetPromisc(true);
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
  dns.start(53, "*", apIP);

  server.on("/", handleRoot);
  server.on("/wifi", HTTP_GET, handleWifi);
  server.on("/wifi", HTTP_POST, handleWifiPost);
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
  snifferLockHop(true);
  WiFi.disconnect(false, false);
  delay(50);
  WiFi.begin(g_cfg.homeSsid, g_cfg.homePass);
  strncpy(g_stats.status, "joining wifi...", sizeof(g_stats.status) - 1);
  Serial.printf("[WIFI] joining %s\n", g_cfg.homeSsid);
}

bool portalHasClients() { return WiFi.softAPgetStationNum() > 0; }

void portalLoop() {
  dns.processNextRequest();
  server.handleClient();
  g_stats.portalClients = (uint8_t)WiFi.softAPgetStationNum();
  bool sta = WiFi.status() == WL_CONNECTED;
  g_stats.staOk = sta;
  if (sta) {
    strncpy(g_stats.staIp, WiFi.localIP().toString().c_str(), sizeof(g_stats.staIp) - 1);
    snifferLockHop(true);
  } else {
    g_stats.staIp[0] = 0;
    if (g_stats.portalClients == 0 && g_cfg.hop) snifferLockHop(false);
  }
  if (g_stats.portalClients > 0) snifferLockHop(true);
}
