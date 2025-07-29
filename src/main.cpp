#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <FS.h>
#include <SPIFFS.h>
#include "esp_camera.h"
#include "secrets.h"

// ✅ Telegram Root-Zertifikat (ISRG Root X1 – Let's Encrypt)
const char TELEGRAM_ROOT_CERT[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgISA6vbbA5Mf3a3IY03X+fMpVdNMA0GCSqGSIb3DQEBCwUA
MEoxCzAJBgNVBAYTAlVTMRMwEQYDVQQKEwpMZXQncyBFbmNyeXB0MR8wHQYDVQQD
ExZJc1JHIFJvb3QgWDEgUjMgQ2VydGlmaWNhdGUwHhcNMjMwNzE4MDAwMDAwWhcN
MzMwNzE4MDAwMDAwWjBKMQswCQYDVQQGEwJVUzETMBEGA1UEChMKTGV0J3MgRW5j
cnlwdDEfMB0GA1UEAxMWSXNSRyBSb290IFgxIFIzIENlcnRpZmljYXRlMIICIjAN
BgkqhkiG9w0BAQEFAAOCAg8AMIICCgKCAgEAz8eGgrz/j1Ez+8sGdWdGBgTXuj16
qk7Fbl+XbPCx7nCJcLRT/PRH+QX+yX93AXt+iG3T7JZTWgy3Cnx5iUuPfW9RHn92
HZPYD+QJ2p6a0TCb6NSZuQwKtG7nk6EoTGyjYcJ5k3W1VVGOBNP1OQ0iEYJ1p79G
bfpSKsmEIsZUsD1BJ0QL7Kk6OG3PtUxAIu6K0C3W7Wf7AfEBbsfYAXnyWyZX1FXG
Yo9Na4PEI9KzPN6XGl6HfZKRklgVqQ0L6Gbv5xQxO2jRp/TbnlK/xzU1Y3P0aQOz
R5mVAYXoyocGuAklCgjyN8gYZnmwe4Ibl1eknUgjcOC8CnLSc9RmI54j9C0PUIxv
GzqC6PvRBl7WRItjqEb7eEVjDYIWdgh5AGK/8JqFgKMRn9RGSR+82ytggqR21EtE
MWDCE9qIKIGbI50+qV1p6FrRk7DwLKmlpFF1aNEO8GndLZX4VovIQcI3FKFj6PDa
6QsAjq9dmb0SE/1YF9P7Sv62xTt2fx82rzZTlNjB2fYO/gBQ1GCNvMTCOyLVH+Iu
dcCxDXL+yMJfQz6mhtUOYxxbgPGuhCHG5gyHmqB80L7bkoVW3QOY7+VoZfhX9BLN
W6RjzJz2A1Y4PZ3VY8gO0yrj+91W9Yg5WvY74BzByaUl9KHbUk38spzZApI7RNUf
OQIDAQABo0IwQDAOBgNVHQ8BAf8EBAMCAqQwDwYDVR0TAQH/BAUwAwEB/zAdBgNV
HQ4EFgQUA8Pn7GhWgF/YVQylvjX6tI4HZfMwDQYJKoZIhvcNAQELBQADggIBAADs
m5KuvzPVU9zv93NNe3iDZmNgTt51PXMSACcH6RtY1M7vstKDKZ3w8Bg42z03pNC9
R9Be6eYf2ebTYHUWZncfZWxUoCWt8zLJyxXcoZLQEMjCNcZb+M2pYxrl0f35eEBY
7SEt/Vopv8b+n/AVMMNlqRQqgPbKkCKrpKoS5XQ0+y44SCe9ZP+l/FNKrbBFUOyo
NLU6F6IJKJjG4K5zytCsdF3VCDMgE3REWm3e2HjvXG/B+VrLCTcRAaUpmJ9UZGEU
D8ebU4FjTqt4szf2q3rLn2pI0qJDGn70HQmfqjL4s6KTyA03FZ1kZP6rDiy79zzF
yGmGm8+2u1v8nDZrpK//OWVml2g+Db9tErTG34+5ZlFc4QCeL9K0sMFE68V/E0s6
o6cQnm4/8X1BCs1sd5qNOrqGGsH2cf0ovJuZReD4+zCVnOGanDqQGzS6+EdumzFu
5OrF0jO53Yvs5FaZKBP3x3thU5w+xJ86kE1K5fnUEBaQ/B4phRQ7ty/AS0AfUWR4
ytQ0vFSWy5S/0S/98XX0aA1SYXpHkqhzBSenqdr4pbU6h9Kn6LBeHgmFLGmFZ5Jf
S9TVyf1A3CDpEtPC5oO2r+yIuG4AKI8Tf1cQbNdtgkyYpy38RbCK12Rh4lhPL96O
Ba4GO2UIehXTdcPpJMHx+UD25qRVIlQfB0RB
-----END CERTIFICATE-----
)EOF";

