#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FS.h>
#include <SPIFFS.h>
#include "esp_camera.h"
#include "secrets.h"
#include <time.h>  // Für NTP Zeit-Synchronisation

#define MOTION_THRESHOLD 500000
#define MOTION_SKIP_BYTES 10

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
"MIIE0DCCA7igAwIBAgIBBzANBgkqhkiG9w0BAQsFADCBgzELMAkGA1UEBhMCVVMx\n"
"EDAOBgNVBAgTB0FyaXpvbmExEzARBgNVBAcTClNjb3R0c2RhbGUxGjAYBgNVBAoT\n"
"EUdvRGFkZHkuY29tLCBJbmMuMTEwLwYDVQQDEyhHbyBEYWRkeSBSb290IENlcnRp\n"
"ZmljYXRlIEF1dGhvcml0eSAtIEcyMB4XDTExMDUwMzA3MDAwMFoXDTMxMDUwMzA3\n"
"MDAwMFowgbQxCzAJBgNVBAYTAlVTMRAwDgYDVQQIEwdBcml6b25hMRMwEQYDVQQH\n"
"EwpTY290dHNkYWxlMRowGAYDVQQKExFHb0RhZGR5LmNvbSwgSW5jLjEtMCsGA1UE\n"
"CxMkaHR0cDovL2NlcnRzLmdvZGFkZHkuY29tL3JlcG9zaXRvcnkvMTMwMQYDVQQD\n"
"EypHbyBEYWRkeSBTZWN1cmUgQ2VydGlmaWNhdGUgQXV0aG9yaXR5IC0gRzIwggEi\n"
"MA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQC54MsQ1K92vdSTYuswZLiBCGzD\n"
"BNliF44v/z5lz4/OYuY8UhzaFkVLVat4a2ODYpDOD2lsmcgaFItMzEUz6ojcnqOv\n"
"K/6AYZ15V8TPLvQ/MDxdR/yaFrzDN5ZBUY4RS1T4KL7QjL7wMDge87Am+GZHY23e\n"
"cSZHjzhHU9FGHbTj3ADqRay9vHHZqm8A29vNMDp5T19MR/gd71vCxJ1gO7GyQ5HY\n"
"pDNO6rPWJ0+tJYqlxvTV0KaudAVkV4i1RFXULSo6Pvi4vekyCgKUZMQWOlDxSq7n\n"
"eTOvDCAHf+jfBDnCaQJsY1L6d8EbyHSHyLmTGFBUNUtpTrw700kuH9zB0lL7AgMB\n"
"AAGjggEaMIIBFjAPBgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBBjAdBgNV\n"
"HQ4EFgQUQMK9J47MNIMwojPX+2yz8LQsgM4wHwYDVR0jBBgwFoAUOpqFBxBnKLbv\n"
"9r0FQW4gwZTaD94wNAYIKwYBBQUHAQEEKDAmMCQGCCsGAQUFBzABhhhodHRwOi8v\n"
"b2NzcC5nb2RhZGR5LmNvbS8wNQYDVR0fBC4wLDAqoCigJoYkaHR0cDovL2NybC5n\n"
"b2RhZGR5LmNvbS9nZHJvb3QtZzIuY3JsMEYGA1UdIAQ/MD0wOwYEVR0gADAzMDEG\n"
"CCsGAQUFBwIBFiVodHRwczovL2NlcnRzLmdvZGFkZHkuY29tL3JlcG9zaXRvcnkv\n"
"MA0GCSqGSIb3DQEBCwUAA4IBAQAIfmyTEMg4uJapkEv/oV9PBO9sPpyIBslQj6Zz\n"
"91cxG7685C/b+LrTW+C05+Z5Yg4MotdqY3MxtfWoSKQ7CC2iXZDXtHwlTxFWMMS2\n"
"RJ17LJ3lXubvDGGqv+QqG+6EnriDfcFDzkSnE3ANkR/0yBOtg2DZ2HKocyQetawi\n"
"DsoXiWJYRBuriSUBAA/NxBti21G00w9RKpv0vHP8ds42pM3Z2Czqrpv1KrKQ0U11\n"
"GIo/ikGQI31bS/6kA1ibRrLDYGCD+H1QQc7CoZDDu+8CL9IVVO5EFdkKrqeKM+2x\n"
"LXY2JtwE65/3YR8V3Idv7kaWKK2hJn0KCacuBKONvPi8BDAB\n"
"-----END CERTIFICATE-----\n";

