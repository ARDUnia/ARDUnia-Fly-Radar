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

## Range-selection button

Connect a momentary normally-open push button between **GPIO12 (NodeMCU D6)** and **GND**. The firmware configures GPIO12 as `INPUT_PULLUP`; the pressed state is LOW. No external pull-up resistor is needed for this wiring.

## Firmware constants

- Screen size: 240 × 240
- Radar center: (120, 120)
- Radar radius on screen: 96 pixels
- Selectable range values: 10, 20, 30, 40, 50, 100, and 500 km
- API requested-distance cap: 250 nautical miles
- Range selection persisted in EEPROM
- Refresh interval: 15,000 ms
