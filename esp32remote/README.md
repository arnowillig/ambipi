# esp32remote — BLE remote for the JMGO projector

ESP-IDF (ESP32, NimBLE) firmware that acts as the JMGO projector's Bluetooth remote
**and** exposes a WiFi HTTP API + web page, so ambipi / Home-Assistant / `curl` can
drive it. It can even **wake the projector from deep standby** by replicating the
original remote's BLE wake advertisement — see
[../JMGO-BT-WAKE.md](../JMGO-BT-WAKE.md) for the reverse-engineering story.

- **Web UI:** `http://<device-ip>/` — full remote (power, D-pad, Back/Home/Menu/Settings/Input, volume) + live status + log.
- **OTA:** `make push` updates over WiFi (no cable).
- **Version:** single source `PROJECT_VER` in [CMakeLists.txt](CMakeLists.txt) → shown in web header + boot log + `/api/status`.

Current device: **`192.168.178.175`** (set a static DHCP lease).

---

## HTTP API

All endpoints are **GET** except the OTA upload. Responses are plain text `ok\n`
unless noted. CORS-free, no auth — intended for the local LAN.

### Control — `GET /api/beamer/<key>`

All codes below are **sniffed from the original JMGO remote** (not guessed) — see
"How the codes were found" below. `cc` = Consumer report (ID 2), `key` = Keyboard report (ID 1).

| `<key>` | Action | HID sent | Works when |
|---------|--------|----------|------------|
| `on` | Wake / power on | wake advertisement, else Consumer Power | always (incl. deep standby) |
| `off` `power` | Power | Consumer `0x0030` | connected |
| `up` `down` `left` `right` | D-pad | **Consumer** `0x0042`/`0x0043`/`0x0044`/`0x0045` | connected |
| `ok` | Select / center | **Consumer** `0x0084` | connected |
| `back` | Back | Consumer `0x0224` | connected |
| `home` `googletv` | Home / launcher | Consumer `0x0223` | connected |
| `menu` | Menu | Consumer `0x0040` | connected |
| `settings` | Settings | **Consumer** `0x00BE` | connected |
| `input` | Input / source list | **Keyboard** `0x1F` | connected |
| `youtube` `netflix` `prime` | App shortcuts | **Keyboard** `0x20`/`0x21`/`0x1E` | connected |
| `hdmi` | HDMI input macro | `input` → `up` → `down` → `ok` sequence | connected |
| `volup` `voldown` `mute` | Volume | Consumer `0x00E9`/`0x00EA`/`0x00E2` | connected |

⚠️ The JMGO D-pad/OK/Settings are **Consumer** usages in a vendor range (`0x0042`–`0x00BE`),
**not** the standard Keyboard arrows/Enter or `AL Control Panel 0x0183` we originally guessed —
those never worked. Only the sniffed codes register on the beamer.

Tuning (find unknown codes live, no reflash needed):

| Endpoint | Action |
|----------|--------|
| `GET /api/beamer/cc?u=<hex>` | send an arbitrary **Consumer** usage (e.g. `?u=223` = Home) |
| `GET /api/beamer/key?k=<hex>` | send an arbitrary **Keyboard** usage (e.g. `?k=28` = Enter) |
| `GET /api/beamer/vkey?k=<num>` | send an experimental JMGO vendor keycode (`K:<num>,A:1/0`) |

### Status / system

| Endpoint | Returns |
|----------|---------|
| `GET /` | Web UI (HTML) |
| `GET /api/status` | JSON `{"connected":true,"version":"1.1.3"}` |
| `GET /log` | recent device log lines (plain text) |
| `POST /api/ota/upload` | firmware update — raw `.bin` body, writes + reboots (used by `make push`) |

Examples:
```sh
curl http://192.168.178.175/api/beamer/on
curl http://192.168.178.175/api/beamer/home
curl http://192.168.178.175/api/status
curl 'http://192.168.178.175/api/beamer/cc?u=223'   # send Home via the tuning endpoint
```

**Connected vs disconnected:** everything except `on` needs the projector connected
(on, or in networked standby). `on` works from real deep standby (BLE wake). Check
`/api/status` or the web badge (● Connected / ○ Disconnected).

---

## Build / flash / update (`make`)

| Command | What |
|---------|------|
| `make deploy` | build + flash over **USB** (one-time, or recovery) — `PORT` auto-detected, 115200 baud |
| `make push` | build + **OTA** update over WiFi — `make push HOST=<ip>` (default `192.168.178.175`) |
| `make monitor` / `make run` | serial monitor / flash+monitor |
| `make clean` / `make erase` | fullclean / erase whole flash |
| `make version` | print `PROJECT_VER` |

