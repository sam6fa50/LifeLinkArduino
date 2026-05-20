#pragma once

#include <Arduino.h>

// MQ-2 analog gas sensor driver (Flying-Fish breakout).
// LPG-equivalent ppm via the datasheet log-log curve:  ppm = A × (RS/R0)^B
// R0 is derived once at startup by averaging RS in clean air for N samples.

class MQ2 {
public:
    MQ2(uint8_t aout_pin, uint8_t dout_pin);

    void  begin();
    // Tries up to `max_attempts` times to derive a plausible R0. Each attempt
    // takes (n_samples × 100 ms); failures wait `retry_delay_ms` before
    // re-sampling so the heater gets more time to stabilise. Returns true
    // when calibration produced an in-range R0, false when it fell back
    // to the default.
    bool  calibrate(uint16_t n_samples,
                    uint8_t  max_attempts   = 5,
                    uint32_t retry_delay_ms = 10000);

    float read_rs();              // instantaneous RS in kΩ
    float read_ppm();             // median-filtered, clamped 0..10000 ppm
    bool  digital_alarm();        // DOUT comparator — see note in main.cpp
    int   read_raw_adc();         // raw 0..4095 — for diagnostics
    bool  saturated();            // true when the ADC is pinned near full-scale

    float r0() const             { return _r0; }
    bool  calibration_ok() const { return _calib_ok; }

private:
    uint8_t _aout_pin;
    uint8_t _dout_pin;
    float   _r0;
    bool    _calib_ok = false;

    // 5-sample ring buffer of RS values, used by read_ppm() to smooth out
    // ADC jitter and slow thermal drift without hiding real gas events.
    static constexpr int RS_FILTER_N = 5;
    float _rs_buf[RS_FILTER_N] = { 0 };
    int   _rs_count = 0;
    int   _rs_idx   = 0;
    float _push_and_median(float rs);
};
