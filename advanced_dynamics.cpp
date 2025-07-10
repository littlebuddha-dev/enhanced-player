// ./advanced_dynamics.cpp
#include "advanced_dynamics.h"
#include <cmath>
#include <nlohmann/json.hpp>
#include <vector>
#include <iostream> // For logging

// --- AnalogSaturationクラスのメソッド実装 ---

void AnalogSaturation::setup(double sr, const json& params) {
    sample_rate_ = sr;
    if (params.is_object() && !params.empty()) {
        // ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↓修正開始◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
        enabled_ = params.value("enabled", true);
        // ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↑修正終わり◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
        drive_ = params.value("drive", 1.0);
        mix_ = params.value("mix", 0.3);
        type_ = params.value("type", "tube");
    }
    dc_blocker_.set_hpf(sr, 15.0, 0.707);
    anti_alias_.set_lpf(sr, sr / 2.1, 0.707);
}

void AnalogSaturation::reset() {
    dc_blocker_.reset();
    anti_alias_.reset();
}

float AnalogSaturation::tubeSaturation(float x) {
    if (drive_ == 0.0) return x;
    float k = 2.0f * drive_;
    float abs_x = std::abs(x);
    return (x > 0 ? 1.0f : -1.0f) * (abs_x - (abs_x * abs_x / (1.0f + k * abs_x)));
}

float AnalogSaturation::tapeSaturation(float x) {
    if (drive_ == 0.0) return x;
    return std::tanh(drive_ * x);
}

float AnalogSaturation::transformerSaturation(float x) {
    if (drive_ == 0.0) return x;
    const float a = 0.8f;
    const float b = 1.5f;
    float x_driven = drive_ * x;
    return std::tanh(x_driven) + a * std::tanh(b * x_driven);
}

float AnalogSaturation::processSample(float input) {
    float dry_signal = input;
    input = dc_blocker_.process(input);

    float wet_signal;
    if (type_ == "tube") {
        wet_signal = tubeSaturation(input);
    } else if (type_ == "tape") {
        wet_signal = tapeSaturation(input);
    } else if (type_ == "transformer") {
        wet_signal = transformerSaturation(input);
    } else {
        wet_signal = input; // Fallback or bypass
    }

    wet_signal = anti_alias_.process(wet_signal);
    return (1.0f - mix_) * dry_signal + mix_ * wet_signal;
}

void AnalogSaturation::process(std::vector<float>& block, int channels) {
    if (!enabled_) return;
    for (size_t i = 0; i < block.size(); ++i) {
        block[i] = processSample(block[i]);
    }
}

// --- MultibandCompressor の実装 ---
// ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↓修正開始◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
void MultibandCompressor::setup(double sr, const json& params) {
    sample_rate_ = sr;
    bands_.clear();

    if (params.is_object() && !params.empty()) {
        enabled_ = params.value("enabled", false); // Default to disabled if not specified
        if (params.contains("bands") && params["bands"].is_array()) {
            for (const auto& band_params : params["bands"]) {
                Band new_band;
                new_band.enabled = band_params.value("enabled", true);
                new_band.freq_low = band_params.value("freq_low", 20.0);
                new_band.freq_high = band_params.value("freq_high", sample_rate_ / 2.0);
                new_band.threshold_db = band_params.value("threshold_db", -10.0);
                new_band.ratio = band_params.value("ratio", 2.0);
                new_band.attack_ms = band_params.value("attack_ms", 10.0);
                new_band.release_ms = band_params.value("release_ms", 100.0);
                new_band.makeup_gain_db = band_params.value("makeup_gain_db", 0.0);

                // Calculate attack/release coefficients
                new_band.attack_coeff = std::exp(-1.0 / (sample_rate_ * new_band.attack_ms / 1000.0));
                new_band.release_coeff = std::exp(-1.0 / (sample_rate_ * new_band.release_ms / 1000.0));

                bands_.push_back(new_band);
            }
        }
    }

    // If no bands were configured or parsing failed, add a default wideband band
    if (bands_.empty()) {
        std::cerr << "[WARN] MultibandCompressor: No bands configured or JSON parsing failed. Adding default wideband." << std::endl;
        Band default_band;
        default_band.enabled = true;
        default_band.freq_low = 20.0;
        default_band.freq_high = sample_rate_ / 2.0 - 100; // Keep slightly below Nyquist
        default_band.threshold_db = -10.0;
        default_band.ratio = 2.0;
        default_band.attack_ms = 10.0;
        default_band.release_ms = 100.0;
        default_band.makeup_gain_db = 0.0;
        default_band.attack_coeff = std::exp(-1.0 / (sample_rate_ * default_band.attack_ms / 1000.0));
        default_band.release_coeff = std::exp(-1.0 / (sample_rate_ * default_band.release_ms / 1000.0));
        bands_.push_back(default_band);
    }

    setupCrossoverFilters(); // Setup filters for all bands after they are defined
    reset();
}

