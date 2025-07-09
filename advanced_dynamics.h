// ./advanced_dynamics.h
// アナログ風ダイナミクス処理エフェクトのクラス定義
#pragma once
#include "SimpleBiquad.h"
#include "AudioEffect.h" // AudioEffect基底クラスをインクルード
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>
#include <nlohmann/json.hpp>

// jsonエイリアスは基底クラスヘッダで定義済み

// マルチバンドコンプレッサー
class MultibandCompressor : public AudioEffect {
public:
    struct Band {
        double freq_low, freq_high;
        double threshold_db, ratio, attack_ms, release_ms;
        double makeup_gain_db;
        bool enabled;
        double envelope = 0.0;
        double attack_coeff = 0.0;
        double release_coeff = 0.0;
        SimpleBiquad lpf, hpf, bpf;
    };
    
    void setup(double sr, const json& params) override;
    void process(std::vector<float>& block, int channels) override;
    void reset() override;
    const std::string& getName() const override { return name_; }

private:
    std::string name_ = "MultibandCompressor";
    double sample_rate_ = 44100.0;
    std::vector<Band> bands_;
    std::vector<SimpleBiquad> crossover_filters_;
    
    void setupCrossoverNetwork();
    std::vector<float> splitToBands(float input);
    float compressBand(float input, Band& band);
    float sumBands(const std::vector<float>& bands);
};

// アナログ風サチュレーション
class AnalogSaturation : public AudioEffect {
public:
    void setup(double sr, const json& params) override;
    void process(std::vector<float>& block, int channels) override;
    void reset() override;
    const std::string& getName() const override { return name_; }

private:
    std::string name_ = "AnalogSaturation";
    double sample_rate_ = 44100.0;
    double drive_ = 1.0;
    double mix_ = 0.3;
    std::string type_ = "tube";
    SimpleBiquad dc_blocker_, anti_alias_;
    
    float tubeSaturation(float x);
    float tapeSaturation(float x);
    float transformerSaturation(float x);
    // サンプル単位の処理を行う内部ヘルパー関数
    float processSample(float sample);
};