#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "server_client.h"

#define LED_ONBOARD 2

const char* WIFI_SSID = "Xom Tro";
const char* WIFI_PASS = "xomtro247";
//const char* WIFI_SSID = "ThisConversation";
//const char* WIFI_PASS = "isover...";
const char* WS_HOST = "gloriainexcelsisdeo";
const int WS_PORT = 3000;
const char* WS_PATH = "/iot_nodes";

const char* CONF1_SSID = "For God so loved the world";
const char* CONF1_PASS = "77777777";

const char* CONF2_SSID = "That whosoever believeth in him";
const char* CONF2_PASS = "77777777";

WebServer configServer1(80);
WebServer configServer2(80);
 
String savedSSID = "";
String savedPass = "";
bool credsSaved = false;

struct sensorData{
  float soil_humidity;
  float water_level;
  float temperature;
  float pressure;
  float humidity;
};
struct sensorData sensor = {0, 0, 0, 0, 0};

struct PendingCmd{
  int id;
  char cmd[16];
  float value;
};
QueueHandle_t cmd_queue;
 
void handleRoot() {
  configServer1.send(200, "text/html",
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>"
    "body{font-family:Arial,sans-serif;background:#f4f7fb;margin:0;padding:24px;}"
    ".card{max-width:360px;margin:auto;background:#fff;padding:24px;border-radius:12px;"
    "box-shadow:0 4px 16px rgba(0,0,0,0.08);}"
    "h2{margin-top:0;color:#1f2937;}"
    "label{display:block;margin:12px 0 6px;color:#374151;font-size:14px;}"
    "input,select{width:100%;padding:10px;border:1px solid #d1d5db;border-radius:8px;box-sizing:border-box;}"
    "button{width:100%;margin-top:16px;padding:10px;border:0;border-radius:8px;"
    "background:#2563eb;color:#fff;font-size:15px;}"
    "</style></head><body>"
    "<div class='card'>"
    "<h2>WiFi Setup</h2>"
    "<form method='POST' action='/save'>"
    "<label>SSID</label><select name='ssid'>"
    + ([]() {
        String s = "";
        int n = WiFi.scanNetworks();

        for (int i = 0; i < n; i++) {
          s += "<option value='";
          s += WiFi.SSID(i);
          s += "'>";
          s += WiFi.SSID(i);
          s += " (";
          s += WiFi.RSSI(i);
          s += " dBm)</option>";
        }

        WiFi.scanDelete();
        return s;
      })()
    + "</select>"
    "<label>Password</label><input name='pass' type='password'>"
    "<button type='submit'>Save</button>"
    "</form>"
    "</div></body></html>"
  );
}
 
void handleSave() {
  savedSSID  = configServer1.arg("ssid");
  savedPass  = configServer1.arg("pass");
  credsSaved = true;
  configServer1.send(200, "text/html",
    "<html><body style='font-family:Arial;padding:20px;'>"
    "<h3>Saved. Connecting...</h3>"
    "<p>You can now close this page.</p>"
    "</body></html>"
  );
}

void handleCreds() {
  String json =
    "{"
    "\"ssid\":\"" + savedSSID + "\","
    "\"pass\":\"" + savedPass + "\""
    "}";

  configServer2.send(200, "application/json", json);
}

bool camAck = false;
bool serverAck = false;
void handleACK() {
  String device = configServer2.arg("device");

  if (device == "cam") {
    camAck = true;
    Serial.printf("[CONF2] CAM ACK\r\n");
  }
  if (device == "server") {
    serverAck = true;
    Serial.printf("[CONF2] SERVER ACK\r\n");
  }
  configServer2.send(200, "text/plain", "OK");
}

bool syncFailed = false;
void handleNACK() {
  String device = configServer2.arg("device");

  syncFailed = true;
  if (device == "cam") {
    camAck = false;
    Serial.printf("[CONF2] CAM NACK\r\n");
  }
  if (device == "server") {
    serverAck = false;
    Serial.printf("[CONF2] SERVER NACK\r\n");
  }
  configServer2.send(200, "text/plain", "OK");
}

uint8_t pending_cmd = 0x00;

SemaphoreHandle_t server_mutex;
SemaphoreHandle_t serial2_mutex;

static EventGroupHandle_t wifi_event_group;
static const EventBits_t  WIFI_CONNECTED_BIT = BIT0;
static uint8_t wifi_fail_count = 0;
static void wifi_event_handler(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      digitalWrite(LED_ONBOARD, 0);
      xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
      wifi_fail_count = 0;
      Serial.printf("\n[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
      break;

     case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      digitalWrite(LED_ONBOARD, 1);
      xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
      if (++wifi_fail_count >= 5) {
        Serial.println("[WiFi] Too many failures, restarting");
        ESP.restart();
      }
      Serial.printf("[WiFi] Reconnecting (%u/5)\n", wifi_fail_count);
      WiFi.reconnect();
      break;

    default: break;
  }
}

