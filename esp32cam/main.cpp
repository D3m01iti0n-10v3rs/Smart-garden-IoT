#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "server_client.h"

//const char* WIFI_SSID = "Xom Tro";
//const char* WIFI_PASSWORD = "xomtro247";
//const char* WIFI_SSID = "ThisConversation";
//const char* WIFI_PASSWORD = "isover...";

const char* AP2_SSID = "That whosoever believeth in him";
const char* AP2_PASS = "77777777";
const char* WS_HOST = "gloriainexcelsisdeo";
const int   WS_PORT = 3000;

const int TARGET_FPS = 15;
const int FRAME_INTERVAL_MS = 1000 / TARGET_FPS;

#define FLASH_LED 4

// esp32 cam pin map
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

static unsigned long lastFrameTime = 0;
static bool prevConnected  = false;
static unsigned long lastWiFiCheck = 0;

// sets camera's pins and conf
bool initCamera() {
    camera_config_t cfg; // camera object

    // pins
    cfg.ledc_channel= LEDC_CHANNEL_0;
    cfg.ledc_timer = LEDC_TIMER_0;
    cfg.pin_d0 = Y2_GPIO_NUM;
    cfg.pin_d1 = Y3_GPIO_NUM;
    cfg.pin_d2 = Y4_GPIO_NUM;
    cfg.pin_d3 = Y5_GPIO_NUM;
    cfg.pin_d4 = Y6_GPIO_NUM;
    cfg.pin_d5 = Y7_GPIO_NUM;
    cfg.pin_d6 = Y8_GPIO_NUM;
    cfg.pin_d7 = Y9_GPIO_NUM;
    cfg.pin_xclk = XCLK_GPIO_NUM;
    cfg.pin_pclk = PCLK_GPIO_NUM;
    cfg.pin_vsync =VSYNC_GPIO_NUM;
    cfg.pin_href = HREF_GPIO_NUM;
    cfg.pin_sccb_sda = SIOD_GPIO_NUM;
    cfg.pin_sccb_scl = SIOC_GPIO_NUM;
    cfg.pin_pwdn = PWDN_GPIO_NUM;
    cfg.pin_reset = RESET_GPIO_NUM;
    cfg.xclk_freq_hz = 20000000;
    cfg.pixel_format = PIXFORMAT_JPEG;

    cfg.frame_size = FRAMESIZE_VGA;
    cfg.jpeg_quality = 20;
    cfg.fb_count = 2;

    if (esp_camera_init(&cfg) != ESP_OK) {
        Serial.println("Camera init failed");
        return false;
    }

    // conf
    sensor_t* s = esp_camera_sensor_get();
    s->set_brightness(s, 0);
    s->set_contrast(s,0);
    s->set_saturation(s, 0);
    s->set_whitebal(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_hmirror(s, 0);
    s->set_vflip(s, 0);

    Serial.println("Camera init ok");
    return true;
}

// convert string sent by server to correct data type
framesize_t framesizeFromString(const String& s) {
    if (s == "QQVGA") return FRAMESIZE_QQVGA;
    if (s == "QVGA")  return FRAMESIZE_QVGA;
    if (s == "CIF")   return FRAMESIZE_CIF;
    if (s == "VGA")   return FRAMESIZE_VGA;
    if (s == "SVGA")  return FRAMESIZE_SVGA;
    if (s == "XGA")   return FRAMESIZE_XGA;
    if (s == "SXGA")  return FRAMESIZE_SXGA;
    if (s == "UXGA")  return FRAMESIZE_UXGA;
    return FRAMESIZE_VGA;
}

// settings callback
void onCamMessage(JsonDocument& doc) {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return;

    if (doc["framesize"].is<const char*>()) {
        framesize_t fs = framesizeFromString(doc["framesize"].as<String>());
        s->set_framesize(s, fs);
        Serial.printf("[CAM] framesize -> %s\n", doc["framesize"].as<const char*>());
    }

    if (!doc["quality"].isNull()) {
        int val = constrain(doc["quality"].as<int>(), 0, 63);
        s->set_quality(s, val);
        Serial.printf("[CAM] quality -> %d\n", val);
    }

    if (!doc["vflip"].isNull()) {
        int val = doc["vflip"].as<int>();
        s->set_vflip(s, val ? 1 : 0);
        Serial.printf("[CAM] vflip -> %d\n", val);
    }

    if (!doc["hmirror"].isNull()) {
        int val = doc["hmirror"].as<int>();
        s->set_hmirror(s, val ? 1 : 0);
        Serial.printf("[CAM] hmirror -> %d\n", val);
    }

    if (!doc["brightness"].isNull()) {
        int val = constrain(doc["brightness"].as<int>(), -2, 2);
        s->set_brightness(s, val);
        Serial.printf("[CAM] brightness -> %d\n", val);
    }

    if (!doc["contrast"].isNull()) {
        int val = constrain(doc["contrast"].as<int>(), -2, 2);
        s->set_contrast(s, val);
        Serial.printf("[CAM] contrast -> %d\n", val);
    }

    if (!doc["saturation"].isNull()) {
        int val = constrain(doc["saturation"].as<int>(), -2, 2);
        s->set_saturation(s, val);
        Serial.printf("[CAM] saturation -> %d\n", val);
    }
}

// get wifi creds from esp32
bool fetchCredentials(String& ssid, String& pass) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(AP2_SSID, AP2_PASS);

    Serial.print("Connecting to gateway AP2");
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > 15000) {
            Serial.println("\n[CREDS] Timed out");
            return false;
        }
        delay(500); Serial.print(".");
    }
    Serial.println("\nConnected to AP2");

    HTTPClient http;
    http.begin("http://10.0.0.1/creds");
    int code = http.GET();
    String body = http.getString();
    http.end();

    auto sendResult = [](const char* endpoint) {
        HTTPClient h;
        h.begin(String("http://10.0.0.1/") + endpoint);
        h.addHeader("Content-Type", "application/x-www-form-urlencoded");
        h.POST("device=cam");
        h.end();
    };

    if (code != 200) {
        Serial.printf("GET credentials failed: %d\n", code);
        sendResult("nack");
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        Serial.println("JSON credentials parse failed");
        sendResult("nack");
        return false;
    }

    ssid = doc["ssid"].as<String>();
    pass = doc["pass"].as<String>();

    sendResult("ack");
    WiFi.disconnect(false);
    delay(500);
    return true;
}

