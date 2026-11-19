# Plane Radar (CYD)

Live ADS-B radar for the **ESP32-2432S028R "Cheap Yellow Display"** (CYD): a 320×240 touch screen showing the aircraft around your location, with flight details, routes, altitude colours and trails.

Based on [MatixYo/ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar) (ESP32-C3 + round GC9A01), ported to the CYD and rebuilt on LVGL.

## Hardware

- **ESP32-2432S028R** (original 1-USB version): ESP32-WROOM-32, ILI9341 320×240 SPI display, XPT2046 resistive touch
- **microSD card** (optional): settings are mirrored to `/planeradar.json`
- Nothing to wire: every pin is on the board (see `include/config.h`)

| Part | Pins |
|------|------|
| Display (HSPI) | SCLK 14, MOSI 13, MISO 12, CS 15, DC 2, backlight 21 (PWM) |
| Touch (bit-banged SPI) | SCLK 25, MOSI 32, MISO 39, CS 33, IRQ 36 |
| microSD (VSPI) | SCLK 18, MISO 19, MOSI 23, CS 5 |
| BOOT button | GPIO 0 |

## First boot

1. **Touch calibration**: tap the four corner targets. To redo it later, hold a finger on the screen for 3 s while powering up.
2. **Wi-Fi**: pick a network from the scan list and type the password. It is saved, and reconnects on its own after that.
3. **Location**: a city-level guess from your IP address is used until you set one. Open **Settings → Location** and search an address (e.g. `14565 gilbert mirabel`).

## Using it

The left two thirds of the screen is the radar; the right third is the detail panel.

| Action | Effect |
|--------|--------|
| Tap a plane | Select it: details in the panel, highlighted tag, brighter trail |
| Tap a dot on the screen edge | Select a plane beyond the outer ring |
| Tap empty space | Clear the selection |
| Tap the range label (top right) | Next range: 5 → 10 → 15 → 20 → 25 → 30 km (or mi), saved |
| Gear (top left) | Settings |
| BOOT button: tap / hold 3 s | Next range / Settings |

### Radar

- **Plane symbols** point along the heading, with a speed vector. The colour shows altitude:

  | Colour | Altitude |
  |--------|----------|
  | Light blue | above 25,000 ft |
  | Green | 10,000–25,000 ft |
  | Yellow | 3,000–10,000 ft |
  | Orange | below 3,000 ft |
  | Grey | on the ground |
  | White | unknown |

- **Tags**: callsign, type, and altitude with a climb/descent arrow (shown above ±300 ft/min). They're placed around the symbol so they don't overlap each other, other planes or the buttons. When there's no room, a tag shrinks to the callsign, or is hidden. The selected plane always gets its full tag.
- **Positions are dead-reckoned** every second between fetches (every 5 s), from each plane's track and speed.
- **Trails**: a fading line through each plane's last ~1 minute of real positions (12 fixes) in its altitude colour. Up to 32 at once, for planes inside the radar.
- **Beyond the ring**: planes past the outer ring (but still in the fetched area) are dots on the screen edge, at their true bearing.
- **Emergencies**: squawk 7700 / 7600 / 7500, or the feed's emergency flag. The symbol blinks red, the tag shows the code (`7700 EMER`, `7600 RADIO`, `7500 HIJACK`), the trail is red, and the panel shows a red banner when the plane is selected.
- **Runways** of major airports (OurAirports `large_airport`, plus CYMX), with one ICAO label per airport.
- **Status corner** (bottom left): `● 6 · 3s`, the aircraft count and the age of the data. The dot is green under 15 s, orange under 60 s, and red when the data is older or Wi-Fi is down.

### Detail panel

- **Selected plane**: callsign, airline and route (from adsbdb), aircraft name, then ALT, V/S, SPD, TRK, DST and BRG. "Signal lost" shows when the plane drops out of the feed; the selection clears after 10 s.
- **Nothing selected**: a summary (in range, beyond the ring, the nearest plane and its distance, emergencies) and the altitude colour legend.

## Settings

| Setting | Notes |
|---------|-------|
| Location | Address search (OpenStreetMap Nominatim). Changing it clears the traffic and the trails. |
| Wi-Fi | Scan and connect to another network |
| Distance units | km or mi (ring label, range label, distances) |
| Runways | Show or hide the runway overlay |
| Ground aircraft | Show or hide aircraft on the ground |
| Brightness | Backlight, 10–100 % |

Settings are saved in the ESP32's flash (NVS). With a microSD card they are also written to **`/planeradar.json`**, which wins at boot. So you can edit the file on a PC:

```json
{
  "version": 1,
  "units": "km",
  "runways": true,
  "ground_aircraft": false,
  "brightness": 100,
  "range_km": 10,
  "location": {
    "lat": 45.635121,
    "lon": -73.992735,
    "name": "14565 Rue Gilbert, Mirabel",
    "approximate": false
  }
}
```

- `units`: `"km"` or `"mi"`
- `range_km`: 5, 10, 15, 20, 25 or 30
- Missing or invalid fields keep the stored value. Wi-Fi credentials are never written to the card.

## Build and flash

