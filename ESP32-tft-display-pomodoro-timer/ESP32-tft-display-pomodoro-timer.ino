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
#include "Adafruit_HTU21DF.h"
#include <time.h>
#include <WiFiUdp.h>

#define button 18
#define buzzer 19
#define LED 15
#define TFT_CS        3
#define TFT_RST        5 // Or set to -1 and connect to Arduino RESET pin
#define TFT_DC         4
#define SPI_SCK   6
#define SPI_MOSI  7
#define SPI_MISO  1

#define myred 0xF800
#define myorange 0xEA8A

// For 1.44" and 1.8" TFT with ST7735 use:
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
Adafruit_HTU21DF htu = Adafruit_HTU21DF();


// ===== Network / AP =====
WebServer server(80);
Preferences prefs;

const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;
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
unsigned long remainingMs = 0;
bool forceUIRefresh = false;
const unsigned long ledTiming = 150;

int apiCode[4] = {200, 200, 200, 200};
const char apiTag[4] = {'W', 'C', 'S', 'T'};

// ===== Scheduling =====
unsigned long lastTimeUpdate     = 0;
unsigned long lastUIRefresh      = 0;
unsigned long lastSensorUpdate   = 0;

const unsigned long TIME_INTERVAL_MS     = 60000;
unsigned long UI_INTERVAL_MS       = 30000;
const unsigned long SENSOR_INTERVAL_MS   = 30000;
const unsigned long WIFI_SYNC_INTERVAL_MS =
    30UL * 60UL * 1000UL;
unsigned long lastNTPSync = 0;
unsigned long lastSyncAttempt = 0;
WiFiUDP ntpUDP;

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
  updateClock();
  const int barWidth = tft.width();  // 128 pixels
  const int barHeight = 10;
  const int barX = 0;
  const int barY = tft.height() - barHeight;

  // ===== Pomodoro display =====
  tft.setTextColor(myorange, ST77XX_BLACK);
  // Clear the entire timer display area before drawing new values
  tft.fillRect(20, 45, 100, 35, ST77XX_BLACK);
  tft.setTextSize(3);
  tft.setCursor(30, 50);
  if (!statuscontrol) {
    tft.print("--");
  } 
  else if (settime < 1) {
    UI_INTERVAL_MS = 250; // was 1000
    // Display seconds with two digits
    tft.printf("%02lu", remainingMs / 1000);
    tft.setTextSize(1);
    tft.setCursor(85, 55);
    tft.print("sec");
  } 
  else {
    tft.print(settime);
    tft.setTextSize(1);
    tft.setCursor(85, 55);
    tft.print("min");
  }  
  
  tft.setTextSize(1);
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
    forceUIRefresh = true;  // Force immediate screen update
    if (justFinished) {
      ringTone(1000);
      justFinished = false;
      UI_INTERVAL_MS = 30000;     
    }
  } else {
    remainingMs = totalDurationMs - elapsed;
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

void updateClock() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 10)) {
    Serial.println("[CLOCK] System time unavailable");
    return;
  }
  // Date
  char dateBuffer[11];
  strftime(dateBuffer, sizeof(dateBuffer),
           "%Y-%m-%d", &timeinfo);

  currentDate = String(dateBuffer);
  // Hour (12-hour format)
  currentHour = timeinfo.tm_hour % 12;
  if (currentHour == 0) {
    currentHour = 12;
  }
  ampm = (timeinfo.tm_hour >= 12) ? "PM" : "AM";
  // Minutes
  currentMinute = timeinfo.tm_min;
  // Weekday
  const char* days[] = {
    "Sun", "Mon", "Tue", "Wed",
    "Thu", "Fri", "Sat"
  };
  currentDayOfWeek = days[timeinfo.tm_wday];
}

