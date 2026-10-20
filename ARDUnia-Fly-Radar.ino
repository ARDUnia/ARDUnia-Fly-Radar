
// Core ESP8266 networking, HTTP/HTTPS, web-server, and persistent storage libraries.
// ArduinoJson parses the flight API response; Adafruit libraries drive the round TFT.
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// ARDUnia Fly Radar
// ESP8266 NodeMCU + GC9A01 240x240
// ARDUnia Fly Radar v1.5: centered teal intro/setup screens + Wi-Fi scanning
// Hamidreza Milaninia
// ============================================================

// The GC9A01 display uses hardware SPI for clock/data and dedicated control pins.
// GPIO numbers are used here, rather than board labels such as D1 or D2.
// -------------------- Display pins ---------------------------

#define TFT_CS   4
#define TFT_DC   5
#define TFT_RST 16

Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_RST);

// Screen geometry and radar range are expressed in pixels and nautical miles.
// The radar center is loaded from EEPROM; zero coordinates mean no location is configured.
// -------------------- Radar settings -------------------------

const int SCREEN_W = 240;
const int SCREEN_H = 240;

const int RADAR_X = 120;
const int RADAR_Y = 120;
const int RADAR_R = 96;

const double RADAR_RANGE_NM = 40.0;

double centerLat = 0.0;
double centerLon = 0.0;
bool locationConfigured = false;
const char* PROJECT_VERSION = "1.5";

const unsigned long UPDATE_INTERVAL = 15000UL;

// RGB565 color constants used by the display driver.
// Intro colors are chosen to match the dark teal palette of the web interface.
// -------------------- Colors ---------------------------------

const uint16_t RADAR_BLACK  = 0x0000;
const uint16_t RADAR_GREEN  = 0x07E0;
const uint16_t RADAR_GRAY   = 0x8410;
const uint16_t RADAR_YELLOW = 0xFFE0;
const uint16_t RADAR_RED    = 0xF800;
const uint16_t RADAR_WHITE  = 0xFFFF;

// Intro colors matched to the web interface palette.
const uint16_t INTRO_BG    = 0x10C4; // #101820
const uint16_t INTRO_CARD  = 0x1925; // #19272F
const uint16_t INTRO_TEAL  = 0x3636; // #35C6B0

// The HTTP server provides setup, Wi-Fi selection, location configuration, and diagnostics.
// In setup mode, the ESP8266 creates this access point so a browser can configure it.
// -------------------- Web server -----------------------------

ESP8266WebServer server(80);

const char* CONFIG_AP_SSID = "ARDUnia-Fly-Radar-Setup";
// Setup AP is intentionally open so first-time setup does not require
// a credential embedded in firmware. Connect only in a trusted location.

String wifiSsid;
String wifiPass;
bool setupApStarted = false;

// EEPROM stores the Wi-Fi credentials and radar center across power cycles.
// CONFIG_MAGIC helps distinguish a valid settings record from erased/random EEPROM data.
// -------------------- Persistent settings --------------------

const int EEPROM_SIZE = 256;
const uint32_t CONFIG_MAGIC = 0x504C5231;

struct RadarSettings {
  uint32_t magic;
  char ssid[33];
  char password[65];
  double latitude;
  double longitude;
};

RadarSettings settings;

// secureClient is reused for HTTPS requests to the aircraft data service.
// The update flags control the first fetch and the regular refresh interval.
// -------------------- Network clients ------------------------

WiFiClientSecure secureClient;

unsigned long lastUpdate = 0;
bool firstUpdate = true;

// Read and validate saved settings before attempting to connect to the saved Wi-Fi network.
// Invalid or uninitialized EEPROM is treated as an unconfigured device.
// ============================================================
// EEPROM SETTINGS
// ============================================================

// Returns true when a valid settings record is present in EEPROM.
bool loadSettings() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, settings);

  settings.ssid[sizeof(settings.ssid) - 1] = '\0';
  settings.password[sizeof(settings.password) - 1] = '\0';

  bool validLocation =
    settings.magic == CONFIG_MAGIC &&
    isfinite(settings.latitude) &&
    isfinite(settings.longitude) &&
    settings.latitude >= -90.0 &&
    settings.latitude <= 90.0 &&
    settings.longitude >= -180.0 &&
    settings.longitude <= 180.0;

  if (!validLocation) {
    memset(&settings, 0, sizeof(settings));
    wifiSsid = "";
    wifiPass = "";
    centerLat = 0.0;
    centerLon = 0.0;
    locationConfigured = false;
    Serial.println(F("No valid settings in EEPROM"));
    return false;
  }

  wifiSsid = String(settings.ssid);
  wifiPass = String(settings.password);
  centerLat = settings.latitude;
  centerLon = settings.longitude;
  locationConfigured = true;

  Serial.println(F("Settings loaded from EEPROM"));
  Serial.print(F("Location configured: "));
  Serial.println(locationConfigured ? F("yes") : F("no"));
  Serial.print(F("Wi-Fi SSID configured: "));
  Serial.println(wifiSsid.length() ? F("yes") : F("no"));
  return true;
}

