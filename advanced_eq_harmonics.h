// ./advanced_eq_harmonics.h
// Header for Harmonic Enhancer and Linear Phase EQ (Final Corrected Version)
#pragma once
#include "SimpleBiquad.h"
#include "AudioEffect.h"
#include <vector>
#include <cmath>
#include <complex>
#include <string>
#include <nlohmann/json.hpp>
#include <fftw3.h>

// 線形位相EQ（FFTベース）
class LinearPhaseEQ : public AudioEffect {
public:
    LinearPhaseEQ();
    ~LinearPhaseEQ() override;
    void setup(double sr, const json& params) override;
    void process(std::vector<float>& block, int channels) override;
    void reset() override;
    const std::string& getName() const override { return name_; }

private:
    std::string name_ = "LinearPhaseEQ";
    double sample_rate_ = 44100.0;
    size_t fft_size_ = 2048;
    size_t hop_size_ = 512;
    bool enabled_ = true;
    int channels_ = 0;

    // 左右チャンネル用のFFTプランとバッファ
    fftwf_plan fft_plan_fwd_L_, fft_plan_fwd_R_;
    fftwf_plan fft_plan_bwd_L_, fft_plan_bwd_R_;

    std::vector<float> window_;

    // 処理に使用するバッファ
    std::vector<float> time_domain_buffer_L_, time_domain_buffer_R_;
    std::vector<std::complex<float>> freq_domain_buffer_L_, freq_domain_buffer_R_;

    // EQカーブ
    std::vector<std::complex<float>> eq_curve_;

    // Overlap-Add法のための入出力バッファ
    std::vector<float> input_buffer_L_, input_buffer_R_;
    std::vector<float> output_buffer_L_, output_buffer_R_;
    size_t input_buffer_write_pos_ = 0;

    void setupEQCurve(const json& bands);
    void applyEQBand(double freq, double gain_db, double q, const std::string& type);
};

// ハーモニックエンハンサー
class HarmonicEnhancer : public AudioEffect {
public:
    void setup(double sr, const json& params) override;
    void process(std::vector<float>& block, int channels) override;
    void reset() override;
    const std::string& getName() const override { return name_; }

private:
    std::string name_ = "HarmonicEnhancer";
    double sample_rate_ = 44100.0;
    bool enabled_ = true;
    double drive_ = 0.3;
    double even_harmonics_ = 0.2;
    double odd_harmonics_ = 0.3;
    double mix_ = 0.25;

    SimpleBiquad dc_blocker_, lowpass_;

    float generateHarmonics(float input);
    float processSample(float sample);
};

// スペクトラルゲート（ノイズ除去）
class SpectralGate : public AudioEffect {
public:
    void setup(double sr, const json& params) override;
    void process(std::vector<float>& block, int channels) override;
    void reset() override;
    const std::string& getName() const override { return name_; }

private:
    std::string name_ = "SpectralGate";
    double sample_rate_ = 44100.0;
    bool enabled_ = false;
    double threshold_db_ = -60.0;
    double attack_ms_ = 5.0;
    double release_ms_ = 100.0;

    float current_gain_ = 0.0f;
    // ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↓修正開始◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
    double attack_coeff_ = 0.0;
    double release_coeff_ = 0.0;
    // ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↑修正終わり◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️

    float processSample(float sample);
};