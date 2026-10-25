#include <Arduino.h>
#include <WiFi.h>
#include <DIYables_ESP32_WebServer.h>
#include "esp_camera.h"

const char WIFI_SSID[] = "Kis-Szikora";
const char WIFI_PASSWORD[] = "Kishangya";

#if defined(LED_BUILTIN)
constexpr int STATUS_LED_PIN = LED_BUILTIN;
#else
constexpr int STATUS_LED_PIN = 2;
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

DIYables_ESP32_WebServer server;

framesize_t currentFrameSize = FRAMESIZE_QVGA;
int currentJpegQuality = 12;
bool currentRotate180 = false;
bool cameraInitialized = false;
String lastStatusMessage = "Kamera keszen all.";

void blinkStatusLed() {
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  delay(80);
  digitalWrite(STATUS_LED_PIN, HIGH);
  delay(180);
  digitalWrite(STATUS_LED_PIN, LOW);
}

const char *sensorName(int pid) {
  switch (pid) {
    case OV2640_PID:
      return "OV2640";
    case OV3660_PID:
      return "OV3660";
    case OV5640_PID:
      return "OV5640";
    default:
      return "Unknown";
  }
}

String selectedIf(bool condition) {
  return condition ? " selected" : "";
}

String frameSizeName(framesize_t size) {
  switch (size) {
    case FRAMESIZE_QVGA:
      return "QVGA";
    case FRAMESIZE_VGA:
      return "VGA";
    case FRAMESIZE_SVGA:
      return "SVGA";
    case FRAMESIZE_XGA:
      return "XGA";
    default:
      return "QVGA";
  }
}

framesize_t sanitizeFrameSize(framesize_t size) {
  if (!psramFound() && size > FRAMESIZE_VGA) {
    return FRAMESIZE_VGA;
  }
  return size;
}

framesize_t parseFrameSize(const String &value) {
  if (value == "VGA") {
    return FRAMESIZE_VGA;
  }
  if (value == "SVGA") {
    return FRAMESIZE_SVGA;
  }
  if (value == "XGA") {
    return FRAMESIZE_XGA;
  }
  return FRAMESIZE_QVGA;
}

String getParamValue(const QueryParams &params, const char *key) {
  for (int i = 0; i < params.count; i++) {
    if (String(params.params[i].key) == key) {
      return String(params.params[i].value);
    }
  }
  return "";
}

bool initCamera() {
  if (cameraInitialized) {
    esp_camera_deinit();
    cameraInitialized = false;
    delay(200);
  }

  currentFrameSize = sanitizeFrameSize(currentFrameSize);

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
  config.frame_size = currentFrameSize;
  config.jpeg_quality = currentJpegQuality;
  config.fb_count = psramFound() ? 2 : 1;
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.grab_mode = psramFound() ? CAMERA_GRAB_LATEST : CAMERA_GRAB_WHEN_EMPTY;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%X\n", err);
    return false;
  }

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor != nullptr) {
    sensor->set_framesize(sensor, currentFrameSize);
    sensor->set_vflip(sensor, currentRotate180 ? 1 : 0);
    sensor->set_hmirror(sensor, currentRotate180 ? 1 : 0);
    Serial.print("Kamera szenzor: ");
    Serial.println(sensorName(sensor->id.PID));
  }

  cameraInitialized = true;
  return true;
}

