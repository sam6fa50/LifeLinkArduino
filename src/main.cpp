// ─────────────────────────────────────────────────────────────────────────────
//  LifeLink — Arduino-CPP firmware for ESP32 (esp-wrover-kit)
//
//  Reads DHT11 (temp + humidity) and MQ-2 (LPG-equivalent gas ppm), shows a
//  live dashboard on a 1.3" SH1106 OLED, publishes telemetry via MQTT
//  (HiveMQ by default), and streams the same JSON over Classic-Bluetooth SPP
//  for the LifeLink Android companion.
//
//  Architecture mirrors the original esp-idf reference in lifelink-firmware/
//  but uses Arduino libraries and FreeRTOS via xTaskCreatePinnedToCore.
//
//  The setup() routine is intentionally noisy on serial — every step prints
//  what it tried and what it got, so a missing OLED / wrong I2C address /
//  bad Wi-Fi credentials / failed BT stack init show up immediately when
//  you `pio device monitor`. Each peripheral degrades to a no-op if its
//  init fails, so a dead OLED never takes the radios down with it.
// ─────────────────────────────────────────────────────────────────────────────

#include <Arduino.h>
#include <SPI.h>
#include <DHT.h>
#include <Adafruit_SH110X.h>

#include "config.h"
#include "sensor_hub.h"
#include "mq2.h"
#include "oled_ui.h"
#include "net.h"
#include "bt_bridge.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ── Hardware singletons ─────────────────────────────────────────────────────
static Adafruit_SH1106G s_display(LIFELINK_OLED_W, LIFELINK_OLED_H, &SPI,
                                  LIFELINK_OLED_DC, LIFELINK_OLED_RST,
                                  LIFELINK_OLED_CS);
static DHT              s_dht(LIFELINK_DHT_PIN, DHT11);
static MQ2              s_mq2(LIFELINK_MQ2_AOUT_PIN, LIFELINK_MQ2_DOUT_PIN);
static bool             s_oled_ok = false;

// DHT11 read-quality counters, updated by task_dht and surfaced on the
// heartbeat so you can see at a glance how reliable the sensor is.
static volatile uint32_t s_dht_ok        = 0;
static volatile uint32_t s_dht_fail      = 0;
static volatile uint32_t s_dht_out_of_spec = 0;   // reads whose humidity fell outside DHT11's spec range

// ── FreeRTOS tasks ──────────────────────────────────────────────────────────

static void task_dht(void*) {
    // DHT11's first 1–2 post-boot samples are unreliable — discard them so
    // the first published value is something we'd actually trust.
    for (int i = 0; i < 2; i++) {
        (void)s_dht.readTemperature();
        (void)s_dht.readHumidity();
        vTaskDelay(pdMS_TO_TICKS(LIFELINK_DHT_PERIOD_MS));
    }

    // Throttle the "out-of-spec" log line: print only when the value crosses
    // the threshold or moves by ≥2 %, so we don't spam if it's flatlined low.
    float last_warn_h = -1000.0f;

    TickType_t last = xTaskGetTickCount();
    for (;;) {
        float t = s_dht.readTemperature();
        float h = s_dht.readHumidity();
        if (!isnan(t) && !isnan(h)) {
            s_dht_ok++;

            // DHT11 datasheet: humidity 20-90 % RH, ±5 % accuracy. Outside
            // that range the sensor's noise floor dominates — publishing the
            // value would be asserting a false reading downstream. Push the
            // valid temperature but mark humidity as unavailable (NaN); every
            // consumer (OLED / MQTT JSON / BT JSON / Android dashboard)
            // already renders NaN as a dash.
            const bool h_in_spec = (h >= 20.0f && h <= 90.0f);
            sensor_hub::set_temp_humi(t, h_in_spec ? h : NAN);

            if (!h_in_spec) {
                s_dht_out_of_spec++;
                if (fabsf(h - last_warn_h) >= 2.0f) {
                    last_warn_h = h;
                    Serial.printf(
                        "[dht] WARN: humidity=%.0f%% is outside DHT11 spec range [20, 90]; "
                        "suppressing — publishing as N/A. Temp=%.1fC still trusted. "
                        "Likely causes: faulty humidity element (most common on cheap DHT11s), "
                        "missing 10kOhm pull-up on DATA, or loose DATA wire.\n", h, t);
                }
            }
        } else {
            s_dht_fail++;
            // Don't flood the log on every failure — print sparingly.
            if ((s_dht_fail % 10) == 1) {
                Serial.printf("[dht] read failed (%u total fails vs %u ok)\n",
                              (unsigned)s_dht_fail, (unsigned)s_dht_ok);
            }
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(LIFELINK_DHT_PERIOD_MS));
    }
}