**Update workflow:** bump `PROJECT_VER` in [CMakeLists.txt](CMakeLists.txt) → `make push`.
The device writes the image to the spare OTA partition, switches boot, reboots (~1 s);
the new version shows in the web header / `/api/status` / boot log.

The Makefile bakes in the workaround for the broken system ESP-IDF env (system python
upgraded): it sets `IDF_PYTHON_ENV_PATH` + `IDF_PYTHON_CHECK_CONSTRAINTS=no` and sources
`export.sh` itself — nothing to set up by hand.

⚠️ **First time / partition change:** the dual-OTA partition layout (and any change to
`partitions.csv`) can only be applied via **USB `make deploy`**, not OTA. That erases
flash → **re-pair the projector once**.

⚠️ **HID descriptor changes** (adding/removing keys, i.e. editing the report map in
`main/main.c`) require the projector to **re-read the descriptor** → remove the
"ESP32 Remote" pairing on the projector and pair again, or the new keys won't register.
Pure additions like new HTTP endpoints / web buttons / status do **not** need re-pairing.

⚠️ **No rollback:** a broken image needs a USB `make deploy`. The boot partition is only
switched after `esp_ota_end` validates the image, so an interrupted upload is safe.

---

## First pairing (one time)

Projector → **Einstellungen → Zubehör koppeln → "ESP32 Remote"**, confirm the numeric
code on the projector (the firmware auto-accepts). It then appears with the same icon
as the original remote (appearance = Remote Control). The bond persists in NVS.

## Configuration (top of [main/main.c](main/main.c))

- `WIFI_SSID` / `WIFI_PASS` — your WiFi (⚠️ plain text in source; reset before committing).
- `TV_ADDR` + `WAKE_MFG` — the projector's BLE address (reversed byte order) used for the
  wake advertisement. Change both if your projector differs.
- Firmware version → `PROJECT_VER` in `CMakeLists.txt`.

## Triggers (besides HTTP)

- **Serial monitor:** type a key (any) — toggles wake/off.
- **GPIO0:** BOOT button, or briefly bridge `IO0`↔`GND`.

## How the codes were found — sniffing the original remote (SOLVED)

The guessed HID codes (`Settings 0x0183`, keyboard arrows for the D-pad, etc.) **never
registered** on the JMGO beamer. Two things had to match the original remote exactly:

1. **The HID descriptor layout, not just the codes.** The original JMGO remote
   (`F4:22:7A:76:93:FA`) presents:
   - **Keyboard = Report ID 1**, **Consumer Control = Report ID 2** (we originally had them
     swapped — consumer on ID 1). The beamer keys its dispatch on the report ID.
   - PnP ID cloned verbatim: **VID `0x1D5A`, PID `0xE081`, version `0x0002`**, source =
     Bluetooth SIG (`pnp_id[] = {0x01,0x5A,0x1D,0x81,0xE0,0x02,0x00}`). This is the
     `Modalias: bluetooth:v1D5ApE081d0002` you see in `bluetoothctl info`.
   - Consumer report is a single Usage value (`hid_report_map` in `main/main.c`).
2. **The actual usage codes are a vendor range, sniffed live.** D-pad/OK/Settings come over
   the **Consumer** page in `0x0042`–`0x00BE` (not Keyboard, not standard AC/AL usages).
   App-launch keys (input/YouTube/Netflix/Prime) come over **Keyboard** as `0x1E`–`0x21`.

**Sniffer (`/api/sniff/start` … `/api/sniff/stop`):** the ESP32 connects to the original
remote as BLE **Central**, deletes any stale bond, discovers the HID service, reads the
Report Map + Report Reference descriptors, subscribes to all NOTIFY characteristics, and
logs every report to `/log`. Each button press shows `sniff: attr=0x.. data=..` → read the
usage straight off the wire.

⚠️ **Why the Pi sniff failed but the ESP32 worked:** a BLE HID remote only sends reports to
its *bonded primary host*. Pairing it to the Pi left it bonded to the beamer, so it sent
nothing. The ESP32 sniffer deletes the bond first and becomes the sole host. After sniffing,
**re-pair the original remote with the beamer** (Einstellungen → Zubehör koppeln).

- Live probing without reflash: `/api/beamer/cc?u=<hex>` (Consumer) / `/api/beamer/key?k=<hex>`
  (Keyboard). The `vkey` vendor-GATT path was a dead end (beamer doesn't expose it on the HID conn).
