# AmbiPi — Project Notes

Raspberry-Pi **Ambilight controller for big screens** (beamer projection on a ~200×115 cm canvas).
Written in C++ (C++20 via Makefile / C++17 via qmake). Captures the screen/video, derives edge
colors, drives local WS2812 LED strips, and mirrors the ambient color to several other devices on
the LAN (a "GameWall" of WLED shelves, a gaming table, a 32×32 display, WiZ bulbs).

## Build & Run

- **Production build (Pi, no GUI):** `make` → `ambipi` binary. Links static `rpi_ws281x` +
  `pistache` from git submodules, OpenCV via `pkg-config`.
  - `make run` → `sudo ./ambipi` · `make restart` → `sudo service ambipi restart` · `make log` → journalctl.
  - `make clean` / `make distclean` (also wipes submodule build dirs).
- **Packaged build/deploy (armhf .deb via Docker):** `make deb` cross-builds inside an
  `arm32v7/debian:bullseye` container (`Dockerfile.cross`, QEMU-emulated; **base must match the target Pi's
  OS** — ataripi runs Raspbian 11/bullseye, so deps resolve to OpenCV 4.5 / glibc 2.31) and assembles
  `dist/ambipi_<ver>_armhf.deb` via `packaging/build-deb.sh` (submodules fetched over HTTPS, deps via
  `dpkg-shlibdeps`). `make deploy` scp's it to `ataripi.local` and `dpkg -i` (verifies install). `make docker-clean` drops the image.
  - The deb installs to FHS paths and ships a systemd unit; its **`preinst` removes the old hand-installed
    infra** (`/usr/local/bin/ambipi`, `/etc/systemd/system/ambipi.service`). Maintainer scripts +
    unit live in `packaging/`.
- **Dev/GUI build:** `ambipi.pro` (qmake). Defines `_GUI_` → LEDs are simulated (no `ws2811_init`),
  frames shown in OpenCV windows via `drawGUI`/`getDebugFrame`, no hardware/network render.
- **Submodules** (`.gitmodules`): `rpi_ws281x` (arnowillig fork), `pistache`. Run `git submodule update --init`.
- **Bundled deps:** `json.hpp` (nlohmann, header-only), used by both ambipi and gamewall.
- **Deploy:** `ambipi.service` runs `/usr/local/bin/ambipi` as root, `WorkingDirectory=/home/pi/src/ambipi`.
- **Runtime file locations (FHS, set by the deb):** web UI `html/` → `/usr/share/ambipi/html`,
  GameWall shelves → `/usr/share/ambipi/shelves.json`, network config → `/etc/ambipi/config.json`,
  `WorkingDirectory`/screenshot → `/var/lib/ambipi`. Binary → `/usr/bin/ambipi`. These paths are
  compiled in (`restserver.cpp` `_basePath`; `loadShelvesFromJson`/`loadNetworkConfig` in `ambipi.cpp`;
  screenshot in `main.cpp`). Test video default still `/home/pi/Videos/...` (dev only).
- **`config.json`** (optional, `/etc/ambipi/config.json`, loaded in `AmbiPi::init` via
  `loadNetworkConfig`) overrides the LAN network targets (display/DDP/table/WiZ hosts & ports).
  If missing/unparsable, the built-in defaults (`struct NetConfig g_net` in `ambipi.cpp`) are used —
  identical to the committed `config.json`. LED geometry stays compile-time (`#define LEDS_*`).

## Top-level layout

| File | Role |
|------|------|
| `main.cpp` | Entry point + main render loop (mode state machine, frame capture, FPS pacing, signals). |
| `ambipi.h` / `ambipi.cpp` | Core `AmbiPi` class: LED buffers, modes, ambilight calc, all network outputs. **Biggest file (~1500 lines).** |
| `restserver.h` / `restserver.cpp` | `RESTServer` (Pistache) on **port 80**: HTTP control API + serves the web UI. |
| `framebuffer.h` / `framebuffer.cpp` | `FrameBuffer`: read/write `/dev/fb0`; grab screen via Dispmanx (`HAVE_DISPMANX`). |
| `html/` | Web UI: `index.html`, `ambipi.js` (vanilla JS), `ambipi.css`. |
| `gamewall/` | **Separate standalone executable** (CMake) — a WLED proxy REST server. See below. |

## Hardware / LED geometry (defined in `ambipi.cpp` top)

