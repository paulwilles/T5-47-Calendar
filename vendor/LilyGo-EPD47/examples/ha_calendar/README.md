## Custom Desk Dashboard Firmware

This repository also contains a custom firmware sketch that turns the LilyGo EPD47 into a Home Assistant-powered desk dashboard. It displays a clock, upcoming calendar events from multiple calendars, and is designed to later include weather and to-do information.

### Project Overview

- Target board: **T5-ePaper-S3 (ESP32-S3, 4.7" EPD, 960x540)**  
- Firmware written in C++ using Arduino core / PlatformIO  
- Uses the LilyGo EPD47 driver (`epd_driver.h`)  
- Talks to Home Assistant over its REST API  
- Optimized for partial refresh (minimal flashing and ghosting)

### Home Assistant Integration

The firmware pulls data from Home Assistant entities exposed via the REST API, for example:

- A clock sensor (e.g. `sensor.desk_clock_line`) with a formatted timestamp string  
- One or more calendar entities (e.g. `calendar.aman_outlook_calendar`, `calendar.home_2`)  
- Optional aggregated “desk event” sensors (e.g. `sensor.desk_event_line_1/2/3`) that combine multiple calendars into a sorted list

All the heavy logic (merging calendars, formatting strings, time zone handling, etc.) is done in Home Assistant using Jinja2 templates.  
The EPD47 sketch just calls the HA REST API, reads the sensor states as strings, and renders them in fixed regions.

### Handling Secrets (Wi-Fi, HA Token, Host)

To keep the repo safe for GitHub, secrets are **not** hard-coded in the main sketch. Instead, they live in a separate header that is *not* committed.

Create a file named `secrets.h` alongside your sketch (e.g. `src/ha_calendar.ino`):

```cpp
#pragma once

// WiFi credentials
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"

// Home Assistant
#define HA_HOST_ADDR   "HOME_ASSISTANT_IP"   // or "homeassistant.local"
#define HA_PORT_NUM    8123

// Long-lived access token from Home Assistant
#define HA_TOKEN_VALUE "YOUR_LONG_LIVED_TOKEN_HERE"
```
Then in the main .ino or .cpp file:

```cpp
#include "secrets.h"
```

Finally, add a .gitignore rule:
```
secrets.h
```

### Vertical Orientation Notes

The 4.7” panel is naturally landscape in hardware (960×540), but can be mounted vertically. Two rendering approaches exist:

1. Use native landscape coordinates and simply rotate the physical device.
2. Implement a coordinate-transform wrapper to map portrait → landscape coordinates.

Your current firmware uses fixed rectangular draw areas (`Rect_t`) for:

- Clock
- Event line 1
- Event line 2
- Event line 3

These zones are designed for clean partial refreshes and minimal ghosting.

### Partial Refresh Behavior

The firmware uses partial updates (`epd_clear_area`) instead of full screen refreshes.

**Benefits**

- Faster updates (clock can refresh every minute).
- Minimal flashing.
- Lower power use.
- Less ghosting buildup.

**Logic**

- Clock only refreshes if the time string changed.
- Calendar lines only refresh when Home Assistant returns updated strings.
- No unnecessary redraws → longer panel life and consistent UI.

### Future Enhancements

Planned features:

- 🌤 Weather summary from a `weather.*` entity.
- 📝 To-do list using Home Assistant’s `todo.*` integration.
- 🔋 Battery indicator using ADC.
- 🔌 Deep sleep mode with timed wakeups.
- 🖼 Improved text layout (wrapping, bold titles, ellipsizing).
- 📡 Auto-rotate based on board orientation (IMU or manual config).