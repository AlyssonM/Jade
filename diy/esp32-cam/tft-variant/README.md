# ESP32-CAM Jade (Touch + TFT) – Assembly Guide

This guide describes how to assemble the ESP32-CAM Jade variant with a TFT display and a touchscreen controller. It follows the same structure used in the base `diy/esp32-cam` guide and focuses on wiring the display and touch modules.

## Overview
- Board: `ESP32-CAM`
- Display: TFT over SPI (`CS`, `DC`, data/clock lines)
- Touch: SPI controller (e.g., XPT2046 or equivalent)
- Power: 5V input (regulated on-board), common GND

Once flashed, the device can run standalone powered by 5V, or via an external USB/Serial bridge for programming.

## Programmer Pins (CP2104/CP2102)
| Function (Programmer) | ESP32-CAM Pin |
|-----------------------|---------------|
| DTR                   | Not Connected |
| 3V3                   | Not Connected |
| 5V                    | 5V |
| TXD                   | UOT |
| RXD                   | UOR |
| GND                   | GND |

## Power Pins
| Function (Module) | ESP32-CAM Pin |
|-------------------|---------------|
| VCC               | VCC |
| GND               | GND |

## Display Pins (SPI)
Mapping aligned with firmware implementation:

| Function (Display) | ESP32-CAM Pin |
|--------------------|---------------|
| MOSI               | 13 |
| SCLK               | 14 |
| CS                 | 12 |
| DC                 | 2 |
| RST                | Not Connected |
| BLK (Backlight)    | Not Connected |

Note: Many TFT panels label `SCLK` as `SCK` and `MOSI` as `SI`/`SDI`.

## Touch Controller (XPT2046, SPI shared)
The XPT2046 shares the same SPI lines as the TFT; only chip-select and MISO are separate for the touch controller:

| Function (Touch) | ESP32-CAM Pin |
|------------------|---------------|
| TP_CS            | 15 |
| MISO             | 4 |
| MOSI             | 13 (shared with TFT) |
| SCLK             | 14 (shared with TFT) |

Notes:
- Touch runs at lower SPI speed; firmware arbitrates access on the bus.
- No `RST` or `INT` lines are required for basic operation.

## Navigation
Touch-only navigation. No physical button required.

## Connection Diagram
Add or replace the following image with your specific wiring diagram:

![Connection Diagram](../connection_diagram.jpg)

## Build & Flash
ESP-IDF profile for this variant:

```
cp configs/sdkconfig_diycam_tft_esp32-cam.defaults sdkconfig.defaults
idf.py -b 115200 flash monitor
```

Config check:
```
CONFIG_DISPLAY_TOUCHSCREEN=y
```

## Notes
- Ensure common ground between all modules.
- Keep SPI lines short and routed cleanly to reduce noise.
- If you change pins or modules, reflect those changes in `sdkconfig.defaults` or via `idf.py menuconfig`.