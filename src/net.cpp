#include "net.h"
#include "config.h"
#include "sensor_hub.h"

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <math.h>

namespace {
    WiFiClient    s_wifi_client;
    PubSubClient  s_mqtt(s_wifi_client);
    uint32_t      s_last_wifi_retry_ms = 0;
    uint32_t      s_last_mqtt_retry_ms = 0;

    static constexpr uint32_t WIFI_RETRY_MS = 5000;
    static constexpr uint32_t MQTT_RETRY_MS = 3000;

    void ensure_wifi() {
        static wl_status_t s_last_status = (wl_status_t)0xFF;
        wl_status_t status = WiFi.status();

        // Loud one-line message every time the WiFi state actually changes.
        if (status != s_last_status) {
            const char* name = "?";
            switch (status) {
                case WL_IDLE_STATUS:     name = "IDLE";          break;
                case WL_NO_SSID_AVAIL:   name = "NO_SSID_AVAIL"; break;
                case WL_SCAN_COMPLETED:  name = "SCAN_DONE";     break;
                case WL_CONNECTED:       name = "CONNECTED";     break;
                case WL_CONNECT_FAILED:  name = "CONNECT_FAILED";break;
                case WL_CONNECTION_LOST: name = "CONNECTION_LOST"; break;
                case WL_DISCONNECTED:    name = "DISCONNECTED";  break;
                case WL_NO_SHIELD:       name = "NO_SHIELD";     break;
                default:                 name = "OTHER";         break;
            }
            if (status == WL_CONNECTED) {
                Serial.printf("[wifi] -> %s  IP=%s  RSSI=%d dBm\n",
                              name, WiFi.localIP().toString().c_str(), WiFi.RSSI());
            } else {
                Serial.printf("[wifi] -> %s (code %d)\n", name, (int)status);
            }
            s_last_status = status;
        }

        if (status == WL_CONNECTED) {
            sensor_hub::set_wifi(true);
            return;
        }
        sensor_hub::set_wifi(false);

        uint32_t now = millis();
        if (now - s_last_wifi_retry_ms < WIFI_RETRY_MS) return;
        s_last_wifi_retry_ms = now;

        Serial.printf("[wifi] retrying connection to \"%s\"...\n", LIFELINK_WIFI_SSID);
        WiFi.disconnect();
        WiFi.begin(LIFELINK_WIFI_SSID, LIFELINK_WIFI_PASSWORD);
    }

    void ensure_mqtt() {
        if (WiFi.status() != WL_CONNECTED) {
            sensor_hub::set_mqtt(false);
            return;
        }
        if (s_mqtt.connected()) {
            sensor_hub::set_mqtt(true);
            return;
        }
        sensor_hub::set_mqtt(false);

        uint32_t now = millis();
        if (now - s_last_mqtt_retry_ms < MQTT_RETRY_MS) return;
        s_last_mqtt_retry_ms = now;

        Serial.printf("[mqtt] connecting to %s:%d as \"%s\"...\n",
                      LIFELINK_MQTT_HOST, LIFELINK_MQTT_PORT, LIFELINK_DEVICE_ID);
        // last-will lets subscribers see the device disappear
        if (s_mqtt.connect(LIFELINK_DEVICE_ID,
                           LIFELINK_MQTT_TOPIC_STATUS, 1, true,
                           "{\"device\":\"" LIFELINK_DEVICE_ID "\",\"online\":false}")) {
            Serial.println(F("[mqtt] CONNECTED — publishing online hello"));
            sensor_hub::set_mqtt(true);
            s_mqtt.publish(LIFELINK_MQTT_TOPIC_STATUS,
                           "{\"device\":\"" LIFELINK_DEVICE_ID "\",\"online\":true}", false);
        } else {
            // PubSubClient state codes: -4 timeout, -3 lost, -2 conn failed,
            // -1 disconnected, 0 connected, 1 bad protocol, 2 bad client id,
            // 3 unavailable, 4 bad credentials, 5 unauthorized.
            Serial.printf("[mqtt] connect FAILED — state=%d\n", s_mqtt.state());
        }
    }
}

namespace net {

void begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(LIFELINK_WIFI_SSID, LIFELINK_WIFI_PASSWORD);
    s_last_wifi_retry_ms = millis();

    s_mqtt.setServer(LIFELINK_MQTT_HOST, LIFELINK_MQTT_PORT);
    s_mqtt.setBufferSize(512);
    s_mqtt.setKeepAlive(30);
    s_mqtt.setSocketTimeout(10);
}

void service_tick() {
    ensure_wifi();
    ensure_mqtt();
    if (s_mqtt.connected()) s_mqtt.loop();
}

void publish_now() {
    if (!s_mqtt.connected()) return;

    LifelinkState st = sensor_hub::get();

    JsonDocument doc;
    doc["device"] = LIFELINK_DEVICE_ID;
    if (!isnan(st.temperature)) doc["temp"] = roundf(st.temperature * 10.0f) / 10.0f;
    if (!isnan(st.humidity))    doc["humi"] = roundf(st.humidity    * 10.0f) / 10.0f;
    doc["gas"]   = (long)roundf(st.gas_ppm);
    doc["alarm"] = st.gas_alarm ? 1 : 0;

    char payload[256];
    size_t n = serializeJson(doc, payload, sizeof(payload));
    s_mqtt.publish(LIFELINK_MQTT_TOPIC_STATUS, (const uint8_t*)payload, n, false);

    // Retained, single-byte alarm flag — matches lifelink-firmware contract.
    const char* alert_val = st.gas_alarm ? "1" : "0";
    s_mqtt.publish(LIFELINK_MQTT_TOPIC_ALERT, (const uint8_t*)alert_val, 1, true);
}

bool wifi_up() { return WiFi.status() == WL_CONNECTED; }
bool mqtt_up() { return s_mqtt.connected(); }

}  // namespace net
