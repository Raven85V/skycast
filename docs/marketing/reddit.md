# Reddit posts (drafts)

## r/homeassistant

**Title:** I built an ESP32-S3 wall weather station that talks to Home Assistant (MQTT + HACS integration)

**Body:**

After months of iterating I finally have the station I wanted on my wall: a Sunton ESP32-8048S070
(7" 800×480, ESP32-S3) showing current weather, a 5-day forecast, my Wi-Fi sensor readings and a big clock.

A few things that might be useful to others:

- **Two HA integrations in one project**: MQTT Discovery (zero install) and a proper HACS component
  that polls a local HTTP API — no MQTT broker required. The component exposes 15+ sensors plus
  control buttons (next screen / home screen / big clock / reboot) and a brightness slider.
- **Animated weather icons** are stored as frame packs in LittleFS, so the 6.5 MB app partition
  stays free for code.
- **Wi-Fi sensors**: a tiny ESP8266/ESP32 firmware auto-detects BME280/BMP280/DS18B20 and pushes
  readings to the station over HTTP. The station then republishes them to HA.
- **Everything is configurable from a web UI** — Wi-Fi, API keys, brightness, clock style, OTA.

Repo (MIT): https://github.com/skycast-project/skycast — screenshots and build instructions inside.

Happy to answer anything about the RGB panel timings, PSRAM usage or the MQTT discovery schema.

---

## r/esp32

**Title:** ESP32-S3 + 7" RGB panel weather station: LovyanGFX, PSRAM frame buffers, LittleFS icon packs

**Body:**

Sharing a project I have been polishing for a while — a weather station on a Sunton ESP32-8048S070
(ESP32-S3 N16R8, 7" 800×480 RGB panel, GT911 touch).

Technical bits that took the most time:

- **RGB panel + PSRAM**: double-buffered sprite doesn't fit comfortably at 800×480×2 bytes,
  so I ended up drawing directly to the panel with partial refreshes and a small off-screen
  sprite only for the animated icons.
- **Animated icons**: 15 weather codes × 8 frames packed into one `icons.bin` (`ICN1` format)
  in LittleFS — 2.7 MB, loaded frame by frame. Prevents the app binary from exploding.
- **Moon phases**: 16 frames `144×144` in a separate `moon.bin`.
- **Partition layout**: 16 MB flash split into two 6.5 MB OTA slots and a 3.6 MB LittleFS.
- **Weather parsing**: ArduinoJson with a deserialization filter, so a 5-day OWM response
  costs ~2 KB of RAM instead of 40 KB.
- **Watchdogs**: the HTTP fetch blocks the loop for a few seconds; I defer audio and touch
  handling around it to avoid the I2S buffer underrunning.

Repo (MIT): https://github.com/skycast-project/skycast

Gotchas worth knowing: Cyrillic paths break the xtensa linker map file, and pioarduino
(Arduino core 3.x) is required — the stock PlatformIO `espressif32` package will not build this.