// ✅ PIN-Konfiguration für M5Stack TimerCAM X
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
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print(".");
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

  camera_config_t config = {
    .ledc_channel = LEDC_CHANNEL_0,
    .ledc_timer = LEDC_TIMER_0,
    .pin_d0 = Y2_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d7 = Y9_GPIO_NUM,
    .pin_xclk = XCLK_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,
    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_sscb_sda = SIOD_GPIO_NUM,
    .pin_sscb_scl = SIOC_GPIO_NUM,
    .pin_pwdn = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,
    .xclk_freq_hz = 20000000,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = psramFound() ? FRAMESIZE_SVGA : FRAMESIZE_QVGA,
    .jpeg_quality = psramFound() ? 10 : 12,
    .fb_count = psramFound() ? 2 : 1
  };

  Serial.println(psramFound() ? "💾 PSRAM gefunden: SVGA, Qualität 10, 2 FB" :
                                "⚠️ Kein PSRAM: QVGA, Qualität 12, 1 FB");

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("❌ Kamera-Init fehlgeschlagen!");
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s && s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -1);
    Serial.println("✅ OV3660 Sensor konfiguriert");
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
    Serial.println("❌ SPIFFS Fehler beim Mounten");
    esp_camera_fb_return(fb);
    return "";
  }

  File file = SPIFFS.open("/photo.jpg", FILE_WRITE);
  if (!file) {
    Serial.println("❌ Datei konnte nicht geöffnet werden");
    esp_camera_fb_return(fb);
    return "";
  }

  file.write(fb->buf, fb->len);
  file.close();
  Serial.println("✅ Foto gespeichert: /photo.jpg");
  esp_camera_fb_return(fb);
  return "/photo.jpg";
}

bool sendPhotoToTelegram(const String& path) {
  Serial.println("🚀 Sende Foto an Telegram ...");

  File photo = SPIFFS.open(path, "r");
  if (!photo || photo.size() == 0) {
    Serial.println("❌ Foto konnte nicht gelesen werden");
    return false;
  }

  size_t size = photo.size();
  Serial.printf("📦 Fotogröße: %d Bytes\n", size);

  uint8_t* buffer = new uint8_t[size];
  photo.readBytes((char*)buffer, size);
  photo.close();

  WiFiClientSecure client;
  client.setCACert(TELEGRAM_ROOT_CERT);

  HTTPClient http;
  String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) + "/sendPhoto";
  http.begin(client, url);
  http.addHeader("Content-Type", "multipart/form-data; boundary=----WebKitFormBoundary");

  String start =
    "------WebKitFormBoundary\r\n"
    "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" + String(TELEGRAM_CHAT_ID) + "\r\n" +
    "------WebKitFormBoundary\r\n"
    "Content-Disposition: form-data; name=\"photo\"; filename=\"photo.jpg\"\r\n"
    "Content-Type: image/jpeg\r\n\r\n";

  String end = "\r\n------WebKitFormBoundary--\r\n";

  int totalLen = start.length() + size + end.length();
  http.sendRequest("POST", (uint8_t*)0, totalLen);

  WiFiClient* stream = http.getStreamPtr();
  stream->print(start);
  stream->write(buffer, size);
  stream->print(end);

  int code = http.GET();
  delete[] buffer;

  if (code == 200) {
    Serial.println("✅ Telegram-Upload erfolgreich!");
    SPIFFS.remove(path);
    return true;
  } else {
    Serial.printf("❌ Telegram-Upload fehlgeschlagen (Code %d)\n", code);
    return false;
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  connectWiFi();
  if (!initCamera()) {
    Serial.println("❌ Abbruch – Kamera nicht bereit");
    while (true) delay(1000);
  }
}

void loop() {
  Serial.println("\n🔁 Starte neuen Erkennungsdurchlauf ...");
  String photoPath = captureAndSavePhoto();
  if (photoPath.length() > 0) {
    sendPhotoToTelegram(photoPath);
  }
  delay(60000); // ⏱️ 1 Minute warten
}