[PlatformIO](https://platformio.org/), environment **`cyd`** (the default):

```bash
pio run -t upload            # build and flash over USB
pio device monitor           # serial log, 115200 baud
```

**Single image for browser flashers** (flash at **0x0**, e.g. [esptool-js](https://espressif.github.io/esptool-js/)):

```bash
pio run -t merge -e cyd      # -> .pio/build/cyd/firmware-merged.bin
./scripts/merge-firmware.sh  # same, copied to release/plane-radar-merged.bin
```

### Debug build flags

Set with `PLATFORMIO_BUILD_FLAGS` (e.g. `PLATFORMIO_BUILD_FLAGS=-DPLANE_RADAR_FAKE_SQUAWK pio run -e cyd -t upload`):

| Flag | Effect |
|------|--------|
| `PLANE_RADAR_FAKE_SQUAWK` | The first aircraft of every fetch shows as squawking 7700 |
| `PLANE_RADAR_HEAP_TRACE` | Logs each new heap low and the network stage that caused it |

The serial log also prints `perf` (render and flush times) and `heap[...]` (free, largest block, lowest) lines every 30 s.

### Unit tests

Pure logic is tested on the PC: HTTP chunked decoding, ADS-B parsing, the trail store, altitude bands, range presets and labels. The tests need a host C++ compiler (`gcc`/`g++`):

```bash
pio test -e native
```

### CI and releases (GitHub Actions)

| Workflow | When | Output |
|----------|------|--------|
| [Build](.github/workflows/build.yml) | Push to `main`, `master` or `cyd-esp32-2432s028r`; PRs; manual | Runs the unit tests, then builds. Artifact `plane-radar-cyd-<sha>`: merged `.bin`, split `.bin` files, `FLASHING.txt` |
| [Release](.github/workflows/release.yml) | Tag `v*` (e.g. `v2.0.0`) | GitHub Release with `plane-radar-cyd-<version>.bin` + `.sha256` |

## How it works

- **Two tasks**: the Arduino loop (core 1) runs LVGL and the UI. A network task (core 0) runs every HTTP request, so the screen never waits on the network.
- **ADS-B**: `opendata.adsb.fi`, every 5 s, over one kept-open HTTPS connection. A TLS handshake briefly needs ~85 KB of heap, so it happens once, not every fetch. The fetch radius reaches the screen edge so the edge dots have data. The aircraft list is double-buffered: the network task fills one buffer while the UI reads the other.
- **One TLS session at a time**: the ADS-B connection is closed before a route, address or IP lookup, and reopened on the next fetch. Routes are cached (last 8 flights).
- **Rendering**: LVGL 9 partial rendering (20-line DMA buffer) through LovyanGFX. Only the areas that changed are redrawn each second (~50 ms per frame instead of ~200 ms for a full redraw).

## Project layout

```
include/
  config.h                pins, timing, default location
  lv_conf.h               LVGL configuration
  hardware/               display (LovyanGFX), touch (XPT2046), BOOT button
  services/
    adsb_client.h         ADS-B fetch, double buffer, kept-open connection
    adsb_parse.h          JSON -> Aircraft (pure, unit-tested)
    http_body_framing.h   HTTP chunked / Content-Length body decoder
    route_lookup.h        airline + route (adsbdb), 8-entry cache
    geocode.h             address search (Nominatim)
    ip_location.h         first-boot location guess (ip-api.com)
    radar_location.h      radar centre (NVS), generation counter
    settings.h            user settings (NVS)
    config_store.h        SD card mirror (/planeradar.json)
    wifi_conn.h           Wi-Fi connection and reconnects
    network_task.h        the network task, heap log
  ui/
    radar_screen.h        radar: frame model, tag placement, redraw only what changed
    detail_panel.h        selected plane / idle summary + legend
    trails.h              trail store (pure, unit-tested)
    settings_screen.h     Settings, address search
    wifi_screen.h         Wi-Fi scan / password / connecting
    radar_theme.h         layout, colours, altitude bands
    radar_range.h         range presets, labels
    radar_draw.h          LVGL draw helpers (skip what's off the strip)
    radar_geometry.h      lat/lon -> screen
    runway_overlay.h      runway lines and labels
    lvgl_port.h, lvgl_mem.h, perf.h, radar_fonts.h
  data/large_airports.h
src/                      matching .cpp files, fonts, airport data
test/                     native unit tests (Unity), stubs
scripts/                  airport list builder, firmware merge
partitions/               flash layout
```

Update the airport list with `python3 scripts/build_large_airports.py`.

## Data sources

- Aircraft: [adsb.fi](https://opendata.adsb.fi/) open data API
- Routes and airlines: [adsbdb](https://www.adsbdb.com/)
- Address search: [OpenStreetMap Nominatim](https://nominatim.org/), max 1 request/s
- First-boot location: [ip-api.com](https://ip-api.com/) (city level)
- Airports and runways: [OurAirports](https://ourairports.com/data/)

## Dependencies

- [LVGL](https://lvgl.io/) 9.2
- [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- [ArduinoJson](https://arduinojson.org/) 7
- Arduino-ESP32 (PlatformIO `espressif32@6.5.0`)
