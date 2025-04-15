#include <WiFi.h>
#include "DHT.h"
#include "ThingSpeak.h"
#include <MD_Parola.h>
#include <MD_MAX72XX.h>
#include <SPI.h>

// ---------- KẾT NỐI WI-FI ----------
const char* ssid = "P 302";          // Thay SSID Wi-Fi của bạn
const char* password = "0327287976"; // Thay mật khẩu Wi-Fi của bạn

// ---------- THINGSPEAK ----------
unsigned long myChannelNumber = 1234567; // Thay ID kênh ThingSpeak của bạn
const char* myWriteAPIKey = "ABCDEF1234567890"; // Thay API Key của bạn
WiFiClient client;

// ---------- CẢM BIẾN DHT ----------
#define DHTPIN 4          // Chân kết nối với cảm biến DHT22
#define DHTTYPE DHT22     // Loại cảm biến DHT22
DHT dht(DHTPIN, DHTTYPE);

// ---------- LED MA TRẬN ----------
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW // Kiểu phần cứng
#define MAX_DEVICES 5                      // Số lượng LED ma trận
#define DATA_PIN 23                        // Chân DATA
#define CS_PIN 5                           // Chân CS
#define CLK_PIN 18                         // Chân CLK

MD_Parola ledMatrix = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

void setup() {
  Serial.begin(115200);

  // Khởi tạo cảm biến DHT
  dht.begin();

  // Khởi tạo LED ma trận
  ledMatrix.begin();
  ledMatrix.setIntensity(3);  // Độ sáng: 0-15
  ledMatrix.displayClear();

  // Kết nối Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Đang kết nối WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nWiFi đã kết nối!");

  // Khởi tạo ThingSpeak
  ThingSpeak.begin(client);
}

void loop() {
  // Đọc giá trị từ cảm biến DHT
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  // Kiểm tra lỗi đọc cảm biến
  if (isnan(h) || isnan(t)) {
    Serial.println("Lỗi đọc cảm biến DHT!");
    ledMatrix.displayClear();
    ledMatrix.displayText("Loi DHT!", PA_CENTER, 100, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
    while (!ledMatrix.displayAnimate());
    delay(8000);
    return;
  }

  // Chỉ lấy phần nguyên của nhiệt độ và độ ẩm để hiển thị
  int temp = (int)t; // Nhiệt độ
  int humidity = (int)h; // Độ ẩm

  // Tạo chuỗi để hiển thị
  char tempStr[10];
  char humStr[10];
  sprintf(tempStr, "T:%dC", temp);
  sprintf(humStr, "H:%d%%", humidity);

  // Hiển thị nhiệt độ trên LED ma trận (T: xxC)
  ledMatrix.displayClear();
  ledMatrix.displayText(tempStr, PA_CENTER, 100, 3000, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  while (!ledMatrix.displayAnimate());

  // Hiển thị độ ẩm trên LED ma trận (H: xx%)
  ledMatrix.displayClear();
  ledMatrix.displayText(humStr, PA_CENTER, 100, 3000, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  while (!ledMatrix.displayAnimate());

  // Gửi dữ liệu lên ThingSpeak
  ThingSpeak.setField(1, t);  // Gửi nhiệt độ
  ThingSpeak.setField(2, h);  // Gửi độ ẩm
  ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  Serial.println("Dữ liệu đã được gửi lên ThingSpeak!");

  delay(20000);  // Gửi dữ liệu mỗi 20 giây
}