bool syncClock() {
  Serial.println("[NTP] Synchronizing clock...");

  lastSyncAttempt = millis();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long startAttempt = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startAttempt < 10000) {

    delay(100);
  }

  bool success = false;

  if (WiFi.status() == WL_CONNECTED) {

    IPAddress ntpIP;

    if (WiFi.hostByName("pool.ntp.org", ntpIP)) {

      byte packetBuffer[48] = {0};

      packetBuffer[0] = 0b11100011;
      packetBuffer[1] = 0;
      packetBuffer[2] = 6;
      packetBuffer[3] = 0xEC;

      ntpUDP.begin(2390);

      ntpUDP.beginPacket(ntpIP, 123);
      ntpUDP.write(packetBuffer, 48);
      ntpUDP.endPacket();

      unsigned long startWait = millis();

      while (millis() - startWait < 2000) {

        int packetSize = ntpUDP.parsePacket();

        if (packetSize >= 48) {

          ntpUDP.read(packetBuffer, 48);

          unsigned long seconds1900 =
              ((unsigned long)packetBuffer[40] << 24) |
              ((unsigned long)packetBuffer[41] << 16) |
              ((unsigned long)packetBuffer[42] << 8) |
              ((unsigned long)packetBuffer[43]);

          time_t unixTime =
              seconds1900 - 2208988800UL;

          struct timeval tv;

          tv.tv_sec = unixTime;
          tv.tv_usec = 0;

          settimeofday(&tv, nullptr);

          lastNTPSync = millis();

          success = true;

          Serial.println("[NTP] Synchronization successful");

          updateClock();

          break;
        }

        delay(10);
      }

      ntpUDP.stop();
    }
  }

  if (!success) {
    Serial.println("[NTP] Synchronization failed");
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  isConnected = false;

  return success;
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);

  Wire.begin(0, 20);
  Wire.setClock(50000);

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
  tft.setCursor(15, 5);
  //tft.setTextColor(ST77XX_GREEN);
  tft.setTextColor(myred);
  tft.println("Pomodoro");  
  tft.setCursor(30, 22);
  tft.println("Timer");  

  if (!htu.begin()) {
    Serial.println("Check circuit. HTU21D not found!");    
  }

  // ===== Timezone =====
  setenv("TZ", "<-03>3", 1);
  tzset();

  // ===== Initial NTP synchronization =====
  if (syncClock()) {
    Serial.println("[CLOCK] Initial synchronization complete");
  } else {
    Serial.println("[CLOCK] Initial synchronization failed");
  }  

  if (startConfigPortal) {
    startAPPortal();
    startConfigPortal = false;
  } 
  drawUI();
}

// ===== Button =====
bool countingPresses = false;
unsigned long pressWindowStart = 0;
const unsigned long PRESS_WINDOW_MS = 4000;   // 4 seconds to collect presses

void handleButton() {
  bool reading = !digitalRead(button); // "!" for a NC push button

  // Debounce
  if (reading != lastReading) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {

    // Button state changed
    if (reading != lastButtonState) {
      lastButtonState = reading;
      Serial.println(reading == LOW ? "BUTTON PRESSED" : "BUTTON RELEASED");
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

          // ===== LONG PRESS -> RESET TIMER =====
          if (pressDuration >= LONG_PRESS_MS) {
            statuscontrol = false;
            settime = 0;
            pressCount = 0;
            countingPresses = false;
            justFinished = false;
            return;
          }

          // ===== SHORT PRESS -> COUNT =====
          if (!countingPresses) {
            countingPresses = true;
            pressCount = 0;
          }
          pressWindowStart = millis();
          pressCount++;
          Serial.printf("Press count: %d\n", pressCount);
          forceUIRefresh = true; 
        }
      }
    }
  }

  lastReading = reading;

  // ===== WINDOW CLOSED -> PROCESS PRESSES =====
  if (!statuscontrol && countingPresses && (millis() - pressWindowStart > PRESS_WINDOW_MS)) {
    
    // 5 presses -> start AP mode without sounding the buzzer
    if (pressCount >= 5) {
      Serial.println("Starting Access Point mode...");
      countingPresses = false;
      buzzerActive = false;
      digitalWrite(buzzer, LOW);
      startConfigPortal = true;
      startAPPortal();
      return;
    }
    ringTone(500);    
    pressCountToMinutes = pressCount * 15;   // Timer is multiples of 15 minutes
    settime = pressCountToMinutes;

    pomodoroStartTime = millis();
    statuscontrol = true;
    justStarted = statuscontrol;
    justFinished = true;

    countingPresses = false;
  }
}

// ===== Sensors =====
void updateSensors() {
  t = htu.readTemperature();
  h = htu.readHumidity();    

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

  // ===== Main tasks =====
  handleButton();
  tickTimer();
  handleBuzzer();

  // ===== Sensor update =====
  if (nowMs - lastSensorUpdate >= SENSOR_INTERVAL_MS) {
    lastSensorUpdate = nowMs;
    updateSensors();
  }

  // ===== NTP synchronization =====
  if (!countingPresses &&
      nowMs - lastSyncAttempt >= WIFI_SYNC_INTERVAL_MS) {
    syncClock();
  }

  // ===== Display refresh =====
  if (!countingPresses &&
      ((nowMs - lastUIRefresh >= UI_INTERVAL_MS) ||
       justStarted || forceUIRefresh)) {
    justStarted = false;
    forceUIRefresh = false;
    lastUIRefresh = millis();
    drawUI();
  }
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
    tft.fillRect(barX, barY, remainingWidth, barHeight, myred);
  }
}

void blinkLED() {
  if ((millis() - totalPeriod > 5000) && blinking == false) {
    totalPeriod = millis();
    ledPeriod = millis();
    digitalWrite(LED, HIGH);
    blinking = true;
  } else {
    if ((millis() - ledPeriod > ledTiming) && blinking == true) {
      digitalWrite(LED, LOW);
      blinking = false;
    }
  }
}

void drawCloudIcon() {
  tft.drawBitmap(
    85,               // X position
    116,              // Y position
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