void MultibandCompressor::setupCrossoverFilters() {
    // This function will set up the band-pass filters for each band.
    // For simplicity, we're using simple LPF/HPF pairs for each band.
    // A true multiband compressor would use more sophisticated Linkwitz-Riley crossovers
    // to ensure a flat sum, but SimpleBiquad does not offer that directly.

    if (bands_.empty()) return;

    // Handle 1-band case (wideband)
    if (bands_.size() == 1) {
        bands_[0].lpf_l.set_lpf(sample_rate_, bands_[0].freq_high, 0.707); // Top of the band
        bands_[0].hpf_l.set_hpf(sample_rate_, bands_[0].freq_low, 0.707);  // Bottom of the band
        bands_[0].lpf_r.set_lpf(sample_rate_, bands_[0].freq_high, 0.707);
        bands_[0].hpf_r.set_hpf(sample_rate_, bands_[0].freq_low, 0.707);
        return;
    }

    // For multiple bands, chain LPFs and HPFs to create band-pass filters
    for (size_t i = 0; i < bands_.size(); ++i) {
        Band& band = bands_[i];

        // LPF for the upper bound of the current band
        band.lpf_l.set_lpf(sample_rate_, band.freq_high, 0.707);
        band.lpf_r.set_lpf(sample_rate_, band.freq_high, 0.707);

        // HPF for the lower bound of the current band
        band.hpf_l.set_hpf(sample_rate_, band.freq_low, 0.707);
        band.hpf_r.set_hpf(sample_rate_, band.freq_low, 0.707);
    }
}


void MultibandCompressor::reset() {
    for (auto& band : bands_) {
        band.envelope_l = 0.0;
        band.envelope_r = 0.0;
        band.lpf_l.reset();
        band.hpf_l.reset();
        band.lpf_r.reset();
        band.hpf_r.reset();
        band.bpf_l.reset(); // In case BPFs were used elsewhere or planned
        band.bpf_r.reset(); // In case BPFs were used elsewhere or planned
    }
}

float MultibandCompressor::calculateGain(float envelope, float threshold_db, float ratio) {
    float threshold_linear = db_to_linear(threshold_db);
    float gain = 1.0f;
    if (envelope > threshold_linear) {
        // Calculate gain reduction in dB
        float gain_reduction_db = (threshold_db - 20.0f * std::log10(envelope)) * (1.0f - (1.0f / ratio));
        gain = db_to_linear(gain_reduction_db);
    }
    return gain;
}


void MultibandCompressor::process(std::vector<float>& block, int channels) {
    if (!enabled_ || bands_.empty() || channels == 0) return;

    size_t num_frames = block.size() / channels;

    // Temporary storage for band-split and compressed signals
    std::vector<std::vector<float>> band_samples_l(bands_.size(), std::vector<float>(num_frames));
    std::vector<std::vector<float>> band_samples_r(bands_.size(), std::vector<float>(num_frames));


    for (size_t i = 0; i < num_frames; ++i) {
        float input_l = block[i * channels];
        float input_r = (channels > 1) ? block[i * channels + 1] : input_l;

        // 1. Split into bands and apply compression
        for (size_t b_idx = 0; b_idx < bands_.size(); ++b_idx) {
            Band& band = bands_[b_idx];
            if (!band.enabled) {
                // If band is disabled, pass through original signal for this band (or silence)
                band_samples_l[b_idx][i] = input_l;
                band_samples_r[b_idx][i] = input_r;
                continue;
            }

            // Apply band-pass filtering (cascaded LPF and HPF)
            float band_signal_l = band.lpf_l.process(band.hpf_l.process(input_l));
            float band_signal_r = band.lpf_r.process(band.hpf_r.process(input_r));

            // Envelope follower for left channel
            float current_level_l = std::abs(band_signal_l);
            if (current_level_l > band.envelope_l) {
                band.envelope_l = band.attack_coeff * band.envelope_l + (1.0f - band.attack_coeff) * current_level_l;
            } else {
                band.envelope_l = band.release_coeff * band.envelope_l + (1.0f - band.release_coeff) * current_level_l;
            }

            // Envelope follower for right channel
            float current_level_r = std::abs(band_signal_r);
            if (current_level_r > band.envelope_r) {
                band.envelope_r = band.attack_coeff * band.envelope_r + (1.0f - band.attack_coeff) * current_level_r;
            } else {
                band.envelope_r = band.release_coeff * band.envelope_r + (1.0f - band.release_coeff) * current_level_r;
            }

            // Calculate gain for left and right channels
            float gain_l = calculateGain(band.envelope_l, band.threshold_db, band.ratio);
            float gain_r = calculateGain(band.envelope_r, band.threshold_db, band.ratio);

            // Apply gain reduction and makeup gain
            band_samples_l[b_idx][i] = band_signal_l * gain_l * db_to_linear(band.makeup_gain_db);
            band_samples_r[b_idx][i] = band_signal_r * gain_r * db_to_linear(band.makeup_gain_db);
        }
    }

    // 2. Sum the processed bands back together
    for (size_t i = 0; i < num_frames; ++i) {
        float summed_l = 0.0f;
        float summed_r = 0.0f;
        for (size_t b_idx = 0; b_idx < bands_.size(); ++b_idx) {
            summed_l += band_samples_l[b_idx][i];
            summed_r += band_samples_r[b_idx][i];
        }
        block[i * channels] = summed_l;
        if (channels > 1) {
            block[i * channels + 1] = summed_r;
        }
    }
}
// ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↑修正終わり◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️