// Draw a centered setup/error screen and show the setup AP name and address when available.
void showSetupMessage(const char* line1,
                      const char* line2,
                      const char* line3) {
  // Centered setup screen. When the setup AP is active, show its SSID
  // and its address prominently so the user can open the web interface.
  tft.fillScreen(INTRO_BG);
  tft.setTextWrap(false);

  tft.setTextSize(2);
  tft.setTextColor(INTRO_TEAL, INTRO_BG);
  tft.setCursor((SCREEN_W - 7 * 12) / 2, 18);
  tft.println(F("ARDUnia"));
  tft.setCursor((SCREEN_W - 9 * 12) / 2, 42);
  tft.println(F("Fly Radar"));

  tft.setTextSize(1);
  tft.setTextColor(RADAR_WHITE, INTRO_BG);
  int w = (int)strlen(line1) * 6;
  tft.setCursor((SCREEN_W - w) / 2, 76);
  tft.println(line1);

  w = (int)strlen(line2) * 6;
  tft.setCursor((SCREEN_W - w) / 2, 94);
  tft.println(line2);

  // Display the configuration web-server SSID when the setup AP is running.
  if (setupApStarted) {
    w = (int)strlen(CONFIG_AP_SSID) * 6;
    tft.setTextColor(INTRO_TEAL, INTRO_BG);
    tft.setCursor((SCREEN_W - w) / 2, 116);
    tft.println(CONFIG_AP_SSID);
  }

  tft.setTextColor(RADAR_WHITE, INTRO_BG);
  w = (int)strlen(line3) * 6;
  tft.setCursor((SCREEN_W - w) / 2, 140);
  tft.println(line3);

  String ipAddress = setupApStarted ? WiFi.softAPIP().toString() : String("192.168.4.1");
  tft.setTextColor(INTRO_TEAL, INTRO_BG);
  tft.setTextSize(2); // one size larger than the surrounding information
  int ipWidth = (int)ipAddress.length() * 12;
  int ipX = (SCREEN_W - ipWidth) / 2;
  if (ipX < 0) ipX = 0;
  tft.setCursor(ipX, 174);
  tft.println(ipAddress);
}

// Boot splash screen: branding is shown without a surrounding card or border.
// The networkInfo argument is optional; when provided, it is displayed as the device IP.
// Boot intro: full-screen dark turquoise background with centered text only.
void showIntroScreen(const String& networkInfo) {
  const uint16_t INTRO_DARK_TEAL = 0x0A8A; // dark turquoise, approx #085152
  tft.fillScreen(INTRO_DARK_TEAL);
  tft.setTextWrap(false);

  tft.setTextColor(RADAR_WHITE, INTRO_DARK_TEAL);
  tft.setTextSize(2);
  tft.setCursor((SCREEN_W - 7 * 12) / 2, 38);
  tft.print(F("ARDUnia"));

  tft.setTextColor(0xD7FF, INTRO_DARK_TEAL); // light turquoise for contrast
  tft.setCursor((SCREEN_W - 9 * 12) / 2, 68);
  tft.print(F("Fly Radar"));

  tft.setTextSize(1);
  tft.setTextColor(RADAR_WHITE, INTRO_DARK_TEAL);
  tft.setCursor((SCREEN_W - 11 * 6) / 2, 101);
  tft.print(F("for ESP8266"));

  if (networkInfo.length() > 0) {
    tft.setTextColor(RADAR_WHITE, INTRO_DARK_TEAL);
    tft.setTextSize(1);
    const char* ipLabel = "DEVICE IP";
    tft.setCursor((SCREEN_W - 9 * 6) / 2, 137);
    tft.print(ipLabel);

    tft.setTextColor(0xD7FF, INTRO_DARK_TEAL);
    tft.setTextSize(2);
    int textWidth = (int)networkInfo.length() * 12;
    int x = (SCREEN_W - textWidth) / 2;
    if (x < 0) x = 0;
    tft.setCursor(x, 158);
    tft.print(networkInfo);
  }
}

// Validate and persist the submitted network credentials and radar coordinates.
// Runtime variables are updated only after the EEPROM write succeeds.
bool saveSettings(
  const String& ssid,
  const String& password,
  double latitude,
  double longitude
) {
  if (
    ssid.length() > 32 ||
    password.length() > 64 ||
    !isfinite(latitude) ||
    !isfinite(longitude) ||
    latitude < -90.0 ||
    latitude > 90.0 ||
    longitude < -180.0 ||
    longitude > 180.0
  ) {
    return false;
  }

  memset(&settings, 0, sizeof(settings));

  ssid.toCharArray(settings.ssid, sizeof(settings.ssid));
  password.toCharArray(
    settings.password,
    sizeof(settings.password)
  );

  settings.latitude = latitude;
  settings.longitude = longitude;
  settings.magic = CONFIG_MAGIC;

  EEPROM.put(0, settings);

  if (!EEPROM.commit()) {
    return false;
  }

  wifiSsid = ssid;
  wifiPass = password;

  centerLat = latitude;
  centerLon = longitude;
  locationConfigured = true;

  return true;
}

// Build the setup page as a compact HTML string to fit the ESP8266's memory limits.
// The page contains Wi-Fi scanning/selection, location entry, and network reset controls.
// ============================================================
// WEB PAGE
// ============================================================

// Escape HTML special characters before inserting user or network text into a page.
String htmlEscape(const String& value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '&') out += F("&amp;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '"') out += F("&quot;");
    else if (c == '\'') out += F("&#39;");
    else out += c;
  }
  return out;
}

