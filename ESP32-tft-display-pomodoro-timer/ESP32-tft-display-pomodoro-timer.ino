// ESP32-C6 Pomodoro + TFT-LCD display
// Display info: https://fritzenlab.net/2025/02/10/st7735-tft-lcd-display/
// AHT21 sensor: https://fritzenlab.com.br/2026/05/21/comparando-sensores-ambientais-medir-eco2-e-tvoc/

// ===== Includes =====
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>
#include "secrets.h"
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_AHTX0.h>
	
#define button 18
#define buzzer 19
#define LED 15
#define TFT_CS        3
#define TFT_RST        5 // Or set to -1 and connect to Arduino RESET pin
#define TFT_DC         4
#define SPI_SCK   6
#define SPI_MOSI  7
#define SPI_MISO  0

#define mygreen 0xBF66

// For 1.44" and 1.8" TFT with ST7735 use:
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
Adafruit_AHTX0 aht;

sensors_event_t humidity, temp;

// ===== Network / AP =====
WebServer server(80);
Preferences prefs;

const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* mycity = MY_CITY;
const char* timekey= TIMEZONEDB_API_KEY;
const char* networkName   = AP_NETWORK_NAME;

bool isConnected       = false;
bool startConfigPortal = false;
bool portalRunning     = false;

const unsigned long WIFI_RESTART_TIMEOUT_MS = 300000;
unsigned long wifiDownSince = 0;
bool wifiWasDown            = false;
// ===== Internet watchdog =====
unsigned long lastApiSuccess = 0;   // millis() of the last API call that worked
bool wifiHardResetDone = false;     // stage 1 already tried for the current stall
const unsigned long API_STALL_RESET_MS  = 15UL * 60UL * 1000UL;  // no API worked -> restart Wi-Fi radio
const unsigned long API_STALL_REBOOT_MS = 30UL * 60UL * 1000UL;  // still nothing -> reboot

// ===== Pomodoro =====
bool statuscontrol       = false;
unsigned long pomodoroStartTime = 0;
uint8_t pressCount       = 0;
int settime              = 0;
int pressCountToMinutes  = 0;
bool justFinished        = false;
bool justStarted         = false;

const unsigned long DEBOUNCE_MS   = 50;
const unsigned long LONG_PRESS_MS = 3000;

bool lastReading       = true;
bool lastButtonState   = 0;
unsigned long lastDebounceTime = 0;
unsigned long pressStartTime   = 0;
bool longPressActive   = false;

// Buzzer
bool buzzerActive        = false;
unsigned long buzzerStartTime = 0;
unsigned long buzzerDuration  = 0;

// ===== Time / Data =====
String currentDate;
String currentDayOfWeek;
int currentHour   = 0;
int currentMinute = 0;
String ampm       = "AM";

long subscribers  = 0;
double conversion = 0;
String weathercondition;
String apIP = "";
float t = 0.0;
float h = 0.0;
float smoothtemp;
float smoothhum;
unsigned long totalPeriod = 0;
unsigned long ledPeriod = 0;
bool blinking = false;

int apiCode[4] = {200, 200, 200, 200};
const char apiTag[4] = {'W', 'C', 'S', 'T'};

#define CLOUD_WIDTH  23
#define CLOUD_HEIGHT 16
#define CLOCK2_WIDTH  23
#define CLOCK2_HEIGHT 23

// generate images: https://javl.github.io/image2cpp/

const unsigned char cloud [] PROGMEM = {
	0x00, 0x3f, 0x00, 0x00, 0xff, 0x80, 0x01, 0xc1, 0xc0, 0x07, 0x80, 0xc0, 0x1f, 0x00, 0xc0, 0x18, 
	0x00, 0x60, 0x30, 0x00, 0x78, 0x70, 0x00, 0x1c, 0x70, 0x00, 0x0e, 0xc0, 0x00, 0x06, 0xc0, 0x00, 
	0x06, 0xc0, 0x00, 0x06, 0xc0, 0x00, 0x0e, 0xe0, 0x00, 0x0c, 0x7f, 0xff, 0xf8, 0x3f, 0xff, 0xf0
};
const unsigned char clock2 [] PROGMEM = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 
	0xfe, 0x00, 0x01, 0x83, 0x00, 0x02, 0x00, 0x80, 0x06, 0x10, 0xc0, 0x04, 0x10, 0x40, 0x0c, 0x10, 
	0x60, 0x0c, 0x10, 0x60, 0x0c, 0x08, 0x60, 0x04, 0x04, 0x40, 0x06, 0x00, 0xc0, 0x02, 0x00, 0x80, 
	0x01, 0x83, 0x00, 0x00, 0xfe, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00
};
class MovingAverage {
  private:
    int _numReadings;
    float *_readings;
    int _readIndex = 0;
    float _total = 0;
    int _startupCounter = 0;

