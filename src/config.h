#pragma once

#include <Arduino.h>

#define CYDGOTCHI_NAME     "cydgotchi"
#define CYDGOTCHI_VERSION  "0.3.1"
#define STEALTH_MODE       1

#ifndef CYD_BL_PIN
#define CYD_BL_PIN 21
#endif
#if CYD_BL_PIN == 21
#define CYD_BL_PIN_ALT     27
#else
#define CYD_BL_PIN_ALT     21
#endif

#define PIN_BOOT           0
#define PIN_LED_R          4
#define PIN_LED_G          16
#define PIN_LED_B          17

#define PIN_SD_SCK         18
#define PIN_SD_MISO        19
#define PIN_SD_MOSI        23
#define PIN_SD_CS          5

#define PIN_TOUCH_CLK      25
#define PIN_TOUCH_MOSI     32
#define PIN_TOUCH_MISO     39
#define PIN_TOUCH_CS       33
#define PIN_TOUCH_IRQ      36

#define SCREEN_W           240
#define SCREEN_H           320

#define HOP_DWELL_MS       280
#define CHANNEL_MIN        1
#define CHANNEL_MAX        13

#define PORTAL_SSID        "CIRUSTHAVIRUS"
#define PORTAL_PASS        "cydgotchi"
#define PORTAL_TITLE       "CIRUSTHAVIRUS TERMINAL"

#define MAX_APS            48
#define MAX_BLE            24
#define MAX_HS_SLOTS       8
#define PKT_QUEUE_LEN      24
#define PKT_MAX_LEN        384
#define SSID_MAX           32

#define HS_SLOT_TIMEOUT_MS 20000
#define MIN_SD_FREE_BYTES  (256 * 1024)

#define SD_ROOT            "/pwnagotchi"
#define SD_HANDSHAKES      "/pwnagotchi/handshakes"
#define SD_PMKID           "/pwnagotchi/pmkid"
#define SD_LOGS            "/pwnagotchi/logs"

#define TOUCH_Z_MIN        200
#define UI_FPS_MS          800
