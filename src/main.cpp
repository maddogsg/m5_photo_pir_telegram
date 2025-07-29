#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
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

// Telegram CA Root Zertifikat (PEM Format)
const char* telegram_cert = \
"-----BEGIN CERTIFICATE-----\n"
"MIIFFjCCAv6gAwIBAgIRAJErCErPDBinU/bWLiWnX1owDQYJKoZIhvcNAQELBQAw\n"
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n"
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMjAwOTA0MDAwMDAw\n"
"WhcNMjUwOTE1MTYwMDAwWjAyMQswCQYDVQQGEwJVUzEWMBQGA1UEChMNTGV0J3Mg\n"
"RW5jcnlwdDELMAkGA1UEAxMCUjMwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEK\n"
"AoIBAQC7AhUozPaglNMPEuyNVZLD+ILxmaZ6QoinXSaqtSu5xUyxr45r+XXIo9cP\n"
"R5QUVTVXjJ6oojkZ9YI8QqlObvU7wy7bjcCwXPNZOOftz2nwWgsbvsCUJCWH+jdx\n"
"sxPnHKzhm+/b5DtFUkWWqcFTzjTIUu61ru2P3mBw4qVUq7ZtDpelQDRrK9O8Zutm\n"
"NHz6a4uPVymZ+DAXXbpyb/uBxa3Shlg9F8fnCbvxK/eG3MHacV3URuPMrSXBiLxg\n"
"Z3Vms/EY96Jc5lP/Ooi2R6X/ExjqmAl3P51T+c8B5fWmcBcUr2Ok/5mzk53cU6cG\n"
"/kiFHaFpriV1uxPMUgP17VGhi9sVAgMBAAGjggEIMIIBBDAOBgNVHQ8BAf8EBAMC\n"
"AYYwHQYDVR0lBBYwFAYIKwYBBQUHAwIGCCsGAQUFBwMBMBIGA1UdEwEB/wQIMAYB\n"
"Af8CAQAwHQYDVR0OBBYEFBQusxe3WFbLrlAJQOYfr52LFMLGMB8GA1UdIwQYMBaA\n"
"FHm0WeZ7tuXkAXOACIjIGlj26ZtuMDIGCCsGAQUFBwEBBCYwJDAiBggrBgEFBQcw\n"
"AoYWaHR0cDovL3gxLmkubGVuY3Iub3JnLzAnBgNVHR8EIDAeMBygGqAYhhZodHRw\n"
"Oi8veDEuYy5sZW5jci5vcmcvMCIGA1UdIAQbMBkwCAYGZ4EMAQIBMA0GCysGAQQB\n"
"gt8TAQEBMA0GCSqGSIb3DQEBCwUAA4ICAQCFyk5HPqP3hUSFvNVneLKYY611TR6W\n"
"PTNlclQtgaDqw+34IL9fzLdwALduO/ZelN7kIJ+m74uyA+eitRY8kc607TkC53wl\n"
"ikfmZW4/RvTZ8M6UK+5UzhK8jCdLuMGYL6KvzXGRSgi3yLgjewQtCPkIVz6D2QQz\n"
"CkcheAmCJ8MqyJu5zlzyZMjAvnnAT45tRAxekrsu94sQ4egdRCnbWSDtY7kh+BIm\n"
"lJNXoB1lBMEKIq4QDUOXoRgffuDghje1WrG9ML+Hbisq/yFOGwXD9RiX8F6sw6W4\n"
"avAuvDszue5L3sz85K+EC4Y/wFVDNvZo4TYXao6Z0f+lQKc0t8DQYzk1OXVu8rp2\n"
"yJMC6alLbBfODALZvYH7n7do1AZls4I9d1P4jnkDrQoxB3UqQ9hVl3LEKQ73xF1O\n"
"yK5GhDDX8oVfGKF5u+decIsH4YaTw7mP3GFxJSqv3+0lUFJoi5Lc5da149p90Ids\n"
"hCExroL1+7mryIkXPeFM5TgO9r0rvZaBFOvV2z0gp35Z0+L4WPlbuEjN/lxPFin+\n"
"HlUjr8gRsI3qfJOQFy/9rKIJR0Y/8Omwt/8oTWgy1mdeHmmjk7j1nYsvC9JSQ6Zv\n"
"MldlTTKB3zhThV1+XWYp6rjd5JW1zbVWEkLNxE7GJThEUG3szgBVGP7pSWTUTsqX\n"
"nLRbwHOoq7hHwg==\n"
"-----END CERTIFICATE-----\n";

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
    Serial.println("❌ Kamera Framebuffer konnte nicht abgerufen werden");
    return "";
  }

  if (SPIFFS.exists("/photo.jpg")) {
    SPIFFS.remove("/photo.jpg");
  }

  File file = SPIFFS.open("/photo.jpg", FILE_WRITE);
  if (!file) {
    Serial.println("❌ Fehler beim Öffnen der Datei zum Schreiben");
    esp_camera_fb_return(fb);
    return "";
  }

  file.write(fb->buf, fb->len);
  file.close();
  esp_camera_fb_return(fb);
  Serial.println("✅ Foto gespeichert als /photo.jpg");
  return "/photo.jpg";
}

void sendPhotoToTelegram(const String& filepath) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Kein WLAN - keine Telegram Nachricht");
    return;
  }

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("❌ Fehler beim Abrufen des Kamerafotos für Telegram");
    return;
  }

  WiFiClientSecure client;
  client.setCACert(telegram_cert);

  HTTPClient https;
  String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) + "/sendPhoto";
  if (!https.begin(client, url)) {
    Serial.println("❌ HTTPS-Verbindung konnte nicht aufgebaut werden");
    esp_camera_fb_return(fb);
    return;
  }

  String boundary = "----ESP32Boundary" + String(millis());
  https.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

  String bodyStart = "--" + boundary + "\r\n" +
    "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" +
    String(TELEGRAM_CHAT_ID) + "\r\n" +

    "--" + boundary + "\r\n" +
    "Content-Disposition: form-data; name=\"photo\"; filename=\"photo.jpg\"\r\n" +
    "Content-Type: image/jpeg\r\n\r\n";

  String bodyEnd = "\r\n--" + boundary + "--\r\n";

  int contentLength = bodyStart.length() + fb->len + bodyEnd.length();

  https.addHeader("Content-Length", String(contentLength));

  WiFiClient *stream = https.getStreamPtr();

  int httpCode = https.sendRequest("POST", nullptr, 0);
  if (httpCode > 0) {
    stream->print(bodyStart);
    stream->write(fb->buf, fb->len);
    stream->print(bodyEnd);

    String response = https.getString();
    Serial.printf("📤 Telegram API Antwort (%d): %s\n", httpCode, response.c_str());
  } else {
    Serial.printf("❌ HTTP Fehler beim Senden an Telegram: %d\n", httpCode);
  }

  https.end();
  esp_camera_fb_return(fb);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  if (!SPIFFS.begin(true)) {
    Serial.println("❌ SPIFFS konnte nicht gestartet werden");
    return;
  }

  connectWiFi();

  if (!initCamera()) {
    Serial.println("❌ Kamera-Init fehlgeschlagen, starte neu...");
    ESP.restart();
  }

  String photoPath = captureAndSavePhoto();
  if (photoPath != "") {
    sendPhotoToTelegram(photoPath);
  } else {
    Serial.println("❌ Kein Foto zum Senden");
  }
}

void loop() {
  // nichts zu tun hier
}
