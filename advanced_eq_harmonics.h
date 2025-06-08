// ./advanced_eq_harmonics.h
#pragma once
#include "SimpleBiquad.h"
#include <vector>
#include <cmath>
#include <complex>
#include <string>

// jsonエイリアスを追加
using json = nlohmann::json;

// 線形位相EQ（FFTベース）
class LinearPhaseEQ {
public:
    void setup(double sr, const json& params);
    std::vector<float> process(const std::vector<float>& input);

    void reset() {
        std::fill(input_buffer_.begin(), input_buffer_.end(), 0.0f);
        std::fill(output_buffer_.begin(), output_buffer_.end(), 0.0f);
        std::fill(overlap_buffer_.begin(), overlap_buffer_.end(), 0.0f);
        buffer_pos_ = 0;
        output_pos_ = 0;
        samples_since_last_process_ = 0;
    }

private:
    double sample_rate_ = 44100.0;
    size_t fft_size_ = 2048;
    double overlap_ = 0.75;
    size_t hop_size_;
    bool enabled_ = true;
    
    std::vector<float> input_buffer_, output_buffer_, overlap_buffer_;
    std::vector<float> window_;
    std::vector<std::complex<float>> eq_response_;
    
    size_t buffer_pos_ = 0;
    size_t output_pos_ = 0;
    size_t samples_since_last_process_ = 0;
    
    void setupEQCurve(const json& bands);
    void applyEQBand(double freq, double gain_db, double q, const std::string& type);
    void processBlock();
};

// ハーモニックエンハンサー
class HarmonicEnhancer {
public:
    void setup(double sr, const json& params);
    float process(float input);
    
    void reset() {
        bandpass_.reset();
        lowpass_.reset();
        dc_blocker_.reset();
    }

private:
    double sample_rate_ = 44100.0;
    bool enabled_ = true;
    double drive_ = 0.3;
    double even_harmonics_ = 0.2;
    double odd_harmonics_ = 0.3;
    double mix_ = 0.25;
    
    SimpleBiquad bandpass_, lowpass_, dc_blocker_;
    
    float generateHarmonics(float input);
};

// スペクトラルゲート（ノイズ除去）
class SpectralGate {
public:
    void setup(double sr, const json& params);
    float process(float input);
    
    void reset() {
        current_gain_ = 0.0f;
        attack_smoother_.reset();
        release_smoother_.reset();
    }

private:
    double sample_rate_ = 44100.0;
    bool enabled_ = false;
    double threshold_db_ = -60.0;
    double attack_ms_ = 5.0;
    double release_ms_ = 100.0;
    
    float current_gain_ = 0.0f;
    SimpleBiquad attack_smoother_, release_smoother_;
};