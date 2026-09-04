# CYDgotchi — CIRUSTHAVIRUS

Pwnagotchi-style firmware for the ESP32 Cheap Yellow Display (ESP32-2432S028).
Pwnagotchi face, BLE/IoT awareness, CIRUSTHAVIRUS web terminal, loot on microSD.

**Use only on networks you own or have written permission to test.**

## What it does

- Channel-hops 1–13 in promiscuous mode
- Parses beacons into a live AP table
- Captures WPA EAPOL (M1+M2) and PMKID frames
- Writes standard libpcap files to the SD card
- Pwnagotchi face + mood
- BLE / IoT scan (time-sliced with Wi-Fi)
- SoftAP **CIRUSTHAVIRUS** (pass `cydgotchi`) → http://192.168.4.1
- Touch: channel, hop, FACE/APS/BLE

This build is **receive-only**. It does not inject deauth or other attack frames.

## Hardware

ESP32-2432S028 (CYD), FAT32 microSD in the onboard slot.

| Function | GPIO |
| --- | --- |
| TFT HSPI | 14/13/12/15, DC 2, BL 21 (alt 27) |
| Touch | CLK 25 MOSI 32 MISO 39 CS 33 |
| SD VSPI | SCK 18 MISO 19 MOSI 23 CS 5 |

## SD card layout

Format **FAT32**. Folders are created on first boot:

```
/pwnagotchi/
  README.txt
  handshakes/     <BSSID>_<SSID>.pcap
  pmkid/          <BSSID>_<SSID>.pcap
  logs/
    aps.csv
    events.log
    stats.txt
```

## Build & flash

```
python -m platformio run -e cyd -t upload --upload-port COM3
```

Dual-USB / ST7789 boards:

```
python -m platformio run -e cyd2usb -t upload --upload-port COM3
```

If auto-reset fails, hold **BOOT**, tap **RST**, keep holding BOOT until esptool connects.

Hold **BOOT** during the splash screen to recalibrate touch.

## On-screen

- Top bar: **CIRUSTHAVIRUS** / CYDgotchi
- Left: animated mask
- Right: mood, APs, EAPOL, PMKID, pwned, SD
- Bottom: `CH`  `HOP`  `APS`
