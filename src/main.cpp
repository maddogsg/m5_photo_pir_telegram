#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <FS.h>
#include <SPIFFS.h>
#include "esp_camera.h"
#include "secrets.h"

// ✅ PIN-Konfiguration für M5Stack TimerCAM X (mit OV3660)
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   15
#define XCLK_GPIO_NUM    27
#define SIOD_GPIO_NUM    25
#define SIOC_GPIO_NUM    23
#define Y9_GPIO_NUM      19
#define Y8_GPIO_NUM      36
#define Y7_GPIO_NUM      18
#define Y6_GPIO_NUM      39
#define Y5_GPIO_NUM      5
#define Y4_GPIO_NUM      34
#define Y3_GPIO_NUM      35
#define Y2_GPIO_NUM      32
#define VSYNC_GPIO_NUM   22
#define HREF_GPIO_NUM    26
#define PCLK_GPIO_NUM    21

void connectWiFi() {
  Serial.print("🔌 Verbinde mit WLAN: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WLAN verbunden!");
    Serial.print("📡 IP-Adresse: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ WLAN-Verbindung fehlgeschlagen!");
  }
}

bool initCamera() {
  Serial.println("📷 Initialisiere Kamera ...");

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
    Serial.println("💾 PSRAM gefunden: SVGA, Qualität 10, 2 Framebuffer");
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    Serial.println("⚠️ Kein PSRAM: QVGA, Qualität 12, 1 Framebuffer");
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ Kamera-Init fehlgeschlagen (0x%x)\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s && s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -1);
    Serial.println("✅ OV3660 Sensor konfiguriert");
  } else {
    Serial.println("⚠️ Sensor nicht erkannt oder kein OV3660");
  }

  Serial.println("📸 Kamera bereit!");
  return true;
}

String captureAndSavePhoto() {
  Serial.println("📷 Nehme Foto auf ...");
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("❌ Kameraaufnahme fehlgeschlagen!");
    return "";
  }

  if (!SPIFFS.begin(true)) {
    Serial.println("❌ SPIFFS konnte nicht gemountet werden");
    esp_camera_fb_return(fb);
    return "";
  }

  String path = "/photo.jpg";
  File file = SPIFFS.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("❌ Foto-Datei konnte nicht geöffnet werden");
    esp_camera_fb_return(fb);
    return "";
  }

  file.write(fb->buf, fb->len);
  file.close();
  esp_camera_fb_return(fb);

  Serial.println("✅ Foto gespeichert: " + path);
  return path;
}

bool sendPhotoToTelegram(const String& path) {
  Serial.println("🚀 Sende Foto an Telegram ...");

  File photo = SPIFFS.open(path, "r");
  if (!photo || photo.size() == 0) {
    Serial.println("❌ Foto konnte nicht gelesen werden");
    return false;
  }

  size_t size = photo.size();
  uint8_t* buffer = new uint8_t[size];
  photo.readBytes((char*)buffer, size);
  photo.close();

  HTTPClient http;
  String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) + "/sendPhoto";
  http.begin(url);
  http.addHeader("Content-Type", "multipart/form-data; boundary=----WebKitFormBoundary");

  String startRequest =
    "------WebKitFormBoundary\r\n"
    "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" + String(TELEGRAM_CHAT_ID) + "\r\n" +
    "------WebKitFormBoundary\r\n"
    "Content-Disposition: form-data; name=\"photo\"; filename=\"photo.jpg\"\r\n"
    "Content-Type: image/jpeg\r\n\r\n";

  String endRequest = "\r\n------WebKitFormBoundary--\r\n";

  int totalLen = startRequest.length() + size + endRequest.length();
  http.setTimeout(10000);
  http.sendRequest("POST", (uint8_t*)0, totalLen); // Reserve space

  WiFiClient* stream = http.getStreamPtr();
  stream->print(startRequest);
  stream->write(buffer, size);
  stream->print(endRequest);

  int code = http.GET();
  delete[] buffer;

  if (code == 200) {
    Serial.println("✅ Foto erfolgreich an Telegram gesendet!");
    SPIFFS.remove(path);
    return true;
  } else {
    Serial.printf("❌ Fehler beim Telegram-Upload: %d\n", code);
    return false;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  connectWiFi();

  if (!initCamera()) {
    Serial.println("❌ Abbruch – Kamera nicht verfügbar");
    while (true) delay(1000);
  }
}

void loop() {
  Serial.println("\n🔁 Starte Erkennungsdurchlauf ...");

  // Bewegungserkennung wäre hier mit Differenzvergleich implementierbar
  // Wir machen erstmal nur ein Foto
  String photoPath = captureAndSavePhoto();

  if (photoPath.length() > 0) {
    sendPhotoToTelegram(photoPath);
  }

  delay(60000); // 1 Minute warten
}
