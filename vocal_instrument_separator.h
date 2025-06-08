// ./vocal_instrument_separator.h (実装統合版)
#pragma once
#include "SimpleBiquad.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <complex>
#include <array>

using json = nlohmann::json;

// M/S (Mid-Side) ベースの分離プロセッサー
class MSVocalInstrumentSeparator {
public:
    void setup(double sr, const json& params) {
        sample_rate_ = sr;
        enabled_ = params.value("enabled", true);
        
        vocal_enhance_ = params.value("vocal_enhance", 0.3);
        vocal_center_freq_ = params.value("vocal_center_freq", 2500.0);
        vocal_bandwidth_ = params.value("vocal_bandwidth", 2000.0);
        
        instrument_enhance_ = params.value("instrument_enhance", 0.2);
        stereo_width_ = params.value("stereo_width", 1.2);
        
        vocal_bandpass_low_.set_hpf(sr, vocal_center_freq_ - vocal_bandwidth_/2, 0.707);
        vocal_bandpass_high_.set_lpf(sr, vocal_center_freq_ + vocal_bandwidth_/2, 0.707);
        
        instrument_low_.set_lpf(sr, 800.0, 0.8);
        instrument_high_.set_hpf(sr, 6000.0, 0.8);
        
        setupEnvelopeFollowers(sr);
    }
    
    std::pair<float, float> process(float left, float right) {
        if (!enabled_) return {left, right};
        
        float mid = (left + right) * 0.5f;
        float side = (left - right) * 0.5f;
        
        auto separated = detectAndSeparate(mid, side);
        float enhanced_mid = separated.first;
        float enhanced_side = separated.second;
        
        float enhanced_left = enhanced_mid + enhanced_side;
        float enhanced_right = enhanced_mid - enhanced_side;
        
        return {enhanced_left, enhanced_right};
    }

    void reset() {
        vocal_bandpass_low_.reset();
        vocal_bandpass_high_.reset();
        instrument_low_.reset();
        instrument_high_.reset();
        vocal_envelope_ = 0.0f;
        instrument_envelope_ = 0.0f;
    }

private:
    double sample_rate_ = 44100.0;
    bool enabled_ = true;
    double vocal_enhance_ = 0.3, vocal_center_freq_ = 2500.0, vocal_bandwidth_ = 2000.0;
    double instrument_enhance_ = 0.2, stereo_width_ = 1.2;
    
    SimpleBiquad vocal_bandpass_low_, vocal_bandpass_high_;
    SimpleBiquad instrument_low_, instrument_high_;
    
    float vocal_envelope_ = 0.0f, instrument_envelope_ = 0.0f;
    float vocal_attack_coeff_ = 0.0f, vocal_release_coeff_ = 0.0f;
    float inst_attack_coeff_ = 0.0f, inst_release_coeff_ = 0.0f;
    
    void setupEnvelopeFollowers(double sr) {
        vocal_attack_coeff_ = std::exp(-1.0 / (0.003 * sr));
        vocal_release_coeff_ = std::exp(-1.0 / (0.1 * sr));
        inst_attack_coeff_ = std::exp(-1.0 / (0.01 * sr));
        inst_release_coeff_ = std::exp(-1.0 / (0.05 * sr));
    }
    
    std::pair<float, float> detectAndSeparate(float mid, float side) {
        float vocal_signal = vocal_bandpass_high_.process(vocal_bandpass_low_.process(mid));
        float vocal_level = std::abs(vocal_signal);
        
        if (vocal_level > vocal_envelope_) {
            vocal_envelope_ = vocal_attack_coeff_ * vocal_envelope_ + (1.0f - vocal_attack_coeff_) * vocal_level;
        } else {
            vocal_envelope_ = vocal_release_coeff_ * vocal_envelope_ + (1.0f - vocal_release_coeff_) * vocal_level;
        }
        
        float inst_low = instrument_low_.process(mid);
        float inst_high = instrument_high_.process(mid);
        float instrument_level = std::max({std::abs(inst_low), std::abs(inst_high), std::abs(side)});
        
        if (instrument_level > instrument_envelope_) {
            instrument_envelope_ = inst_attack_coeff_ * instrument_envelope_ + (1.0f - inst_attack_coeff_) * instrument_level;
        } else {
            instrument_envelope_ = inst_release_coeff_ * instrument_envelope_ + (1.0f - inst_release_coeff_) * instrument_level;
        }
        
        return applyDynamicSeparation(mid, side);
    }
    
    std::pair<float, float> applyDynamicSeparation(float mid, float side) {
        float vocal_dominance = vocal_envelope_ / (vocal_envelope_ + instrument_envelope_ + 1e-10f);
        float instrument_dominance = 1.0f - vocal_dominance;
        float enhanced_mid = mid;
        float enhanced_side = side;
        
        if (vocal_dominance > 0.6f) {
            enhanced_mid *= 1.0f + vocal_enhance_ * vocal_dominance;
            enhanced_side *= (1.0f - vocal_enhance_ * 0.3f);
        } else if (instrument_dominance > 0.7f) {
            enhanced_side *= (1.0f + instrument_enhance_ * instrument_dominance) * stereo_width_;
        }
        return {enhanced_mid, enhanced_side};
    }
};

// 動的マルチバンドセパレーター
class DynamicMultibandSeparator {
public:
    struct Band {
        double freq_low, freq_high;
        bool vocal_dominant;
        double separation_strength;
        SimpleBiquad lpf, hpf;
        float envelope = 0.0f;
        double attack_coeff = 0.0;
        double release_coeff = 0.0;
    };
    
    void setup(double sr, const json& params);
    std::pair<float, float> process(float left, float right);

    void reset() {
        for (auto& band : bands_) {
            band.lpf.reset();
            band.hpf.reset();
            band.envelope = 0.0f;
        }
    }

private:
    double sample_rate_ = 44100.0;
    bool enabled_ = true;
    std::vector<Band> bands_;
    
    std::pair<float, float> extractBand(float left, float right, Band& band);
    std::pair<float, float> processBand(float left, float right, Band& band);
};

// スペクトラル・ボーカル分離（高度版）
class SpectralVocalSeparator {
public:
    void setup(double sr, const json& params);
    std::pair<float, float> process(float left, float right);

    void reset() {
        std::fill(input_buffer_.begin(), input_buffer_.end(), 0.0f);
        std::fill(output_buffer_.begin(), output_buffer_.end(), 0.0f);
        std::fill(overlap_buffer_.begin(), overlap_buffer_.end(), 0.0f);
    }

private:
    double sample_rate_ = 44100.0;
    size_t fft_size_ = 2048;
    bool enabled_ = false;
    double vocal_threshold_ = 0.7;
    double separation_strength_ = 0.3;
    
    std::vector<float> window_;
    std::vector<float> input_buffer_, output_buffer_, overlap_buffer_;
    
    bool detectVocalActivity(float mid, float side);
};