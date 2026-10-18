#include "payload.h"
#include "config.h"
#include "sensor_hub.h"

#include <ArduinoJson.h>
#include <math.h>

namespace payload {

size_t serialize_status(char* out, size_t cap) {
    if (!out || cap == 0) return 0;
    LifelinkState st = sensor_hub::get();
    JsonDocument doc;
    doc["device"] = LIFELINK_DEVICE_ID;
    doc["temp"] = nullptr;
    doc["humi"] = nullptr;
    doc["gas"] = nullptr;
    if (isfinite(st.temperature)) doc["temp"] = roundf(st.temperature * 10.0f) / 10.0f;
    if (isfinite(st.humidity))    doc["humi"] = roundf(st.humidity * 10.0f) / 10.0f;
    if (isfinite(st.gas_ppm))     doc["gas"] = (long)roundf(st.gas_ppm);
    doc["temp_valid"] = isfinite(st.temperature);
    doc["humi_valid"] = isfinite(st.humidity);
    doc["gas_valid"] = isfinite(st.gas_ppm);
    doc["alarm"] = st.gas_alarm ? 1 : 0;
    const size_t required = measureJson(doc);
    if (required >= cap) { out[0] = '\0'; return 0; }
    const size_t written = serializeJson(doc, out, cap);
    if (written != required) { out[0] = '\0'; return 0; }
    return written;
}

}  // namespace payload
