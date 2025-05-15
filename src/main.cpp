#include <WiFi.h>
#include <DHT.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <RTClib.h>
#include "Font7Seg.h"
#include "lunar_converter.h"
#include <FastLED.h>

// ---------- Cấu hình LED WS2812 ----------
#define LED_PIN         15
#define NUM_LEDS        24
#define LED_BRIGHTNESS  25

CRGB leds[NUM_LEDS];
#define LED_ALWAYS_ON_PIN 2

// Biến để quản lý hiệu ứng chuyển màu
uint8_t gHue = 0;
bool ledsActive = false;

// ---------- WiFi ----------
const char* ssid       = "P 302";
const char* password   = "0327287976";
unsigned long lastNTPUpdate = 0;
const unsigned long NTP_UPDATE_INTERVAL = 2UL * 60 * 60 * 1000;
unsigned long lastWelcomeTime = 0;
const unsigned long WELCOME_INTERVAL = 5UL * 60 * 1000;

// ---------- DHT ----------
#define DHTPIN  4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// ---------- Matrix ----------
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES   6
#define DATA_PIN      23
#define CLK_PIN       18
#define CS_PIN        5
MD_Parola ledMatrix(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

// ---------- NTP + RTC ----------
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7 * 3600, 60 * 60 * 1000);
RTC_DS1307 rtc;

// ---------- Light Sensor ----------
#define LIGHT_SENSOR_PIN 34
#define NUM_SAMPLES 10  // Số mẫu để lấy trung bình
int lightSensorValues[NUM_SAMPLES];
int sampleIndex = 0;

// ---------- Globals ----------
char szTime[9];
char szMesg[64];
unsigned long lastLEDUpdate = 0;
const uint32_t LED_UPDATE_INTERVAL = 30;
const uint8_t heartBitmapPart1[] = {
  8,
  0b00000000,
  0b00001000,
  0b00001000,
  0b00001000,
  0b00001110,
  0b00011111,
  0b00111111,
  0b01111110
};
const uint8_t heartBitmapPart2[] = {
  8,
  0b00111111,
  0b00011111,
  0b00001110,
  0b00001000,
  0b00001000,
  0b00011100,
  0b00001000,
  0b00000000
};

float humidity = 0, temperature = 0;
uint32_t timerDHT = 0;
const uint32_t DHT_INTERVAL = 5000;
uint8_t degC[] = {8, 3, 3, 0, 62, 65, 65, 65, 34};
enum DisplayMode { SCROLL_TEXT, SHOW_TIME, SHOW_TEMP_HUMID, SHOW_DATE };
DisplayMode mode = SCROLL_TEXT;

// ---------- Function Prototypes ----------
void adjustBrightness();
void updateTemperature();
void updateTimeString();
String getLunarDate(int d, int m, int y);
DateTime getCurrentTime();
void convertSolar2Lunar(int dd, int mm, int yy, int &ld, int &lm, int &ly, bool &leap);
void updateLEDs();
void turnOffLEDs();

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  Wire.begin();
  dht.begin();
  pinMode(LIGHT_SENSOR_PIN, INPUT);

  // Khởi tạo mảng giá trị cảm biến ánh sáng
  for (int i = 0; i < NUM_SAMPLES; i++) {
    lightSensorValues[i] = 0;
  }

  ledcAttachPin(LED_ALWAYS_ON_PIN, 0);
  ledcSetup(0, 5000, 8);
  ledcWrite(0, 245);

  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(LED_BRIGHTNESS);
  turnOffLEDs();

  if (!rtc.begin()) {
    Serial.println("❌ Không tìm thấy RTC (có thể dây kết nối I2C sai hoặc chưa cắm).");
  } else if (!rtc.isrunning()) {
    Serial.println("⚠️ RTC không chạy! Đang khởi tạo lại với thời gian biên dịch.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  } else {
    Serial.println("✅ RTC DS1307 đang hoạt động.");
  }

  ledMatrix.begin();
  ledMatrix.setIntensity(5);
  ledMatrix.setZone(0, 0, MAX_DEVICES - 1);
  ledMatrix.setFont(0, nullptr);
  ledMatrix.addChar('$', degC);
  ledMatrix.addChar('<', heartBitmapPart1);
  ledMatrix.addChar('>', heartBitmapPart2);

  WiFi.begin(ssid, password);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(200);
  }
  if (WiFi.status() == WL_CONNECTED) {
    timeClient.begin();
    timeClient.update();
    rtc.adjust(DateTime((uint32_t)timeClient.getEpochTime()));
  }
}

