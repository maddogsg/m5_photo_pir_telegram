#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FS.h>
#include <SPIFFS.h>
#include "esp_camera.h"
#include "secrets.h"

#define MOTION_THRESHOLD 150000
#define MOTION_SKIP_BYTES 50

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

const char* telegram_cert = \
"-----BEGIN CERTIFICATE-----\n"
"... dein Zertifikat hier ...\n"
"-----END CERTIFICATE-----\n";

uint8_t* lastFrameBuf = nullptr;
size_t lastFrameLen = 0;

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

bool initCamera(pixformat_t format) {
  Serial.println("📷 Initialisiere Kamera ...");

  camera_config_t config;
  config.ledc_timer   = LEDC_TIMER_0;
  config.ledc_channel = LEDC_CHANNEL_0;
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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = format;

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
    s->set_saturation(s, -2);
  }

  Serial.println("✅ Kamera initialisiert.");
  return true;
}

bool detectMotion() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb || !fb->buf) {
    Serial.println("❌ Kein Frame für Bewegungserkennung.");
    return false;
  }

  bool motionDetected = false;

  if (lastFrameBuf && lastFrameLen == fb->len) {
    size_t diffSum = 0;
    for (size_t i = 0; i < fb->len; i += MOTION_SKIP_BYTES) {
      diffSum += abs((int)fb->buf[i] - (int)lastFrameBuf[i]);
      if (diffSum > MOTION_THRESHOLD) {
        motionDetected = true;
        break;
      }
    }
  } else {
    motionDetected = true;  // erstes Bild → keine Referenz
  }

  if (lastFrameBuf) free(lastFrameBuf);
  lastFrameLen = fb->len;
  lastFrameBuf = (uint8_t*)malloc(lastFrameLen);
  if (lastFrameBuf) {
    memcpy(lastFrameBuf, fb->buf, lastFrameLen);
  }

  esp_camera_fb_return(fb);
  return motionDetected;
}

void sendPhotoToTelegram(const String& fileName) {
  WiFiClientSecure client;
  client.setCACert(telegram_cert);

  if (!client.connect("api.telegram.org", 443)) {
    Serial.println("❌ Verbindung zu Telegram API fehlgeschlagen");
    return;
  }

  File photo = SPIFFS.open(fileName, "r");
  if (!photo) {
    Serial.println("❌ Foto konnte nicht geöffnet werden.");
    return;
  }

  String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
  String url = "/bot" + String(TELEGRAM_BOT_TOKEN) + "/sendPhoto?chat_id=" + String(TELEGRAM_CHAT_ID);

  String bodyStart = "--" + boundary + "\r\n"
                     "Content-Disposition: form-data; name=\"photo\"; filename=\"" + fileName + "\"\r\n"
                     "Content-Type: image/jpeg\r\n\r\n";

  String bodyEnd = "\r\n--" + boundary + "--\r\n";

  int contentLength = bodyStart.length() + photo.size() + bodyEnd.length();

  client.printf("POST %s HTTP/1.1\r\n", url.c_str());
  client.printf("Host: api.telegram.org\r\n");
  client.printf("User-Agent: ESP32-Camera\r\n");
  client.printf("Connection: close\r\n");
  client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary.c_str());
  client.printf("Content-Length: %d\r\n\r\n", contentLength);

  client.print(bodyStart);

  uint8_t buf[512];
  size_t len;
  while ((len = photo.read(buf, sizeof(buf))) > 0) {
    client.write(buf, len);
  }
  photo.close();

  client.print(bodyEnd);

  while (client.connected() || client.available()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
  }

  String response;
  while (client.available()) {
    response += client.readString();
  }

  Serial.println("✅ Foto an Telegram gesendet:");
  Serial.println(response);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("🚀 Starte...");

  if (!SPIFFS.begin(true)) {
    Serial.println("❌ SPIFFS Mount fehlgeschlagen!");
    return;
  }

  connectWiFi();

  if (!initCamera(PIXFORMAT_GRAYSCALE)) {
    Serial.println("❌ Kamera-Init (Graustufen) fehlgeschlagen.");
    return;
  }
}

void loop() {
  delay(3000);

  if (detectMotion()) {
    Serial.println("⚠️ Bewegung erkannt!");

    esp_camera_deinit();
    if (!initCamera(PIXFORMAT_JPEG)) {
      Serial.println("❌ Kamera Init (JPEG) fehlgeschlagen.");
      return;
    }

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("❌ Kamera Frame fehlgeschlagen.");
      return;
    }

    String photoFile = "/photo.jpg";
    File file = SPIFFS.open(photoFile, FILE_WRITE);
    if (!file) {
      Serial.println("❌ Datei konnte nicht geöffnet werden.");
      esp_camera_fb_return(fb);
      return;
    }

    file.write(fb->buf, fb->len);
    file.close();
    esp_camera_fb_return(fb);

    Serial.println("📸 Foto gespeichert: " + photoFile);
    sendPhotoToTelegram(photoFile);

    esp_camera_deinit();
    initCamera(PIXFORMAT_GRAYSCALE);  // zurück zu Bewegungsmodus
  } else {
    Serial.println("✅ Keine Bewegung.");
  }
}