  public:
    MovingAverage(int size) {
      _numReadings = size;
      _readings = new float[_numReadings];
      for (int i = 0; i < _numReadings; i++) _readings[i] = 0.0;
    }
    ~MovingAverage() { delete[] _readings; }

    float update(float newValue) {
      _startupCounter++;
      _total -= _readings[_readIndex];
      _readings[_readIndex] = newValue;
      _total += newValue;

      _readIndex++;
      if (_readIndex >= _numReadings) _readIndex = 0;

      if (_startupCounter < _numReadings) {
        return _total / _startupCounter;
      } else {
        return _total / _numReadings;
      }
    }
};

MovingAverage tempSensorAvg(25);
MovingAverage humSensorAvg(25);


// ===== UI =====
void drawUI() {
  const int barWidth = tft.width();  // 128 pixels
  const int barHeight = 10;
  const int barX = 0;
  const int barY = tft.height() - barHeight;

  tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  tft.setTextSize(3);
  tft.setCursor(30, 50);
  if(statuscontrol == false){
    tft.print("--");
  }else{
    if (settime < 10) tft.print("0");
    tft.print(settime);
  }  
  
  tft.setTextSize(1);
  tft.setCursor(85, 55);
  tft.print("min");
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  // Date
  tft.setCursor(15, 90);
  tft.print(currentDate);
  // Time
  tft.setCursor(15, 103);
  char timeBuffer[12];
  snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d %s",
           currentHour, currentMinute, ampm.c_str());
  tft.print(timeBuffer);
  
  tft.setCursor(15, 116);
  drawClock2Icon();
  tft.print(smoothtemp);
  tft.setCursor(45, 116);
  tft.print(" oC");
  tft.setCursor(15, 129);
  drawCloudIcon();
  tft.print(smoothhum);
  tft.setCursor(45, 129);
  tft.print(" %");
  
  

  if(statuscontrol == false){
    tft.setCursor(30, 149);
    // Clear previous bar
    tft.fillRect(0, barY - 5, barWidth, barHeight + 5, ST77XX_BLACK);
    tft.print("Push button");
  }else{
    tft.setCursor(0, 149);
    drawTimeBar();
  }
  
}

// ===== Pomodoro =====
void ringTone(unsigned long durationMs) {
  buzzerActive = true;
  buzzerDuration = durationMs;
  buzzerStartTime = millis();
  digitalWrite(buzzer, HIGH);
}

void handleBuzzer() {
  if (buzzerActive && (millis() - buzzerStartTime) >= buzzerDuration) {
    buzzerActive = false;
    digitalWrite(buzzer, LOW);
  }
}

void tickTimer() {
  if (!statuscontrol) return;

  unsigned long nowMs = millis();
  unsigned long totalDurationMs = (unsigned long)pressCountToMinutes * 60000UL;
  unsigned long elapsed = nowMs - pomodoroStartTime;

  if (elapsed >= totalDurationMs) {
    settime = 0;
    statuscontrol = false;
    if (justFinished) {
      ringTone(1000);
      justFinished = false;
    }
  } else {
    unsigned long remainingMs = totalDurationMs - elapsed;
    settime = (int)((remainingMs + 59999UL) / 60000UL) - 1;
    if (settime < 0) settime = 0;
    blinkLED();
  }
}

// ===== WiFi =====
bool connectSavedWiFi() {
  WiFi.mode(WIFI_STA);
  // Disable modem power-save; a common cause of an associated-but-silent link with some routers
  // https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 8000) {
    delay(200);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected. IP: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("Failed to connect to saved WiFi.");
    return false;
  }
}

