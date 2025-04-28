#include <WiFi.h>
#include <DHT.h>
#include <MD_Parola.h>
#include <MD_MAX72XX.h>
#include <SPI.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <RTClib.h>

// ---------- CẤU HÌNH WI-FI ----------
const char* ssid     = "P 302";
const char* password = "0327287976";

// ---------- CẢM BIẾN DHT22 ----------
#define DHTPIN   4
#define DHTTYPE  DHT22
DHT dht(DHTPIN, DHTTYPE);

// ---------- LED MA TRẬN MAX7219 ----------
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES   5  // số module
#define DATA_PIN      23
#define CLK_PIN       18
#define CS_PIN        5
MD_Parola ledMatrix(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

// ---------- NTP CLIENT ----------
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7 * 3600, 60 * 60 * 1000);

// ---------- RTC DS1307 ----------
RTC_DS1307 rtc;

// ----- Thời gian hiển thị -----
const unsigned long TIME_PHASE = 20000;  // 20 giây hiển thị đồng hồ
const unsigned long TEMP_PHASE = 5000;   // 5 giây hiển thị nhiệt độ/độ ẩm
const uint8_t SCROLL_SPEED = 50;

enum Phase { SHOW_TIME, SHOW_TEMP };
Phase lastPhase = SHOW_TEMP;

// Forward declarations
void setSplitZone();
void setSingleZone();
void showTime();
void showTemp();

void setup() {
  Serial.begin(115200);
  dht.begin();
  Wire.begin();

  // Khởi tạo RTC
  if (!rtc.begin()) {
    Serial.println("RTC DS1307 not found!");
  } else if (!rtc.isrunning()) {
    Serial.println("RTC lost power. Setting to compile time.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Khởi tạo LED ma trận
  ledMatrix.begin();
  ledMatrix.setIntensity(3);
  setSplitZone();
  ledMatrix.displayClear();
  ledMatrix.displaySuspend(false);
  ledMatrix.displayReset();

  // Kết nối Wi-Fi và NTP
  Serial.print("Connecting WiFi");
  WiFi.begin(ssid, password);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" OK");
    timeClient.begin();
    timeClient.update();
    rtc.adjust(DateTime((uint32_t)timeClient.getEpochTime()));
  } else {
    Serial.println(" FAIL (offline mode)");
  }
}

void loop() {
  unsigned long now = millis();
  Phase phase = ((now % (TIME_PHASE + TEMP_PHASE)) < TIME_PHASE) ? SHOW_TIME : SHOW_TEMP;

  if (phase != lastPhase) {
    if (phase == SHOW_TIME) showTime();
    else showTemp();
    lastPhase = phase;
  }
}

void setSplitZone() {
  for (uint8_t z = 0; z < MAX_DEVICES; z++) {
    ledMatrix.setZone(z, z, z);
  }
}

void setSingleZone() {
  ledMatrix.setZone(0, 0, MAX_DEVICES - 1);
}

// Hiển thị và cập nhật giây liên tục trong TIME_PHASE
void showTime() {
  unsigned long start = millis();
  int lastSecond = -1;

  // Chuẩn bị hiển thị
  setSingleZone();
  ledMatrix.displayClear();
  ledMatrix.displayReset();

  while (millis() - start < TIME_PHASE) {
    uint32_t epoch = (WiFi.status() == WL_CONNECTED)
                     ? (timeClient.update(), timeClient.getEpochTime())
                     : rtc.now().unixtime();
    int h = (epoch / 3600) % 24;
    int m = (epoch / 60) % 60;
    int s = epoch % 60;

    if (s != lastSecond) {
      char buf[9];
      snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
      ledMatrix.displayText(buf, PA_CENTER, 0, 0, PA_NO_EFFECT, PA_NO_EFFECT);
      lastSecond = s;
    }
    ledMatrix.displayAnimate();
  }

  // Khôi phục split zones
  setSplitZone();
  ledMatrix.displayReset();
}

// Hiển thị nhiệt độ & độ ẩm scroll toàn màn
void showTemp() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  char buf[16];
  if (isnan(t) || isnan(h)) {
    snprintf(buf, sizeof(buf), "Error DHT");
  } else {
    snprintf(buf, sizeof(buf), "T:%dC H:%d%%", (int)t, (int)h);
  }

  setSingleZone();
  ledMatrix.displayClear();
  ledMatrix.displayText(buf, PA_LEFT, SCROLL_SPEED, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  ledMatrix.displayReset();
  while (!ledMatrix.displayAnimate());

  setSplitZone();
  ledMatrix.displayReset();
}