// Generate the main configuration page, optionally including a status/error message.
String makePage(const String& message = "") {
  String page;
  page.reserve(4400);
  page += F(
    "<!doctype html><html lang='fa' dir='rtl'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ARDUnia Fly Radar</title><style>*{box-sizing:border-box}"
    "body{margin:0;background:#101820;color:#e7f4f2;font-family:Tahoma,Arial,sans-serif;padding:14px}"
    ".wrap{max-width:650px;margin:18px auto}.card{background:#19272f;border:1px solid #355158;border-radius:18px;padding:20px;margin-bottom:14px}"
    "h1{font-size:24px;margin:0 0 8px;color:#35c6b0}h2{font-size:18px;color:#35c6b0;margin:0 0 14px}"
    ".sub,.hint{color:#a8c0bf;font-size:13px;line-height:1.9}.nav{display:flex;gap:10px;flex-wrap:wrap;margin:16px 0}"
    ".nav a,.btn{display:inline-block;text-decoration:none;text-align:center;padding:11px 14px;border:0;border-radius:10px;background:#35c6b0;color:#101820;font-weight:bold;cursor:pointer}"
    "label{display:block;margin:14px 0 7px;font-size:14px}input{width:100%;padding:12px;border-radius:10px;border:1px solid #355158;background:#101820;color:#e7f4f2;font-size:16px}"
    "input:focus{outline:1px solid #35c6b0}.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}.msg{padding:12px;background:#243a40;border-radius:10px;margin-bottom:14px;color:#35c6b0;line-height:1.8}"
    ".danger{background:#b83b4b;color:white}.line{height:1px;background:#355158;margin:20px 0}.hidden{display:none}"
    "table{width:100%;border-collapse:collapse}td{padding:9px 4px;border-bottom:1px solid #355158;vertical-align:top}td:first-child{color:#a8c0bf;width:45%}"
    "@media(max-width:450px){.grid{grid-template-columns:1fr}.card{padding:15px}}"
    "</style></head><body><div class='wrap'><div class='card'><h1>ARDUnia Fly Radar</h1>"
    "<p class='sub'>تنظیم شبکه و موقعیت رادار</p><div class='nav'><a href='/'>تنظیمات</a><a href='/info'>اطلاعات پروژه (Info)</a></div>"
  );
  if (message.length()) {
    page += F("<div class='msg'>"); page += htmlEscape(message); page += F("</div>");
  }
  page += F("<form method='POST' action='/save'><h2>تنظیم شبکه وای فای</h2>");
  page += F("<label>انتخاب شبکه وای فای (SSID)</label><div style='display:flex;gap:8px;align-items:stretch'>");
  page += F("<select id='ssid' name='ssid' required style='flex:1;min-width:0;padding:12px;border-radius:10px;border:1px solid #355158;background:#101820;color:#e7f4f2;font-size:16px'>");
  if (wifiSsid.length()) {
    page += F("<option selected value='"); page += htmlEscape(wifiSsid);
    page += F("'>شبکه ذخیره شده: "); page += htmlEscape(wifiSsid); page += F("</option>");
  } else {
    page += F("<option value='' selected disabled>برای دریافت فهرست، اسکن را بزنید</option>");
  }
  page += F("</select><button class='btn' type='button' onclick='scanNetworks()' style='white-space:nowrap;padding:10px'>اسکن شبکه ها</button></div>");
  page += F("<p id='scanStatus' class='hint'>برای مشاهده شبکه های اطراف، دکمه اسکن شبکه ها را بزنید.</p>");
  page += F("<label>رمز شبکه</label><input name='password' type='password' maxlength='64' autocomplete='new-password' placeholder='برای حفظ رمز فعلی، خالی بگذارید'>");
  page += F("<p class='hint'>اگر SSID فعلی را حفظ کنید و رمز را خالی بگذارید، رمز ذخیره شده حفظ می شود. برای شبکه بدون رمز، SSID جدید را وارد و رمز را خالی بگذارید.</p>");
  page += F("<div class='line'></div><h2>موقعیت مرکز رادار</h2>");
  page += F("<label><input style='width:auto' type='radio' name='locationMode' value='coords' checked onchange='toggleLocation()'> ورود مختصات جغرافیایی</label>");
  page += F("<div id='coords'><div class='grid'><div><label>عرض جغرافیایی (Latitude)</label><input name='lat' type='number' step='any' min='-90' max='90' value='");
  if (locationConfigured) page += String(centerLat, 6);
  page += F("' placeholder='مثلا 25.25278'></div><div><label>طول جغرافیایی (Longitude)</label><input name='lon' type='number' step='any' min='-180' max='180' value='");
  if (locationConfigured) page += String(centerLon, 6);
  page += F("' placeholder='مثلا 55.36444'></div></div></div>");
  page += F("<label><input style='width:auto' type='radio' name='locationMode' value='city' onchange='toggleLocation()'> تعیین موقعیت با نام شهر</label>");
  page += F("<div id='city' class='hidden'><label>نام شهر و کشور (ترجیحا هر دو)</label><input name='city' maxlength='100' placeholder='مثلا Mashhad, Iran'><p class='hint'>مختصات از سرویس Nominatim / OpenStreetMap دریافت می شود؛ برای استفاده از این امکان، دستگاه باید به اینترنت دسترسی داشته باشد.</p></div>");
  page += F("<button class='btn' type='submit' style='width:100%;margin-top:20px'>ذخیره تنظیمات و راه اندازی مجدد</button></form>");
  page += F("<div class='line'></div><form method='POST' action='/clear-network' onsubmit=\"return confirm('اطلاعات شبکه ذخیره شده پاک شود؟')\"><button class='btn danger' type='submit' style='width:100%;margin-top:0'>پاک کردن اطلاعات شبکه</button><p class='hint'>این کار فقط نام و رمز وای فای را پاک می کند و موقعیت رادار باقی می ماند. دستگاه سپس نقطه دسترسی تنظیمات را فعال می کند.</p></form>");
  page += F("<p class='hint'>شعاع رادار: ۴۰ مایل دریایی | به روزرسانی: هر ۱۵ ثانیه</p></div></div>");
  page += F("<script>function toggleLocation(){let m=document.querySelector('input[name=locationMode]:checked').value;document.getElementById('coords').classList.toggle('hidden',m!=='coords');document.getElementById('city').classList.toggle('hidden',m!=='city');}function scanNetworks(){let sel=document.getElementById('ssid'),status=document.getElementById('scanStatus'),old=sel.value;status.textContent='در حال اسکن شبکه های اطراف...';fetch('/scan-networks',{cache:'no-store'}).then(r=>{if(!r.ok)throw new Error('scan');return r.json()}).then(items=>{sel.innerHTML='';if(!items.length){let o=document.createElement('option');o.value='';o.textContent='شبکه ای پیدا نشد؛ دوباره اسکن کنید';o.disabled=true;o.selected=true;sel.appendChild(o);}items.forEach(n=>{let o=document.createElement('option');o.value=n.ssid;o.textContent=n.ssid+'  ('+n.rssi+' dBm)'+(n.secure?' 🔒':'  [باز]');sel.appendChild(o);});if(old){let found=false;for(let i=0;i<sel.options.length;i++){if(sel.options[i].value===old){sel.selectedIndex=i;found=true;break;}}if(!found){let o=document.createElement('option');o.value=old;o.textContent=old+' (ذخیره شده؛ در اسکن دیده نشد)';sel.insertBefore(o,sel.firstChild);sel.value=old;}}status.textContent='تعداد شبکه های پیدا شده: '+items.length;}).catch(()=>{status.textContent='اسکن ناموفق بود؛ دوباره تلاش کنید.';});}window.addEventListener('load',scanNetworks);</script></body></html>");
  return page;
}

