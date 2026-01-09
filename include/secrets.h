#pragma once

// Wi-Fi
#define WIFI_SSID "HOTWIFI1"
#define WIFI_PASS "lian1234"

// Public MQTT broker (Mosquitto test broker - better connectivity)
#define MQTT_HOST "test.mosquitto.org"
#define MQTT_PORT 8883
#define MQTT_USER ""
#define MQTT_PASS ""

// Make this UNIQUE
#define MQTT_BASE "ziv_water_9f3a72e1b8c44c2a"

// Command token (keep secret)
#define CMD_TOKEN "K3Y-7pQ9-DoNotShare"

// Mosquitto has valid certificates, so we can use secure TLS
#define MQTT_TLS_INSECURE 0
