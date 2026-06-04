#include "server_client.h"
#include <Arduino.h>

static WebsocketsClient ws;
static const char* _host = NULL;
static int _port = 0;
static const char* _path = NULL;
static bool             _connected     = false;
static unsigned long    _lastReconnect = 0;
static char             _resolvedIP[16] = {0};

static DeviceUpdateCb _deviceUpdateCb = NULL;
static ResponseCb     _responseCb     = NULL;

// ─────────────────────────────────────────────────────────────────────────────

static void onMessage(WebsocketsMessage msg) {
    if (!msg.isText()) return;

    JsonDocument doc;
    if (deserializeJson(doc, msg.data()) != DeserializationError::Ok) return;

    const char* type = doc["type"];
    if (!type) return;

    if (strcmp(type, "devices.update") == 0 && _deviceUpdateCb)
        _deviceUpdateCb(doc["deviceID"], doc["value"].as<int>());

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

static const char* _resolve(const char* host) {
    if (strstr(host, ".local") == NULL) return host;

    IPAddress ip;
    int ret = WiFi.hostByName(host, ip);
    if (ret != 1) {
        Serial.printf("[mDNS] failed to resolve %s\n", host);
        return host;
    }

    snprintf(_resolvedIP, sizeof(_resolvedIP), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    Serial.printf("[mDNS] %s -> %s\n", host, _resolvedIP);
    return _resolvedIP;
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

void server_on_device_update(DeviceUpdateCb cb) { _deviceUpdateCb = cb; }
void server_on_response     (ResponseCb cb)     { _responseCb     = cb; }

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

void server_complete_command(int id) {
    JsonDocument doc;
    doc["type"] = "commands.complete";
    doc["id"]   = id;
    _send(doc);
}