// Percent-encode a city name so it can safely be included in a URL query string.
String urlEncode(const String& input) {
  const char hex[] = "0123456789ABCDEF";
  String out;
  out.reserve(input.length() * 3);
  for (size_t i = 0; i < input.length(); i++) {
    uint8_t c = (uint8_t)input[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else if (c == ' ') {
      out += '+';
    } else {
      out += '%'; out += hex[c >> 4]; out += hex[c & 0x0F];
    }
  }
  return out;
}

bool parseNumber(const String& input, double& result);

// Resolve a city name to coordinates through the OpenStreetMap Nominatim search API.
// Internet connectivity is required; the first returned result is used if valid.
bool geocodeCity(const String& city, double& latitude, double& longitude) {
  if (WiFi.status() != WL_CONNECTED || city.length() == 0) return false;
  WiFiClientSecure geoClient;
  geoClient.setInsecure();
  HTTPClient http;
  String url = "https://nominatim.openstreetmap.org/search?q=";
  url += urlEncode(city);
  url += "&format=json&limit=1";
  http.setTimeout(12000);
  http.setUserAgent("ARDUnia-Fly-Radar/1.5 (ESP8266 project)");
  if (!http.begin(geoClient, url)) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }
  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err || !doc.is<JsonArray>() || doc.as<JsonArray>().size() == 0) return false;
  JsonObject first = doc[0];
  String latText = first["lat"] | "";
  String lonText = first["lon"] | "";
  return parseNumber(latText, latitude) && parseNumber(lonText, longitude) &&
         latitude >= -90.0 && latitude <= 90.0 &&
         longitude >= -180.0 && longitude <= 180.0;
}

// Shared input validation helpers used by the web form handlers.
// ============================================================
// INPUT VALIDATION
// ============================================================

// Parse a complete finite numeric string; reject empty, oversized, or partially numeric input.
bool parseNumber(const String& input, double& result) {
  if (input.length() == 0 || input.length() >= 32) {
    return false;
  }

  char buffer[32];
  input.toCharArray(buffer, sizeof(buffer));

  char* endPtr = nullptr;
  result = strtod(buffer, &endPtr);

  return (
    endPtr != buffer &&
    *endPtr == '\0' &&
    isfinite(result)
  );
}

// HTTP route handlers below connect browser requests to settings, scanning, and diagnostics.
// ============================================================
// WEB SERVER ROUTES
// ============================================================

