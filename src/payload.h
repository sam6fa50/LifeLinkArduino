#pragma once

#include <stddef.h>

// Single source of truth for the LifeLink status JSON wire format.
// Both the MQTT publisher (net.cpp) and the SPP broadcaster (bt_bridge.cpp)
// share this serializer so the payload schema lives in exactly one place.
//
// Output schema:
//   { "device":"<id>", "temp":<°C|null>, "humi":<%|null>, "gas":<ppm|null>, "alarm":<0|1> }
//
// Invalid/stale measurements are null. temp_valid, humi_valid and gas_valid
// are explicit booleans; alarm is preserved during a gas sensor fault.

namespace payload {

// Serializes the current sensor_hub snapshot into `out`. Returns the number
// of bytes written (excluding the null terminator). Returns 0 if `cap` is
// too small to hold the payload.
size_t serialize_status(char* out, size_t cap);

}  // namespace payload
