#pragma once

#include <stddef.h>

// Single source of truth for the LifeLink status JSON wire format.
// Both the MQTT publisher (net.cpp) and the SPP broadcaster (bt_bridge.cpp)
// share this serializer so the payload schema lives in exactly one place.
//
// Output schema:
//   { "device":"<id>", "temp":<°C>?, "humi":<%>?, "gas":<ppm>, "alarm":<0|1> }
//
// `temp` and `humi` are omitted when the underlying sensor read is NaN.

namespace payload {

// Serializes the current sensor_hub snapshot into `out`. Returns the number
// of bytes written (excluding the null terminator). Returns 0 if `cap` is
// too small to hold the payload.
size_t serialize_status(char* out, size_t cap);

}  // namespace payload