// Show firmware, chip, memory, flash, Wi-Fi, radar-center, uptime, and display information.
// ESP8266 does not expose a general-purpose board temperature reading through this API.
String infoPage() {
  String page;
  page.reserve(2600);
  page += F("<!doctype html><html lang='fa' dir='rtl'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>ARDUnia Fly Radar - Info</title><style>body{margin:0;padding:14px;background:#101820;color:#e7f4f2;font-family:Tahoma,Arial}.wrap{max-width:650px;margin:18px auto}.card{background:#19272f;border:1px solid #355158;border-radius:18px;padding:20px}h1,h2{color:#35c6b0}a,.btn{display:inline-block;padding:11px 14px;border-radius:10px;background:#35c6b0;color:#101820;text-decoration:none;font-weight:bold}table{width:100%;border-collapse:collapse}td{padding:9px 4px;border-bottom:1px solid #355158;overflow-wrap:anywhere}td:first-child{color:#a8c0bf;width:45%}.hint{color:#a8c0bf;font-size:12px;line-height:1.8}</style></head><body><div class='wrap'><div class='card'><h1>ARDUnia Fly Radar</h1><h2>اطلاعات پروژه و سخت افزار</h2><p><a href='/'>بازگشت به تنظیمات</a></p><table>");
  page += F("<tr><td>نسخه نرم افزار</td><td>"); page += PROJECT_VERSION; page += F("</td></tr>");
  page += F("<tr><td>برد / تراشه اصلی</td><td>ماژول ESP8266 (شناسه تراشه: "); page += String(ESP.getChipId(), HEX); page += F(")</td></tr>");
  page += F("<tr><td>فرکانس پردازنده</td><td>"); page += String(ESP.getCpuFreqMHz()); page += F(" MHz</td></tr>");
  page += F("<tr><td>حافظه Heap آزاد</td><td>"); page += String(ESP.getFreeHeap()); page += F(" bytes</td></tr>");
  page += F("<tr><td>بزرگ ترین بلوک آزاد Heap</td><td>"); page += String(ESP.getMaxFreeBlockSize()); page += F(" bytes</td></tr>");
  page += F("<tr><td>تکه تکه شدن Heap</td><td>"); page += String(ESP.getHeapFragmentation()); page += F("%</td></tr>");
  page += F("<tr><td>ظرفیت فلش</td><td>"); page += String(ESP.getFlashChipRealSize() / 1024); page += F(" KB</td></tr>");
  page += F("<tr><td>حجم برنامه فعلی</td><td>"); page += String(ESP.getSketchSize()); page += F(" bytes</td></tr>");
  page += F("<tr><td>فضای قابل استفاده برای برنامه / OTA</td><td>"); page += String(ESP.getFreeSketchSpace()); page += F(" bytes</td></tr>");
  page += F("<tr><td>وضعیت وای فای</td><td>");
  if (WiFi.status() == WL_CONNECTED) page += F("متصل"); else page += F("متصل نیست / حالت تنظیمات");
  page += F("</td></tr><tr><td>نام شبکه</td><td>");
  if (WiFi.status() == WL_CONNECTED) page += htmlEscape(WiFi.SSID()); else page += F("-");
  page += F("</td></tr><tr><td>IP دستگاه</td><td>");
  if (WiFi.status() == WL_CONNECTED) page += WiFi.localIP().toString(); else page += WiFi.softAPIP().toString();
  page += F("</td></tr><tr><td>قدرت سیگنال</td><td>");
  if (WiFi.status() == WL_CONNECTED) { page += String(WiFi.RSSI()); page += F(" dBm"); } else page += F("در حالت نقطه دسترسی");
  page += F("</td></tr><tr><td>مرکز رادار</td><td>");
  if (locationConfigured) { page += String(centerLat, 6); page += F(", "); page += String(centerLon, 6); }
  else page += F("تنظیم نشده");
  page += F("</td></tr><tr><td>مدت روشن بودن</td><td>");
  unsigned long sec = millis() / 1000UL;
  page += String(sec / 3600UL); page += F(" ساعت "); page += String((sec % 3600UL) / 60UL); page += F(" دقیقه</td></tr>");
  page += F("<tr><td>دمای تراشه</td><td>از API استاندارد این برد قابل خواندن نیست؛ حسگر دمای خارجی نصب نشده است.</td></tr>");
  page += F("<tr><td>نمایشگر</td><td>GC9A01A گرد، 240 × 240، SPI</td></tr><tr><td>منبع اطلاعات پرواز</td><td>ADS-B.fi API</td></tr></table><p class='hint'>مدل دقیق برد (مثلا NodeMCU) از شناسه تراشه به تنهایی قابل تشخیص قطعی نیست. حافظه Heap با حافظه فلش متفاوت است.</p></div></div></body></html>");
  return page;
}

// Scan nearby Wi-Fi networks and return their SSID, signal strength, and security status as JSON.
// AP+STA mode allows scanning while keeping the setup access point available.
void handleScanNetworks() {
  // Keep the setup access point running while enabling the station radio
  // if the device is currently in AP-only setup mode.
  if (WiFi.getMode() == WIFI_AP) {
    WiFi.mode(WIFI_AP_STA);
    delay(100);
  }

  int count = WiFi.scanNetworks(false, true);
  DynamicJsonDocument doc(4096);
  JsonArray list = doc.to<JsonArray>();

  if (count > 0) {
    for (int i = 0; i < count; i++) {
      String name = WiFi.SSID(i);
      if (name.length() == 0) continue;
      JsonObject item = list.createNestedObject();
      item["ssid"] = name;
      item["rssi"] = WiFi.RSSI(i);
      item["secure"] = (WiFi.encryptionType(i) != ENC_TYPE_NONE);
      yield();
    }
  }

  String response;
  serializeJson(doc, response);
  WiFi.scanDelete();
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", response);
}

// Serve the main configuration page.
void handleRoot() {
  server.send(200, "text/html; charset=utf-8", makePage());
}

// Serve the read-only project and hardware information page.
void handleInfo() {
  server.send(200, "text/html; charset=utf-8", infoPage());
}