- LED counts: `LEDS_TOP=60`, `LEDS_BOTTOM=60`, `LEDS_LEFT=34`, `LEDS_RIGHT=34` (188 total, 30 leds/m).
- Two ws2811 channels:
  - **channel[0]** GPIO **12** (`GPIO_PIN1`): `LEDS_LEFT + LEDS_TOP` (left strip continues into top).
  - **channel[1]** GPIO **13** (`GPIO_PIN2`): `LEDS_BOTTOM + LEDS_RIGHT`.
  - DMA 10, `WS2811_TARGET_FREQ`, `WS2812_STRIP`.
- ⚠️ The README says strip A = GPIO 18, but the **code uses GPIO 12** — trust the code.
- LED index addressing helpers: `setColor(idx,...)` maps a single global index across L→T→R→B;
  per-side setters (`setColorLeft/Top/Bottom/Right`) handle the orientation/reversal. Physical
  **corners are handled specially** (averaged/forced) in `calculateAmbilightFromFrame`.

## Main loop (`main.cpp`)

Infinite loop switching on `ambiPi.mode()`:
- **Off / White / Color** — static fills.
- **Rainbow / Vegas / Knightrider / TestPattern / LeftSide(goal) / RightSide(goal)** — animations.
- **AmbiLight** — grab from a **USB capture device** `VideoCapture(0, CAP_V4L2)` at 160×120@30 (V4L2
  backend forced — GStreamer mis-decodes the EasyCap "AV TO USB2.0" YUYV → green/R-B corruption). Then per
  enabled output: `calculateAmbilightFromFrame` (always), `calculateDisplayFrameFromFrame`,
  `calculateGameWallFrameFromFrame` (kicker lights call is commented out). ~40 ms/frame.
  - **Manual R/B swap** (`getSwapRB`, `/api/swaprb`, web UI toggle, persisted to `/var/lib/ambipi/settings.json`):
    safety net for the EasyCap, which randomly locks Cb/Cr swapped on each source (AppleTV) re-sync.
  - **USB auto-recovery:** the EasyCap drops off USB and won't re-enumerate ("Cannot enable"). After ~15 s of
    no frames the loop calls `cycleCaptureUsbPort()` (`uhubctl -l 1-1 -p 3 -a cycle`, configurable via
    `usb_recovery`/`uhubctl_location`/`uhubctl_port` in `config.json`) then reopens the device. `uhubctl` is a
    deb dependency. (Capture/source quality issues all trace to the cheap analog grabber.)
- **AmbiLight2** — grab from **framebuffer/Dispmanx** instead of camera; does crop-border (letterbox)
  detection. Writes a debug screenshot on `SIGUSR1`.
- Each iteration ends with `ambiPi.render()` then `usleep`.
- Signals: SIGINT/SIGTERM → quit (clears LEDs); SIGUSR1 → take screenshot.

## `AmbiPi` core (`ambipi.cpp`)

- **Ambilight algorithm** (`calculateAmbilightFromFrame`): downsamples each edge band to a 1-D strip
  with `cv::resize(INTER_AREA)`, then **exponential smoothing** via `cv::addWeighted` with `_alpha`
  (the `/api/alpha` knob = how much of the previous frame to keep). Corners set from strip ends.
- **Gamma:** `setGamma` builds a 256-entry LUT (`buildGammaLUT`) *and* sets the ws2811 custom gamma.
  Init default gamma = **1.73** (`main.cpp`).
- **Crop / letterbox:** `updateCropRect` uses Canny edge scan on the borders to find content rect;
  `cropBorders` stretches content back out. Toggled via `/api/crop`.
- Animations: `rainbow`, `vegas`, `knightrider`, `goal` (a sweep used for LeftSide/RightSide),
  `drawTestPattern`, `fadeColors`.

### Network outputs (all UDP, all in `ambipi.cpp`) — toggled independently

1. **Local WS2812 strips** — `render()` → `ws2811_render`. Always on (unless `_GUI_`).
2. **GameWall** (`_enableGameWallAmbilight`, `/api/gamewall`) — frame resized to **6×4**, each of 40
   "shelves" mapped to a grid cell (`shelfToXY`), expanded to LED segments from `shelves.json`, blended
   with previous frame (`_alpha`), sent via **DDP protocol** (`sendDDP`, port **4048**) to two WLED
   controllers: **`192.168.178.146`** = shelves 1–24 (right board), **`192.168.178.185`** = shelves 25–40
   (left, L-shaped layout).
