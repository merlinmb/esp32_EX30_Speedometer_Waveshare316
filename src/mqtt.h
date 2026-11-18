#pragma once
#include <Arduino.h>

// MQTT client (PubSubClient), same topic scheme as the other merlin devices:
//   stat/<device>/<name>, tele/<device>/alive|ip, cmnd/<device>/#, cmnd/mcmddevices/#
// Driven from the network task; only runs while Wi-Fi is connected.

void mqttLoop();                                  // call often while Wi-Fi is up
void mqttOnWifiConnected();                       // call on every Wi-Fi (re)connect
void mqttPublishStat(String name, String value);
void mqttTransmitInitStat(String deviceName);
