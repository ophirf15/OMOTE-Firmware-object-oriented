# OMOTE Bridge Firmware

Standalone firmware for the ESP32-C3 / ESP32-S3 bridge device (plugged in at the TV).

## First-time WiFi setup (captive portal)

1. Power the bridge (USB at the TV).
2. On your phone, join WiFi **`OMOTE-Bridge-Setup`** (open network).
3. A setup page should appear automatically; if not, open `http://192.168.4.1`.
4. Enter your home WiFi SSID and password → **Save & reboot**.
5. After reboot, serial should show `[bridge] WiFi OK` and `ESP-NOW listening`.

Credentials are stored in NVS (`wifiSettings`, same keys as the OMOTE remote). No PlatformIO edits required.

If WiFi connect fails (wrong password, router offline), the bridge re-opens the captive portal.

## Build & flash

```powershell
cd Platformio/bridge

# If the board boot-loops after a bad flash, erase once:
pio run -e esp32c3_bridge -t erase --upload-port COMx

# C3
pio run -e esp32c3_bridge -t upload --upload-port COMx
pio device monitor -b 115200 -p COMx
```

**Serial monitor:** `monitor_dtr` / `monitor_rts` are disabled in `platformio.ini` so opening the monitor does not reset the chip in a loop. On USB-native C3 boards, wait a few seconds after upload for the USB serial port to appear.

S3 N16R8 (when available): `pio run -e esp32s3_bridge -t upload --upload-port COMx`

## Phase 1 link test

1. Flash **bridge** first; complete captive portal WiFi setup.
2. Flash **remote** from parent `Platformio/`:
   ```powershell
   cd ..\..
   pio run -e esp32_Rev1 -t upload --upload-port COM7
   ```
3. Remote serial (after its own WiFi connects): `[OLP] linked bridge …`
4. Bridge serial: `[OLP] ping from …`

Both devices must use the **same home WiFi** (same channel) for ESP-NOW.

## Phase 2 — editor, config sync, HA proxy

1. **Upload config to the bridge** (authoritative copy):
   - Put your JSON pack on the bridge LittleFS once: `pio run -e esp32c3_bridge -t uploadfs` (after adding a `data/` folder under `bridge/`, or deploy via editor).
   - Ensure `HaSettings.json` is on the bridge with your HA URL + long-lived token.
2. **Config editor** — connect to **`http://omote.local`** (bridge), **not** the remote. The remote no longer serves HTTP when built with `OMOTE_BRIDGE_CLIENT=1`.
3. **Boot flow** — remote links over ESP-NOW → pulls manifest + JSON files from bridge → refreshes scene list.
4. **HA buttons** — tap on remote → `HA_CALL` over ESP-NOW → bridge REST to Home Assistant → state pushed back as `HA_STATE`.

Serial markers: `[bridge_client] config sync complete`, `[bridge_ha] light.turn_on …`, `[Scene] load tab …` (lazy tabs — one page at a time to avoid OOM).
