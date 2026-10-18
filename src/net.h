#pragma once

#include <Arduino.h>

// Wi-Fi + verified TLS MQTT manager. Owns WiFiClientSecure and PubSubClient; runs
// reconnect logic on a single FreeRTOS task and publishes the current
// sensor_hub snapshot every LIFELINK_MQTT_PERIOD_MS.

namespace net {

// Kicks off WiFi.begin() and configures MQTT; main.cpp starts the task.
void begin();

// Called periodically by the maintenance task; users normally don't invoke.
void service_tick();

// Latest sensor_hub snapshot → JSON status payload + retained alert flag.
void publish_now();

}  // namespace net
