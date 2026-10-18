#pragma once

// Copy to config_local.h (ignored by Git) and fill in your connection details.
#define LIFELINK_WIFI_SSID      "YOUR_WIFI_SSID"
#define LIFELINK_WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"

// Use a private broker with TLS, credentials, and topic access controls.
// Leave these empty to use Bluetooth without MQTT.
#define LIFELINK_MQTT_HOST      ""
#define LIFELINK_MQTT_PORT      8883
#define LIFELINK_MQTT_USERNAME  ""
#define LIFELINK_MQTT_PASSWORD  ""

// Paste the complete trusted root CA PEM from your broker provider between
// the parentheses below (including BEGIN/END CERTIFICATE lines).
static const char lifelink_mqtt_root_ca[] = R"PEM()PEM";
#define LIFELINK_MQTT_ROOT_CA lifelink_mqtt_root_ca

// Give each simultaneously connected device its own ID and allowed topics.
// #define LIFELINK_DEVICE_ID "lifelink-002"
