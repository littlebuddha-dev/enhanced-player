// ./advanced_dynamics.cpp
#include "advanced_dynamics.h"
#include <cmath>
#include <nlohmann/json.hpp>

// --- AnalogSaturationクラスのメソッド実装 ---

void AnalogSaturation::setup(double sr, const json& params) {
    sample_rate_ = sr;
    if (params.is_object() && !params.empty()) {
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
    for (size_t i = 0; i < block.size(); ++i) {
        block[i] = processSample(block[i]);
    }
}

// --- MultibandCompressor の実装 ---
void MultibandCompressor::setup(double sr, const json& params) {
    sample_rate_ = sr;
    // (スタブ) JSONからバンド設定を読み込むロジックをここに追加
    // setupCrossoverNetwork(); 
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
    // (スタブ) マルチバンドコンプレッサーのブロック処理ロジックをここに実装
    // この実装は複雑になるため、今回は何もしない
}

// (スタブ) プライベートメソッド
void MultibandCompressor::setupCrossoverNetwork() {}
std::vector<float> MultibandCompressor::splitToBands(float input) { return {}; }
float MultibandCompressor::compressBand(float input, Band& band) { return input; }
float MultibandCompressor::sumBands(const std::vector<float>& bands) { return 0.0f; }