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
#include <FastLED.h>  // Thêm thư viện FastLED

// ---------- Cấu hình LED WS2812 ----------
#define LED_PIN  15  // Chân kết nối thanh LED thứ nhất
#define NUM_LEDS 24   // Số LED mỗi thanh
#define LED_BRIGHTNESS 25 // Độ sáng trung bình (0-255)

CRGB leds[NUM_LEDS];

#define LED_ALWAYS_ON_PIN 2

// Biến để quản lý hiệu ứng chuyển màu
uint8_t gHue = 0;  // Biến màu sắc
bool ledsActive = false;  // Trạng thái LED có đang hoạt động không

// ---------- WiFi ----------
const char* ssid     = "P 302";
const char* password = "0327287976";
unsigned long lastNTPUpdate = 0;
const unsigned long NTP_UPDATE_INTERVAL = 2UL * 60 * 60 * 1000;  // 2 tiếng = 7200000 ms
unsigned long lastWelcomeTime = 0;
const unsigned long WELCOME_INTERVAL = 5UL * 60 * 1000;  // 5 phút = 300000 ms

// ---------- DHT ----------
#define DHTPIN    4
#define DHTTYPE   DHT22
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
unsigned long lastLEDUpdate = 0;
const uint32_t LED_UPDATE_INTERVAL = 30;  // 30ms ~ 33fps
const uint8_t heartBitmapPart1[] = {
  8, // <- Chiều rộng của ký tự
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
  8, // <- Chiều rộng
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
const uint32_t DHT_INTERVAL = 5000;  // update every 5s
uint8_t degC[] = {8, 3, 3,0, 62, 65, 65, 65, 34};
enum DisplayMode { SCROLL_TEXT, SHOW_TIME, SHOW_TEMP_HUMID, SHOW_DATE };
DisplayMode mode = SCROLL_TEXT;

// ---------- Function Prototypes ----------
void adjustBrightness();
void updateTemperature();
void updateTimeString();
String getLunarDate(int d, int m, int y);
DateTime getCurrentTime();
void convertSolar2Lunar(int dd, int mm, int yy, int &ld, int &lm, int &ly, bool &leap);
void updateLEDs();  // Hàm cập nhật hiệu ứng LED
void turnOffLEDs(); // Hàm tắt LED

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  Wire.begin();
  dht.begin();
  pinMode(LIGHT_SENSOR_PIN, INPUT);

   // Cấu hình LED luôn sáng với PWM
  ledcAttachPin(LED_ALWAYS_ON_PIN, 0);   // Kênh 0
  ledcSetup(0, 5000, 8);  // PWM tần số 5kHz, độ phân giải 8 bit (0 - 255)
  ledcWrite(0, 245);  // Độ sáng trung bình (0-255). Thử 80, có thể chỉnh cao/thấp tùy ý
  // Khởi tạo LED WS2812
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(LED_BRIGHTNESS);
  turnOffLEDs();  // Tắt LED ban đầu

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
  // Cập nhật hiệu ứng LED nếu đang hoạt động
  if (ledsActive && millis() - lastLEDUpdate >= LED_UPDATE_INTERVAL) {
  lastLEDUpdate = millis();
  updateLEDs();
}

  // --- Ưu tiên hiển thị "GIANG <> NIEN" mỗi phút ---
  if (millis() - lastWelcomeTime > 190000) {
    lastWelcomeTime = millis();
    mode = SCROLL_TEXT;
    ledsActive = true;  // Bật LED khi chuyển mode
  }

  // --- Đồng bộ thời gian từ NTP mỗi 2 tiếng ---
  if (WiFi.status() == WL_CONNECTED && millis() - lastNTPUpdate > NTP_UPDATE_INTERVAL) {
    timeClient.update();
    rtc.adjust(DateTime((uint32_t)timeClient.getEpochTime()));
    lastNTPUpdate = millis();
    Serial.println("⏰ Đã đồng bộ DS1307 từ NTP");
  }

  // --- Điều khiển các chế độ hiển thị ---
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
        updateLEDs();  // Thêm dòng này
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
        updateLEDs();  // Thêm dòng này
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
        updateLEDs();  // Thêm dòng này
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

      snprintf(szMesg, sizeof(szMesg), "%s   %s", solar, lunarBuf);
      ledMatrix.setFont(0, nullptr);
      ledMatrix.displayClear();
      ledMatrix.setTextEffect(0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
      ledMatrix.displayText(szMesg, PA_RIGHT, 50, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);

      while (!ledMatrix.getZoneStatus(0)) {
        adjustBrightness();
        ledMatrix.displayAnimate();
        updateLEDs();  // Thêm dòng này
      }
      mode = SHOW_TIME;
      ledsActive = true;
      break;
    }
  }
}


void adjustBrightness() {
  int lightSensorValue = digitalRead(LIGHT_SENSOR_PIN); 
  int brightnessLevel;
  if (lightSensorValue == HIGH) {
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

// ---------- Hàm điều khiển LED WS2812 ----------
void updateLEDs() {
  // Hiệu ứng chuyển màu rainbow
  fill_rainbow(leds, NUM_LEDS, gHue, 7);
  FastLED.show();
  FastLED.delay(30);  // Làm mượt hiệu ứng
  
  gHue++;  // Thay đổi màu sắc
}

void turnOffLEDs() {
  // Tắt tất cả LED
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  ledsActive = false;
}

// ---------- Lunar Date ----------
String getLunarDate(int d, int m, int y) {
  int ld, lm, ly;
  bool isLeap;
  
  // Chuyển đổi dương lịch sang âm lịch
  convertSolar2Lunar(d, m, y, ld, lm, ly, isLeap);
  
  // Chuẩn bị chuỗi kết quả cho ngày tháng năm âm lịch
  String result = "AL, " + String(ld) + "-" + String(lm) + "-" + String(ly);  // Thêm năm âm lịch
  if (isLeap) result += "N";  // Nếu là tháng nhuận, thêm chữ "N"
  
  return result;
}
