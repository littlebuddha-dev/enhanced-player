// ./custom_effects.h
// 新規作成：ExciterとGlossEnhancerエフェクトのクラス定義
#pragma once

#include "AudioEffect.h"
#include "SimpleBiquad.h"
#include <vector>
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * @class Exciter
 * @brief 高周波数帯域に特化したハーモニックエキサイター
 *
 * クロスオーバーフィルタで高域成分を抽出し、それにサチュレーションを適用して
 * 明瞭度やディテールを復元します。
 */
class Exciter : public AudioEffect {
public:
    void setup(double sr, const json& params) override;
    void process(std::vector<float>& block, int channels) override;
    void reset() override;
    const std::string& getName() const override { return name_; }

private:
    // ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↓修正開始◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
    std::string name_ = "exciter";
    // ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↑修正終わり◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
    bool enabled_ = true;
    double sample_rate_ = 48000.0;
    double crossover_freq_ = 7800.0;
    double drive_ = 1.0;
    double mix_ = 0.2;

    // 2チャンネル分のフィルター
    SimpleBiquad highpass_filter_l_, highpass_filter_r_;
    SimpleBiquad lowpass_filter_l_, lowpass_filter_r_;

    float processSample(float sample, SimpleBiquad& hpf, SimpleBiquad& lpf);
    float saturate(float x);
};

/**
 * @class GlossEnhancer
 * @brief サウンドに艶と輝きを与えるプレミアム・グロス・エンハンサー
 *
 * 複数のフィルターとサチュレーションを組み合わせて、音楽的な倍音、
 * プレゼンス、エアー感を調整します。
 */
class GlossEnhancer : public AudioEffect {
public:
    void setup(double sr, const json& params) override;
    void process(std::vector<float>& block, int channels) override;
    void reset() override;
    const std::string& getName() const override { return name_; }

private:
    // ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↓修正開始◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
    std::string name_ = "gloss_enhancer";
    // ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↑修正終わり◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
    bool enabled_ = true;
    double sample_rate_ = 48000.0;
    double harmonic_drive_ = 0.35;
    double even_harmonics_ = 0.28;
    double odd_harmonics_ = 0.18;
    double total_mix_ = 0.22;

    // 2チャンネル分のフィルターと状態
    SimpleBiquad dc_blocker_l_, dc_blocker_r_;
    SimpleBiquad presence_filter_l_, presence_filter_r_;
    SimpleBiquad air_filter_l_, air_filter_r_;

    float processSample(float sample, SimpleBiquad& dc, SimpleBiquad& pres, SimpleBiquad& air);
};