static void task_mq2(void*) {
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        // Trust the analog reading only. The MQ-2 breakout's DOUT comparator
        // is set by a sensitivity trimpot on the board, and on input-only
        // GPIO 35 (no internal pull-up) a loose DOUT wire floats LOW —
        // which would force alarm=1 forever. The analog ppm we already
        // compute is the same information from a more reliable channel.
        const float ppm = s_mq2.read_ppm();
        sensor_hub::set_gas(ppm, ppm > LIFELINK_GAS_ALARM_PPM);
        vTaskDelayUntil(&last, pdMS_TO_TICKS(LIFELINK_MQ2_PERIOD_MS));
    }
}

static void task_display(void*) {
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        LifelinkState st = sensor_hub::get();
        if (oled_ui::is_critical(st)) {
            oled_ui::show_critical(st);
        } else {
            oled_ui::show_main(st);
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(LIFELINK_DISPLAY_PERIOD_MS));
    }
}

static void task_net(void*) {
    TickType_t last = xTaskGetTickCount();
    uint32_t   last_publish = 0;
    for (;;) {
        net::service_tick();
        uint32_t now = millis();
        if (now - last_publish >= LIFELINK_MQTT_PERIOD_MS) {
            last_publish = now;
            net::publish_now();
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(LIFELINK_NET_PERIOD_MS));
    }
}

static void task_bt(void*) {
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        bt_bridge::service_tick();
        vTaskDelayUntil(&last, pdMS_TO_TICKS(200));
    }
}

