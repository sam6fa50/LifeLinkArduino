#include "sensor_hub.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {
    LifelinkState     s_state;
    SemaphoreHandle_t s_mutex = nullptr;

    inline void lock()   { if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY); }
    inline void unlock() { if (s_mutex) xSemaphoreGive(s_mutex); }
}

namespace sensor_hub {

void init() {
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
}

void set_temp_humi(float t_c, float h_pct) {
    lock();
    s_state.temperature = t_c;
    s_state.humidity    = h_pct;
    unlock();
}

void set_gas(float ppm, bool alarm) {
    lock();
    s_state.gas_ppm   = ppm;
    s_state.gas_alarm = alarm;
    unlock();
}

void set_wifi(bool up)       { lock(); s_state.wifi_up      = up; unlock(); }
void set_mqtt(bool up)       { lock(); s_state.mqtt_up      = up; unlock(); }
void set_bt_client(bool up)  { lock(); s_state.bt_client_up = up; unlock(); }

LifelinkState get() {
    lock();
    LifelinkState copy = s_state;
    unlock();
    return copy;
}

}  // namespace sensor_hub
