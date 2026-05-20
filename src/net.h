#pragma once

#include <Arduino.h>

// Wi-Fi + MQTT manager. Owns the WiFi STA and PubSubClient instances; runs
// reconnect logic on a single FreeRTOS task and publishes the current
// sensor_hub snapshot every LIFELINK_MQTT_PERIOD_MS.

namespace net {

// Non-blocking — kicks off WiFi.begin() and creates the maintenance task.
void begin();

// Called periodically by the maintenance task; users normally don't invoke.
void service_tick();

// Latest sensor_hub snapshot → JSON status payload + retained alert flag.
void publish_now();

}  // namespace net
