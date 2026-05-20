#include "mq2.h"
#include "config.h"
#include <Arduino.h>
#include <math.h>

// ─── Tuning constants for robustness ────────────────────────────────────────
//
// The MQ-2 LPG curve `ppm = A × (RS/R0)^B` has a negative exponent (B ≈ -2.222),
// so as RS/R0 approaches zero the predicted ppm explodes toward infinity. The
// datasheet curve is only specified up to ~10 000 ppm — beyond that we'd be
// extrapolating a power-law into nonsense, so we clamp.

static constexpr float    MQ2_PPM_MAX        = 10000.0f;
static constexpr float    MQ2_R0_MIN_OK      = 0.5f;     // kΩ
static constexpr float    MQ2_R0_MAX_OK      = 200.0f;   // kΩ
static constexpr float    MQ2_R0_DEFAULT     = 10.0f;    // datasheet-typical
static constexpr int      MQ2_ADC_SATURATED  = 3900;     // ~94 % of 4095
static constexpr uint16_t MQ2_MAX_CALIB_N    = 128;

MQ2::MQ2(uint8_t aout_pin, uint8_t dout_pin)
    : _aout_pin(aout_pin), _dout_pin(dout_pin), _r0(MQ2_R0_DEFAULT) {}

void MQ2::begin() {
    pinMode(_aout_pin, INPUT);
    pinMode(_dout_pin, INPUT);
    analogReadResolution(12);                  // 0–4095
    analogSetPinAttenuation(_aout_pin, ADC_11db); // ~0–3.3 V usable range
}

int MQ2::read_raw_adc() {
    return analogRead(_aout_pin);
}

bool MQ2::saturated() {
    return read_raw_adc() >= MQ2_ADC_SATURATED;
}

float MQ2::read_rs() {
    int raw = analogRead(_aout_pin);
    float v = (raw / 4095.0f) * 3.3f;
    if (v < 0.001f) v = 0.001f;                // avoid div-by-zero
    return ((3.3f - v) / v) * LIFELINK_MQ2_RL_KOHM;
}

// ── Calibration ─────────────────────────────────────────────────────────────
//
// Each attempt averages the median RS over n_samples (~5 s at 100 ms each).
// If the resulting R0 is plausible, we accept it; if not, we sleep
// `retry_delay_ms` to let the heater finish warming up and try again.
// A fresh MQ-2 can take 30–60 s of burn-in to settle; the retry loop adapts
// to that without making every boot wait the full 60 s.

bool MQ2::calibrate(uint16_t n_samples,
                    uint8_t  max_attempts,
                    uint32_t retry_delay_ms) {
    if (n_samples == 0) n_samples = 1;
    if (n_samples > MQ2_MAX_CALIB_N) n_samples = MQ2_MAX_CALIB_N;
    if (max_attempts == 0) max_attempts = 1;

    float samples[MQ2_MAX_CALIB_N];
    _calib_ok = false;

    for (uint8_t attempt = 1; attempt <= max_attempts; attempt++) {
        for (uint16_t i = 0; i < n_samples; i++) {
            samples[i] = read_rs();
            delay(100);
        }
        // Insertion sort — N ≤ 128 and this only runs at boot.
        for (uint16_t i = 1; i < n_samples; i++) {
            float key = samples[i];
            int   j   = (int)i - 1;
            while (j >= 0 && samples[j] > key) {
                samples[j + 1] = samples[j];
                j--;
            }
            samples[j + 1] = key;
        }
        const float median_rs    = samples[n_samples / 2];
        const float r0_candidate = median_rs / LIFELINK_MQ2_CLEAN_AIR;

        if (r0_candidate >= MQ2_R0_MIN_OK && r0_candidate <= MQ2_R0_MAX_OK) {
            _r0       = r0_candidate;
            _calib_ok = true;
            Serial.printf("[mq2] R0 = %.2f kOhm  (median RS = %.2f kOhm, attempt %u/%u)\n",
                          _r0, median_rs, attempt, max_attempts);
            return true;
        }

        Serial.printf("[mq2] attempt %u/%u: R0 candidate %.2f kOhm out of "
                      "plausible range [%.1f, %.1f] (median RS = %.2f kOhm)\n",
                      attempt, max_attempts, r0_candidate,
                      MQ2_R0_MIN_OK, MQ2_R0_MAX_OK, median_rs);
        if (attempt < max_attempts) {
            Serial.printf("[mq2]   heater still settling — waiting %lu ms before retry...\n",
                          (unsigned long)retry_delay_ms);
            delay(retry_delay_ms);
        }
    }

    _r0       = MQ2_R0_DEFAULT;
    _calib_ok = false;
    Serial.printf("[mq2] all %u attempts failed — falling back to default R0 = %.1f kOhm. "
                  "Live readings will still publish, but treat them as relative until "
                  "the sensor has had more burn-in time.\n",
                  max_attempts, MQ2_R0_DEFAULT);
    return false;
}

// ── Running median filter for live reads ────────────────────────────────────

float MQ2::_push_and_median(float rs) {
    _rs_buf[_rs_idx] = rs;
    _rs_idx = (_rs_idx + 1) % RS_FILTER_N;
    if (_rs_count < RS_FILTER_N) _rs_count++;

    float sorted[RS_FILTER_N];
    for (int i = 0; i < _rs_count; i++) sorted[i] = _rs_buf[i];
    for (int i = 1; i < _rs_count; i++) {
        float key = sorted[i];
        int   j   = i - 1;
        while (j >= 0 && sorted[j] > key) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }
    return sorted[_rs_count / 2];
}

float MQ2::read_ppm() {
    const float rs    = _push_and_median(read_rs());
    const float ratio = rs / _r0;
    if (ratio <= 0.0f) return 0.0f;

    float ppm = LIFELINK_MQ2_LPG_A * powf(ratio, LIFELINK_MQ2_LPG_B);
    if (ppm < 0.0f || !isfinite(ppm)) ppm = 0.0f;
    if (ppm > MQ2_PPM_MAX)            ppm = MQ2_PPM_MAX;
    return ppm;
}

bool MQ2::digital_alarm() {
    return digitalRead(_dout_pin) == LOW;       // DOUT is active-LOW
}
