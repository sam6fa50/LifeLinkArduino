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
static constexpr uint16_t MQ2_MAX_CALIB_N    = 128;

// Per-call oversampling. Each read_rs() takes this many quick ADC samples
// and averages the usable ones. Near-rail readings may mean saturation,
// shorts, or disconnected wiring; report them as unavailable, never low gas.
static constexpr int      MQ2_OVERSAMPLE_N   = 8;
static constexpr int      MQ2_ADC_NOISE_LOW  = 10;
static constexpr int      MQ2_ADC_NOISE_HIGH = 4085;

MQ2::MQ2(uint8_t aout_pin)
    : _aout_pin(aout_pin), _r0(MQ2_R0_DEFAULT) {}

void MQ2::begin() {
    pinMode(_aout_pin, INPUT);
    analogReadResolution(12);                  // 0–4095
    analogSetPinAttenuation(_aout_pin, ADC_11db); // raw-count conversion below uses an approximate 3.3 V scale
}

int MQ2::read_raw_adc() {
    return analogRead(_aout_pin);
}

float MQ2::read_rs() {
    long sum = 0;
    int  kept = 0;
    for (int i = 0; i < MQ2_OVERSAMPLE_N; i++) {
        int r = analogRead(_aout_pin);
        if (r >= MQ2_ADC_NOISE_LOW && r <= MQ2_ADC_NOISE_HIGH) {
            sum += r;
            kept++;
        }
    }
    // Require at least 6 of 8 usable samples. Never substitute an old value.
    if (kept < 6) return NAN;

    int raw = (int)(sum / kept);
    float v = (raw / 4095.0f) * 3.3f;
    const float rs = ((3.3f - v) / v) * LIFELINK_MQ2_RL_KOHM;
    return isfinite(rs) && rs > 0.0f ? rs : NAN;
}

// ── Calibration ─────────────────────────────────────────────────────────────
//
// Each attempt averages the median RS over n_samples (~5 s at 100 ms each).
// If the resulting R0 is plausible, we accept it; if not, we sleep
// `retry_delay_ms` to let the heater finish warming up and try again.
// These retries are only a software startup sequence; they do not establish
// that the sensor has completed manufacturer-specified conditioning.

bool MQ2::calibrate(uint16_t n_samples,
                    uint8_t  max_attempts,
                    uint32_t retry_delay_ms) {
    if (n_samples == 0) n_samples = 1;
    if (n_samples > MQ2_MAX_CALIB_N) n_samples = MQ2_MAX_CALIB_N;
    if (max_attempts == 0) max_attempts = 1;

    float samples[MQ2_MAX_CALIB_N];
    _calib_ok = false;
    _rs_count = _rs_idx = 0;

    for (uint16_t attempt = 1; attempt <= max_attempts; attempt++) {
        uint16_t valid_count = 0;
        for (uint16_t i = 0; i < n_samples; i++) {
            const float rs = read_rs();
            if (isfinite(rs) && rs > 0.0f) samples[valid_count++] = rs;
            delay(100);
        }
        // Insertion sort — N ≤ 128 and this only runs at boot.
        for (uint16_t i = 1; i < valid_count; i++) {
            float key = samples[i];
            int   j   = (int)i - 1;
            while (j >= 0 && samples[j] > key) {
                samples[j + 1] = samples[j];
                j--;
            }
            samples[j + 1] = key;
        }
        // Reject an attempt with more than 20% unusable measurements.
        const float median_rs = valid_count * 5 >= n_samples * 4
                              ? samples[valid_count / 2] : NAN;
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
    Serial.printf("[mq2] all %u attempts failed — default R0 = %.1f kOhm retained internally. "
                  "Gas readings are unavailable; check the circuit and restart in clean air.\n",
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
    const float current_rs = read_rs();
    if (!_calib_ok || !isfinite(current_rs) || current_rs <= 0.0f) {
        _rs_count = _rs_idx = 0;  // recovery must not reuse pre-failure readings
        return NAN;
    }
    const float rs    = _push_and_median(current_rs);
    const float ratio = rs / _r0;
    if (!isfinite(ratio) || ratio <= 0.0f) return NAN;

    // Saturate before powf can overflow; a high estimate must never become zero.
    const float max_ratio = powf(MQ2_PPM_MAX / LIFELINK_MQ2_LPG_A,
                                 1.0f / LIFELINK_MQ2_LPG_B);
    if (ratio <= max_ratio) return MQ2_PPM_MAX;

    float ppm = LIFELINK_MQ2_LPG_A * powf(ratio, LIFELINK_MQ2_LPG_B);
    if (ppm < 0.0f || !isfinite(ppm)) return NAN;
    if (ppm > MQ2_PPM_MAX)            ppm = MQ2_PPM_MAX;
    return ppm;
}