void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    isConnected = true;
    wifiWasDown = false;
    wifiDownSince = 0;
    return;
  }

  if (!wifiWasDown) {
    wifiWasDown = true;
    wifiDownSince = millis();
  }

  Serial.println("WiFi lost. Reconnecting...");
  WiFi.disconnect();
  WiFi.begin(ssid, password);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 8000) {
    delay(200);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi reconnected.");
    isConnected = true;
    wifiWasDown = false;
    wifiDownSince = 0;
  } else {
    Serial.println("WiFi failed to reconnect.");
    isConnected = false;
    if (millis() - wifiDownSince > WIFI_RESTART_TIMEOUT_MS) {
      Serial.println("WiFi down too long. Restarting ESP32...");
      ESP.restart();
    }
  }
}

// ===== AP Portal =====
String buildSSIDOptions() {
  int n = WiFi.scanNetworks();
  String opts;
  for (int i = 0; i < n; i++) {
    opts += "<option value='" + WiFi.SSID(i) + "'>";
    opts += WiFi.SSID(i);
    opts += " (" + String(WiFi.RSSI(i)) + " dBm)";
    opts += "</option>";
  }
  return opts;
}

String makePage(String options) {
  return String(R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <title>Clock Wi-Fi setup</title>
    <style>
    body {
      font-family: Arial, sans-serif;
      background: #f2f4f8;
      margin: 0;
      padding: 0;
    }
    .container {
      max-width: 380px;
      margin: 40px auto;
      background: white;
      padding: 24px;
      border-radius: 12px;
      box-shadow: 0 4px 14px rgba(0,0,0,0.12);
    }
    h2 {
      text-align: center;
      margin-bottom: 20px;
      color: #333;
    }
    label {
      font-weight: bold;
      font-size: 14px;
    }
    select, input[type=password] {
      width: 100%;
      padding: 12px;
      margin-top: 6px;
      margin-bottom: 16px;
      border-radius: 8px;
      border: 1px solid #ccc;
      font-size: 16px;
    }
    button {
      width: 100%;
      padding: 14px;
      border: none;
      border-radius: 8px;
      background: #2e86de;
      color: white;
      font-size: 16px;
      font-weight: bold;
    }
    button:active {
      background: #1b4f72;
    }
    .footer {
      text-align: center;
      margin-top: 14px;
      font-size: 12px;
      color: #777;
    }
    </style>
    </head>
    <body>
    <div class="container">
    <h2>Configure WiFi</h2>
    <form action="/connect" method="POST">
    <label>Network</label>
    <select name="ssid">
  )rawliteral") + options + R"rawliteral(
    </select>
    <label>Password</label>
    <input type="password" name="pass" placeholder="Enter WiFi password">
    <button type="submit">Connect</button>
    </form>
    <div class="footer">
    ESP32 Setup Portal
    </div>
    </div>
    </body>
    </html>
  )rawliteral";
}

void handleRoot() {
  String options = buildSSIDOptions();
  String page = makePage(options);
  server.send(200, "text/html", page);
}

void handleConnect() {
  if (!server.hasArg("ssid") || !server.hasArg("pass")) {
    server.send(400, "text/plain", "Missing SSID or password");
    return;
  }

  String newSSID = server.arg("ssid");
  String newPASS = server.arg("pass");

  prefs.begin("wifi", false);
  prefs.putString("ssid", newSSID);
  prefs.putString("pass", newPASS);
  prefs.end();

  server.send(200, "text/plain", "Saved. Device will reboot and connect.");
  delay(1000);
  ESP.restart();
}

void startAPPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(networkName);
  IPAddress IP = WiFi.softAPIP();
  apIP = IP.toString();
  Serial.print("AP started. IP: ");
  Serial.println(IP);

  server.on("/", handleRoot);
  server.on("/connect", HTTP_POST, handleConnect);
  server.begin();

  portalRunning = true;
}

