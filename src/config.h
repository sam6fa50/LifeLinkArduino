#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  LifeLink Arduino firmware — central configuration
//  Pins, intervals, MQ-2 curve constants, network credentials.
// ─────────────────────────────────────────────────────────────────────────────

// ─── Device identity ─────────────────────────────────────────────────────────
#define LIFELINK_DEVICE_ID      "lifelink-001"
// Classic-BT advertised name. Must start with "LifeLink" — the Android
// companion app filters bonded devices by this prefix (see BluetoothBridge.kt).
#define LIFELINK_BT_NAME        "LifeLink-001"

// ─── Wi-Fi credentials ───────────────────────────────────────────────────────
#define LIFELINK_WIFI_SSID      "Tessellator"
#define LIFELINK_WIFI_PASSWORD  "@DingusCakes2008!"

// ─── MQTT broker (default: HiveMQ public) ────────────────────────────────────
#define LIFELINK_MQTT_HOST      "broker.hivemq.com"
#define LIFELINK_MQTT_PORT      1883
// Topics — match the JS dashboard subscription pattern `lifelink/+/status`
#define LIFELINK_MQTT_TOPIC_STATUS  "lifelink/" LIFELINK_DEVICE_ID "/status"
#define LIFELINK_MQTT_TOPIC_ALERT   "lifelink/" LIFELINK_DEVICE_ID "/alert"

// ─── SPI / SH1106 OLED ───────────────────────────────────────────────────────
// 7/8-pin Waveshare-style 1.3" module wired for 4-wire SPI (matches the
// original LifeLink.ino harness). Uses the ESP32's VSPI peripheral.
//   GND  -> ESP32 GND      (required — not optional)
//   VCC  -> ESP32 3.3 V    (required)
//   CLK  -> GPIO 18  (SPI SCK)
//   DIN  -> GPIO 23  (SPI MOSI)
//   CS   -> GPIO 5
//   D/C  -> GPIO 16
//   RES  -> GPIO 17
#define LIFELINK_OLED_CLK       18
#define LIFELINK_OLED_MOSI      23
#define LIFELINK_OLED_CS        5
#define LIFELINK_OLED_DC        16
#define LIFELINK_OLED_RST       17
#define LIFELINK_OLED_W         128
#define LIFELINK_OLED_H         64

// ─── DHT11 ───────────────────────────────────────────────────────────────────
#define LIFELINK_DHT_PIN        4

// ─── MQ-2 gas sensor ─────────────────────────────────────────────────────────
// GPIO 34 is input-only on ESP32 — ideal for ADC. We don't wire DOUT:
// its onboard comparator is redundant with the analog ppm calculation,
// and a floating input-only GPIO would force false alarms.
#define LIFELINK_MQ2_AOUT_PIN   34

// Load resistor on the MQ-2 breakout (kΩ)
#define LIFELINK_MQ2_RL_KOHM    10.0f
// RS/R0 ratio in clean air (MQ-2 datasheet)
#define LIFELINK_MQ2_CLEAN_AIR  9.83f
// LPG power-law:  ppm = A × (RS/R0)^B
#define LIFELINK_MQ2_LPG_A      574.25f
#define LIFELINK_MQ2_LPG_B      -2.222f
// Heater warm-up (seconds) before R0 calibration
#define LIFELINK_MQ2_WARMUP_S   20
// Samples averaged for R0
#define LIFELINK_MQ2_CALIB_N    50
// LPG-equivalent PPM at which the alarm latches on
#define LIFELINK_GAS_ALARM_PPM  500.0f

// ─── Critical thresholds (full-screen flashing OLED warning) ─────────────────
// These sit above the regular alarm: temperatures this hot or gas this high
// imply the device is in or near an active fire / dangerous leak.
#define LIFELINK_TEMP_CRITICAL_C    45.0f    // °C — DHT11 reads up to 50 °C
#define LIFELINK_GAS_CRITICAL_PPM   2000.0f  // LPG-eq ppm — 4× the alarm level

// ─── Task periods (ms) ───────────────────────────────────────────────────────
#define LIFELINK_DHT_PERIOD_MS       2000
#define LIFELINK_MQ2_PERIOD_MS        500
#define LIFELINK_DISPLAY_PERIOD_MS    200   // Snappy enough for a ~2.5 Hz critical flash
#define LIFELINK_MQTT_PERIOD_MS      5000
#define LIFELINK_BT_PERIOD_MS        1000
#define LIFELINK_NET_PERIOD_MS       2000   // WiFi / MQTT reconnect tick

// ─── Task stacks (bytes) ─────────────────────────────────────────────────────
#define LIFELINK_STACK_DHT       3072
#define LIFELINK_STACK_MQ2       3072
#define LIFELINK_STACK_DISPLAY   4096
#define LIFELINK_STACK_BT        4096
#define LIFELINK_STACK_NET       4096