// ---------- Loop ----------
void loop() {
  if (ledsActive && millis() - lastLEDUpdate >= LED_UPDATE_INTERVAL) {
    lastLEDUpdate = millis();
    updateLEDs();
  }

  if (millis() - lastWelcomeTime > 190000) {
    lastWelcomeTime = millis();
    mode = SCROLL_TEXT;
    ledsActive = true;
  }

  if (WiFi.status() == WL_CONNECTED && millis() - lastNTPUpdate > NTP_UPDATE_INTERVAL) {
    timeClient.update();
    rtc.adjust(DateTime((uint32_t)timeClient.getEpochTime()));
    lastNTPUpdate = millis();
    Serial.println("⏰ Đã đồng bộ DS1307 từ NTP");
  }

  switch (mode) {
    case SCROLL_TEXT: {
      ledMatrix.setFont(0, nullptr);
      ledMatrix.displayClear();
      ledMatrix.setCharSpacing(0);
      const char* welcomeMsg = "G I A N G <> N I E N";
      ledMatrix.setTextEffect(0, PA_SCROLL_RIGHT, PA_SCROLL_RIGHT);
      ledMatrix.displayText(welcomeMsg, PA_LEFT, 68, 0, PA_SCROLL_RIGHT, PA_SCROLL_RIGHT);

      while (!ledMatrix.getZoneStatus(0)) {
        adjustBrightness();
        ledMatrix.displayAnimate();
        updateLEDs();
      }

      ledMatrix.setCharSpacing(1);
      mode = SHOW_TIME;
      ledsActive = true;
      break;
    }

    case SHOW_TIME: {
      ledMatrix.setFont(0, numeric7Seg);
      uint32_t tStart = millis();
      while (millis() - tStart < 30000) {
        adjustBrightness();
        updateTimeString();
        ledMatrix.displayClear();
        ledMatrix.setTextEffect(0, PA_PRINT, PA_NO_EFFECT);
        ledMatrix.displayText(szTime, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
        ledMatrix.displayAnimate();
        updateLEDs();
        delay(1000);
      }
      mode = SHOW_TEMP_HUMID;
      ledsActive = true;
      break;
    }

    case SHOW_TEMP_HUMID: {
      updateTemperature();
      if (isnan(temperature) || isnan(humidity)) strcpy(szMesg, "ERR");
      else snprintf(szMesg, sizeof(szMesg), "%2.0f$ %2.0f%%", temperature, humidity);

      ledMatrix.setFont(0, nullptr);
      ledMatrix.displayClear();
      ledMatrix.setTextEffect(0, PA_PRINT, PA_NO_EFFECT);
      ledMatrix.displayText(szMesg, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);

      uint32_t t0 = millis();
      while (millis() - t0 < 3000) {
        adjustBrightness();
        ledMatrix.displayAnimate();
        updateLEDs();
        delay(100);
      }
      mode = SHOW_DATE;
      ledsActive = true;
      break;
    }

    case SHOW_DATE: {
      DateTime now = getCurrentTime();
      const char* daysOfWeek[] = {"CN,", "T2,", "T3,", "T4,", "T5,", "T6,", "T7,"};
      char solar[32];
      snprintf(solar, sizeof(solar), "%s %02d-%02d-%04d", daysOfWeek[now.dayOfTheWeek()], now.day(), now.month(), now.year());

      String lunar = getLunarDate(now.day(), now.month(), now.year());
      char lunarBuf[32];
      lunar.toCharArray(lunarBuf, sizeof(lunarBuf));

      snprintf(szMesg, sizeof(szMesg), "%s  %s", solar, lunarBuf);
      ledMatrix.setFont(0, nullptr);
      ledMatrix.displayClear();
      ledMatrix.setTextEffect(0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
      ledMatrix.displayText(szMesg, PA_RIGHT, 50, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);

      while (!ledMatrix.getZoneStatus(0)) {
        adjustBrightness();
        ledMatrix.displayAnimate();
        updateLEDs();
      }
      mode = SHOW_TIME;
      ledsActive = true;
      break;
    }
  }
}

void adjustBrightness() {
  // Lấy trung bình của nhiều lần đọc
  int total = 0;
  for (int i = 0; i < NUM_SAMPLES; i++) {
    lightSensorValues[sampleIndex] = digitalRead(LIGHT_SENSOR_PIN);
    total += lightSensorValues[sampleIndex];
    sampleIndex = (sampleIndex + 1) % NUM_SAMPLES; // Vòng qua các phần tử trong mảng
  }
  int averageValue = total / NUM_SAMPLES;

  // Sử dụng giá trị trung bình để điều chỉnh độ sáng
  int brightnessLevel;
  if (averageValue == HIGH) {
    brightnessLevel = 1;
  } else {
    brightnessLevel = 14;
  }
  ledMatrix.setIntensity(brightnessLevel);
}

void updateTemperature() {
  if (millis() - timerDHT > DHT_INTERVAL) {
    timerDHT = millis();
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();
  }
}

void updateTimeString() {
  DateTime now = getCurrentTime();
  snprintf(szTime, sizeof(szTime), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
}

DateTime getCurrentTime() {
  if (WiFi.status() == WL_CONNECTED) {
    timeClient.update();
    return DateTime((uint32_t)timeClient.getEpochTime());
  } else {
    return rtc.now();
  }
}

void updateLEDs() {
  fill_rainbow(leds, NUM_LEDS, gHue, 7);
  FastLED.show();
  FastLED.delay(30);
  gHue++;
}

void turnOffLEDs() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  ledsActive = false;
}

String getLunarDate(int d, int m, int y) {
  int ld, lm, ly;
  bool isLeap;
  convertSolar2Lunar(d, m, y, ld, lm, ly, isLeap);
  String result = "AL, " + String(ld) + "-" + String(lm) + "-" + String(ly);
  if (isLeap) result += "N";
  return result;
}
