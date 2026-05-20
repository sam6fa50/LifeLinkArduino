#include "oled_ui.h"
#include "config.h"
#include <math.h>

namespace {
    Adafruit_SH1106G* g_d = nullptr;

    // Tracks the last polarity command we sent so we only hit the I2C bus
    // when the desired state actually changes.
    bool g_inverted = false;

    // Every public draw function below returns early if the OLED never
    // initialized — that way a missing/miswired panel doesn't take the
    // whole device down with a null deref.
    inline bool ready() { return g_d != nullptr; }

    void set_invert(bool inv) {
        if (!ready()) return;
        if (inv != g_inverted) {
            g_d->invertDisplay(inv);
            g_inverted = inv;
        }
    }

    void draw_progress(int x, int y, int w, int h, float ratio) {
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        g_d->drawRect(x, y, w, h, SH110X_WHITE);
        int filled = (int)((w - 2) * ratio);
        if (filled > 0) g_d->fillRect(x + 1, y + 1, filled, h - 2, SH110X_WHITE);
    }

    void draw_wifi_icon(int x, int y, bool on) {
        if (on) {
            g_d->drawPixel(x + 2, y + 4, SH110X_WHITE);
            g_d->drawFastHLine(x + 1, y + 3, 3, SH110X_WHITE);
            g_d->drawFastHLine(x,     y + 1, 5, SH110X_WHITE);
        } else {
            g_d->drawLine(x,     y,     x + 4, y + 4, SH110X_WHITE);
            g_d->drawLine(x + 4, y,     x,     y + 4, SH110X_WHITE);
        }
    }

    void draw_mqtt_icon(int x, int y, bool on) {
        if (on) g_d->fillRect(x, y, 5, 5, SH110X_WHITE);
        else    g_d->drawRect(x, y, 5, 5, SH110X_WHITE);
    }

    void draw_bt_icon(int x, int y, bool on) {
        // tiny B glyph — filled when an SPP client is connected
        if (on) g_d->fillRect(x, y, 5, 5, SH110X_WHITE);
        else    g_d->drawRect(x, y, 5, 5, SH110X_WHITE);
    }
}

