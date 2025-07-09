// ./custom_effects.cpp
// 新規作成：ExciterとGlossEnhancerエフェクトの実装
#include "custom_effects.h"
#include <cmath>

// --- Exciterクラスのメソッド実装 ---

void Exciter::setup(double sr, const json& params) {
    sample_rate_ = sr;
    if (params.is_object() && !params.empty()) {
        enabled_ = params.value("enabled", true);
        crossover_freq_ = params.value("crossover_freq", 7800.0);
        drive_ = params.value("drive", 2.8);
        mix_ = params.value("mix", 0.18);
    }

    highpass_filter_l_.set_hpf(sr, crossover_freq_, 0.707);
    highpass_filter_r_.set_hpf(sr, crossover_freq_, 0.707);
    lowpass_filter_l_.set_lpf(sr, crossover_freq_, 0.707);
    lowpass_filter_r_.set_lpf(sr, crossover_freq_, 0.707);
    reset();
}

void Exciter::reset() {
    highpass_filter_l_.reset();
    highpass_filter_r_.reset();
    lowpass_filter_l_.reset();
    lowpass_filter_r_.reset();
}

float Exciter::saturate(float x) {
    // drive_を適用したtanh関数でシンプルなサチュレーションを実装
    return std::tanh(x * drive_);
}

float Exciter::processSample(float sample, SimpleBiquad& hpf, SimpleBiquad& lpf) {
    float dry_signal = sample;
    float high_freq_component = hpf.process(dry_signal);
    float low_freq_component = lpf.process(dry_signal);

    float saturated_highs = saturate(high_freq_component);

    return low_freq_component + (dry_signal * (1.0f - mix_)) + (saturated_highs * mix_);
}

void Exciter::process(std::vector<float>& block, int channels) {
    if (!enabled_) return;

    for (size_t i = 0; i < block.size(); i += channels) {
        if (channels == 1) {
            block[i] = processSample(block[i], highpass_filter_l_, lowpass_filter_l_);
        } else if (channels == 2) {
            block[i] = processSample(block[i], highpass_filter_l_, lowpass_filter_l_);
            block[i + 1] = processSample(block[i + 1], highpass_filter_r_, lowpass_filter_r_);
        }
    }
}


// --- GlossEnhancerクラスのメソッド実装 ---

void GlossEnhancer::setup(double sr, const json& params) {
    sample_rate_ = sr;
    if (params.is_object() && !params.empty()) {
        enabled_ = params.value("enabled", true);
        harmonic_drive_ = params.value("harmonic_drive", 0.35);
        even_harmonics_ = params.value("even_harmonics", 0.28);
        odd_harmonics_ = params.value("odd_harmonics", 0.18);
        total_mix_ = params.value("total_mix", 0.22);

        // JSONから直接ゲインを取得
        double presence_gain = params.value("presence_gain", 1.0);
        double air_gain = params.value("air_gain", 1.0);
        
        // gainをdBに変換
        double presence_gain_db = 20.0 * log10(presence_gain);
        double air_gain_db = 20.0 * log10(air_gain);

        presence_filter_l_.set_peaking(sr, 4000.0, 1.5, presence_gain_db);
        presence_filter_r_.set_peaking(sr, 4000.0, 1.5, presence_gain_db);
        air_filter_l_.set_peaking(sr, 12000.0, 2.0, air_gain_db);
        air_filter_r_.set_peaking(sr, 12000.0, 2.0, air_gain_db);
    }
    dc_blocker_l_.set_hpf(sr, 15.0, 0.707);
    dc_blocker_r_.set_hpf(sr, 15.0, 0.707);
    reset();
}

void GlossEnhancer::reset() {
    dc_blocker_l_.reset();
    dc_blocker_r_.reset();
    presence_filter_l_.reset();
    presence_filter_r_.reset();
    air_filter_l_.reset();
    air_filter_r_.reset();
}

float GlossEnhancer::processSample(float sample, SimpleBiquad& dc, SimpleBiquad& pres, SimpleBiquad& air) {
    float dry_signal = sample;

    // 1. DCオフセット除去
    float processed = dc.process(sample);
    
    // 2. 倍音付加
    float harmonics = 0.0f;
    float abs_processed = std::abs(processed);
    // 偶数次倍音 (x^2 - |x|)
    harmonics += (processed * processed - abs_processed) * even_harmonics_;
    // 奇数次倍音 (tanh)
    harmonics += (std::tanh(processed * 1.5f) - processed) * odd_harmonics_;
    processed += harmonics * harmonic_drive_;

    // 3. プレゼンスとエアーの調整
    processed = pres.process(processed);
    processed = air.process(processed);

    return (dry_signal * (1.0f - total_mix_)) + (processed * total_mix_);
}

void GlossEnhancer::process(std::vector<float>& block, int channels) {
    if (!enabled_) return;

    for (size_t i = 0; i < block.size(); i += channels) {
        if (channels == 1) {
            block[i] = processSample(block[i], dc_blocker_l_, presence_filter_l_, air_filter_l_);
        } else if (channels == 2) {
            block[i] = processSample(block[i], dc_blocker_l_, presence_filter_l_, air_filter_l_);
            block[i + 1] = processSample(block[i + 1], dc_blocker_r_, presence_filter_r_, air_filter_r_);
        }
    }
}