void getTime() {
  if (!isConnected || portalRunning) {
    return;
  }
  
  String url = "https://api.timezonedb.com/v2.1/get-time-zone?key=" + String(timekey) + "&format=json&by=zone&zone=" + String(mycity);

  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  HTTPClient http;

  if (http.begin(secureClient, url)) {
    http.setReuse(false);
    http.setTimeout(5000);
    int httpCode = http.GET();
    apiCode[3] = httpCode;   // getTime()
    if (httpCode != 200) {
      Serial.print("[TIME] HTTP request failed, code: ");
      Serial.println(httpCode);
    }

    if (httpCode == 200) {
      DynamicJsonDocument doc(512);
      String payload = http.getString();  // pulls everything fast
      yield();

      DeserializationError err = deserializeJson(doc, payload);
      if (err) {
        // ArduinoJson error codes: https://arduinojson.org/v6/api/misc/deserializationerror/
        Serial.print("[TIME] JSON parse failed: ");
        Serial.println(err.c_str());
      }

      if (!err) {
        const char* datetime = doc["formatted"];

        // doc["formatted"] returns nullptr if the key is missing (e.g. an
        // error payload from an exceeded API quota), so guard against that
        // before strncpy to avoid undefined behavior on a null source:
        // https://arduinojson.org/v6/api/jsonvariant/as/
        if (datetime == nullptr) { apiCode[3] = -100; return; }

        char dateBuf[11];
        char hourBuf[3];
        char minBuf[3];

        strncpy(dateBuf, datetime, 10);
        dateBuf[10] = '\0';

        strncpy(hourBuf, datetime + 11, 2);
        hourBuf[2] = '\0';

        strncpy(minBuf, datetime + 14, 2);
        minBuf[2] = '\0';

        currentDate = String(dateBuf);
        currentHour = atoi(hourBuf);
        if(atoi(hourBuf)> 12){
          currentHour= atoi(hourBuf) - 12;
          ampm= "PM";
        }else{
          currentHour= atoi(hourBuf);
          ampm= "AM";
        }
        currentMinute = atoi(minBuf);
        lastApiSuccess = millis();
        // Derive weekday name from currentDate ("YYYY-MM-DD").
        // https://cplusplus.com/reference/ctime/tm/
        struct tm dowInfo = {0};
        dowInfo.tm_year = currentDate.substring(0, 4).toInt() - 1900;
        dowInfo.tm_mon  = currentDate.substring(5, 7).toInt() - 1;
        dowInfo.tm_mday = currentDate.substring(8, 10).toInt();
        dowInfo.tm_hour = 12;  // noon avoids DST/timezone edge cases

        // mktime() fills in tm_wday (0=Sunday..6=Saturday) as a side effect.
        // https://cplusplus.com/reference/ctime/mktime/
        mktime(&dowInfo);

        // %A = full weekday name, locale-dependent (default "C" locale = English).
        // https://cplusplus.com/reference/ctime/strftime/
        char dowBuf[10];
        strftime(dowBuf, sizeof(dowBuf), "%A", &dowInfo);
        currentDayOfWeek = String(dowBuf);

        if(currentDayOfWeek == "Sunday"){
          currentDayOfWeek= "Sun";
        }else if(currentDayOfWeek == "Monday"){
          currentDayOfWeek= "Mon";
        }else if(currentDayOfWeek == "Tuesday"){
          currentDayOfWeek= "Tue";
        }else if(currentDayOfWeek == "Wednesday"){
          currentDayOfWeek= "Wed";
        }else if(currentDayOfWeek == "Thursday"){
          currentDayOfWeek= "Thu";
        }else if(currentDayOfWeek == "Friday"){
          currentDayOfWeek= "Fri";
        }else if(currentDayOfWeek == "Saturday"){
          currentDayOfWeek= "Sat";
        }else{
          currentDayOfWeek= "NaN";
        }
        
        
      }
    }

    http.end();
  }
}

// ===== Scheduling =====
unsigned long lastTimeUpdate     = 0;
unsigned long lastUIRefresh      = 0;
unsigned long lastSensorUpdate   = 0;