// Validate submitted Wi-Fi and location values, geocode a city if requested, save settings, and reboot.
void handleSave() {
  String newSsid = server.arg("ssid");
  String newPassword = server.arg("password");
  String locationMode = server.arg("locationMode");
  double newLat = 0.0, newLon = 0.0;

  if (newSsid.length() == 0 || newSsid.length() > 32 || newPassword.length() > 64) {
    server.send(400, "text/html; charset=utf-8", makePage("نام شبکه الزامی است و طول نام شبکه یا رمز نباید از حد مجاز بیشتر باشد. برای حذف شبکه از دکمه پاک کردن اطلاعات شبکه استفاده کنید."));
    return;
  }

  if (newPassword.length() == 0 && newSsid == wifiSsid && wifiPass.length() > 0) {
    newPassword = wifiPass;
  }

  if (locationMode == "city") {
    String city = server.arg("city");

    // If the device is currently in setup AP mode, try connecting to the
    // newly supplied Wi-Fi while keeping the setup AP available.
    if (WiFi.status() != WL_CONNECTED && newSsid.length() > 0) {
      WiFi.mode(WIFI_AP_STA);
      WiFi.begin(newSsid.c_str(), newPassword.c_str());
      unsigned long connectStart = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - connectStart < 15000UL) {
        delay(250);
        yield();
      }
    }

    if (city.length() == 0 || !geocodeCity(city, newLat, newLon)) {
      server.send(400, "text/html; charset=utf-8", makePage("شهر پیدا نشد یا اتصال اینترنت برقرار نشد. برای جستجوی شهر، نام شبکه و رمز معتبر را وارد کنید؛ یا مختصات را دستی ثبت کنید."));
      return;
    }
  } else {
    if (!parseNumber(server.arg("lat"), newLat) ||
        !parseNumber(server.arg("lon"), newLon) ||
        newLat < -90.0 || newLat > 90.0 ||
        newLon < -180.0 || newLon > 180.0) {
      server.send(400, "text/html; charset=utf-8", makePage("مختصات وارد شده معتبر نیست."));
      return;
    }
  }

  if (!saveSettings(newSsid, newPassword, newLat, newLon)) {
    server.send(500, "text/html; charset=utf-8", makePage("ذخیره تنظیمات در حافظه ناموفق بود."));
    return;
  }

  server.send(200, "text/html; charset=utf-8",
    "<!doctype html><html lang='fa' dir='rtl'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><body style='background:#101820;color:#e7f4f2;font-family:Tahoma;text-align:center;padding:35px'><h2 style='color:#35c6b0'>تنظیمات ذخیره شد</h2><p>دستگاه در حال راه اندازی مجدد است.</p></body></html>");
  delay(700);
  ESP.restart();
}

// Clear only the stored Wi-Fi credentials while preserving the configured radar location.
void handleClearNetwork() {
  if (!locationConfigured) {
    server.send(400, "text/html; charset=utf-8", makePage("ابتدا موقعیت رادار را ذخیره کنید؛ سپس اطلاعات شبکه را پاک کنید."));
    return;
  }
  settings.magic = CONFIG_MAGIC;
  memset(settings.ssid, 0, sizeof(settings.ssid));
  memset(settings.password, 0, sizeof(settings.password));
  settings.latitude = centerLat;
  settings.longitude = centerLon;
  EEPROM.put(0, settings);
  if (!EEPROM.commit()) {
    server.send(500, "text/html; charset=utf-8", makePage("پاک کردن اطلاعات شبکه از EEPROM ناموفق بود."));
    return;
  }
  wifiSsid = "";
  wifiPass = "";
  server.send(200, "text/html; charset=utf-8", "<!doctype html><html lang='fa' dir='rtl'><meta charset='utf-8'><body style='background:#101820;color:#e7f4f2;font-family:Tahoma;text-align:center;padding:35px'><h2 style='color:#35c6b0'>اطلاعات شبکه پاک شد</h2><p>دستگاه در حال راه اندازی مجدد است.</p></body></html>");
  delay(700);
  ESP.restart();
}

// Register all HTTP endpoints and start listening for browser requests on port 80.
void startWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/scan-networks", HTTP_GET, handleScanNetworks);
  server.on("/info", HTTP_GET, handleInfo);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/clear-network", HTTP_POST, handleClearNetwork);
  server.onNotFound([]() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });
  server.begin();
  Serial.println(F("Web server started"));
}

// Connect using saved credentials when possible; otherwise start the configuration access point.
// ============================================================
// WI-FI CONNECTION AND SETUP ACCESS POINT
// ============================================================

// Attempt station-mode connection for a limited time, then fall back to setup AP mode.
void startNetworkAndWeb(bool hasSavedSettings) {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);

  if (hasSavedSettings && wifiSsid.length() > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
    Serial.print(F("Connecting to saved Wi-Fi"));
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000UL) {
      delay(250);
      yield();
      Serial.print('.');
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println();
      Serial.print(F("Device IP: "));
      Serial.println(WiFi.localIP());
      startWebServer();
      return;
    }
    Serial.println();
    Serial.println(F("Saved Wi-Fi connection failed"));
  } else {
    Serial.println(F("No saved Wi-Fi credentials"));
  }

  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  setupApStarted = WiFi.softAP(CONFIG_AP_SSID);
  if (setupApStarted) {
    Serial.println(F("Configuration access point started (open network)"));
    Serial.print(F("SSID: ")); Serial.println(CONFIG_AP_SSID);
    Serial.print(F("Address: ")); Serial.println(WiFi.softAPIP());
  } else {
    Serial.println(F("Failed to start setup AP"));
  }
  startWebServer();
}