String buildHomePage() {
  sensor_t *sensor = esp_camera_sensor_get();
  String html;
  html.reserve(5000);
  html += F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>");
  html += F("<title>ESP32-S3 Kamera</title><style>");
  html += F("body{font-family:Arial,sans-serif;text-align:center;margin:0;padding:16px;background:#111;color:#eee;}");
  html += F(".card{max-width:980px;margin:0 auto;background:#1d1d1d;padding:20px;border-radius:14px;}");
  html += F("img{width:100%;max-width:800px;border-radius:10px;background:#000;}select,button{padding:10px 12px;margin:6px;border-radius:8px;border:1px solid #444;font-size:15px;}");
  html += F("button{background:#2d6cdf;color:#fff;cursor:pointer;}label{display:inline-block;margin:8px;color:#ddd;}.status{color:#9ad27a;margin:12px 0;}");
  html += F(".hint{color:#bbb;font-size:14px;}</style></head><body><div class='card'>");
  html += F("<h1>ESP32-S3 kamera</h1>");
  html += F("<div class='status'>");
  html += lastStatusMessage;
  html += F("</div><p>Erzekelt szenzor: ");
  html += (sensor != nullptr ? sensorName(sensor->id.PID) : "unknown");
  html += F("</p><p>PSRAM: ");
  html += String(ESP.getPsramSize());
  html += F(" byte</p>");
  html += F("<img id='cam' src='/capture' alt='ESP32-S3 kamera kep'>");
  html += F("<p><a href='/capture' target='_blank' style='color:#9ec4ff;'>Allokep kulon megnyitasa</a> | <a href='/info' target='_blank' style='color:#9ec4ff;'>Statusz JSON</a></p>");
  html += F("<form action='/config' method='get'>");
  html += F("<label>Forgatas <select name='rotation'>");
  html += "<option value='0'" + selectedIf(!currentRotate180) + ">0 fok</option>";
  html += "<option value='180'" + selectedIf(currentRotate180) + ">180 fok</option>";
  html += F("</select></label>");
  html += F("<label>Felbontas <select name='size'>");
  html += "<option value='QVGA'" + selectedIf(currentFrameSize == FRAMESIZE_QVGA) + ">QVGA 320x240</option>";
  html += "<option value='VGA'" + selectedIf(currentFrameSize == FRAMESIZE_VGA) + ">VGA 640x480</option>";
  html += "<option value='SVGA'" + selectedIf(currentFrameSize == FRAMESIZE_SVGA) + ">SVGA 800x600</option>";
  html += "<option value='XGA'" + selectedIf(currentFrameSize == FRAMESIZE_XGA) + ">XGA 1024x768</option>";
  html += F("</select></label>");
  html += F("<label>JPEG minoseg <select name='quality'>");
  html += "<option value='10'" + selectedIf(currentJpegQuality == 10) + ">10 - jobb kep</option>";
  html += "<option value='12'" + selectedIf(currentJpegQuality == 12) + ">12 - alap</option>";
  html += "<option value='15'" + selectedIf(currentJpegQuality == 15) + ">15 - kisebb meret</option>";
  html += "<option value='20'" + selectedIf(currentJpegQuality == 20) + ">20 - gyengebb kep</option>";
  html += F("</select></label>");
  html += F("<div><button type='submit'>Beallitasok mentese</button></div></form>");
  html += F("<p class='hint'>A kamera kep 1 mp-enkent ujratoltodik. A kisebb JPEG ertek jobb kepminoseget jelent. PSRAM nelkul a nagyobb felbontas VGA-ra korlatozodik.</p>");
  html += F("</div><script>function refreshImage(){document.getElementById('cam').src='/capture?t='+Date.now();}setInterval(refreshImage,1000);</script></body></html>");
  return html;
}

void sendRedirect(WiFiClient &client, const char *location) {
  client.println("HTTP/1.1 303 See Other");
  client.print("Location: ");
  client.println(location);
  client.println("Connection: close");
  client.println();
}

void handleHome(WiFiClient &client, const String &method, const String &request, const QueryParams &params, const String &jsonData) {
  String html = buildHomePage();
  server.sendResponse(client, html.c_str());
}

