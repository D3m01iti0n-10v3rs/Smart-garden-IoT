// for esp32 cam
#include "server_client.h"
#include <Arduino.h>

static WebsocketsClient ws;
static const char*      _host          = NULL;
static int              _port          = 0;
static const char*      _path          = "/ws";
static bool             _connected     = false;
static unsigned long    _lastReconnect = 0;

static DeviceUpdateCb _deviceUpdateCb = NULL;
static ResponseCb     _responseCb     = NULL;

// ─────────────────────────────────────────────────────────────────────────────

static void onMessage(WebsocketsMessage msg) {
    if (!msg.isText()) return;

    JsonDocument doc;
    if (deserializeJson(doc, msg.data()) != DeserializationError::Ok) return;

    const char* type = doc["type"];

    // only attempt device update if type field is present
    if (type && strcmp(type, "devices.update") == 0 && _deviceUpdateCb)
        _deviceUpdateCb(doc["deviceID"], doc["value"].as<int>());

    // always fire response callback — covers typeless messages like camera settings
    if (_responseCb)
        _responseCb(doc);
}

static void onEvent(WebsocketsEvent event, String data) {
    if      (event == WebsocketsEvent::ConnectionOpened) _connected = true;
    else if (event == WebsocketsEvent::ConnectionClosed) _connected = false;
}

static void _send(JsonDocument& doc) {
    char buf[256];
    serializeJson(doc, buf, sizeof(buf));
    ws.send(buf);
}

// ─────────────────────────────────────────────────────────────────────────────

void server_connect(const char* host, int port, const char* path) {
    _host = host;
    _port = port;
    _path = path;
    _connected = false;

    IPAddress ip = MDNS.queryHost(host);

    if (ip == IPAddress((uint32_t)0)) {
        Serial.printf("[mDNS] failed to resolve %s\n", host);
        return;
    }

    Serial.printf(
        "[mDNS] %s -> %u.%u.%u.%u\n",
        host,
        ip[0], ip[1], ip[2], ip[3]
    );

    ws.onMessage(onMessage);
    ws.onEvent(onEvent);
    ws.connect(ip.toString(), port, path);
}

void server_loop(void) {
    ws.poll();
    if (!_connected && millis() - _lastReconnect > 3000) {
        _lastReconnect = millis();
        server_connect(_host, _port, _path);
    }
}

bool server_is_connected(void) { return _connected; }

void server_send_binary(const uint8_t* buf, size_t len) {
    ws.sendBinary((const char*)buf, len);
}

void server_on_device_update(DeviceUpdateCb cb) { _deviceUpdateCb = cb; }
void server_on_response     (ResponseCb cb)     { _responseCb     = cb; }

// ─────────────────────────────────────────────────────────────────────────────

void server_insert_sensor(const char* type, float value) {
    JsonDocument doc;
    doc["type"]       = "sensors.insert";
    doc["sensorType"] = type;
    doc["value"]      = value;
    _send(doc);
}

void server_get_sensors(void) {
    JsonDocument doc;
    doc["type"] = "sensors.get";
    _send(doc);
}

void server_get_devices(void) {
    JsonDocument doc;
    doc["type"] = "devices.get";
    _send(doc);
}

void server_update_device(const char* deviceID, int value) {
    JsonDocument doc;
    doc["type"]     = "devices.update";
    doc["deviceID"] = deviceID;
    doc["value"]    = value;
    _send(doc);
}