// Drawing routines translate the fetched aircraft positions into the round radar display.
// ============================================================
// RADAR DRAWING
// ============================================================

// Clear and redraw the radar background, range rings, crosshair, compass labels, and title.
void drawRadarBase() {
  tft.fillScreen(RADAR_BLACK);

  // Radar circles
  for (int r = 24; r <= RADAR_R; r += 24) {
    tft.drawCircle(
      RADAR_X,
      RADAR_Y,
      r,
      RADAR_GREEN
    );
  }

  // Crosshair
  tft.drawFastHLine(
    RADAR_X - RADAR_R,
    RADAR_Y,
    RADAR_R * 2,
    RADAR_GREEN
  );

  tft.drawFastVLine(
    RADAR_X,
    RADAR_Y - RADAR_R,
    RADAR_R * 2,
    RADAR_GREEN
  );

  // Center point
  tft.fillCircle(
    RADAR_X,
    RADAR_Y,
    3,
    RADAR_GREEN
  );

  // Cardinal directions
  tft.setTextSize(1);
  tft.setTextColor(RADAR_GREEN, RADAR_BLACK);

  tft.setCursor(RADAR_X - 3, RADAR_Y - RADAR_R - 9);
  tft.print('N');

  tft.setCursor(RADAR_X - 3, RADAR_Y + RADAR_R + 2);
  tft.print('S');

  tft.setCursor(RADAR_X - RADAR_R - 10, RADAR_Y - 3);
  tft.print('W');

  tft.setCursor(RADAR_X + RADAR_R + 3, RADAR_Y - 3);
  tft.print('E');

  // Centered project title, slightly lower for easier reading.
  tft.setTextColor(RADAR_WHITE, RADAR_BLACK);
  tft.setTextSize(1);
  tft.setCursor((SCREEN_W - 17 * 6) / 2, 10);
  tft.print(F("ARDUnia Fly Radar"));
}

// Project an aircraft's latitude/longitude onto the radar and draw its position and heading.
void drawAircraft(
  double latitude,
  double longitude,
  double track
) {
  const double DEG_TO_RAD_VALUE =
    0.017453292519943295;

  // Convert geographic position to nautical miles.
  double yNm = (latitude - centerLat) * 60.0;

  double xNm =
    (longitude - centerLon) *
    60.0 *
    cos(centerLat * DEG_TO_RAD_VALUE);

  double distanceNm = sqrt(
    xNm * xNm + yNm * yNm
  );

  if (distanceNm > RADAR_RANGE_NM) {
    return;
  }

  // Convert nautical miles to screen coordinates.
  int px = RADAR_X +
    (int)round(
      xNm / RADAR_RANGE_NM * RADAR_R
    );

  int py = RADAR_Y -
    (int)round(
      yNm / RADAR_RANGE_NM * RADAR_R
    );

  if (
    px < 2 || px >= SCREEN_W - 2 ||
    py < 2 || py >= SCREEN_H - 2
  ) {
    return;
  }

  // Aircraft dot
  tft.fillCircle(
    px,
    py,
    2,
    RADAR_YELLOW
  );

  // Draw heading only when track is available.
  if (!isfinite(track) || track < 0.0 || track > 360.0) {
    return;
  }

  double angle = track * DEG_TO_RAD_VALUE;

  int dx = (int)round(sin(angle) * 10.0);
  int dy = (int)round(-cos(angle) * 10.0);

  int tipX = px + dx;
  int tipY = py + dy;

  tft.drawLine(
    px, py,
    tipX, tipY,
    RADAR_YELLOW
  );

  // Arrowhead
  double leftAngle = (track + 150.0) * DEG_TO_RAD_VALUE;
  double rightAngle = (track - 150.0) * DEG_TO_RAD_VALUE;

  int leftX = tipX +
    (int)round(sin(leftAngle) * 4.0);

  int leftY = tipY -
    (int)round(cos(leftAngle) * 4.0);

  int rightX = tipX +
    (int)round(sin(rightAngle) * 4.0);

  int rightY = tipY -
    (int)round(cos(rightAngle) * 4.0);

  tft.drawLine(
    tipX, tipY,
    leftX, leftY,
    RADAR_YELLOW
  );

  tft.drawLine(
    tipX, tipY,
    rightX, rightY,
    RADAR_YELLOW
  );
}

// Update the aircraft count area at the bottom of the screen.
void drawAircraftCount(int count) {
  tft.fillRect(
    65,
    226,
    110,
    12,
    RADAR_BLACK
  );

  tft.setTextSize(1);
  tft.setTextColor(RADAR_YELLOW, RADAR_BLACK);
  tft.setCursor(88, 228);
  tft.print(F("Aircraft: "));
  tft.print(count);
}

// Request nearby aircraft from ADS-B.fi, parse only required fields, and refresh the radar.
// A failed or empty response leaves the previous valid radar picture visible.
// ============================================================
// FETCH AIRCRAFT FROM ADS-B.FI
// ============================================================