3. **Gaming Table** (`_enableGamingTable`, `/api/table`) — top & bottom strips stretched to 86 LEDs each
   (`stretchAndInterpolate`) and sent via **WLED DNRGB** (`sendWledDnRgbRange`, mode 4) to
   **`192.168.178.150:21324`**.
4. **Display** (`_enableDisplayVideo`, `/api/display`) — center-cropped, downscaled to **32×32**, sent via
   a custom **"KDP" datagram** (`sendKDPDatagram`, header `'K''D'…`) to **`192.168.178.46:14000`** (prod) /
   `127.0.0.1` (`DEVEL`). Only emitted in `AmbiLight` (camera) mode, not `AmbiLight2`.
   **Receiver:** the `flow-grid` firmware (`/Users/akw/src/flow/firmware-grid`) has an `ambipi` high-priority
   animation that binds UDP :14000, decodes the KDP frame (8-byte header + 3072 RGB bytes, row-major,
   direct RGB copy) and renders it on the 32×32 panel. Priority 50 → a live `kicker` game (100) wins;
   frame goes stale after `display_receiver_timeout_ms` (2 s).
5. **Kicker / WiZ bulbs** (`calculateKickerLightsFromFrame`) — samples a 1×4 + side quarters, sends WiZ
   `setPilot` JSON over UDP **port 38899** to bulbs at `192.168.178.{80,87,50,53,109,127}`.
   **Currently not called** (commented out in the main loop).

## REST API (`restserver.cpp`, Pistache, port 80)

Mostly `GET` "set" endpoints (path params), plain-text responses, CORS `*`:
- `/api/mode/:mode` (off|ambilight|white|color|rainbow|vegas|knightrider|testpattern|leftside|rightside), `/api/mode`
- `/api/col/:r/:g/:b`, `/api/bri[/:bri]`, `/api/gamma[/:gamma]`, `/api/alpha[/:alpha]`, `/api/crop[/:crop]`
- `/api/display/:enabled`, `/api/table/:enabled`, `/api/gamewall[/:enabled]`
- `/api/screenshot.jpg` — JPEG of the debug frame (annotated LED layout)
- `/`, `/index.html`, `/static/*` — serves `html/`
- `POST /api` — Alexa-style smart-home directives: `TurnOn/TurnOff/SetBrightness/SetColor/SetColorTemperature`.
  Note quirk: `SetColor` with pure blue (0,0,255) switches into **AmbiLight** mode.

## `gamewall/` — standalone WLED proxy (separate program)

- Own `CMakeLists.txt` (CMake, FetchContent for pistache/json), binary `gamewall`, default **port 9080**
  (`--port`/`--threads`). Runs on `ataripi.local:9080` (linked from the web UI).
- Pure WLED REST→UDP proxy. Reads `gamewall/public/shelves.json` (hot-reloaded on mtime change),
  serves `public/` static + `swagger.json`.
- Endpoints: `POST /api/v1/wled/{dnrgb-range, ranges-color, shelf-color, shelves-color}` — color shelves by
  1-based index or send raw DNRGB ranges. Each shelf entry has `host`, `port`, and LED `segments`
  (`{startIndex,count}`).
- **`shelves.json` is shared:** ambipi reads the same file (first 40 entries, `loadShelvesFromJson`) for its
  GameWall mapping but ignores per-shelf host/port — it hardcodes the two `.146`/`.185` DDP targets instead.

## LAN device map (quick reference)

| Device | Address | Protocol |
|--------|---------|----------|
| WLED GameWall right (shelves 1–24) | `192.168.178.146:4048` | DDP |
| WLED GameWall left (shelves 25–40) | `192.168.178.185:4048` | DDP |
| Gaming table WLED | `192.168.178.150:21324` | WLED DNRGB |
| 32×32 Display | `192.168.178.46:14000` | custom KDP |
| WiZ bulbs | `192.168.178.{80,87,50,53,109,127}:38899` | WiZ JSON UDP |

All addresses above are defaults in `NetConfig` and overridable via `config.json`.

## Gotchas

- Code targets the Pi; many absolute `/home/pi/...` paths and hardcoded LAN IPs. Editing on macOS won't
  build/run locally without OpenCV + the submodules + the hardware.
- `_GUI_` build is the only way to run off-Pi; it stubs out LED hardware and network render is skipped only
  inside `render()` (other senders still fire if enabled — they just fail silently on no route).
- LED corner colors and the bottom-left "fix" (`LEDS_LEFT-2/-3`) are intentional empirical tweaks
  (see recent commits "Fixed shelves 30 and 36", "Adds quarter-sampling").
