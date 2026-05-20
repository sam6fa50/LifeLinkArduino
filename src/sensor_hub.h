#pragma once

#include <Arduino.h>

// Thread-safe shared snapshot of every sensor + connectivity flag.
// All public functions take/release an internal FreeRTOS mutex.

struct LifelinkState {
    float temperature   = NAN;     // °C  (NAN until first valid DHT read)
    float humidity      = NAN;     // %
    float gas_ppm       = 0.0f;    // LPG-equivalent PPM
    bool  gas_alarm     = false;   // true when ppm > threshold or DOUT active
    bool  wifi_up       = false;
    bool  mqtt_up       = false;
    bool  bt_client_up  = false;   // SPP client is currently connected
};

namespace sensor_hub {

void          init();

void          set_temp_humi(float t_c, float h_pct);
void          set_gas(float ppm, bool alarm);
void          set_wifi(bool up);
void          set_mqtt(bool up);
void          set_bt_client(bool up);

LifelinkState get();

}  // namespace sensor_hub