// Returns true when a valid aircraft response is processed successfully.
bool fetchAircraft() {
  if (!locationConfigured) {
    Serial.println(F("Radar location is not configured"));
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("Wi-Fi disconnected"));
    return false;
  }

  String url =
    "https://opendata.adsb.fi/api/v3/lat/";

  url += String(centerLat, 5);
  url += "/lon/";
  url += String(centerLon, 5);
  url += "/dist/40";

  secureClient.setInsecure();

  HTTPClient http;
  http.setTimeout(15000);
  http.useHTTP10(true);

  Serial.println();
  Serial.println(F("Fetching aircraft data..."));

  if (!http.begin(secureClient, url)) {
    Serial.println(F("HTTP begin failed"));
    return false;
  }

  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.print(F("HTTP error: "));
    Serial.println(httpCode);

    http.end();
    return false;
  }

  // Parse only the required JSON fields to save RAM.
  StaticJsonDocument<128> filter;
  filter["ac"][0]["lat"] = true;
  filter["ac"][0]["lon"] = true;
  filter["ac"][0]["track"] = true;

  DynamicJsonDocument doc(8192);

  DeserializationError error = deserializeJson(
    doc,
    http.getStream(),
    DeserializationOption::Filter(filter)
  );

  http.end();

  if (error) {
    Serial.print(F("JSON parse failed: "));
    Serial.println(error.c_str());
    return false;
  }

  JsonArray aircraft = doc["ac"].as<JsonArray>();

  if (aircraft.isNull()) {
    Serial.println(F("JSON has no aircraft array"));
    return false;
  }

  // Validate the response before clearing the previous display.
  int validCount = 0;

  for (JsonObject ac : aircraft) {
    if (
      ac["lat"].is<double>() &&
      ac["lon"].is<double>()
    ) {
      double lat = ac["lat"].as<double>();
      double lon = ac["lon"].as<double>();

      if (
        isfinite(lat) &&
        isfinite(lon) &&
        lat >= -90.0 && lat <= 90.0 &&
        lon >= -180.0 && lon <= 180.0
      ) {
        validCount++;
      }
    }
  }

  // Keep the previous screen if the response has no valid data.
  if (validCount == 0) {
    Serial.println(F("No valid aircraft records"));
    return false;
  }

  drawRadarBase();

  int plottedCount = 0;

  for (JsonObject ac : aircraft) {
    if (
      !ac["lat"].is<double>() ||
      !ac["lon"].is<double>()
    ) {
      continue;
    }

    double lat = ac["lat"].as<double>();
    double lon = ac["lon"].as<double>();

    if (
      !isfinite(lat) ||
      !isfinite(lon) ||
      lat < -90.0 || lat > 90.0 ||
      lon < -180.0 || lon > 180.0
    ) {
      continue;
    }

    double track = -1.0;

    if (ac["track"].is<double>()) {
      track = ac["track"].as<double>();
    }

    double latDeltaNm = (lat - centerLat) * 60.0;

    double lonDeltaNm =
      (lon - centerLon) *
      60.0 *
      cos(centerLat * 0.017453292519943295);

    double distanceNm = sqrt(
      latDeltaNm * latDeltaNm +
      lonDeltaNm * lonDeltaNm
    );

    if (distanceNm > RADAR_RANGE_NM) {
      continue;
    }

    drawAircraft(lat, lon, track);
    plottedCount++;
  }

  drawAircraftCount(plottedCount);

  Serial.print(F("Aircraft in response: "));
  Serial.println(validCount);

  Serial.print(F("Aircraft plotted: "));
  Serial.println(plottedCount);

  return true;
}

// Arduino startup: initialize serial/display, load EEPROM settings, start networking, and show the intro.
// ============================================================
// SETUP
// ============================================================

// Runs once after power-up or reset.
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("================================"));
  Serial.println(F("ARDUnia Fly Radar"));
  Serial.print(F("Version: ")); Serial.println(PROJECT_VERSION);
  Serial.println(F("================================"));

  tft.begin(10000000);
  tft.setRotation(0);
  tft.invertDisplay(true);

  // Show branded intro immediately while EEPROM and Wi-Fi are initialized.
  showIntroScreen("Connecting...");

  bool hasSavedSettings = loadSettings();
  startNetworkAndWeb(hasSavedSettings);

  if (WiFi.status() == WL_CONNECTED) {
    String ipAddress = WiFi.localIP().toString();
    Serial.print(F("Device IP: "));
    Serial.println(ipAddress);
    showIntroScreen(ipAddress);
    delay(5000);

    if (locationConfigured) {
      drawRadarBase();
      drawAircraftCount(0);
    } else {
      showSetupMessage("Radar location required", "Open the web settings", "Configure a location");
    }
  } else if (setupApStarted) {
    String ipAddress = WiFi.softAPIP().toString();
    showIntroScreen(ipAddress);
    delay(5000);
    showSetupMessage("Wi-Fi setup required", "Connect to ARDUnia-Fly-Radar", "Open setup network");
  } else {
    showSetupMessage("Setup AP failed", "Restart the device", "Check power supply");
  }

  Serial.print(F("Radar location configured: "));
  Serial.println(locationConfigured ? F("yes") : F("no"));
  lastUpdate = millis() - UPDATE_INTERVAL;
  firstUpdate = true;
}

// Main service loop: keep the web interface responsive and refresh aircraft data on schedule.
// ============================================================
// MAIN LOOP
// ============================================================

// Runs continuously after setup(); avoid long blocking work here where possible.
void loop() {
  // Handle browser requests as frequently as possible.
  server.handleClient();

  unsigned long now = millis();

  if (
    WiFi.status() == WL_CONNECTED && locationConfigured &&
    (firstUpdate || now - lastUpdate >= UPDATE_INTERVAL)
  ) {
    firstUpdate = false;
    lastUpdate = now;

    fetchAircraft();
  }

  // Give the ESP8266 background tasks time to run.
  yield();
}

