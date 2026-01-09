#include <WiFi.h>
#include <lwip/inet.h> 
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#include "secrets.h"

// Pins
#define FLOW_SENSOR_PIN 22
#define VALVE_PIN 23

// Calibrate
static float pulsesPerLiter = 450.0f;

// Pulse filter
static const uint32_t MIN_PULSE_US = 300;
volatile uint32_t pulseCount = 0;
volatile uint32_t lastPulseMicros = 0;

uint32_t lastCalcMs = 0;
float flowRateLpm = 0.0f;
float totalLiters = 0.0f;
bool valveState = false;

// MQTT ssss
WiFiClientSecure tlsClient;
PubSubClient mqtt(tlsClient);

String tCmdValve   = String(MQTT_BASE) + "/cmd/valve";
String tCmdReset   = String(MQTT_BASE) + "/cmd/reset";

String tStateValve = String(MQTT_BASE) + "/state/valve";
String tStateFlow  = String(MQTT_BASE) + "/state/flow_lpm";
String tStateTotal = String(MQTT_BASE) + "/state/total_l";
String tStateTs    = String(MQTT_BASE) + "/state/ts";

static const uint32_t PUBLISH_INTERVAL_MS = 1000;
uint32_t lastPubMs = 0;

static void wifiConnectFix() {
  WiFi.mode(WIFI_STA);

  // Hard reset WiFi state
  WiFi.disconnect(true, true);
  delay(500);

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("WiFi connecting");
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
    if (millis() - start > 20000) {
      Serial.println("\nWiFi connect timeout - rebooting");
      ESP.restart();
    }
  }
  Serial.println();

  // Give DHCP time
  delay(1000);

  IPAddress ip = WiFi.localIP();
  IPAddress gw = WiFi.gatewayIP();
  IPAddress sn = WiFi.subnetMask();

  Serial.print("IP: "); Serial.println(ip);
  Serial.print("GW: "); Serial.println(gw);
  Serial.print("SN: "); Serial.println(sn);

  // Detect broken DHCP
  if (ip[0] == 255 || ip[0] == 0 || gw[0] == 0) {
    Serial.println("DHCP failed (bad IP/GW). Rebooting...");
    delay(2000);
    ESP.restart();
  }

  // DNS test (should now work)
  IPAddress test;
  bool ok = WiFi.hostByName("example.com", test);
  Serial.print("Resolve example.com: ");
  Serial.println(ok ? test.toString() : "FAILED");

  // Stability improvement
  WiFi.setSleep(false);
}

static void wifiConnectWithDNS() {
  WiFi.mode(WIFI_STA);

  // Force DNS servers (prevents router DNS bugs / captive portal DNS)
  IPAddress ip(192,168,1,41);    // or another free IP
  IPAddress gw(192,168,1,1);
  IPAddress sn(255,255,255,0);
  IPAddress dns1(8, 8, 8, 8);  // Google
  IPAddress dns2(1, 1, 1, 1);  // Cloudflare
  // Keep DHCP for IP/gateway/subnet, but override DNS
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, dns1, dns2);

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("WiFi connecting");
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
    if (millis() - start > 20000) { // 20s
      Serial.println("\nWiFi connect timeout - rebooting");
      ESP.restart();
    }
  }
  Serial.println();

  Serial.print("WiFi OK. IP: ");
  Serial.println(WiFi.localIP());

  // Print DNS servers that ESP32 will use
  Serial.print("DNS1: "); Serial.println(WiFi.dnsIP(0));
  Serial.print("DNS2: "); Serial.println(WiFi.dnsIP(1));

  // Test DNS resolution
  bool ok1 = WiFi.hostByName("mqtt.freemqtt.com", ip);
  Serial.print("Resolve mqtt.freemqtt.com: ");
  Serial.println(ok1 ? ip.toString() : "FAILED");

  bool ok2 = WiFi.hostByName("broker.freemqtt.com", ip);
  Serial.print("Resolve broker.freemqtt.com: ");
  Serial.println(ok2 ? ip.toString() : "FAILED");

  // Also test a generic domain to detect captive portal / DNS interception
  bool ok3 = WiFi.hostByName("example.com", ip);
  Serial.print("Resolve example.com: ");
  Serial.println(ok3 ? ip.toString() : "FAILED");
}

