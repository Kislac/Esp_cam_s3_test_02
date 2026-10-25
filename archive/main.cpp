#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "esp_camera.h"

const char *WIFI_SSID = "Kis-Szikora";
const char *WIFI_PASSWORD = "Kishangya";

#if defined(LED_BUILTIN)
constexpr int STATUS_LED_PIN = LED_BUILTIN;
#else
constexpr int STATUS_LED_PIN = 4;
#endif

constexpr int PWDN_GPIO_NUM = -1;
constexpr int RESET_GPIO_NUM = -1;
constexpr int XCLK_GPIO_NUM = 15;
constexpr int SIOD_GPIO_NUM = 4;
constexpr int SIOC_GPIO_NUM = 5;
constexpr int Y9_GPIO_NUM = 16;
constexpr int Y8_GPIO_NUM = 17;
constexpr int Y7_GPIO_NUM = 18;
constexpr int Y6_GPIO_NUM = 12;
constexpr int Y5_GPIO_NUM = 10;
constexpr int Y4_GPIO_NUM = 8;
constexpr int Y3_GPIO_NUM = 9;
constexpr int Y2_GPIO_NUM = 11;
constexpr int VSYNC_GPIO_NUM = 6;
constexpr int HREF_GPIO_NUM = 7;
constexpr int PCLK_GPIO_NUM = 13;

WebServer server(80);

void blinkStatusLed() {
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  delay(50);
  digitalWrite(STATUS_LED_PIN, HIGH);
  delay(150);
  digitalWrite(STATUS_LED_PIN, LOW);
}

bool initCamera() {
  Serial.println("Kamera pinout: newbiely ESP32-S3 camera");

  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = psramFound() ? FRAMESIZE_SVGA : FRAMESIZE_QVGA;
  config.jpeg_quality = psramFound() ? 10 : 12;
  config.fb_count = psramFound() ? 2 : 1;
  config.grab_mode = psramFound() ? CAMERA_GRAB_LATEST : CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;

  const esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%X\n", err);
    return false;
  }

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor != nullptr) {
    sensor->set_framesize(sensor, psramFound() ? FRAMESIZE_SVGA : FRAMESIZE_QVGA);
  }

  return true;
}

void handleRoot() {
  String html = F(
      "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>ESP32-S3 Camera</title><style>body{font-family:Arial,sans-serif;background:#111;color:#eee;text-align:center;margin:0;padding:20px;}"
      "img{width:min(100%,960px);height:auto;border:2px solid #333;border-radius:12px;background:#000;}"
      ".card{max-width:1000px;margin:0 auto;background:#1b1b1b;padding:20px;border-radius:16px;}"
      "button{font-size:16px;padding:10px 14px;margin:8px;border-radius:10px;border:0;background:#2d6cdf;color:white;cursor:pointer;}</style></head><body><div class='card'>"
      "<h1>ESP32-S3 kamera</h1><p>A kep 1 mp-enkent automatikusan frissul.</p>"
      "<img id='cam' src='/capture' alt='Kamera kep'><br>"
      "<button onclick='refreshImage()'>Frissites</button>"
      "</div><script>function refreshImage(){document.getElementById('cam').src='/capture?t='+Date.now();}setInterval(refreshImage,1000);</script></body></html>");
  server.send(200, "text/html; charset=utf-8", html);
}

void handleCapture() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb == nullptr) {
    server.send(503, "text/plain; charset=utf-8", "Nem sikerult kepet olvasni a kamerabol.");
    return;
  }

  WiFiClient client = server.client();
  client.printf(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: image/jpeg\r\n"
      "Content-Length: %u\r\n"
      "Cache-Control: no-store, no-cache, must-revalidate, max-age=0\r\n"
      "Pragma: no-cache\r\n"
      "Connection: close\r\n\r\n",
      fb->len);
  client.write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void handleNotFound() {
  server.send(404, "text/plain; charset=utf-8", "Nincs ilyen oldal.");
}

void startWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/capture", HTTP_GET, handleCapture);
  server.onNotFound(handleNotFound);
  server.begin();
}

void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("Csatlakozas WiFi-re: %s\n", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  Serial.println("WiFi csatlakozva.");
  Serial.print("IP cim: ");
  Serial.println(WiFi.localIP());
  blinkStatusLed();
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("ESP32-S3 camera webserver indul...");
  Serial.printf("Flash size: %u bytes\n", ESP.getFlashChipSize());
  Serial.printf("PSRAM size: %u bytes\n", ESP.getPsramSize());

  if (!initCamera()) {
    Serial.println("Ellenorizd a kamera GPIO kiosztast, vagy a PSRAM/beallitasokat.");
    return;
  }

  connectToWiFi();
  startWebServer();

  Serial.println("Webszerver elindult.");
  Serial.print("Nyisd meg ezt a cimet: http://");
  Serial.println(WiFi.localIP());
}

void loop() {
  server.handleClient();
  delay(2);
}
