#include "bt_bridge.h"
#include "config.h"
#include "sensor_hub.h"
#include "payload.h"

#include <BluetoothSerial.h>
#include <ArduinoJson.h>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error "Bluetooth is not enabled in this build. Use a board variant with Bluedroid."
#endif

namespace {
    BluetoothSerial s_bt;
    uint32_t        s_last_broadcast_ms = 0;
    bool            s_was_client_up     = false;
    String          s_rx_line;
    uint32_t        s_tx_count          = 0;
    uint32_t        s_tx_bytes          = 0;

    void send_status_line() {
        char buf[256];
        size_t n = payload::serialize_status(buf, sizeof(buf) - 2);
        if (n == 0) return;
        buf[n++] = '\n';
        buf[n]   = '\0';
        size_t written = s_bt.write((const uint8_t*)buf, n);
        s_tx_count++;
        s_tx_bytes += written;
        // One log line every 10th frame so we can see traffic is flowing
        // without spamming serial. If you see this line but the phone shows
        // no telemetry, the problem is on the Android side.
        if ((s_tx_count % 10) == 1) {
            Serial.printf("[bt] tx #%lu  (%u/%u bytes, total %lu bytes since boot)\n",
                          (unsigned long)s_tx_count, (unsigned)written, (unsigned)n,
                          (unsigned long)s_tx_bytes);
        }
    }

    void handle_command(const String& cmd) {
        if (cmd.equalsIgnoreCase("PING")) {
            s_bt.println("PONG");
        } else if (cmd.equalsIgnoreCase("STATUS")) {
            send_status_line();
        } else if (cmd.equalsIgnoreCase("ALARM_RESET")) {
            // Latching is sensor-side; ack only.
            s_bt.println("{\"ack\":\"alarm_reset\"}");
        } else if (cmd.length() > 0) {
            // ArduinoJson handles quote-escaping so a malicious or weird
            // inbound command can't malform our reply JSON.
            JsonDocument doc;
            doc["error"] = "unknown";
            doc["cmd"]   = cmd.c_str();
            char out[96];
            size_t n = serializeJson(doc, out, sizeof(out));
            out[n++] = '\n';
            s_bt.write((const uint8_t*)out, n);
        }
    }

    void bt_event(esp_spp_cb_event_t event, esp_spp_cb_param_t* /*param*/) {
        switch (event) {
            case ESP_SPP_INIT_EVT:
                Serial.println(F("[bt] SPP_INIT — Bluedroid stack ready"));
                break;
            case ESP_SPP_START_EVT:
                Serial.println(F("[bt] SPP_START — server up; phone can now connect"));
                break;
            case ESP_SPP_SRV_OPEN_EVT:
                sensor_hub::set_bt_client(true);
                Serial.println(F("[bt] SPP_SRV_OPEN — *** client CONNECTED ***"));
                break;
            case ESP_SPP_CLOSE_EVT:
                sensor_hub::set_bt_client(false);
                Serial.println(F("[bt] SPP_CLOSE — client disconnected"));
                break;
            case ESP_SPP_DATA_IND_EVT:
                // Inbound data — drained by service_tick() reading s_bt.available().
                break;
            case ESP_SPP_WRITE_EVT:
                // Outbound complete — too noisy to log per-write.
                break;
            case ESP_SPP_CONG_EVT:
                Serial.println(F("[bt] SPP_CONG — TX congested (phone not draining)"));
                break;
            default:
                Serial.printf("[bt] SPP event %d (unhandled)\n", (int)event);
                break;
        }
    }
}

namespace bt_bridge {

void begin() {
    s_rx_line.reserve(64);
    s_bt.register_callback(bt_event);
    if (!s_bt.begin(LIFELINK_BT_NAME)) {
        Serial.println(F("[bt] ⚠ SPP begin() FAILED — Classic-BT stack did not start."));
        Serial.println(F("[bt]   Common causes: partition scheme too small (need huge_app),"));
        Serial.println(F("[bt]   or this build was made for an ESP32 variant without classic BT."));
        return;
    }
    Serial.printf("[bt] SPP server ADVERTISING as \"%s\"\n", LIFELINK_BT_NAME);
    Serial.println(F("[bt] On phone: Settings -> Bluetooth -> pair this name, then open the app."));
}

void service_tick() {
    bool client_up = s_bt.hasClient();
    if (client_up != s_was_client_up) {
        // hasClient() is the authoritative truth — sync the hub for the case
        // where we missed the callback (e.g. before begin() finished).
        sensor_hub::set_bt_client(client_up);
        s_was_client_up = client_up;
    }
    if (!client_up) return;

    // ── Inbound: read up to one full line, then dispatch ─────────────────
    while (s_bt.available()) {
        char c = (char)s_bt.read();
        if (c == '\r') continue;
        if (c == '\n') {
            handle_command(s_rx_line);
            s_rx_line = "";
        } else if (s_rx_line.length() < 64) {
            s_rx_line += c;
        }
    }

    // ── Outbound: scheduled telemetry broadcast ──────────────────────────
    uint32_t now = millis();
    if (now - s_last_broadcast_ms >= LIFELINK_BT_PERIOD_MS) {
        s_last_broadcast_ms = now;
        send_status_line();
    }
}

}  // namespace bt_bridge
