#pragma once

#include <Arduino.h>

// Classic-Bluetooth (SPP) bridge that streams JSON telemetry to the
// LifeLink Android companion. The Android side enumerates bonded BR/EDR
// devices and filters on names beginning with "LifeLink" — see
// BluetoothBridge.java in the companion app. Once paired through the
// system Bluetooth settings, the phone connects to this SPP server and
// reads one JSON object per line.
//
// Inbound line protocol (one ASCII command per line):
//   PING          → replies "PONG"
//   STATUS        → replies with the latest JSON status frame
//   ALARM_RESET   → currently a no-op acknowledgement (latching alarm is
//                   sensor-driven; included so the Android side has a
//                   forward-compatible hook)

namespace bt_bridge {

void begin();

// Called periodically. Reads any inbound bytes from the SPP client and
// pushes a fresh JSON line if at least LIFELINK_BT_PERIOD_MS has elapsed
// since the last broadcast.
void service_tick();

}  // namespace bt_bridge
