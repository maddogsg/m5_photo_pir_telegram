#include <Arduino.h>
#include <esp_camera.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <FS.h>
#include <SPIFFS.h>
#include "exclude/secrets.h"

// --- Pin-Definitionen M5Stack TimerCAM (OV3660) ---
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

// --- Globals ---
#define QVGA_FRAME_SIZE FRAMESIZE_QVGA
#define VGA_FRAME_SIZE  FRAMESIZE_VGA

#define MOTION_THRESHOLD_PERCENT 8   // 8% Pixeländerung
#define DELAY_BETWEEN_SHOTS 5000     // 5 Sekunden

camera_fb_t *prev_fb = nullptr;

WiFiClientSecure telegramClient;

void connectWiFi() {
  Serial.printf("Verbinde mit WLAN %s ", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nVerbunden! IP: %s\n", WiFi.localIP().toString().c_str());
}

bool initCamera() {
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
    config.frame_size = VGA_FRAME_SIZE;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = QVGA_FRAME_SIZE;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Kamerainitialisierung fehlgeschlagen: 0x%x\n", err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s && s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -1);
    Serial.println("OV3660 erkannt und konfiguriert");
  } else {
    Serial.printf("Sensor-ID nicht OV3660 (ID: 0x%02x)\n", s ? s->id.PID : 0);
  }

  return true;
}

size_t pixelDifferencePercent(camera_fb_t *fb1, camera_fb_t *fb2) {
  // Vergleich beider JPEG-Bilder auf Pixelebene ist zu komplex.
  // Deshalb dekodieren wir JPEG nicht, sondern vergleichen einfach die Raw-Frames nicht möglich.
  // Für schnelle Bewegungserkennung: Wir greifen auf jpg-Komprimierung zu und schätzen anhand Dateigröße.
  // Hier machen wir eine simple Heuristik: Unterschiedliche Dateigrößen als Bewegung.
  // Bessere Lösung: Nutze RGB-Rohbilder, aber das braucht viel RAM.

  if (!fb1 || !fb2) return 100;

  int diff = abs((int)fb1->len - (int)fb2->len);
  int maxLen = max(fb1->len, fb2->len);
  if (maxLen == 0) return 0;

  return (diff * 100) / maxLen;
}

bool saveImageToSPIFFS(const char* path, camera_fb_t* fb) {
  File file = SPIFFS.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Fehler beim Öffnen der Datei zum Schreiben");
    return false;
  }
  file.write(fb->buf, fb->len);
  file.close();
  Serial.printf("Bild gespeichert: %s (%u bytes)\n", path, (unsigned)fb->len);
  return true;
}

bool sendPhotoTelegram(const char* filepath) {
  if (!telegramClient.connect("api.telegram.org", 443)) {
    Serial.println("Verbindung zu Telegram fehlgeschlagen");
    return false;
  }

  String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";

  String url = "/bot";
  url += TELEGRAM_BOT_TOKEN;
  url += "/sendPhoto";

  String header = String("POST ") + url + " HTTP/1.1\r\n" +
                  "Host: api.telegram.org\r\n" +
                  "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n";

  // Datei öffnen
  File photo = SPIFFS.open(filepath, FILE_READ);
  if (!photo) {
    Serial.println("Bilddatei zum Senden nicht gefunden");
    return false;
  }
  size_t fileSize = photo.size();

  String bodyStart = "--" + boundary + "\r\n" +
                     "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" +
                     TELEGRAM_CHAT_ID + "\r\n" +

                     "--" + boundary + "\r\n" +
                     "Content-Disposition: form-data; name=\"photo\"; filename=\"image.jpg\"\r\n" +
                     "Content-Type: image/jpeg\r\n\r\n";

  String bodyEnd = "\r\n--" + boundary + "--\r\n";

  size_t contentLength = bodyStart.length() + fileSize + bodyEnd.length();

  header += "Content-Length: " + String(contentLength) + "\r\n\r\n";

  telegramClient.print(header);
  telegramClient.print(bodyStart);

  // Datei an Telegram senden (Chunkweise)
  const size_t bufferSize = 1024;
  uint8_t buffer[bufferSize];
  while (photo.available()) {
    size_t bytesRead = photo.readBytes(buffer, bufferSize);
    telegramClient.write(buffer, bytesRead);
  }
  photo.close();

  telegramClient.print(bodyEnd);

  // Warten auf Antwort (optional)
  unsigned long timeout = millis() + 10000;
  while (telegramClient.connected() && millis() < timeout) {
    while (telegramClient.available()) {
      String line = telegramClient.readStringUntil('\n');
      // Optional: Hier könntest du prüfen, ob der Upload erfolgreich war
      // Serial.println(line);
    }
  }
  telegramClient.stop();

  Serial.println("Bild an Telegram gesendet");
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS konnte nicht gemountet werden");
    return;
  }

  connectWiFi();

  if (!initCamera()) {
    Serial.println("Kamera Init fehlgeschlagen");
    return;
  }
}

void loop() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Kamerabild konnte nicht geholt werden");
    delay(DELAY_BETWEEN_SHOTS);
    return;
  }

  // QVGA-Bilder: Immer so aufnehmen (evtl. für erstes Bild)
  if (fb->width != 320 || fb->height != 240) {
    // Wenn aktuelles Bild nicht QVGA, neu holen mit QVGA
    esp_camera_deinit();
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
    config.frame_size = QVGA_FRAME_SIZE;
    config.jpeg_quality = 12;
    config.fb_count = 1;

    if (esp_camera_init(&config) != ESP_OK) {
      Serial.println("Fehler bei Neustart Kamera mit QVGA");
      esp_camera_fb_return(fb);
      delay(DELAY_BETWEEN_SHOTS);
      return;
    }
    esp_camera_fb_return(fb);
    delay(DELAY_BETWEEN_SHOTS);
    return;
  }

  // Bewegungserkennung per Heuristik auf JPEG-Größe
  size_t diffPercent = pixelDifferencePercent(fb, prev_fb);

  Serial.printf("Pixel Unterschied: %u%%\n", (unsigned)diffPercent);

  if (prev_fb) esp_camera_fb_return(prev_fb);
  prev_fb = fb;

  if (diffPercent > MOTION_THRESHOLD_PERCENT) {
    Serial.println("Bewegung erkannt! Mache VGA Bild und sende...");

    // VGA Bild aufnehmen
    esp_camera_fb_return(fb);

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
    config.frame_size = VGA_FRAME_SIZE;
    config.jpeg_quality = 10;
    config.fb_count = 1;

    if (esp_camera_init(&config) != ESP_OK) {
      Serial.println("Fehler bei VGA Kamera Init");
      delay(DELAY_BETWEEN_SHOTS);
      return;
    }

    camera_fb_t *vga_fb = esp_camera_fb_get();
    if (!vga_fb) {
      Serial.println("Konnte VGA Bild nicht holen");
      esp_camera_deinit();
      delay(DELAY_BETWEEN_SHOTS);
      return;
    }

    // Bild speichern
    const char* path = "/motion.jpg";
    if (saveImageToSPIFFS(path, vga_fb)) {
      // Bild per Telegram senden
      if (sendPhotoTelegram(path)) {
        // Datei löschen nach Versand
        SPIFFS.remove(path);
      }
    }

    esp_camera_fb_return(vga_fb);
    esp_camera_deinit();

    // Wieder zurück auf QVGA initialisieren
    initCamera();
  }

  delay(DELAY_BETWEEN_SHOTS);
}