void on_device_update(const char* deviceID, int value) {
    Serial.printf("device %s -> %d\n", deviceID, value);
}

void on_response(JsonDocument& doc) {
  const char* type = doc["type"];
  if (strcmp(type, "commands.new") == 0) {
    PendingCmd pending;
    pending.id    = doc["id"].as<int>();
    strlcpy(pending.cmd, doc["cmd"].as<const char*>(), sizeof(pending.cmd)); // Copy string
    pending.value = doc["value"].as<float>();
    
    xQueueSend(cmd_queue, &pending, 0);
    Serial.printf("[SERVER] Queued command id=%d cmd=%s\n", pending.id, pending.cmd);
  }
}

void server_keepalive_task(void* parameter) {
  xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
  for (;;) {
    xSemaphoreTake(server_mutex, portMAX_DELAY);
    if (!server_loop()) digitalWrite(LED_ONBOARD, !digitalRead(LED_ONBOARD));
    else digitalWrite(LED_ONBOARD, 0);
    xSemaphoreGive(server_mutex);

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void sensor_task(void * parameter) {
  xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
  String data;
  vTaskDelay(pdMS_TO_TICKS(10000));
  for (;;) {
    xSemaphoreTake(serial2_mutex, portMAX_DELAY);

    while (Serial2.available()) Serial2.read();
    Serial2.printf("REQ SENSOR\r\n");
    Serial.printf("[SENSOR TASK] Serial2 SENT: REQ SENSOR\r\n");

    uint32_t t = millis();
    while (!Serial2.available()) {
      if (millis() - t > 5000) {
        Serial.println("[SENSOR TASK] Sensor request timed out");
        xSemaphoreGive(serial2_mutex);
        goto sensor_timeout;
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    data = Serial2.readStringUntil('\n');
    data.trim();
    Serial.printf("[SENSOR TASK] STM RESPOND: %s\r\n", data.c_str());

    sscanf(data.c_str(), "%f,%f,%f,%f,%f", &sensor.soil_humidity, &sensor.water_level, &sensor.temperature, &sensor.pressure, &sensor.humidity);
    xSemaphoreGive(serial2_mutex);

    xSemaphoreTake(server_mutex, portMAX_DELAY);
    server_insert_sensor("soil_humidity", sensor.soil_humidity);
    server_insert_sensor("water_level",   sensor.water_level);
    server_insert_sensor("temperature",   sensor.temperature);
    server_insert_sensor("pressure",      sensor.pressure);
    server_insert_sensor("humidity",      sensor.humidity);
    Serial.printf("[SENSOR TASK] DATA PUSHED TO SERVER\n");
    xSemaphoreGive(server_mutex);

    sensor_timeout:
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}


void cmd_task(void* parameter) {
  PendingCmd pending;
  String data;

  for (;;) {
    xQueueReceive(cmd_queue, &pending, portMAX_DELAY);
    Serial.printf("[CMD TASK] Received command id=%d cmd=%02X\n", pending.id, pending.cmd);

    xSemaphoreTake(serial2_mutex, portMAX_DELAY);

    while (Serial2.available()) Serial2.read();
    Serial2.print("REQ CMD\r\n");
    Serial.printf("[CMD TASK] Serial2 SENT: REQ CMD\r\n");

    uint32_t t = millis();
    while (!Serial2.available()) {
      if (millis() - t > 3000) {
        Serial.println("[CMD TASK] CMD ACK timed out");
        xSemaphoreGive(serial2_mutex);
        goto cmd_timeout;
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    data = Serial2.readStringUntil('\n');
    data.trim();
    Serial.printf("[CMD TASK] STM RESPOND: %s\r\n", data.c_str());

    if (data == "ACK") {
      char outBuf[32];
      snprintf(outBuf, sizeof(outBuf), "%s %d\r\n", pending.cmd, (int)pending.value);
      
      Serial2.print(outBuf);
      Serial.printf("[CMD TASK] Serial2 SENT: %s", outBuf);

      t = millis();
      while (!Serial2.available()) {
        if (millis() - t > 3000) {
          Serial.println("[CMD TASK] EXE OK timed out");
          xSemaphoreGive(serial2_mutex);
          goto cmd_timeout;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
      }
      data = Serial2.readStringUntil('\n');
      data.trim();
      Serial.printf("[CMD TASK] STM RESPOND: %s\r\n", data.c_str());

      if (data == "EXE OK") {
        xSemaphoreTake(server_mutex, portMAX_DELAY);
        server_complete_command(pending.id);
        server_update_device(String(pending.cmd).c_str(), (int)pending.value);
        xSemaphoreGive(server_mutex);
      }
    }

    xSemaphoreGive(serial2_mutex);
    cmd_timeout:;
  }
}


void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 16, 17);

  pinMode(LED_ONBOARD, OUTPUT);
  digitalWrite(LED_ONBOARD, 1);

  configServer1.on("/",     HTTP_GET,  handleRoot);
  configServer1.on("/save", HTTP_POST, handleSave);

  configServer2.on("/creds", HTTP_GET, handleCreds);
  configServer2.on("/ack", HTTP_POST, handleACK);
  configServer2.on("/nack", HTTP_POST, handleNACK);


  // test portion ==============================================================================
  AP1:
  syncFailed = false;
  camAck = false;
  serverAck = false;

  IPAddress apIP(10, 0, 0, 1);
  IPAddress apGateway(10, 0, 0, 1);
  IPAddress apSubnet(255, 255, 255, 0);

  while (1) {
    credsSaved = false;
    
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(apIP, apGateway, apSubnet);
    WiFi.softAP(CONF1_SSID, CONF1_PASS);
    Serial.printf("[BOOT] AP conf1: http://%s\n", WiFi.softAPIP().toString().c_str());
    configServer1.begin();

    while (!credsSaved) {
      configServer1.handleClient();
      delay(10);
    }

    configServer1.stop();
    WiFi.softAPdisconnect(true);

    bool connected = false;

    for (int attempt = 1; attempt <= 5; attempt++) {

      Serial.printf("[WIFI] Attempt %d connecting to %s\n",
        attempt,
        savedSSID.c_str());

      WiFi.begin(savedSSID.c_str(), savedPass.c_str());

      uint32_t start = millis();
      while (millis() - start < 10000) {

        if (WiFi.status() == WL_CONNECTED) {
          connected = true;
          break;
        }

        delay(500);
        Serial.print(".");
      }

      if (connected) break;

      Serial.println("\n[WIFI] Connection failed");
      WiFi.disconnect(true);
    }

    if (connected) {
      Serial.printf("\n[WIFI] Connected, IP: %s\n",
        WiFi.localIP().toString().c_str());
      break;
    }

    Serial.println("[WIFI] All attempts failed, reopening config AP");
  }

  WiFi.disconnect(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apGateway, apSubnet);
  WiFi.softAP(CONF2_SSID, CONF2_PASS);
  Serial.printf("[BOOT] AP conf2: http://%s\n", WiFi.softAPIP().toString().c_str());

  configServer2.begin();


  // real implementation
  while (!(camAck && serverAck) && !syncFailed) {
    configServer2.handleClient();
    delay(10);
  }
  // real implementation

  /*
  // test cam ack only
  while (!camAck && !syncFailed) {
    configServer2.handleClient();
    delay(10);
  }
  // test cam ack only
  */

  if (syncFailed) {
    Serial.println("[SYNC] Device reported failure");
    configServer2.stop();
    WiFi.softAPdisconnect(true);
    goto AP1;
  }

  Serial.println("[SYNC] All devices connected");

  configServer2.stop();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, true);

  delay(5000);

  wifi_event_group = xEventGroupCreate();

  WiFi.onEvent(wifi_event_handler);
  WiFi.mode(WIFI_STA);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  Serial.printf("[WIFI] Reconnecting to %s\n", savedSSID.c_str());
  WiFi.begin(savedSSID.c_str(), savedPass.c_str());
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

  Serial.printf("\n[WIFI] Connected, IP: %s\n", WiFi.localIP().toString().c_str());
  // test portion ==============================================================================



  /*
  // test portion ==============================================================================
  wifi_event_group = xEventGroupCreate();

  WiFi.onEvent(wifi_event_handler);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  //wifi_event_group = xEventGroupCreate();
  xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
  // test portion ==============================================================================
  */

  if (!MDNS.begin("esp32_gateway")) {
    Serial.println("MDNS init failed");
    return;
  }

  server_on_response(on_response);
  server_on_device_update(on_device_update);
  server_connect(WS_HOST, WS_PORT, WS_PATH);

  serial2_mutex = xSemaphoreCreateMutex();
  server_mutex = xSemaphoreCreateMutex();

  cmd_queue = xQueueCreate(10, sizeof(PendingCmd));

  xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 1, NULL);
  xTaskCreate(cmd_task, "cmd_task", 2048, NULL, 1, NULL);
  xTaskCreate(server_keepalive_task, "server_coms_task", 4096, NULL, 3, NULL);
}

void loop() {}