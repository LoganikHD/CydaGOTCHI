# What this ESP32 CYD can actually do

Hardware: ESP32-D0WD-V3, **one 2.4 GHz radio**, no PSRAM, 4 MB flash.

## Radio rules (from Espressif)

| Combo | Reality |
| --- | --- |
| Wi-Fi sniffer + SoftAP | Supported. Sniffer hurts AP throughput. |
| SoftAP + STA (join home Wi-Fi) | Supported, **same channel only**. |
| Channel hop + SoftAP clients | Clients drop. Firmware **freezes hop** while someone is on the portal or joined to home Wi-Fi. |
| BLE scan + Wi-Fi sniffer | Officially **C1 = unstable**. We time-slice: pause sniffer ~2s, BLE scan, resume. Skip BLE while a phone is on the portal. |
| BLE + deauth/inject | Not implemented. Capture stays **receive-only**. |
| 5 GHz / Thread / Zigbee | Not on this chip. |

## Implemented in 0.3.0

- Pwnagotchi-style drawn face (no V mask)
- SoftAP **`CIRUSTHAVIRUS`** / pass **`cydgotchi`**
- Captive portal **http://192.168.4.1** — status, join home Wi-Fi, settings, air (APs + BLE)
- BLE scanner with coarse kinds: apple / iot / wear / audio / phone / tag
- Wi-Fi SSID heuristics for IoT cameras, ESP, Shelly, Tuya, printers, etc.
- Settings in NVS (SSID/pass, hop, BLE)

## Useful next (not in this build)

- mDNS `cydgotchi.local` once on home Wi-Fi
- List SD captures in the portal and download `.pcap`
- Bluetooth Classic inquiry (headphones / speakers that do not advertise BLE)
- Probe-request STA table (phones near an AP, not just the AP itself)
- ESP-NOW / vendor action frames
- OUI database on SD for nicer vendor names
- MQTT/syslog offload when STA is up

## How to use the portal

1. Phone Wi-Fi → **CIRUSTHAVIRUS**
2. Password **cydgotchi**
3. Open **http://192.168.4.1** if it does not pop automatically
4. **wifi** tab → pick home network → connect
5. After it joins, hop stops (required). BLE still time-slices only when idle.