// ── Setup ───────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println(F("========================================"));
    Serial.println(F("  LifeLink — Know Your Air (Arduino)"));
    Serial.print  (F("  Device: ")); Serial.println(LIFELINK_DEVICE_ID);
    Serial.print  (F("  BT name: ")); Serial.println(LIFELINK_BT_NAME);
    Serial.print  (F("  WiFi SSID: ")); Serial.println(LIFELINK_WIFI_SSID);
    Serial.println(F("========================================"));

    sensor_hub::init();

    // ── SPI + OLED ──────────────────────────────────────────────────────
    SPI.begin(LIFELINK_OLED_CLK, -1, LIFELINK_OLED_MOSI, LIFELINK_OLED_CS);
    Serial.printf("[spi] bus up — CLK=%d MOSI=%d CS=%d DC=%d RES=%d\n",
                  LIFELINK_OLED_CLK, LIFELINK_OLED_MOSI,
                  LIFELINK_OLED_CS,  LIFELINK_OLED_DC, LIFELINK_OLED_RST);
    // begin()'s first argument is the I2C address — unused on the SPI ctor,
    // so any value works. The second is "reset the panel via the RST pin".
    if (s_display.begin(0, true)) {
        s_oled_ok = true;
        oled_ui::begin(&s_display);
        oled_ui::show_boot();
        Serial.println(F("[oled] SH1106 init OK over SPI"));
    } else {
        Serial.println(F("[oled] ⚠ SH1106 init FAILED — check VCC/GND and the CLK/DIN/CS/DC/RES wiring."));
        Serial.println(F("[oled]   Sensor data will still publish over MQTT / BT / serial."));
    }
    delay(1500);

    // ── DHT11 ───────────────────────────────────────────────────────────
    s_dht.begin();
    Serial.printf("[dht] DHT11 ready on GPIO %d\n", LIFELINK_DHT_PIN);

    // ── MQ-2: warm-up countdown + R0 calibration in clean air ───────────
    s_mq2.begin();
    Serial.printf("[mq2] ADC on GPIO %d, DOUT on GPIO %d — warming up %d s\n",
                  LIFELINK_MQ2_AOUT_PIN, LIFELINK_MQ2_DOUT_PIN, LIFELINK_MQ2_WARMUP_S);
    for (int s = LIFELINK_MQ2_WARMUP_S; s > 0; s--) {
        oled_ui::show_warmup(s);
        if (s % 5 == 0 || s <= 5) {
            Serial.printf("[mq2] warm-up: %d s remaining\n", s);
        }
        delay(1000);
    }
    Serial.println(F("[mq2] calibrating R0 (clean-air baseline; will retry if sensor not yet stable)..."));
    oled_ui::show_calibrating();
    s_mq2.calibrate(LIFELINK_MQ2_CALIB_N);
    Serial.printf("[mq2] calibration %s — R0 = %.2f kOhm\n",
                  s_mq2.calibration_ok() ? "OK" : "FELL BACK to default",
                  s_mq2.r0());

    // ── Connectivity ────────────────────────────────────────────────────
    Serial.println(F("[net] starting WiFi..."));
    net::begin();
    Serial.println(F("[bt] starting Classic-BT SPP..."));
    bt_bridge::begin();

    // ── Spawn tasks. Core 0 runs Wi-Fi/BT stack; pin networking there too.
    //    Sensors + display live on core 1 so the radio stack doesn't starve
    //    them.
    xTaskCreatePinnedToCore(task_dht,     "dht",     LIFELINK_STACK_DHT,     nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(task_mq2,     "mq2",     LIFELINK_STACK_MQ2,     nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(task_display, "display", LIFELINK_STACK_DISPLAY, nullptr, 4, nullptr, 1);
    xTaskCreatePinnedToCore(task_net,     "net",     LIFELINK_STACK_NET,     nullptr, 3, nullptr, 0);
    xTaskCreatePinnedToCore(task_bt,      "bt",      LIFELINK_STACK_BT,      nullptr, 3, nullptr, 0);

    Serial.println(F("[boot] all tasks spawned — heartbeat follows every 5 s."));
}

// loop() runs in the Arduino main task. Real work lives in the spawned
// FreeRTOS tasks; this just emits a heartbeat so the user can see live
// sensor + link state on serial even when the OLED is dead.
void loop() {
    static uint32_t last_beat = 0;
    uint32_t now = millis();
    if (now - last_beat >= 5000) {
        last_beat = now;
        LifelinkState st = sensor_hub::get();
        // Direct MQ-2 probes so the user can see the actual sensor state
        // (raw ADC + computed RS + R0) without inferring it from the cooked
        // ppm value. If raw is pinned near 4095 the breakout/wiring/trimpot
        // is the problem, not calibration.
        const int   raw = s_mq2.read_raw_adc();
        const float rs  = s_mq2.read_rs();
        Serial.printf(
            "[heartbeat] t=%5.1fC h=%3.0f%% gas=%5.0fppm "
            "(raw=%4d RS=%6.2fk R0=%6.2fk calib=%s | dht_ok=%u dht_fail=%u dht_oos=%u)  "
            "alarm=%d crit=%d  wifi=%d mqtt=%d bt=%d oled=%d\n",
            st.temperature, st.humidity, st.gas_ppm,
            raw, rs, s_mq2.r0(),
            s_mq2.calibration_ok() ? "ok" : "FALLBACK",
            (unsigned)s_dht_ok, (unsigned)s_dht_fail, (unsigned)s_dht_out_of_spec,
            st.gas_alarm ? 1 : 0,
            oled_ui::is_critical(st) ? 1 : 0,
            st.wifi_up ? 1 : 0,
            st.mqtt_up ? 1 : 0,
            st.bt_client_up ? 1 : 0,
            s_oled_ok ? 1 : 0);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
}
