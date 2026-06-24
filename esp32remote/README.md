# esp32-hid — BLE wake/control for the JMGO projector

ESP-IDF (NimBLE) firmware that wakes the JMGO projector from **deep standby** over
Bluetooth and turns it off — by replicating the original remote's wake signal.
Exposes a tiny **WiFi HTTP API + web page** so ambipi (or anything) can trigger it.

✅ **This works.** The beamer wakes from real deep standby and powers off. See
[../ambipi/JMGO-BT-WAKE.md](../ambipi/JMGO-BT-WAKE.md) for the full reverse-engineering story.

## How the wake works

The beamer's standby BLE scanner listens for the original remote's advertisement:
a **MediaTek manufacturer payload containing the beamer's own MAC** (`35 <addr> ffffffff`).
This firmware broadcasts exactly that → the beamer connects out of deep standby →
the connection itself wakes it (no key needed). Turning **off** sends Consumer
"Power" (0x30) over HID while connected.

## Setup

1. **VS Code** + ESP-IDF extension, ESP-IDF **v6.x**. Board: ESP32-WROOM-32 (`set-target esp32`).
2. **Fill in your WiFi** at the top of [main/main.c](main/main.c):
   ```c
   #define WIFI_SSID "CHANGE_ME"
   #define WIFI_PASS "CHANGE_ME"
   ```
3. Build → Flash → Monitor (115200). The monitor prints the assigned IP:
   `wifi: got IP 192.168.x.y`.

## HTTP API

| Endpoint | Action |
|----------|--------|
| `GET /` | Mini web page: **Ein** / **Aus** buttons + live log |
| `GET /api/beamer/on` | Wake from standby (broadcast the wake replica) |
| `GET /api/beamer/off` | Power off (Consumer Power, when connected) |
| `GET /log` | Recent log lines (plain text) |

Example: `curl http://<esp32-ip>/api/beamer/on`

## First pairing (one time)

On the JMGO: **Einstellungen → Zubehör koppeln → "JMGO BT Remote"**, confirm the
code on the beamer (our side auto-accepts the numeric comparison). It then shows
the same icon as the real remote (appearance = Remote Control). The bond persists
in NVS across reboots.

## Triggers

- **HTTP** (above) — the intended path for automation.
- **Serial monitor:** press any key (e.g. `p`) — toggles wake/off.
- **GPIO0:** BOOT button, or briefly bridge `IO0`↔`GND` (the D1-Mini has no BOOT button).

## Wiring into ambipi

Run the ESP32 on a USB power supply near the beamer. Then point ambipi's
`/api/beamer/on` at it:
```
curl -s http://<esp32-ip>/api/beamer/on
```
Off can use either the ESP32 (`/api/beamer/off`) or ambipi's existing network
ATV-remote `/api/beamer/off` (works while the beamer is on / reachable).

## Notes / tuning

- Wake = the connection. Do **not** send Power after waking (Power is a toggle and
  would switch it back off).
- Beamer address is `TV_ADDR` + `WAKE_MFG` in `main.c` (both reversed byte order).
- WiFi password stays in your local source — not committed anywhere by this project.
