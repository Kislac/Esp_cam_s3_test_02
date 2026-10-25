#include "esp_camera.h"
#include <WiFi.h>

#define CAMERA_MODEL_ESP32S3_EYE
#include "camera_pins.h"

// WiFi configuration
const char* ssid_Router     = "Kis-Szikora"; // Router SSID
const char* password_Router = "Kishangya"; // Router password

WiFiServer cameraServer(80);
WiFiServer streamServer(81);
bool cameraReady = false;
bool serverStarted = false;
bool hasPsram = false;
volatile uint8_t targetFps = 1;
TaskHandle_t streamTaskHandle = nullptr;

bool cameraSetup();
bool startServers();
void handleHttpClient(WiFiClient &client);
bool streamCamera(WiFiClient &client);
bool writeAll(WiFiClient &client, const uint8_t *data, size_t length);
void streamServerTask(void *parameter);
bool sendCameraStatus(WiFiClient &client);
bool handleCameraControl(WiFiClient &client, const String &requestLine);
bool getQueryParameter(const String &query, const char *name, String &value);

static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="hu">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ESP32 kamera</title>
    <style>
        :root { color-scheme: dark; font-family: system-ui, sans-serif; background: #101820; color: #f2f5f7; }
        body { margin: 0; padding: 20px; }
        main { max-width: 1120px; margin: 0 auto; }
        h1 { margin: 0 0 16px; font-size: 22px; }
        .layout { display: grid; grid-template-columns: minmax(0, 1fr) 280px; gap: 18px; align-items: start; }
        img { display: block; width: 100%; height: auto; min-height: 180px; background: #05080a; }
        section { padding: 14px; background: #19252d; border: 1px solid #34434c; border-radius: 6px; }
        .control { display: grid; grid-template-columns: 1fr auto; gap: 8px 12px; margin: 12px 0; align-items: center; }
        .control input[type="range"], .control select { grid-column: 1 / -1; width: 100%; }
        select, input { accent-color: #4cc9a6; }
        select { padding: 7px; color: inherit; background: #101820; border: 1px solid #52616a; }
        .toggle { display: flex; justify-content: space-between; padding: 9px 0; border-top: 1px solid #34434c; }
        #message { min-height: 1.3em; color: #9adbc8; font-size: 13px; }
        @media (max-width: 760px) { body { padding: 12px; } .layout { grid-template-columns: 1fr; } }
    </style>
</head>
<body>
    <main>
        <h1>ESP32 kamera</h1>
        <div class="layout">
            <img id="stream" alt="Élő kamerakép">
            <section>
                <label class="control">Felbontás<select id="framesize"></select></label>
                <label class="control">FPS célérték <output id="fps-value"></output><input id="fps" type="range" min="1" max="20"></label>
                <label class="control">JPEG-minőség <output id="quality-value"></output><input id="quality" type="range" min="10" max="63"></label>
                <label class="control">Fényerő <output id="brightness-value"></output><input id="brightness" type="range" min="-2" max="2"></label>
                <label class="control">Kontraszt <output id="contrast-value"></output><input id="contrast" type="range" min="-2" max="2"></label>
                <label class="control">Telítettség <output id="saturation-value"></output><input id="saturation" type="range" min="-2" max="2"></label>
                <label class="control">Fehéregyensúly mód<select id="wb_mode"><option value="0">Automatikus</option><option value="1">Napfény</option><option value="2">Felhős</option><option value="3">Iroda</option><option value="4">Otthon</option></select></label>
                <label class="toggle">Automatikus fehéregyensúly<input id="awb" type="checkbox"></label>
                <label class="toggle">Automatikus expozíció<input id="aec" type="checkbox"></label>
                <label class="toggle">Automatikus erősítés<input id="agc" type="checkbox"></label>
                <label class="toggle">Vízszintes tükrözés<input id="hmirror" type="checkbox"></label>
                <label class="toggle">Függőleges tükrözés<input id="vflip" type="checkbox"></label>
                <div id="message" role="status"></div>
            </section>
        </div>
    </main>
    <script>
        const message = document.getElementById('message');
        const framesize = document.getElementById('framesize');
        async function setControl(name, value) {
            message.textContent = 'Beállítás...';
            try {
                const response = await fetch(`/control?var=${name}&val=${value}`);
                if (!response.ok) throw new Error('A kamera elutasította a beállítást.');
                message.textContent = 'Beállítva';
            } catch (error) {
                message.textContent = error.message;
            }
        }
        document.querySelectorAll('input[type="range"], select').forEach((input) => {
            input.addEventListener('change', () => setControl(input.id, input.value));
        });
        document.querySelectorAll('input[type="checkbox"]').forEach((input) => {
            input.addEventListener('change', () => setControl(input.id, input.checked ? 1 : 0));
        });
        document.querySelectorAll('input[type="range"]').forEach((input) => {
            input.addEventListener('input', () => {
                document.getElementById(`${input.id}-value`).value = input.value;
            });
        });
        async function loadSettings() {
            const response = await fetch('/status');
            const status = await response.json();
            status.resolutions.forEach((resolution) => {
                const option = document.createElement('option');
                option.value = resolution.value;
                option.textContent = resolution.label;
                framesize.append(option);
            });
            ['framesize', 'fps', 'quality', 'brightness', 'contrast', 'saturation'].forEach((name) => {
                document.getElementById(name).value = status[name];
            });
            ['hmirror', 'vflip'].forEach((name) => {
                document.getElementById(name).checked = Boolean(status[name]);
            });
            document.querySelectorAll('input[type="range"]').forEach((input) => {
                document.getElementById(`${input.id}-value`).value = input.value;
            });
            document.getElementById('stream').src = `http://${location.hostname}:81/stream`;
        }
        loadSettings().catch(() => { message.textContent = 'Nem sikerült betölteni a kamera beállításait.'; });
    </script>
</body>
</html>
)rawliteral";

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(false);
    Serial.println();
    Serial.printf("Reset reason: %d\n", static_cast<int>(esp_reset_reason()));

    cameraReady = cameraSetup();
    if (!cameraReady) {
        Serial.println("Camera unavailable; HTTP server not started.");
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    Serial.printf("Connecting to %s ", ssid_Router);
    WiFi.begin(ssid_Router, password_Router);

    uint8_t retries = 30;
    while (WiFi.status() != WL_CONNECTED && retries--) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        startServers();
        Serial.println("WiFi connected");
    } else {
        Serial.println("WiFi connection failed; will retry.");
    }
}

void loop() {
    if (!cameraReady) {
    delay(1000);
    return;
    }

    if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
        delay(1000);
        return;
    }

    if (!serverStarted) {
        startServers();
    }

    WiFiClient client = cameraServer.accept();
    if (client) {
        handleHttpClient(client);
        client.stop();
    }
    delay(2);
}

void handleHttpClient(WiFiClient &client) {
    client.setTimeout(2000);
    String requestLine = client.readStringUntil('\n');
    requestLine.trim();

    while (client.connected()) {
        String header = client.readStringUntil('\n');
        if (header == "\r" || header.length() == 0) {
            break;
        }
    }

    if (requestLine.startsWith("GET /status ")) {
        sendCameraStatus(client);
        return;
    }

    if (requestLine.startsWith("GET /control?")) {
        handleCameraControl(client, requestLine);
        return;
    }

    if (requestLine.startsWith("GET / ") || requestLine.startsWith("GET /?")) {
        client.print("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n");
        client.write(reinterpret_cast<const uint8_t *>(INDEX_HTML), sizeof(INDEX_HTML) - 1);
        return;
    }

    if (requestLine.startsWith("GET /stream ")) {
        client.printf("HTTP/1.1 307 Temporary Redirect\r\nLocation: http://%s:81/stream\r\nConnection: close\r\n\r\n", WiFi.localIP().toString().c_str());
        return;
    }

    client.print("HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nNot found");
}

bool startServers() {
    cameraServer.begin();
    streamServer.begin();
    if (xTaskCreatePinnedToCore(streamServerTask, "cameraStream", 8192, nullptr, 1, &streamTaskHandle, 0) != pdPASS) {
        cameraServer.end();
        streamServer.end();
        Serial.println("Could not start camera stream task.");
        return false;
    }

    serverStarted = true;
    Serial.print("Camera ready: http://");
    Serial.println(WiFi.localIP());
    return true;
}

void streamServerTask(void *parameter) {
    (void)parameter;
    for (;;) {
        WiFiClient client = streamServer.accept();
        if (client) {
            client.setNoDelay(true);
            streamCamera(client);
            client.stop();
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

bool sendCameraStatus(WiFiClient &client) {
    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor == nullptr) {
        client.print("HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n");
        return false;
    }

    char resolutions[768];
    if (hasPsram) {
        snprintf(resolutions, sizeof(resolutions),
                 "[{\"value\":%d,\"label\":\"QQVGA 160x120\"},{\"value\":%d,\"label\":\"QCIF 176x144\"},{\"value\":%d,\"label\":\"HQVGA 240x160\"},{\"value\":%d,\"label\":\"QVGA 320x240\"},{\"value\":%d,\"label\":\"CIF 400x296\"},{\"value\":%d,\"label\":\"VGA 640x480\"},{\"value\":%d,\"label\":\"SVGA 800x600\"},{\"value\":%d,\"label\":\"XGA 1024x768\"},{\"value\":%d,\"label\":\"SXGA 1280x1024\"},{\"value\":%d,\"label\":\"UXGA 1600x1200\"}]",
                 FRAMESIZE_QQVGA, FRAMESIZE_QCIF, FRAMESIZE_HQVGA, FRAMESIZE_QVGA, FRAMESIZE_CIF, FRAMESIZE_VGA, FRAMESIZE_SVGA, FRAMESIZE_XGA, FRAMESIZE_SXGA, FRAMESIZE_UXGA);
    } else {
        snprintf(resolutions, sizeof(resolutions),
                 "[{\"value\":%d,\"label\":\"QQVGA 160x120\"},{\"value\":%d,\"label\":\"QCIF 176x144\"},{\"value\":%d,\"label\":\"HQVGA 240x160\"},{\"value\":%d,\"label\":\"QVGA 320x240\"}]",
                 FRAMESIZE_QQVGA, FRAMESIZE_QCIF, FRAMESIZE_HQVGA, FRAMESIZE_QVGA);
    }

    char json[768];
    int length = snprintf(json, sizeof(json),
                          "{\"psram\":%s,\"resolutions\":%s,\"framesize\":%d,\"fps\":%u,\"quality\":%d,\"brightness\":%d,\"contrast\":%d,\"saturation\":%d,\"hmirror\":%d,\"vflip\":%d}",
                          hasPsram ? "true" : "false", resolutions, sensor->status.framesize,
                          static_cast<unsigned>(targetFps), sensor->status.quality, sensor->status.brightness, sensor->status.contrast,
                          sensor->status.saturation, sensor->status.hmirror, sensor->status.vflip);
    client.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
    return length > 0 && writeAll(client, reinterpret_cast<const uint8_t *>(json), static_cast<size_t>(length));
}

bool getQueryParameter(const String &query, const char *name, String &value) {
    String key = String(name) + "=";
    int start = query.indexOf(key);
    if (start < 0) {
        return false;
    }
    start += key.length();
    int end = query.indexOf('&', start);
    value = end < 0 ? query.substring(start) : query.substring(start, end);
    return value.length() > 0;
}

bool handleCameraControl(WiFiClient &client, const String &requestLine) {
    int queryStart = requestLine.indexOf('?');
    int queryEnd = requestLine.indexOf(' ', queryStart);
    if (queryStart < 0 || queryEnd < 0) {
        client.print("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n");
        return false;
    }

    String query = requestLine.substring(queryStart + 1, queryEnd);
    String variable;
    String valueText;
    if (!getQueryParameter(query, "var", variable) || !getQueryParameter(query, "val", valueText)) {
        client.print("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n");
        return false;
    }

    int value = valueText.toInt();
    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor == nullptr) {
        client.print("HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n");
        return false;
    }

    int result = -1;
    if (variable == "framesize" && value >= FRAMESIZE_QQVGA && value <= (hasPsram ? FRAMESIZE_UXGA : FRAMESIZE_QVGA)) result = sensor->set_framesize(sensor, static_cast<framesize_t>(value));
    else if (variable == "fps" && value >= 1 && value <= 20) {
        targetFps = static_cast<uint8_t>(value);
        result = 0;
    }
    else if (variable == "quality" && value >= 10 && value <= 63) result = sensor->set_quality(sensor, value);
    else if (variable == "brightness" && value >= -2 && value <= 2) result = sensor->set_brightness(sensor, value);
    else if (variable == "contrast" && value >= -2 && value <= 2) result = sensor->set_contrast(sensor, value);
    else if (variable == "saturation" && value >= -2 && value <= 2) result = sensor->set_saturation(sensor, value);
    else if (variable == "wb_mode" && value >= 0 && value <= 4) result = sensor->set_wb_mode(sensor, value);
    else if (variable == "awb" && (value == 0 || value == 1)) result = sensor->set_whitebal(sensor, value);
    else if (variable == "aec" && (value == 0 || value == 1)) result = sensor->set_exposure_ctrl(sensor, value);
    else if (variable == "agc" && (value == 0 || value == 1)) result = sensor->set_gain_ctrl(sensor, value);
    else if (variable == "hmirror" && (value == 0 || value == 1)) result = sensor->set_hmirror(sensor, value);
    else if (variable == "vflip" && (value == 0 || value == 1)) result = sensor->set_vflip(sensor, value);

    if (result != 0) {
        client.print("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\nUnsupported camera setting");
        return false;
    }

    client.print("HTTP/1.1 204 No Content\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
    return true;
}

bool streamCamera(WiFiClient &client) {
    client.print("HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=frame\r\nCache-Control: no-cache\r\nPragma: no-cache\r\nConnection: close\r\n\r\n");

    static const char boundary[] = "--frame\r\n";
    static const char frameEnding[] = "\r\n";
    char partHeader[80];
    uint32_t lastReport = millis();
    uint32_t lastFrameAt = lastReport;
    uint32_t framesSent = 0;
    uint32_t captureFailures = 0;
    uint32_t sendFailures = 0;
    const char *lastFailure = "none";

    Serial.println("Camera stream client connected.");

    while (client.connected()) {
        uint32_t frameStartedAt = millis();
        uint32_t captureStarted = millis();
        camera_fb_t *frame = esp_camera_fb_get();
        uint32_t captureDuration = millis() - captureStarted;
        if (frame == nullptr) {
            ++captureFailures;
            lastFailure = "camera_capture";
            delay(30);
        } else {
            lastFrameAt = millis();
            int headerLength = snprintf(partHeader, sizeof(partHeader),
                                        "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                                        static_cast<unsigned>(frame->len));
            const char *sendStage = "boundary";
            bool sent = writeAll(client, reinterpret_cast<const uint8_t *>(boundary), sizeof(boundary) - 1);
            if (sent) {
                sendStage = "part_header";
                sent = headerLength > 0 &&
                       writeAll(client, reinterpret_cast<const uint8_t *>(partHeader), static_cast<size_t>(headerLength));
            }
            if (sent) {
                sendStage = "jpeg_data";
                sent = writeAll(client, frame->buf, frame->len);
            }
            if (sent) {
                sendStage = "frame_ending";
                sent = writeAll(client, reinterpret_cast<const uint8_t *>(frameEnding), sizeof(frameEnding) - 1);
            }

            esp_camera_fb_return(frame);
            if (sent) {
                ++framesSent;
                lastFailure = "none";
                uint32_t frameIntervalMs = 1000 / targetFps;
                uint32_t frameElapsedMs = millis() - frameStartedAt;
                if (frameElapsedMs < frameIntervalMs) {
                    delay(frameIntervalMs - frameElapsedMs);
                }
            } else {
                ++sendFailures;
                lastFailure = sendStage;
                Serial.printf("Camera stream send failed at %s; client_connected=%d\n", sendStage, client.connected());
                break;
            }
        }

        uint32_t now = millis();
        if (now - lastReport >= 5000) {
            Serial.printf("Stream diag: frames=%lu capture_fail=%lu send_fail=%lu last_frame_age_ms=%lu capture_ms=%lu last_error=%s heap=%u min_heap=%u stack_free=%u rssi=%d\n",
                          static_cast<unsigned long>(framesSent),
                          static_cast<unsigned long>(captureFailures),
                          static_cast<unsigned long>(sendFailures),
                          static_cast<unsigned long>(now - lastFrameAt),
                          static_cast<unsigned long>(captureDuration),
                          lastFailure,
                          ESP.getFreeHeap(),
                          ESP.getMinFreeHeap(),
                          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                          WiFi.RSSI());
            lastReport = now;
        }
    }

    Serial.printf("Camera stream ended: frames=%lu capture_fail=%lu send_fail=%lu client_connected=%d\n",
                  static_cast<unsigned long>(framesSent),
                  static_cast<unsigned long>(captureFailures),
                  static_cast<unsigned long>(sendFailures),
                  client.connected());
    return !client.connected();
}

bool writeAll(WiFiClient &client, const uint8_t *data, size_t length) {
    size_t sentBytes = 0;
    uint32_t lastProgress = millis();

    while (sentBytes < length && client.connected()) {
        size_t sent = client.write(data + sentBytes, length - sentBytes);
        if (sent > 0) {
            sentBytes += sent;
            lastProgress = millis();
        } else if (millis() - lastProgress > 2000) {
            return false;
        } else {
            delay(1);
        }
    }

    return sentBytes == length;
}

bool cameraSetup() {
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

    hasPsram = psramFound();
    Serial.printf("PSRAM %s\n", hasPsram ? "detected" : "not detected");
    config.xclk_freq_hz = 20000000;
    config.frame_size = hasPsram ? FRAMESIZE_UXGA : FRAMESIZE_QVGA;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode = hasPsram ? CAMERA_GRAB_LATEST : CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = hasPsram ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
    config.jpeg_quality = hasPsram ? 12 : 15;
    config.fb_count = hasPsram ? 2 : 1;

    esp_err_t error = esp_camera_init(&config);
    if (error != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", error);
        return false;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor != nullptr) {
        sensor->set_whitebal(sensor, 0);
        sensor->set_awb_gain(sensor, 0);
        sensor->set_exposure_ctrl(sensor, 0);
        sensor->set_aec2(sensor, 0);
        sensor->set_gain_ctrl(sensor, 0);
        sensor->set_vflip(sensor, 0);
        sensor->set_brightness(sensor, 0);
        sensor->set_saturation(sensor, -2);
        sensor->set_contrast(sensor, 0);
        sensor->set_sharpness(sensor, -2);
        sensor->set_denoise(sensor, 0);
        sensor->set_quality(sensor, config.jpeg_quality);
        sensor->set_gainceiling(sensor, static_cast<gainceiling_t>(6));
        sensor->set_agc_gain(sensor, 0);
        sensor->set_aec_value(sensor, 300);
        sensor->set_special_effect(sensor, 0);
    }

    Serial.println("Camera configuration complete!");
    return true;
}