const unsigned long TIME_INTERVAL_MS     = 60000;
const unsigned long UI_INTERVAL_MS       = 30000;
const unsigned long SENSOR_INTERVAL_MS   = 5000;
// ===== Internet watchdog =====
void internetWatchdog() {
  unsigned long nowMs = millis();

  // Only evaluate every 30 s (loop() runs every ~10 ms)
  static unsigned long lastCheck = 0;
  if (nowMs - lastCheck < 30000UL) return;
  lastCheck = nowMs;

  unsigned long stalledFor = nowMs - lastApiSuccess;

  // APIs are working -> re-arm the watchdog
  if (stalledFor < API_STALL_RESET_MS) {
    wifiHardResetDone = false;
    return;
  }

  // Evidence for next time: link state, signal and heap health.
  // Serial.printf: https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/Print.h
  // WiFi.status()/WiFi.RSSI(): https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html
  // ESP.getFreeHeap()/getMaxAllocHeap() (largest contiguous block, what TLS needs):
  // https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/Esp.h
  Serial.printf("[WDT] no API success for %lu s | wifi=%d rssi=%d freeHeap=%u maxBlock=%u\n",
                stalledFor / 1000UL, (int)WiFi.status(), WiFi.RSSI(),
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  // Stage 2: radio restart didn't help -> reboot (not while a Pomodoro is running)
  if (stalledFor >= API_STALL_REBOOT_MS) {
    if (!statuscontrol) {
      Serial.println("[WDT] still no internet, rebooting");
      delay(100);
      // https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/Esp.h
      ESP.restart();
    }
    return;
  }

  // Stage 1: full radio restart, once per stall
  if (!wifiHardResetDone) {
    wifiHardResetDone = true;
    Serial.println("[WDT] restarting Wi-Fi radio");

    // disconnect(true) turns the radio off; connectSavedWiFi() calls WiFi.mode(WIFI_STA) to turn it back on
    // https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html
    WiFi.disconnect(true);
    delay(500);
    isConnected = connectSavedWiFi();

    if (isConnected) {
      // Make every API refresh on the next loop() pass instead of waiting for its interval
      lastTimeUpdate     = nowMs - TIME_INTERVAL_MS     - 1;
    }
  }
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);

  Wire.begin(1, 2);
  Wire.setClock(100000);

  pinMode(button, INPUT_PULLUP);   
  pinMode(buzzer, OUTPUT);
  pinMode(LED, OUTPUT);
  digitalWrite(buzzer, LOW);

  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  // Use this initializer if using a 1.8" TFT screen:
  tft.initR(INITR_BLACKTAB);      // Init ST7735S chip, black tab
  uint16_t time = millis();
  tft.fillScreen(ST77XX_BLACK); 

  // large block of text
  tft.setTextSize(2);
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(20, 5);
  //tft.setTextColor(ST77XX_GREEN);
  tft.setTextColor(mygreen);
  tft.println("Pomodoro");  
  tft.setCursor(20, 22);
  tft.println("Timer");  

  if (! aht.begin()) {
    Serial.println("Could not find AHT? Check wiring");    
  }

  if (!connectSavedWiFi()) {
    Serial.println("WiFi not connected, will keep retrying in loop().");
    isConnected = false;
  }else {
    Serial.println("WiFi connected.");
    isConnected = true;// Timezone GMT-3 (Brazil)
    setenv("TZ", "<-03>3", 1);
    tzset();
    configTime(0, 0, "pool.ntp.org");
    getTime();
  }

  if (startConfigPortal) {
    startAPPortal();
    startConfigPortal= false;
  } 

  drawUI();
}

// ===== Button =====
bool countingPresses = false;
unsigned long pressWindowStart = 0;
const unsigned long PRESS_WINDOW_MS = 4000;   // 4 seconds to collect presses

void handleButton() {
  bool reading = digitalRead(button);

  // Debounce
  if (reading != lastReading) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {

    // Button state changed
    if (reading != lastButtonState) {
      lastButtonState = reading;

      // Button pressed
      if (reading == LOW) {
        pressStartTime = millis();
        longPressActive = true;
        
      }

      // Button released
      else {
        if (longPressActive) {
          unsigned long pressDuration = millis() - pressStartTime;
          longPressActive = false;

          // ===== LONG PRESS → RESET TIMER =====
          if (pressDuration >= LONG_PRESS_MS) {
            statuscontrol = false;
            settime = 0;
            pressCount = 0;
            countingPresses = false;
            justFinished = false;
            return;
          }

          // ===== SHORT PRESS → COUNT =====
          if (!countingPresses) {
            countingPresses = true;
            pressWindowStart = millis();
            pressCount = 0;
          }

          pressCount++;
        }
      }
    }
  }

  lastReading = reading;

  // ===== WINDOW CLOSED → PROCESS PRESSES =====
  if (!statuscontrol && countingPresses && (millis() - pressWindowStart > PRESS_WINDOW_MS)) {
    
    // 5 presses → start AP mode without sounding the buzzer
    if(pressCount >= 5) {
      Serial.println("Starting Access Point mode...");
      countingPresses = false;
      buzzerActive = false;
      digitalWrite(buzzer, LOW);
      startConfigPortal = true;
      startAPPortal();
      return;
    }
    ringTone(500);
    // 1–4 presses → start timer
    pressCountToMinutes = pressCount * 15;   // Timer is multiples of 15 minutes
    settime = pressCountToMinutes;

    pomodoroStartTime = millis();
    statuscontrol = true;
    justStarted= statuscontrol;
    justFinished = true;

    countingPresses = false;

  }
}

