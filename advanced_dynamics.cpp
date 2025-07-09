// ./advanced_dynamics.cpp
#include "advanced_dynamics.h"
#include <cmath>
#include <nlohmann/json.hpp>

// jsonエイリアス
using json = nlohmann::json;

// --- AnalogSaturationクラスのメソッド実装 ---

// setup: JSONからパラメータを読み込む
void AnalogSaturation::setup(double sr, const json& params) {
    sample_rate_ = sr;
    if (params.is_object() && !params.empty()) {
        drive_ = params.value("drive", 1.0);
        mix_ = params.value("mix", 0.3);
        type_ = params.value("type", "tube");
    }
    // DCオフセット除去とアンチエイリアシングのためのフィルタ設定
    dc_blocker_.set_hpf(sr, 15.0, 0.707);
    anti_alias_.set_lpf(sr, sr / 2.1, 0.707); // ナイキスト周波数の少し下
}

// tubeSaturation: 真空管サチュレーションのシミュレーション
float AnalogSaturation::tubeSaturation(float x) {
    if (drive_ == 0.0) return x;
    // 非線形関数による倍音付加
    float k = 2.0f * drive_;
    float abs_x = std::abs(x);
    // サインカーブに近いソフトクリッピング
    return (x > 0 ? 1.0f : -1.0f) * (abs_x - (abs_x * abs_x / (1.0f + k * abs_x)));
}

// tapeSaturation: テープサチュレーションのシミュレーション
float AnalogSaturation::tapeSaturation(float x) {
    if (drive_ == 0.0) return x;
    // tanh関数によるソフトクリッピング
    return std::tanh(drive_ * x);
}

// transformerSaturation: トランスサチュレーションのシミュレーション
float AnalogSaturation::transformerSaturation(float x) {
    if (drive_ == 0.0) return x;
    const float a = 0.8f; 
    const float b = 1.5f;
    float x_driven = drive_ * x;
    // 複数のtanhの組み合わせで複雑な倍音を生成
    return std::tanh(x_driven) + a * std::tanh(b * x_driven);
}

// process: メインの処理関数
float AnalogSaturation::process(float input) {
    float dry_signal = input;
    // DCオフセットを除去
    input = dc_blocker_.process(input);

    float wet_signal;
    if (type_ == "tube") {
        wet_signal = tubeSaturation(input);
    } else if (type_ == "tape") {
        wet_signal = tapeSaturation(input);
    } else if (type_ == "transformer") {
        wet_signal = transformerSaturation(input);
    } else {
        wet_signal = input; // 未知のタイプなら何もしない
    }
    
    // エイリアシングノイズを抑制
    wet_signal = anti_alias_.process(wet_signal);

    // Dry/Wetミックス
    return (1.0f - mix_) * dry_signal + mix_ * wet_signal;
}


// --- MultibandCompressor のスタブ実装 (将来の拡張用) ---
void MultibandCompressor::setup(double sr, const std::vector<Band>& bands) {
    sample_rate_ = sr;
    bands_ = bands;
    // setupCrossoverNetwork(); // 必要に応じて実装
}

float MultibandCompressor::process(float input) {
    // マルチバンドコンプレッサーの処理ロジックをここに実装
    return input; 
}