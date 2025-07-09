// ./advanced_dynamics.cpp
#include "advanced_dynamics.h"
#include <cmath>

// AnalogSaturationクラスのメソッド実装
float AnalogSaturation::tubeSaturation(float x) {
    if (drive_ == 0.0) return x;
    float k = 2.0 * drive_;
    return (x + k * x) / (1.0 + k * std::abs(x));
}

float AnalogSaturation::tapeSaturation(float x) {
    if (drive_ == 0.0) return x;
    return std::tanh(drive_ * x);
}

float AnalogSaturation::transformerSaturation(float x) {
    if (drive_ == 0.0) return x;
    const float a = 0.8f; 
    const float b = 1.5f;
    float x_driven = drive_ * x;
    return std::tanh(x_driven) + a * std::tanh(b * x_driven);
}

float AnalogSaturation::process(float input) {
    float dry_signal = input;
    input = dc_blocker_.process(input);

    float wet_signal;
    if (type_ == "tube") {
        wet_signal = tubeSaturation(input);
    } else if (type_ == "tape") {
        wet_signal = tapeSaturation(input);
    } else if (type_ == "transformer") {
        wet_signal = transformerSaturation(input);
    } else {
        wet_signal = input;
    }
    
    wet_signal = anti_alias_.process(wet_signal);

    return (1.0 - mix_) * dry_signal + mix_ * wet_signal;
}

// MultibandCompressor のスタブ実装 (将来的な拡張用)
void MultibandCompressor::setup(double sr, const std::vector<Band>& bands) {
    sample_rate_ = sr;
    bands_ = bands;
    // setupCrossoverNetwork(); // 必要に応じて実装
}

float MultibandCompressor::process(float input) {
    // マルチバンドコンプレッサーの処理ロジックをここに実装
    return input; 
}
