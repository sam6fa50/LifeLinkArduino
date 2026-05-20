#include "payload.h"
#include "config.h"
#include "sensor_hub.h"

#include <ArduinoJson.h>
#include <math.h>

namespace payload {

size_t serialize_status(char* out, size_t cap) {
    LifelinkState st = sensor_hub::get();
    JsonDocument doc;
    doc["device"] = LIFELINK_DEVICE_ID;
    if (!isnan(st.temperature)) doc["temp"] = roundf(st.temperature * 10.0f) / 10.0f;
    if (!isnan(st.humidity))    doc["humi"] = roundf(st.humidity    * 10.0f) / 10.0f;
    doc["gas"]   = (long)roundf(st.gas_ppm);
    doc["alarm"] = st.gas_alarm ? 1 : 0;
    return serializeJson(doc, out, cap);
}

}  // namespace payload
