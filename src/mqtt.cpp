#include "mqtt.h"
#include <WiFi.h>
#include <PubSubClient.h>

// Broker details can be overridden in include/secrets.h (see secrets_example.h).
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef MQTT_SERVER_IP
#define MQTT_SERVER_IP "192.168.1.55"
#endif
#ifndef MQTT_SERVER_PORT
#define MQTT_SERVER_PORT 1883
#endif
#ifndef MQTT_FRIENDLYNAME
#define MQTT_FRIENDLYNAME "mcmddevices"
#endif

static const char* DEVICE_NAME = "esp32_Speedometer";
static constexpr uint32_t RETRY_MS = 5000;

static WiFiClient wifiClient;
static PubSubClient client(wifiClient);
static uint32_t lastAttemptMs = 0;
static bool attempted = false;

static String IpAddress2String(const IPAddress& ipAddress) {
  return String(ipAddress[0]) + String(".") +
         String(ipAddress[1]) + String(".") +
         String(ipAddress[2]) + String(".") +
         String(ipAddress[3]);
}

void mqttPublishStat(String name, String value) {
  String topic = "stat/" + String(DEVICE_NAME) + "/" + name;
  client.publish(topic.c_str(), value.c_str(), false);
}

void mqttTransmitInitStat(String deviceName) {
  mqttPublishStat("init", "{\"value1\":\"" + IpAddress2String(WiFi.localIP()) + "\",\"value2\":\"" + WiFi.macAddress() + "\",\"value3\":\"" + deviceName + "\"}");
}

static void callback(char* topic, byte* payload, unsigned int length) {
  String t(topic);
  String p;
  p.reserve(length);
  for (unsigned int i = 0; i < length; i++) p += (char)payload[i];
  if (t == "cmnd/" MQTT_FRIENDLYNAME "/sendstat") {
    mqttTransmitInitStat(DEVICE_NAME);
  } else if (t == "cmnd/" MQTT_FRIENDLYNAME "/reset") {
    mqttPublishStat("restart", p);
    delay(1000);
    ESP.restart();
  }
}

// One connection attempt; the caller retries every RETRY_MS so the web page stays responsive.
static bool reconnect() {
  String clientId = String(DEVICE_NAME) + "_" + String(random(0xffff), HEX);
  Serial.printf("[MQTT] Connecting to %s:%d as %s\n", MQTT_SERVER_IP, MQTT_SERVER_PORT, clientId.c_str());
  if (!client.connect(clientId.c_str())) {
    Serial.printf("[MQTT] Connect failed, rc=%d\n", client.state());
    return false;
  }
  Serial.println("[MQTT] Connected");
  String base = "tele/" + String(DEVICE_NAME);
  client.publish((base + "/alive").c_str(), "1");
  client.publish((base + "/ip").c_str(), IpAddress2String(WiFi.localIP()).c_str());
  client.subscribe(("cmnd/" + String(DEVICE_NAME) + "/#").c_str());
  client.subscribe("cmnd/" MQTT_FRIENDLYNAME "/#");
  mqttTransmitInitStat(DEVICE_NAME);
  return true;
}

void mqttOnWifiConnected() {
  // A connection that survived the Wi-Fi drop is still announced; otherwise
  // reconnect immediately (which sends the init stat itself).
  if (client.connected()) mqttTransmitInitStat(DEVICE_NAME);
  else attempted = false;
}

void mqttLoop() {
  if (!client.connected()) {
    if (attempted && millis() - lastAttemptMs < RETRY_MS) return;
    attempted = true;
    lastAttemptMs = millis();
    client.setServer(MQTT_SERVER_IP, MQTT_SERVER_PORT);
    client.setCallback(callback);
    client.setSocketTimeout(2);   // seconds; keeps a dead broker from stalling the net task
    if (!reconnect()) return;
  }
  client.loop();
}
