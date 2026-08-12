#pragma once

#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>

using namespace websockets;

typedef void (*DeviceUpdateCb)(const char* deviceID, int value);
typedef void (*ResponseCb)(JsonDocument& doc);

uint8_t server_connect(const char* host, int port, const char* path);
uint8_t server_loop(void);

void server_insert_sensor(const char* type, float value);
void server_get_sensors(void);
void server_get_devices(void);
void server_update_device(const char* deviceID, int value);
void server_complete_command(int id);

void server_on_device_update(DeviceUpdateCb cb);
void server_on_response(ResponseCb cb);