// ===== Sensors =====
void updateSensors() {
  if (aht.begin()) {
    aht.getEvent(&humidity, &temp);// populate temp and humidity AHT21 objects with fresh data
    t = temp.temperature;
    h = humidity.relative_humidity;
  } else {
    t = 0;
    h = 0;
  }   

  smoothtemp = tempSensorAvg.update(t);
  smoothhum  = humSensorAvg.update(h);

}

// ===== Loop =====

void loop() {
  unsigned long nowMs = millis();

  // ===== AP Portal =====
  if (portalRunning) {
    server.handleClient();
    drawUI();
    delay(100);
    return;
  }

  // ===== WiFi connection check =====
  static unsigned long lastWiFiCheck = 0;

  if (nowMs - lastWiFiCheck >= 10000UL) {
    lastWiFiCheck = nowMs;
    ensureWiFi();
  }

  internetWatchdog();

  // ===== Main tasks =====
  handleButton();
  tickTimer();
  handleBuzzer();

  // ===== Sensor update =====
  if (nowMs - lastSensorUpdate > SENSOR_INTERVAL_MS) {
    lastSensorUpdate = nowMs;
    updateSensors();
  }

  // ===== Time API =====
  if (nowMs - lastTimeUpdate > TIME_INTERVAL_MS) {
    lastTimeUpdate = nowMs;
    getTime();
  }

  // ===== Display refresh =====
  if ((nowMs - lastUIRefresh > UI_INTERVAL_MS) || justStarted) {
    justStarted = false;
    lastUIRefresh = nowMs;
    drawUI();
  }

  // ===== Normal polling =====
  delay(10);
}
void drawTimeBar() {
  const int barWidth = tft.width();  // 128 pixels
  const int barHeight = 10;
  const int barX = 0;
  const int barY = tft.height() - barHeight;

  unsigned long totalDurationMs =
      (unsigned long)pressCountToMinutes * 60000UL;

  unsigned long elapsed = millis() - pomodoroStartTime;

  int remainingWidth = 0;

  if (statuscontrol && elapsed < totalDurationMs) {
    remainingWidth = (int)(
      (totalDurationMs - elapsed) * barWidth / totalDurationMs
    );
  }

  // Clear previous bar
  tft.fillRect(0, barY - 5, barWidth, barHeight + 5, ST77XX_BLACK);

  // Draw remaining time
  if (remainingWidth > 0) {
    tft.fillRect(barX, barY, remainingWidth, barHeight, mygreen);
  }
}
void blinkLED(){
  if((millis() - totalPeriod > 5000) && blinking == false){
    totalPeriod= millis();
    ledPeriod = millis();
    digitalWrite(LED, HIGH);
    blinking= true;
  }else{
    if((millis() - ledPeriod > 300) && blinking == true){
      
      digitalWrite(LED, LOW);
      blinking = false;
    }
  }
}

void drawCloudIcon() {
  tft.drawBitmap(
    85,               // X position
    116,               // Y position
    cloud,
    CLOUD_WIDTH,
    CLOUD_HEIGHT,
    ST77XX_WHITE      // Foreground color
  );
}
void drawClock2Icon() {
  tft.drawBitmap(
    85,               // X position
    90,               // Y position
    clock2,
    CLOCK2_WIDTH,
    CLOCK2_HEIGHT,
    ST77XX_WHITE      // Foreground color
  );
}