// --- MasteringLimiterクラスのメソッド実装 ---

void MasteringLimiter::setup(double sr, const json& params) {
    sample_rate_ = sr;
    if (params.is_object() && !params.empty()) {
        enabled_ = params.value("enabled", true);
        threshold_db_ = params.value("threshold_db", -0.1);
        attack_ms_ = params.value("attack_ms", 1.5);
        release_ms_ = params.value("release_ms", 50.0);
        lookahead_ms_ = params.value("lookahead_ms", 5.0);
    }

    threshold_linear_ = db_to_linear(threshold_db_);
    attack_coeff_ = (attack_ms_ > 0) ? std::exp(-1.0 / (sample_rate_ * attack_ms_ / 1000.0)) : 0.0;
    release_coeff_ = (release_ms_ > 0) ? std::exp(-1.0 / (sample_rate_ * release_ms_ / 1000.0)) : 0.0;
    lookahead_samples_ = static_cast<int>(sample_rate_ * lookahead_ms_ / 1000.0);

    shelf_filter_l_.set_highshelf(sr, 8000.0, 0.7, -1.5);
    shelf_filter_r_.set_highshelf(sr, 8000.0, 0.7, -1.5);

    reset();
}

void MasteringLimiter::reset() {
    lookahead_buffer_l_.assign(lookahead_samples_, 0.0f);
    lookahead_buffer_r_.assign(lookahead_samples_, 0.0f);
    envelope_ = 0.0f;
    shelf_filter_l_.reset();
    shelf_filter_r_.reset();
}

void MasteringLimiter::process(std::vector<float>& block, int channels) {
    if (!enabled_ || channels == 0) return;

    size_t num_frames = block.size() / channels;
    std::vector<float> processed_block(block.size());

    for (size_t i = 0; i < num_frames; ++i) {
        size_t base_idx = i * channels;
        float current_l = block[base_idx];
        float current_r = (channels > 1) ? block[base_idx + 1] : current_l;

        processed_block[base_idx] = lookahead_buffer_l_.front();
        if (channels > 1) processed_block[base_idx + 1] = lookahead_buffer_r_.front();

        lookahead_buffer_l_.pop_front();
        lookahead_buffer_r_.pop_front();
        lookahead_buffer_l_.push_back(current_l);
        lookahead_buffer_r_.push_back(current_r);
    }

    for (size_t i = 0; i < num_frames; ++i) {
        size_t base_idx = i * channels;
        float sample_l = processed_block[base_idx];
        float sample_r = (channels > 1) ? processed_block[base_idx + 1] : sample_l;

        float sidechain_l = shelf_filter_l_.process(sample_l);
        float sidechain_r = (channels > 1) ? shelf_filter_r_.process(sample_r) : sidechain_l;
        float peak_level = std::max(std::abs(sidechain_l), std::abs(sidechain_r));

        if (peak_level > envelope_) {
            envelope_ = attack_coeff_ * envelope_ + (1.0f - attack_coeff_) * peak_level;
        } else {
            envelope_ = release_coeff_ * envelope_ + (1.0f - release_coeff_) * peak_level;
        }

        float gain = 1.0f;
        if (envelope_ > threshold_linear_) {
            gain = threshold_linear_ / envelope_;
        }

        block[base_idx] = sample_l * gain;
        if (channels > 1) {
            block[base_idx + 1] = sample_r * gain;
        }
    }
}