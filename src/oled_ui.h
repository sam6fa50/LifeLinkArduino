#pragma once

#include <Adafruit_SH110X.h>
#include "sensor_hub.h"

// Thin UI layer around Adafruit_SH1106G. Each function clears and redraws the
// whole framebuffer — caller does not need to manage state between screens.

namespace oled_ui {

void begin(Adafruit_SH1106G* d);

void show_boot();
void show_warmup(int seconds_left);
void show_calibrating();
void show_main(const LifelinkState& s);

// Full-screen "really off" warning: doubled font, lists offending readings,
// toggles the SH1106's display-polarity command each frame so the entire panel
// flashes white↔black at roughly 2.5 Hz. Used when temperature or gas crosses
// LIFELINK_TEMP_CRITICAL_C / LIFELINK_GAS_CRITICAL_PPM.
void show_critical(const LifelinkState& s);

// Returns true when the given snapshot crosses either critical threshold.
bool is_critical(const LifelinkState& s);

}  // namespace oled_ui