void IRAM_ATTR pulseCounter() {
  uint32_t now = micros();
  if ((uint32_t)(now - lastPulseMicros) >= MIN_PULSE_US) {
    pulseCount++;
    lastPulseMicros = now;
  }
}

void openValve() {
  digitalWrite(VALVE_PIN, HIGH);
  valveState = true;
  Serial.println(">>> VALVE OPENED <<<");
}

void closeValve() {
  digitalWrite(VALVE_PIN, LOW);
  valveState = false;
  Serial.println(">>> VALVE CLOSED <<<");
}

void resetCounters() {
  noInterrupts();
  pulseCount = 0;
  interrupts();
  totalLiters = 0.0f;
  flowRateLpm = 0.0f;
  Serial.println("* Counters RESET *");
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String t(topic);
  String msg;
  msg.reserve(length);
  for (unsigned int i=0; i<length; i++) msg += (char)payload[i];
  msg.trim();

  Serial.print("[MQTT] "); Serial.print(t); Serial.print(" => "); Serial.println(msg);

  if (t == tCmdValve) {
    msg.toUpperCase();
    if (msg == "OPEN") openValve();
    else if (msg == "CLOSE") closeValve();
  } else if (t == tCmdReset) {
    if (msg == "1") resetCounters();
  }
}

void mqttConnect() {
  while (!mqtt.connected()) {
    String clientId = "esp32-water-" + String((uint32_t)ESP.getEfuseMac(), HEX);

    Serial.print("[MQTT] Connecting... ");
    bool ok = mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS);
    if (ok) {
      Serial.println("OK");
      mqtt.subscribe(tCmdValve.c_str());
      mqtt.subscribe(tCmdReset.c_str());
      return;
    }
    Serial.print("FAILED rc=");
    Serial.println(mqtt.state());
    delay(2000);
  }
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi OK: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);

  pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
  pinMode(VALVE_PIN, OUTPUT);
  closeValve();

  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, RISING);

  wifiConnectFix();
  WiFi.setSleep(false);

  Serial.print("DNS0: "); Serial.println(WiFi.dnsIP(0));
  Serial.print("DNS1: "); Serial.println(WiFi.dnsIP(1));
  // setupWiFi();

#if MQTT_TLS_INSECURE
  tlsClient.setInsecure();
#endif
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);

  IPAddress ip;
  Serial.print("Resolve broker.freemqtt.com: ");
  Serial.println(WiFi.hostByName("broker.freemqtt.com", ip) ? ip.toString() : "FAILED");

  mqttConnect();
  lastCalcMs = millis();
}

void loop() {
  if (!mqtt.connected()) mqttConnect();
  mqtt.loop();

  uint32_t nowMs = millis();

  // compute flow once per second
  if (nowMs - lastCalcMs >= 1000) {
    uint32_t pulses;
    noInterrupts();
    pulses = pulseCount;
    pulseCount = 0;
    interrupts();

    float pulsesPerSec = (float)pulses;
    flowRateLpm = (pulsesPerSec * 60.0f) / pulsesPerLiter;
    totalLiters += flowRateLpm / 60.0f;

    lastCalcMs = nowMs;
  }

  // publish once per second
  if (nowMs - lastPubMs >= PUBLISH_INTERVAL_MS) {
    lastPubMs = nowMs;

    char buf[32];

    mqtt.publish(tStateValve.c_str(), valveState ? "OPEN" : "CLOSED", false);

    dtostrf(flowRateLpm, 0, 2, buf);
    mqtt.publish(tStateFlow.c_str(), buf, false);

    dtostrf(totalLiters, 0, 3, buf);
    mqtt.publish(tStateTotal.c_str(), buf, false);

    // timestamp (browser can use its own too; this helps)
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)(time(nullptr)));
    mqtt.publish(tStateTs.c_str(), buf, false);
  }
}