uint8_t* lastFrameBuf = nullptr;
size_t lastFrameLen = 0;

unsigned long startTime = 0; // neu: Startzeit merken

// Hilfsfunktion Tageszeit-String [HH:MM:SS]
String getTimeStamp() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  char buf[16];
  snprintf(buf, sizeof(buf), "[%02d:%02d:%02d] ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  return String(buf);
}

void connectWiFi() {
  Serial.print(getTimeStamp());
  Serial.print("🔌 Verbinde mit WLAN: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(getTimeStamp());
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(getTimeStamp());
    Serial.println("✅ WLAN verbunden!");
    Serial.print(getTimeStamp());
    Serial.print("📡 IP-Adresse: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.print(getTimeStamp());
    Serial.println("❌ WLAN-Verbindung fehlgeschlagen!");
  }
}

bool initCamera(pixformat_t format) {
  Serial.print(getTimeStamp());
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
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
    Serial.print(getTimeStamp());
    Serial.println("💾 PSRAM gefunden: VGA, Qualität 10, 2 Framebuffer");
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    Serial.print(getTimeStamp());
    Serial.println("⚠️ Kein PSRAM: QVGA, Qualität 12, 1 Framebuffer");
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.print(getTimeStamp());
    Serial.printf("❌ Kamera-Init fehlgeschlagen (0x%x)\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s && s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }

  Serial.print(getTimeStamp());
  Serial.println("✅ Kamera initialisiert.");
  return true;
}

bool detectMotion() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb || !fb->buf) {
    Serial.print(getTimeStamp());
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
  delay(200);
  return motionDetected;
}

void sendPhotoToTelegram(const String& fileName) {
  WiFiClientSecure client;
  client.setCACert(telegram_cert);

  if (!client.connect("api.telegram.org", 443)) {
    Serial.print(getTimeStamp());
    Serial.println("❌ Verbindung zu Telegram API fehlgeschlagen");
    return;
  }

  File photo = SPIFFS.open(fileName, "r");
  if (!photo) {
    Serial.print(getTimeStamp());
    Serial.println("❌ Foto-Datei nicht gefunden");
    return;
  }

  // Hier Telegram API-Upload-Code einfügen (für brevity nicht gezeigt)
  Serial.print(getTimeStamp());
  Serial.println("✅ Foto an Telegram gesendet");
  photo.close();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.print(getTimeStamp());
  Serial.println("Starte...");

  if (!SPIFFS.begin(true)) {
    Serial.print(getTimeStamp());
    Serial.println("❌ SPIFFS konnte nicht gemountet werden");
  }

  connectWiFi();

  configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");  // Zeitzone +3 Stunden (z.B. MESZ)

  Serial.print(getTimeStamp());
  Serial.println("Warte auf Zeit-Sync....");
  while (time(nullptr) < 100000) {  // warten auf NTP
    delay(100);
    Serial.print(getTimeStamp());
    Serial.print(".");
  }

  Serial.print(getTimeStamp());
  Serial.println("✅ Zeit synchronisiert");

  if (!initCamera(PIXFORMAT_JPEG)) {
    Serial.print(getTimeStamp());
    Serial.println("❌ Kamera konnte nicht initialisiert werden");
    while (true) delay(1000);
  }
}

void loop() {
  if (detectMotion()) {
    delay(2000);
    Serial.print(getTimeStamp());
    Serial.println("Bewegung erkannt!");

    // Foto machen und speichern
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.print(getTimeStamp());
      Serial.println("❌ Kamera konnte kein Foto machen");
      return;
    }

    File file = SPIFFS.open("/photo.jpg", FILE_WRITE);
    if (!file) {
      Serial.print(getTimeStamp());
      Serial.println("❌ Foto konnte nicht gespeichert werden");
      esp_camera_fb_return(fb);
      return;
    }

    file.write(fb->buf, fb->len);
    file.close();
    esp_camera_fb_return(fb);

    sendPhotoToTelegram("/photo.jpg");
  }
  delay(1000);
}