void setup() {
    Serial.begin(115200);

    pinMode(FLASH_LED, OUTPUT);
    digitalWrite(FLASH_LED, LOW);

    if (!initCamera()) {
        Serial.println("Camera failed");
        while (true) {
            digitalWrite(FLASH_LED, !digitalRead(FLASH_LED));
            delay(100);
        }
    }

    /*
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("[WIFI] Connecting");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        digitalWrite(FLASH_LED, !digitalRead(FLASH_LED));
        Serial.print(".");
    }
    */

    // connect to esp32 to get wifi creds
    String wifiSSID, wifiPass;
    while (!fetchCredentials(wifiSSID, wifiPass)) {
        Serial.println("Retrying in 3s");
        delay(3000);
    }

    WiFi.mode(WIFI_OFF);
    delay(100);

    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
    Serial.printf("Connecting to %s\n", wifiSSID.c_str());

    while (WiFi.status() != WL_CONNECTED) {
        for (uint8_t i = 0; i < 10; i++) {
            if (WiFi.status() == WL_CONNECTED) break; 
            delay(500);
            digitalWrite(FLASH_LED, !digitalRead(FLASH_LED));
            Serial.print(".");
        }
        
        if (WiFi.status() != WL_CONNECTED) {
            Serial.printf("\n[WIFI] Connect to %s failed. Retrying...\n", wifiSSID.c_str());
            WiFi.disconnect();
            WiFi.reconnect();
        }
    }

    /*
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        digitalWrite(FLASH_LED, !digitalRead(FLASH_LED));
        Serial.print(".");
    }
    */

    Serial.printf("\n[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
    digitalWrite(FLASH_LED, HIGH);

    MDNS.begin("esp32-cam"); // send mdns query
    server_on_response(onCamMessage); // server resp with its ip
    server_connect(WS_HOST, WS_PORT, "/cam"); // connect
}

void loop() {

    // reconnect to wifi if needed
    if (WiFi.status() != WL_CONNECTED) {
        unsigned long now = millis();
        if (now - lastWiFiCheck > 5000) {
            Serial.println("Disconnected from wifi. Attempting to reconnect");
            WiFi.disconnect();
            WiFi.reconnect(); 
            lastWiFiCheck = now;
        }
        return;
    }

    server_loop(); // maintain connection + auto reconnect

    bool connected = server_is_connected();
    if (connected != prevConnected) {
        prevConnected = connected;
        digitalWrite(FLASH_LED, connected ? LOW : HIGH);
        Serial.println(connected ? "[WS] Connected" : "[WS] Disconnected");
    }

    if (!connected) return;

    // capture frame basw on fps
    unsigned long now = millis();
    if (now - lastFrameTime < FRAME_INTERVAL_MS) return;
    lastFrameTime = now;

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("[CAM] Capture failed");
        return;
    }

    // send to server
    server_send_binary(fb->buf, fb->len);
    esp_camera_fb_return(fb);
}