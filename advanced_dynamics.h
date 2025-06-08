// ./advanced_dynamics.h
#pragma once
#include "SimpleBiquad.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>

// jsonエイリアスを追加
using json = nlohmann::json;

// マルチバンドコンプレッサー
class MultibandCompressor {
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
    
    void setup(double sr, const std::vector<Band>& bands);
    float process(float input);

    void reset() {
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

private:
    double sample_rate_ = 44100.0;
    std::vector<Band> bands_;
    std::vector<SimpleBiquad> crossover_filters_;
    
    void setupCrossoverNetwork();
    std::vector<float> splitToBands(float input);
    float compressBand(float input, Band& band);
    float sumBands(const std::vector<float>& bands);
};

// アナログ風サチュレーション
class AnalogSaturation {
public:
    void setup(double sr, const json& params);
    float process(float input);

    void reset() {
        dc_blocker_.reset();
        anti_alias_.reset();
    }
private:
    double sample_rate_ = 44100.0;
    double drive_ = 1.0;
    double mix_ = 0.3;
    std::string type_ = "tube";
    SimpleBiquad dc_blocker_, anti_alias_;
    
    float tubeSaturation(float x);
    float tapeSaturation(float x);
    float transformerSaturation(float x);
};