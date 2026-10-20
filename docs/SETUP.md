# Setup guide

1. Install Arduino IDE and the ESP8266 board platform.
2. Install ArduinoJson (compatible with the sketch's ArduinoJson 6 API), Adafruit GFX Library, and Adafruit GC9A01A.
3. Open `ARDUnia_Fly_Radar_v1_5.ino`.
4. Select your ESP8266 NodeMCU board and port, then upload.
5. Connect to the device's setup Wi-Fi network: `ARDUnia-Fly-Radar-Setup`.
6. Open `http://192.168.4.1`.
7. Scan and select your Wi-Fi network, enter the password, and set the radar center.
8. Save settings. The board restarts and connects using the stored configuration.

The device needs internet access to retrieve aircraft data. City-name lookup also needs internet access. You can use latitude/longitude instead if the lookup service is unavailable.

## Important security note

The setup access point is open, and the current firmware disables TLS certificate verification for HTTPS requests. Configure the device only in a trusted environment and do not expose the setup network as a secure provisioning mechanism.
