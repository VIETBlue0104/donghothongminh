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
// ---------- WiFi ----------
const char* ssid     = "P 302";
const char* password = "0327287976";

// ---------- DHT ----------
#define DHTPIN   4
#define DHTTYPE  DHT22
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

// ---------- Globals ----------
char szTime[9];      // HH:MM:SS
char szMesg[64];
float humidity = 0, temperature = 0;
uint32_t timerDHT = 0;
const uint32_t DHT_INTERVAL = 5000;  // update every 5s
uint8_t degC[] = {8, 3, 3,0, 62, 65, 65, 65, 34};

enum DisplayMode { SHOW_TIME, SHOW_TEMP_HUMID, SHOW_DATE };
DisplayMode mode = SHOW_TIME;

// ---------- Function Prototypes ----------
void adjustBrightness();
void updateTemperature();
void updateTimeString();
String getLunarDate(int d, int m, int y);
DateTime getCurrentTime();
void convertSolar2Lunar(int dd, int mm, int yy, int &ld, int &lm, int &ly, bool &leap);

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  Wire.begin();
  dht.begin();
  pinMode(LIGHT_SENSOR_PIN, INPUT);

  if (!rtc.begin()) {
    Serial.println("RTC not found!");
  } else if (!rtc.isrunning()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  ledMatrix.begin();
  ledMatrix.setIntensity(5);
  ledMatrix.setZone(0, 0, MAX_DEVICES - 1);
  ledMatrix.setFont(0, nullptr);
  ledMatrix.addChar('$', degC);

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
  switch (mode) {
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
        delay(1000);
      }
      mode = SHOW_TEMP_HUMID;
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
      while (millis() - t0 < 5000) {
        adjustBrightness();
        ledMatrix.displayAnimate();
        delay(100);
      }
      mode = SHOW_DATE;
      break;
    }

    case SHOW_DATE: {
      DateTime now = getCurrentTime();
      const char* daysOfWeek[] = {"CN,", "T2,", "T3,", "T4,", "T5,", "T6,", "T7,"};
      char solar[32];
      snprintf(solar, sizeof(solar), "%s %02d-%02d-%04d", daysOfWeek[now.dayOfTheWeek()], now.day(), now.month(), now.year());
      
      // Lấy thông tin âm lịch với ngày, tháng, năm dương lịch
      String lunar = getLunarDate(now.day(), now.month(), now.year());
      char lunarBuf[32];
      lunar.toCharArray(lunarBuf, sizeof(lunarBuf));
      
      // Hiển thị ngày dương lịch và âm lịch
      snprintf(szMesg, sizeof(szMesg), "%s     %s", solar, lunarBuf);
      ledMatrix.setFont(0, nullptr);
      ledMatrix.displayClear();
      ledMatrix.setTextEffect(0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
      ledMatrix.displayText(szMesg, PA_RIGHT, 25, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
      
      while (!ledMatrix.getZoneStatus(0)) {
        adjustBrightness();
        ledMatrix.displayAnimate();
      }
      mode = SHOW_TIME;
      break;
    }    
  }
}

void adjustBrightness() {
  int light = analogRead(LIGHT_SENSOR_PIN);  // Giá trị từ 0 đến 4095
  
  int brightnessLevel;

  if (light < 500) {
    brightnessLevel = 15;  // Rất tối
  } else if (light < 1500) {
    brightnessLevel = 10;  // Tối vừa
  } else if (light < 2500) {
    brightnessLevel = 7;   // Ánh sáng trung bình
  } else if (light < 3500) {
    brightnessLevel = 4;   // Khá sáng
  } else {
    brightnessLevel = 1;   // Rất sáng
  }

  ledMatrix.setIntensity(brightnessLevel);
}


void updateTemperature() {
  if (millis() - timerDHT > DHT_INTERVAL) {
    timerDHT = millis();
    temperature = dht.readTemperature();
    humidity    = dht.readHumidity();
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

// ---------- Lunar Date ----------
String getLunarDate(int d, int m, int y) {
  int ld, lm, ly;
  bool isLeap;
  
  // Chuyển đổi dương lịch sang âm lịch
  convertSolar2Lunar(d, m, y, ld, lm, ly, isLeap);
  
  // Chuẩn bị chuỗi kết quả cho ngày tháng năm âm lịch
  String result = "AL, " + String(ld) + "/" + String(lm) + "/" + String(ly);  // Thêm năm âm lịch
  if (isLeap) result += "N";  // Nếu là tháng nhuận, thêm chữ "N"
  
  return result;
}
