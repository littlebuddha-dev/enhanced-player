// ./advanced_eq_harmonics.cpp
#include "advanced_eq_harmonics.h"
#include <cmath>
#include <algorithm>

// HarmonicEnhancerクラスのメソッド実装
void HarmonicEnhancer::setup(double sr, const json& params) {
    sample_rate_ = sr;
    if (params.is_object() && !params.empty()) {
        enabled_ = params.value("enabled", true);
        drive_ = params.value("drive", 0.3);
        even_harmonics_ = params.value("even_harmonics", 0.2);
        odd_harmonics_ = params.value("odd_harmonics", 0.3);
        mix_ = params.value("mix", 0.25);
    }
    
    // DC成分を除去するためのハイパスフィルタ
    dc_blocker_.set_hpf(sr, 15.0, 0.707);
    // 倍音生成によって発生するエイリアシングを抑制するためのローパスフィルタ
    lowpass_.set_lpf(sr, sr / 2.2, 0.707);
}

float HarmonicEnhancer::generateHarmonics(float input) {
    float processed = 0.0f;
    float abs_input = std::fabs(input);

    // 偶数次倍音: 絶対値や二乗関数で生成
    if (even_harmonics_ > 0) {
        processed += (input * input - abs_input) * even_harmonics_;
    }

    // 奇数次倍音: tanh関数や三次関数で生成
    if (odd_harmonics_ > 0) {
        processed += (std::tanh(input * 1.5f) - input) * odd_harmonics_;
    }
    
    return input + processed * drive_;
}

float HarmonicEnhancer::process(float input) {
    if (!enabled_) return input;

    float dry_signal = input;
    
    input = dc_blocker_.process(input);
    float wet_signal = generateHarmonics(input);
    wet_signal = lowpass_.process(wet_signal);
    
    return (1.0 - mix_) * dry_signal + mix_ * wet_signal;
}


// LinearPhaseEQ, SpectralGate のスタブ実装 (将来的な拡張用)
void LinearPhaseEQ::setup(double sr, const json& params) {
    sample_rate_ = sr;
    // 実装
}

std::vector<float> LinearPhaseEQ::process(const std::vector<float>& input) {
    // 実装
    return input;
}

void SpectralGate::setup(double sr, const json& params) {
    sample_rate_ = sr;
    // 実装
}

float SpectralGate::process(float input) {
    // 実装
    return input;
}
