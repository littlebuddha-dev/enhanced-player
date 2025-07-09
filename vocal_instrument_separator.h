// ./vocal_instrument_separator.h
// M/S Vocal-Instrument Separator with Full Implementation
#pragma once
#include "SimpleBiquad.h"
#include "AudioEffect.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <complex>
#include <array>

// M/S (Mid-Side) ベースの分離プロセッサー
class MSVocalInstrumentSeparator : public AudioEffect {
public:
    void setup(double sr, const json& params) override {
        sample_rate_ = sr;
        if (params.is_object() && !params.empty()) {
            enabled_ = params.value("enabled", true);
            vocal_enhance_ = params.value("vocal_enhance", 0.3);
            vocal_center_freq_ = params.value("vocal_center_freq", 2500.0);
            vocal_bandwidth_ = params.value("vocal_bandwidth", 2000.0);
            instrument_enhance_ = params.value("instrument_enhance", 0.2);
            stereo_width_ = params.value("stereo_width", 1.2);
        }

        vocal_bandpass_low_.set_hpf(sr, vocal_center_freq_ - vocal_bandwidth_/2, 0.707);
        vocal_bandpass_high_.set_lpf(sr, vocal_center_freq_ + vocal_bandwidth_/2, 0.707);
        instrument_low_.set_lpf(sr, 800.0, 0.8);
        instrument_high_.set_hpf(sr, 6000.0, 0.8);

        setupEnvelopeFollowers(sr);
    }

    void process(std::vector<float>& block, int channels) override {
        if (!enabled_ || channels != 2) return;

        size_t frame_count = block.size() / 2;
        for (size_t i = 0; i < frame_count; ++i) {
            float left = block[i * 2];
            float right = block[i * 2 + 1];

            auto processed_pair = processSample(left, right);

            block[i * 2] = processed_pair.first;
            block[i * 2 + 1] = processed_pair.second;
        }
    }

    void reset() override {
        vocal_bandpass_low_.reset();
        vocal_bandpass_high_.reset();
        instrument_low_.reset();
        instrument_high_.reset();
        vocal_envelope_ = 0.0f;
        instrument_envelope_ = 0.0f;
    }

    const std::string& getName() const override { return name_; }

private:
    // ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↓修正開始◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
    std::string name_ = "ms_separator";
    // ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↑修正終わり◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
    double sample_rate_ = 44100.0;
    bool enabled_ = true;
    double vocal_enhance_ = 0.3, vocal_center_freq_ = 2500.0, vocal_bandwidth_ = 2000.0;
    double instrument_enhance_ = 0.2, stereo_width_ = 1.2;

    SimpleBiquad vocal_bandpass_low_, vocal_bandpass_high_;
    SimpleBiquad instrument_low_, instrument_high_;

    float vocal_envelope_ = 0.0f, instrument_envelope_ = 0.0f;
    float vocal_attack_coeff_ = 0.0f, vocal_release_coeff_ = 0.0f;
    float inst_attack_coeff_ = 0.0f, inst_release_coeff_ = 0.0f;

    std::pair<float, float> processSample(float left, float right) {
        float mid = (left + right) * 0.5f;
        float side = (left - right) * 0.5f;

        auto separated = detectAndSeparate(mid, side);
        float enhanced_mid = separated.first;
        float enhanced_side = separated.second;

        float enhanced_left = enhanced_mid + enhanced_side;
        float enhanced_right = enhanced_mid - enhanced_side;

        return {enhanced_left, enhanced_right};
    }

    void setupEnvelopeFollowers(double sr) {
        // Attack/Releaseタイムを少し長めに設定して、よりスムーズなエンベロープを生成
        vocal_attack_coeff_ = std::exp(-1.0f / (0.01f * sr));   // 10ms
        vocal_release_coeff_ = std::exp(-1.0f / (0.15f * sr)); // 150ms
        inst_attack_coeff_ = std::exp(-1.0f / (0.02f * sr));   // 20ms
        inst_release_coeff_ = std::exp(-1.0f / (0.1f * sr));  // 100ms
    }

    std::pair<float, float> detectAndSeparate(float mid, float side) {
        // ボーカル成分を抽出
        float vocal_signal = vocal_bandpass_high_.process(vocal_bandpass_low_.process(mid));
        float vocal_level = std::abs(vocal_signal);

        // ボーカルのエンベロープを計算
        if (vocal_level > vocal_envelope_) {
            vocal_envelope_ = vocal_attack_coeff_ * vocal_envelope_ + (1.0f - vocal_attack_coeff_) * vocal_level;
        } else {
            vocal_envelope_ = vocal_release_coeff_ * vocal_envelope_ + (1.0f - vocal_release_coeff_) * vocal_level;
        }

        // 楽器成分を抽出（低域、高域、ステレオ成分）
        float inst_low = instrument_low_.process(mid);
        float inst_high = instrument_high_.process(mid);
        float instrument_level = std::max({std::abs(inst_low), std::abs(inst_high), std::abs(side)});

        // 楽器のエンベロープを計算
        if (instrument_level > instrument_envelope_) {
            instrument_envelope_ = inst_attack_coeff_ * instrument_envelope_ + (1.0f - inst_attack_coeff_) * instrument_level;
        } else {
            instrument_envelope_ = inst_release_coeff_ * instrument_envelope_ + (1.0f - inst_release_coeff_) * instrument_level;
        }

        return applyDynamicSeparation(mid, side);
    }

    std::pair<float, float> applyDynamicSeparation(float mid, float side) {
        // ボーカルと楽器の優勢度を計算
        float total_envelope = vocal_envelope_ + instrument_envelope_ + 1e-10f;
        float vocal_dominance = vocal_envelope_ / total_envelope;
        float instrument_dominance = 1.0f - vocal_dominance;

        // ゲインを滑らかに適用
        float mid_gain = 1.0f + vocal_enhance_ * vocal_dominance;
        float side_enhancement = (1.0f + instrument_enhance_ * instrument_dominance) * stereo_width_;
        float side_reduction = 1.0f - vocal_enhance_ * vocal_dominance * 0.3f;

        float enhanced_mid = mid * mid_gain;
        float enhanced_side = side * side_enhancement * side_reduction;

        return {enhanced_mid, enhanced_side};
    }
};