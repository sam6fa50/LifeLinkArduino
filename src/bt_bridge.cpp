#include "bt_bridge.h"
#include "config.h"
#include "sensor_hub.h"

#include <BluetoothSerial.h>
#include <ArduinoJson.h>
#include <math.h>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error "Bluetooth is not enabled in this build. Use a board variant with Bluedroid."
#endif

namespace {
    BluetoothSerial s_bt;
    uint32_t        s_last_broadcast_ms = 0;
    bool            s_was_client_up     = false;
    String          s_rx_line;

    void serialize_status(char* out, size_t cap, size_t& written) {
        LifelinkState st = sensor_hub::get();
        JsonDocument doc;
        doc["device"] = LIFELINK_DEVICE_ID;
        if (!isnan(st.temperature)) doc["temp"] = roundf(st.temperature * 10.0f) / 10.0f;
        if (!isnan(st.humidity))    doc["humi"] = roundf(st.humidity    * 10.0f) / 10.0f;
        doc["gas"]   = (long)roundf(st.gas_ppm);
        doc["alarm"] = st.gas_alarm ? 1 : 0;
        written = serializeJson(doc, out, cap);
    }

    void send_status_line() {
        char buf[256];
        size_t n = 0;
        serialize_status(buf, sizeof(buf) - 2, n);
        buf[n++] = '\n';
        buf[n]   = '\0';
        s_bt.write((const uint8_t*)buf, n);
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
            s_bt.print("{\"error\":\"unknown\",\"cmd\":\"");
            s_bt.print(cmd);
            s_bt.println("\"}");
        }
    }

    void bt_event(esp_spp_cb_event_t event, esp_spp_cb_param_t* /*param*/) {
        switch (event) {
            case ESP_SPP_SRV_OPEN_EVT:
                sensor_hub::set_bt_client(true);
                log_i("BT: client connected");
                break;
            case ESP_SPP_CLOSE_EVT:
                sensor_hub::set_bt_client(false);
                log_i("BT: client disconnected");
                break;
            default:
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
