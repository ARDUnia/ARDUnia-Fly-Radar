# Changelog

## v1.6.4 — 2026-10-20

- Add selectable radar ranges: 10, 20, 30, 40, 50, 100, and 500 km.
- Add GPIO12 (NodeMCU D6) push-button range cycling.
- Persist the selected radar range in EEPROM.
- Center the aircraft count in the display status area.
- Adapt aircraft labels to traffic density; hide labels when more than 20 aircraft are in range.
- Keep the existing ESP8266 NodeMCU + GC9A01 240 × 240 hardware platform, web configuration flow, and ADS-B.fi data source.

### Verification

The project maintainer reports that the physical hardware was tested successfully. No claim of a fresh independent hardware test is made here. Firmware compilation/simulation should be confirmed by the repository's CI workflow or a local Arduino build.
