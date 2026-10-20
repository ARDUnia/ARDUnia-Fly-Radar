# Hardware notes

## Main components

- ESP8266 NodeMCU development board
- GC9A01 240 × 240 round TFT display
- USB supply appropriate for the board and display

## Pin mapping

| Display signal | ESP8266 GPIO |
|---|---:|
| CS | GPIO4 |
| DC | GPIO5 |
| RST | GPIO16 |
| SCLK / SCL | GPIO14 (hardware SPI clock on standard NodeMCU) |
| MOSI / SDA | GPIO13 (hardware SPI MOSI on standard NodeMCU) |
| VCC | Per module specification |
| GND | GND |

The sketch defines CS, DC, and RST directly. SPI clock and MOSI use the ESP8266 hardware SPI pins. Verify the labels and voltage requirements of the exact display module you have.

## Firmware constants

- Screen size: 240 × 240
- Radar center: (120, 120)
- Radar radius on screen: 96 pixels
- Geographic radius: 40 nautical miles
- Refresh interval: 15,000 ms
