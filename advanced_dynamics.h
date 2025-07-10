// ./advanced_dynamics.h
// アナログ風ダイナミクス処理エフェクトのクラス定義
#pragma once
#include "SimpleBiquad.h"
#include "AudioEffect.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>
#include <deque>
#include <nlohmann/json.hpp>

// jsonエイリアスは基底クラスヘッダで定義済み

// マルチバンドコンプレッサー（スタブ）
class MultibandCompressor : public AudioEffect {
public:
    struct Band {
        double freq_low, freq_high;
        double threshold_db, ratio, attack_ms, release_ms;
        double makeup_gain_db;
        bool enabled;
        double envelope_l = 0.0; // Left channel envelope
        double envelope_r = 0.0; // Right channel envelope
        double attack_coeff = 0.0;
        double release_coeff = 0.0;
        SimpleBiquad lpf_l, hpf_l, bpf_l; // Filters for Left Channel
        SimpleBiquad lpf_r, hpf_r, bpf_r; // Filters for Right Channel
    };

    void setup(double sr, const json& params) override;
    void process(std::vector<float>& block, int channels) override;
    void reset() override;
    const std::string& getName() const override { return name_; }

private:
    std::string name_ = "multiband_compressor"; // Changed name for consistency
    // ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↓修正開始◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
    bool enabled_ = false; // デフォルトは無効
    // ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↑修正終わり◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
    double sample_rate_ = 44100.0;
    std::vector<Band> bands_;
    // Crossover filters can be handled within the setup method of each band or
    // within a centralized crossover network. For simplicity, I'll manage it
    // within the process function directly or set up band-specific filters.
    // std::vector<SimpleBiquad> crossover_filters_; // This can be removed or re-purposed

    // Helper functions
    float calculateGain(float envelope, float threshold_db, float ratio);
    void setupCrossoverFilters(); // Renamed and re-purposed from setupCrossoverNetwork
};

// アナログ風サチュレーション
class AnalogSaturation : public AudioEffect {
public:
    void setup(double sr, const json& params) override;
    void process(std::vector<float>& block, int channels) override;
    void reset() override;
    const std::string& getName() const override { return name_; }

private:
    std::string name_ = "analog_saturation";
    bool enabled_ = true;
    double sample_rate_ = 44100.0;
    double drive_ = 1.0;
    double mix_ = 0.3;
    std::string type_ = "tube";
    SimpleBiquad dc_blocker_, anti_alias_;

    float tubeSaturation(float x);
    float tapeSaturation(float x);
    float transformerSaturation(float x);
    float processSample(float sample);
};

// マスタリング・リミッター
class MasteringLimiter : public AudioEffect {
public:
    void setup(double sr, const json& params) override;
    void process(std::vector<float>& block, int channels) override;
    void reset() override;
    const std::string& getName() const override { return name_; }

private:
    std::string name_ = "mastering_limiter";
    bool enabled_ = true;
    double sample_rate_ = 48000.0;
    double threshold_db_ = -0.1;
    double attack_ms_ = 1.5;
    double release_ms_ = 50.0;
    double lookahead_ms_ = 5.0;

    double threshold_linear_ = 1.0;
    double attack_coeff_ = 0.0;
    double release_coeff_ = 0.0;
    int lookahead_samples_ = 0;

    // 2チャンネル対応
    std::deque<float> lookahead_buffer_l_;
    std::deque<float> lookahead_buffer_r_;
    float envelope_ = 0.0f;
    SimpleBiquad shelf_filter_l_, shelf_filter_r_;
};