namespace oled_ui {

void begin(Adafruit_SH1106G* d) {
    g_d = d;
    g_d->setTextSize(1);
    g_d->setTextColor(SH110X_WHITE);
    g_d->setTextWrap(false);
}

void show_boot() {
    if (!ready()) return;
    g_d->clearDisplay();
    g_d->setCursor(28, 16); g_d->print(F("LIFELINK"));
    g_d->setCursor(16, 32); g_d->print(F("Know Your Air"));
    g_d->setCursor(28, 48); g_d->print(F("Starting..."));
    g_d->display();
}

void show_warmup(int seconds_left) {
    if (!ready()) return;
    g_d->clearDisplay();
    g_d->setCursor(16, 0);  g_d->print(F("MQ-2 Warm-Up"));
    g_d->drawFastHLine(0, 9, 128, SH110X_WHITE);
    g_d->setCursor(0, 16);  g_d->print(F("Keep in clean air."));

    char buf[24];
    snprintf(buf, sizeof(buf), "Ready in %2d s...", seconds_left);
    g_d->setCursor(0, 32);  g_d->print(buf);

    float r = 1.0f - (float)seconds_left / (float)LIFELINK_MQ2_WARMUP_S;
    draw_progress(4, 48, 120, 8, r);
    g_d->display();
}

void show_calibrating() {
    if (!ready()) return;
    g_d->clearDisplay();
    g_d->setCursor(20, 16); g_d->print(F("Calibrating..."));
    g_d->setCursor(20, 32); g_d->print(F("Please wait."));
    g_d->display();
}

void show_main(const LifelinkState& s) {
    if (!ready()) return;
    char buf[24];
    bool blink = (millis() / 400) & 1;

    // Make sure we're not still inverted from a prior critical state.
    set_invert(false);

    g_d->clearDisplay();

    // ── Title bar (y=0) + status icons (y=1) ──────────────────────────────
    g_d->setCursor(0, 0); g_d->print(F("LIFELINK"));
    draw_wifi_icon(100, 1, s.wifi_up);
    draw_mqtt_icon(110, 1, s.mqtt_up);
    draw_bt_icon  (120, 1, s.bt_client_up);
    g_d->drawFastHLine(0, 9, 128, SH110X_WHITE);

    // ── Row: Temperature + Humidity (y=16) ───────────────────────────────
    g_d->setCursor(0, 16);  g_d->print(F("Temp:"));
    g_d->setCursor(36, 16);
    if (isnan(s.temperature)) g_d->print(F("--- C"));
    else { snprintf(buf, sizeof(buf), "%.1fC", s.temperature); g_d->print(buf); }

    g_d->setCursor(72, 16); g_d->print(F("H:"));
    g_d->setCursor(86, 16);
    if (isnan(s.humidity)) g_d->print(F("-- %"));
    else { snprintf(buf, sizeof(buf), "%.0f%%", s.humidity); g_d->print(buf); }

    // ── Row: Gas PPM (y=24) ───────────────────────────────────────────────
    g_d->setCursor(0, 24); g_d->print(F("Gas:"));
    snprintf(buf, sizeof(buf), "%.0f ppm", s.gas_ppm);
    g_d->setCursor(30, 24); g_d->print(buf);

    // ── Gas-level bar (y=33) ──────────────────────────────────────────────
    float ratio = s.gas_ppm / LIFELINK_GAS_ALARM_PPM;
    draw_progress(0, 33, 92, 7, ratio);
    snprintf(buf, sizeof(buf), "%3.0f%%", (ratio > 1.0f ? 1.0f : ratio) * 100.0f);
    g_d->setCursor(98, 32); g_d->print(buf);

    g_d->drawFastHLine(0, 41, 128, SH110X_WHITE);

    // ── Alert row (y=48) ──────────────────────────────────────────────────
    g_d->setCursor(8, 48);
    if (s.gas_alarm) {
        if (blink) g_d->print(F("!! GAS ALARM !!"));
    } else {
        g_d->print(F("Status: OK"));
    }

    // ── Connectivity row (y=56) ───────────────────────────────────────────
    g_d->setCursor(0,  56); g_d->print(s.wifi_up ? F("WiFi:ON") : F("WiFi:--"));
    g_d->setCursor(48, 56); g_d->print(s.mqtt_up ? F("MQTT:ON") : F("MQTT:--"));
    g_d->setCursor(96, 56); g_d->print(s.bt_client_up ? F("BT:ON") : F("BT:--"));

    g_d->display();
}

bool is_critical(const LifelinkState& s) {
    const bool temp_critical =
        !isnan(s.temperature) && s.temperature >= LIFELINK_TEMP_CRITICAL_C;
    const bool gas_critical  =
        s.gas_ppm >= LIFELINK_GAS_CRITICAL_PPM;
    return temp_critical || gas_critical;
}

void show_critical(const LifelinkState& s) {
    if (!ready()) return;
    char buf[28];

    // Toggle display polarity at ~2.5 Hz. This is the SH1106's hardware
    // 0xA6/0xA7 command — it inverts the whole panel in one shot, so the
    // effect is much more striking than blinking individual pixels.
    bool flash = (millis() / 200) & 1;
    set_invert(flash);

    g_d->clearDisplay();

    // ── "!! CRITICAL !!" banner (double-sized text fills the top 16 px) ──
    g_d->setTextSize(2);
    g_d->setCursor(4, 0); g_d->print(F("CRITICAL"));
    g_d->setTextSize(1);
    g_d->drawFastHLine(0, 17, 128, SH110X_WHITE);

    // ── Offending readings (only the ones actually over threshold) ──────
    int y = 22;
    const bool temp_critical =
        !isnan(s.temperature) && s.temperature >= LIFELINK_TEMP_CRITICAL_C;
    const bool gas_critical  =
        s.gas_ppm >= LIFELINK_GAS_CRITICAL_PPM;

    if (temp_critical) {
        snprintf(buf, sizeof(buf), "TEMP %.1f C  (>%.0f)",
                 s.temperature, LIFELINK_TEMP_CRITICAL_C);
        g_d->setCursor(0, y); g_d->print(buf);
        y += 10;
    }
    if (gas_critical) {
        snprintf(buf, sizeof(buf), "GAS  %.0f ppm  (>%.0f)",
                 s.gas_ppm, LIFELINK_GAS_CRITICAL_PPM);
        g_d->setCursor(0, y); g_d->print(buf);
        y += 10;
    }

    // ── "EVACUATE" footer (double-sized) ────────────────────────────────
    g_d->drawFastHLine(0, 46, 128, SH110X_WHITE);
    g_d->setTextSize(2);
    g_d->setCursor(4, 49); g_d->print(F("EVACUATE"));
    g_d->setTextSize(1);

    g_d->display();
}

}  // namespace oled_ui
