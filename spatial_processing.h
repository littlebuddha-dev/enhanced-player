// ./spatial_processing.h
#pragma once
#include "SimpleBiquad.h"
#include "AudioEffect.h"
#include <vector>
#include <cmath>
#include <array>

// ステレオ幅調整とイメージング
class StereoEnhancer : public AudioEffect {
public:
    void setup(double sr, const json& params) override {
        if(params.is_object() && !params.empty()) {
            width_ = params.value("width", 1.2);
            bass_mono_freq_ = params.value("bass_mono_freq", 120.0);
            enabled_ = params.value("enabled", true);
        }
        bass_lpf_l_.set_lpf(sr, bass_mono_freq_, 0.707);
        bass_lpf_r_.set_lpf(sr, bass_mono_freq_, 0.707);
        bass_hpf_l_.set_hpf(sr, bass_mono_freq_, 0.707);
        bass_hpf_r_.set_hpf(sr, bass_mono_freq_, 0.707);
    }
    
    void process(std::vector<float>& block, int channels) override {
        if (!enabled_ || channels != 2) return;

        size_t frame_count = block.size() / 2;
        for(size_t i = 0; i < frame_count; ++i) {
            float left = block[i*2];
            float right = block[i*2 + 1];
            
            auto processed = processSample(left, right);

            block[i*2] = processed.first;
            block[i*2 + 1] = processed.second;
        }
    }

    void reset() override {
        bass_lpf_l_.reset();
        bass_lpf_r_.reset();
        bass_hpf_l_.reset();
        bass_hpf_r_.reset();
    }
    
    const std::string& getName() const override { return name_; }

private:
    std::string name_ = "StereoEnhancer";
    double width_ = 1.2, bass_mono_freq_ = 120.0;
    bool enabled_ = true;
    SimpleBiquad bass_lpf_l_, bass_lpf_r_, bass_hpf_l_, bass_hpf_r_;

    std::pair<float, float> processSample(float left, float right) {
        float mid = (left + right) * 0.5f;
        float side = (left - right) * 0.5f;
        float bass_l = bass_lpf_l_.process(left);
        float bass_r = bass_lpf_r_.process(right);
        float bass_mono = (bass_l + bass_r) * 0.5f;
        float high_l = bass_hpf_l_.process(left);
        float high_r = bass_hpf_r_.process(right);
        float high_mid = (high_l + high_r) * 0.5f;
        float high_side = (high_l - high_r) * 0.5f * width_;
        float processed_l = bass_mono + high_mid + high_side;
        float processed_r = bass_mono + high_mid - high_side;
        return {processed_l, processed_r};
    }
};