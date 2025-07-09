// ./advanced_dynamics.cpp
#include "advanced_dynamics.h"
#include <cmath>
#include <nlohmann/json.hpp>
#include <vector>

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
        wet_signal = input;
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
void MultibandCompressor::setup(double sr, const json& params) {
    sample_rate_ = sr;
    if (params.is_object() && !params.empty()) {
        enabled_ = params.value("enabled", false); // スタブなのでデフォルト無効
    }
}

void MultibandCompressor::reset() {
    for (auto& band : bands_) {
        band.envelope = 0.0;
        band.lpf.reset();
        band.hpf.reset();
        band.bpf.reset();
    }
    for (auto& filter : crossover_filters_) {
        filter.reset();
    }
}

void MultibandCompressor::process(std::vector<float>& block, int channels) {
    // ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↓修正開始◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
    if (!enabled_) return;
    // (スタブ) マルチバンドコンプレッサーのブロック処理ロジック
    // ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↑修正終わり◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
}

void MultibandCompressor::setupCrossoverNetwork() {}
std::vector<float> MultibandCompressor::splitToBands(float input) { return {}; }
float MultibandCompressor::compressBand(float input, Band& band) { return input; }
float MultibandCompressor::sumBands(const std::vector<float>& bands) { return 0.0f; }


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