void handleInfo(WiFiClient &client, const String &method, const String &request, const QueryParams &params, const String &jsonData) {
  sensor_t *sensor = esp_camera_sensor_get();
  String body = "{\"ip\":\"" + WiFi.localIP().toString() + "\",\"psram\":" + String(ESP.getPsramSize()) + ",\"sensor\":\"";
  body += sensor != nullptr ? sensorName(sensor->id.PID) : "unknown";
  body += "\",\"rotation\":" + String(currentRotate180 ? 180 : 0);
  body += ",\"resolution\":\"" + frameSizeName(currentFrameSize) + "\",\"quality\":" + String(currentJpegQuality) + "}";
  server.sendResponse(client, body.c_str(), "application/json");
}

void handleCapture(WiFiClient &client, const String &method, const String &request, const QueryParams &params, const String &jsonData) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb == nullptr) {
    client.println("HTTP/1.1 503 Service Unavailable");
    client.println("Content-Type: text/plain; charset=utf-8");
    client.println("Connection: close");
    client.println();
    client.print("Nem sikerult kepet olvasni a kamerabol.");
    return;
  }

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: image/jpeg");
  client.print("Content-Length: ");
  client.println(fb->len);
  client.println("Cache-Control: no-store, no-cache, must-revalidate, max-age=0");
  client.println("Pragma: no-cache");
  client.println("Connection: close");
  client.println();
  client.write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void handleConfig(WiFiClient &client, const String &method, const String &request, const QueryParams &params, const String &jsonData) {
  framesize_t oldFrameSize = currentFrameSize;
  int oldQuality = currentJpegQuality;
  bool oldRotate180 = currentRotate180;

  String rotation = getParamValue(params, "rotation");
  String size = getParamValue(params, "size");
  String quality = getParamValue(params, "quality");

  currentRotate180 = (rotation == "180");
  currentFrameSize = sanitizeFrameSize(parseFrameSize(size));

  int parsedQuality = quality.toInt();
  if (parsedQuality >= 8 && parsedQuality <= 30) {
    currentJpegQuality = parsedQuality;
  }

  if (initCamera()) {
    lastStatusMessage = "Beallitasok frissitve: " + frameSizeName(currentFrameSize) + ", JPEG " + String(currentJpegQuality) + ", forgatas " + String(currentRotate180 ? 180 : 0) + " fok.";
  } else {
    currentFrameSize = oldFrameSize;
    currentJpegQuality = oldQuality;
    currentRotate180 = oldRotate180;
    initCamera();
    lastStatusMessage = "A kert beallitas nem indult el, visszaallitottam az elozo mukodo konfiguraciot.";
  }

  sendRedirect(client, "/");
}

void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  Serial.print("Csatlakozas WiFi-re: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

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
  delay(1200);
  Serial.println();
  Serial.println("Hello, világ!");
  Serial.println("Indul a rendszer.");
  Serial.println("ESP32-S3 kamera webszerver indul...");
  Serial.println("Rendszerinditas folyamatban...");
  Serial.printf("Flash size: %u bytes\n", ESP.getFlashChipSize());
  Serial.printf("PSRAM size: %u bytes\n", ESP.getPsramSize());

  currentFrameSize = psramFound() ? FRAMESIZE_SVGA : FRAMESIZE_QVGA;
  currentJpegQuality = psramFound() ? 10 : 12;

  Serial.println("Kamera inicializalasa...");
  if (!initCamera()) {
    Serial.println("Ellenorizd a kamera bekotest vagy a pinoutot.");
    return;
  }
  Serial.println("Kamera sikeresen inicializalva.");

  Serial.println("Csatlakozas a Wi-Fi halozathoz...");
  connectToWiFi();

  Serial.println("Webszerver vegpontok beallitasa...");
  server.addRoute("/", handleHome);
  server.addRoute("/capture", handleCapture);
  server.addRoute("/config", handleConfig);
  server.addRoute("/info", handleInfo);
  server.begin();

  Serial.println("Webszerver elindult.");
  Serial.print("Nyisd meg ezt a cimet: http://");
  Serial.println(WiFi.localIP());
}

void loop() {
  server.handleClient();
}
