# JMGO-Beamer per Bluetooth aus dem Tiefschlaf wecken

Reverse-Engineering der JMGO-Original-Fernbedienung und Nachbau des Weck-Signals
auf einem ESP32. Ziel: den Beamer (Android-TV-Projektor „Garagebeamer") aus dem
**echten Tiefschlaf** fernsteuerbar einschalten — etwas, das weder der Netzwerk-Weg
(`/api/beamer/on`, ATV-Remote) noch eine normale BLE-HID-Tastatur schaffen.

## Das Problem

Der JMGO hat **zwei Standby-Stufen**:

| Zustand | Dauer nach Aus | WLAN | Bluetooth | Wecken möglich? |
|---------|----------------|------|-----------|-----------------|
| Leichter Standby | ~erste 10 s | an (ping geht) | an | ja, alles geht |
| **Tiefschlaf** | nach ~10 s | **aus** (ping tot) | Funk aus, **nur ein Mini-Scanner aktiv** | nur die Original-FB … bis jetzt |

Die Einstellung „Netzwerk bei Standby" war aktiv, hat den Tiefschlaf aber **nicht**
verhindert. Im Tiefschlaf ist der Beamer per Netzwerk nicht erreichbar → der
ATV-Remote-Weg (`/api/beamer/on`) scheitert dort grundsätzlich.

## Sackgassen (was NICHT funktioniert)

- **Raspberry Pi / BlueZ als BLE-HID-Gerät:** koppelt zwar, aber
  - der Beamer reconnectet im Standby **nicht** zu einem Fremd-Peripheral;
  - **gerichtetes Advertising** (das, was eine FB beim Tastendruck macht) lehnt
    der Pi-Stack ab: HCI-Status **`0x0C` (Command Disallowed)** — BlueZ verwaltet
    Advertising selbst und gibt die rohen HCI-Befehle nicht frei. Kein directed adv.
- **Falsche Rolle:** Wir haben als Peripheral *geworben und gewartet*, dass der
  Beamer sich verbindet. Tut er nicht. Im Standby ist **der Beamer der Werbende**.
- **Falsche Appearance:** Mit `0x03C1` (Keyboard) erscheint das Gerät als Tastatur
  und der Beamer behandelt es anders als seine FB (die als „Remote Control" gilt).

## Der Durchbruch — Sniffing der Original-FB

Mit dem Pi als **aktivem BLE-Scanner** (`bluetoothctl scan on` + `btmon`) wurde
mitgeschnitten, was die Original-FB beim Aufwecken aus dem Tiefschlaf sendet.

### Geräte-Adressen

| Gerät | BLE-Adresse | Typ |
|-------|-------------|-----|
| JMGO Beamer („Garagebeamer") | `B8:D8:2D:28:61:2F` | public |
| Original-Fernbedienung | `F4:22:7A:76:93:FA` | public |
| ESP32 (unser Nachbau) | `A0:A3:B3:AA:E0:0A` | public |

### Der Beamer wirbt im Standby

```
LE Address: B8:D8:2D:28:61:2F   Flags: LE General Discoverable
Company: Ecotest (0x0088)   Data: 23 4761726167656265616d652e2e2e   →  "#Garagebeame..."
16-bit Service UUID: 0x9a9b (vendor)
```

### Das Weck-Signal der Original-FB ← **das ist der Kern**

Die FB sendet beim Power-Druck ein **connectable** Advertising (`ADV_IND`):

```
Event type: Connectable undirected (ADV_IND)
Flags:      0x05  (LE Limited Discoverable, BR/EDR Not Supported)
Service:    Human Interface Device (0x1812)
Appearance: Keyboard (0x03C1)
Company:    MediaTek (0x0046)
Data:       35 | 2F 61 28 2D D8 B8 | FF FF FF FF
                 └───────┬────────┘
                 = B8:D8:2D:28:61:2F  (Beamer-MAC, little-endian / rückwärts)
```

**Die „geheime Startsequenz" ist also kein magischer Code, sondern ein
MediaTek-Herstellerpaket, das die MAC des Ziel-Beamers enthält** (umrahmt von
`0x35` und `0xFF FF FF FF`). Der Mini-Scanner im Beamer-Tiefschlaf lauscht genau
auf dieses Muster, wacht auf und verbindet sich mit dem Werbenden.

> Die FB hat noch ein **zweites** Advertising (normaler Betrieb): Flags `0x06`,
> Services HID `0x1812` + Battery `0x180F`, Appearance **Remote Control (0x0180)**.
> Das ist nicht der Weck-Trigger, sondern ihr „bin verbunden/koppelbar"-Beacon.

## Die Lösung (ESP32, NimBLE)

Warum ESP32 statt Pi: voller Low-Level-Zugriff auf Advertising **und** GATT in
einem Programm. Der Pi/BlueZ kann das benötigte Advertising nicht ausführen.

Auf Tastendruck (bzw. später per API) sendet der ESP32 **exakt das Weck-Paket**:

- `ADV_IND`, connectable, Flags `0x05`
- Service `0x1812`, Appearance `0x03C1` (Keyboard — wie das echte Weck-Adv)
- Hersteller-Daten: `46 00` (MediaTek) + `35 2F 61 28 2D D8 B8 FF FF FF FF`

```c
static const uint8_t WAKE_MFG[] = {
    0x46, 0x00,                                 /* Company: MediaTek (0x0046) */
    0x35,                                       /* prefix                     */
    0x2F, 0x61, 0x28, 0x2D, 0xD8, 0xB8,         /* B8:D8:2D:28:61:2F reversed */
    0xFF, 0xFF, 0xFF, 0xFF
};
```

Ergebnis: Beamer verbindet sich **aus dem echten Tiefschlaf** (`CONNECTED` ~3–4 s
nach dem Weck-Adv) und geht an. ✅

### Wichtige Erkenntnisse für den Code

- **Wecken = die Verbindung selbst.** Nach dem `CONNECTED` **kein** HID-Power
  senden! `Power` ist ein Toggle und schaltet den gerade geweckten Beamer sonst
  wieder aus (Symptom: „musste 2× drücken"). Für reines Einschalten reicht das
  Weck-Adv + die Verbindung.
- **Ausschalten** dagegen = im verbundenen Zustand **Consumer „Power" (Usage 0x30)**
  als HID-Report senden (Toggle).
- **Pairing:** Der Beamer verlangt **MITM / Numeric Comparison**. NimBLE mit
  `BLE_HS_IO_DISPLAY_YESNO` + Auto-Accept (`ble_sm_inject_io`, `numcmp_accept=1`);
  am Beamer den Code bestätigen.
- **Appearance `0x0180` (Remote Control)** für das normale Advertising → der
  Beamer zeigt dasselbe Icon wie für die Original-FB und behandelt das Gerät als
  Fernbedienung.
- **MAC-Spoofing war nicht nötig** — der Beamer reagiert allein auf das
  MediaTek-Muster mit seiner Adresse, nicht auf die Quelladresse der FB.

## Code

Eigenständiges ESP-IDF-Projekt: **`/Users/akw/src/esp32-hid`** (NimBLE-Host,
`main/main.c`). Trigger aktuell per Taste im Serial-Monitor (`p`) oder GPIO0.
`p` = Umschalten: aus dem Standby wecken / im Betrieb ausschalten.

## Offen / nächster Schritt (Integration in ambipi)

ESP32 dauerhaft per USB-Netzteil in Beamer-Nähe betreiben und an ambipi anbinden:

- ESP32 ins WLAN + winziger HTTP-Server: `GET /wake` (Weck-Adv) und `GET /off`
  (Consumer Power).
- ambipi `/api/beamer/on` → `curl http://<esp32-ip>/wake`.
- Für **Aus** entweder ESP32 `/off` oder das bestehende `/api/beamer/off`
  (ATV-Remote, funktioniert solange der Beamer an/erreichbar ist).

## Sniff-Kochrezept (zum Nachstellen)

```sh
# Pi als Luft-Scanner; Beamer in Tiefschlaf, dann Original-FB drücken
sudo systemd-run --unit=btmon --collect bash -c "btmon > /tmp/sniff.txt 2>&1"
sudo systemd-run --unit=blescan --collect bash -c "bluetoothctl --timeout 200 scan on >/dev/null 2>&1"
# danach auswerten:
grep -anB4 -A12 "<FB-ADRESSE>" /tmp/sniff.txt      # Weck-Advertising der FB
grep -aE "^\s+Address:" /tmp/sniff.txt | sort | uniq -c   # alle Werbenden
```
