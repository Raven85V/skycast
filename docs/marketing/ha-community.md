# SKYCAST — ESP32-S3 weather station for Home Assistant

*Draft post for the Home Assistant Community forum → «Share your project». English.*

---

Hi everyone,

I wanted a wall weather station that shows what I actually care about — outside temperature,
5-day forecast, indoor readings from my Wi-Fi sensors and a big clock — without opening an app.
So I built **SKYCAST** on a Sunton ESP32-8048S070 board (ESP32-S3, 7" 800×480, 16 MB flash / 8 MB PSRAM).

**What it does**

- Main screen: current weather (OpenWeatherMap by default, Pirate Weather by key), 5-day forecast,
  sunrise/sunset, scrolling ticker with the second screen values.
- Second screen: clock, "Home" block (Wi-Fi sensors), "Street" block, precipitation probability,
  24 h pressure chart, moon phase.
- Separate "Home" screen with three cards and a 3/6/12 h chart — each source is chosen independently in the web UI.
- Clock screen with 7 styles.
- Animated weather icons and moon phases (frames stored in LittleFS, not in the binary).
- Wi-Fi sensors: a second small ESP8266/ESP32 firmware pushes BME280/BMP28/DS18B20 readings over HTTP.
- Web UI for network, keys, brightness, clock; OTA updates.
- Telegram bot for reports and commands.
- Alarm: MP3 from SD over I2S (MAX98357).

**Home Assistant integration — two options**

1. **MQTT Discovery** — enable MQTT on the station, entities appear automatically (sensors, buttons
   "Next screen", "Home screen", "Big clock", "Reboot", brightness number).
2. **HACS integration** (`custom_components/skycast`) — polls `GET /api/state` directly, no MQTT needed.
   Gives the same entities plus 5-day forecast min/max/precipitation, and control buttons.

There is also a small HTTP API (`/api/state`, `/api/cmd?c=screen|home|clock|reboot|brightness&v=NN`)
if you want to wire it into your own automations.

**Links**

- Repo: https://github.com/skycast-project/skycast
- Screenshots: in the README
- License: MIT

It is a hobby project but it has been running 24/7 on my wall for months. Feedback and feature
requests are welcome — especially from people with the same Sunton board, since panel timings
differ between revisions.

Happy to answer questions about the display setup (LovyanGFX RGB panel quirks), MQTT discovery
or the HACS component.
