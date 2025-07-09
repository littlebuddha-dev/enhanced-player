// ./advanced_eq_harmonics.cpp
#include "advanced_eq_harmonics.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include <fftw3.h>

// jsonエイリアス
using json = nlohmann::json;

// --- HarmonicEnhancerクラスのメソッド実装 ---

// setup: JSONからパラメータを読み込む
void HarmonicEnhancer::setup(double sr, const json& params) {
    sample_rate_ = sr;
    if (params.is_object() && !params.empty()) {
        enabled_ = params.value("enabled", true);
        drive_ = params.value("drive", 0.3);
        even_harmonics_ = params.value("even_harmonics", 0.2);
        odd_harmonics_ = params.value("odd_harmonics", 0.3);
        mix_ = params.value("mix", 0.25);
    }
    
    // DC成分を除去するためのハイパスフィルタ
    dc_blocker_.set_hpf(sr, 15.0, 0.707);
    // 倍音生成によって発生するエイリアシングを抑制するためのローパスフィルタ
    lowpass_.set_lpf(sr, sr / 2.2, 0.707);
}

// generateHarmonics: 倍音を生成するコアロジック
float HarmonicEnhancer::generateHarmonics(float input) {
    float processed = 0.0f;
    float abs_input = std::fabs(input);

    // 偶数次倍音: 絶対値や二乗関数で生成 (x*x - |x| でクリッピングを防ぎつつ偶数次倍音を強調)
    if (even_harmonics_ > 0) {
        processed += (input * input - abs_input) * even_harmonics_;
    }

    // 奇数次倍音: tanh関数や三次関数で生成 (tanh(x) - x で元の信号との差分から奇数次倍音を抽出)
    if (odd_harmonics_ > 0) {
        processed += (std::tanh(input * 1.5f) - input) * odd_harmonics_;
    }
    
    // 生成した倍音にドライブを適用し、元の信号に加える
    return input + processed * drive_;
}

// process: メインの処理関数
float HarmonicEnhancer::process(float input) {
    if (!enabled_) return input;

    float dry_signal = input;
    
    input = dc_blocker_.process(input);
    float wet_signal = generateHarmonics(input);
    wet_signal = lowpass_.process(wet_signal);
    
    // Dry/Wetミックス
    return (1.0f - mix_) * dry_signal + mix_ * wet_signal;
}

// --- LinearPhaseEQの実装 ---
void LinearPhaseEQ::setup(double sr, const json& params) {
    sample_rate_ = sr;
    if (params.is_object()) {
        enabled_ = params.value("enabled", true);
        fft_size_ = params.value("fft_size", 2048);
        overlap_ = params.value("overlap", 0.75); // 75%
    }
    if (!enabled_) return;

    hop_size_ = static_cast<size_t>(fft_size_ * (1.0 - overlap_));
    if (hop_size_ == 0) {
        throw std::runtime_error("Hop size for LinearPhaseEQ cannot be zero.");
    }

    input_buffer_.assign(fft_size_, 0.0f);
    output_buffer_.assign(fft_size_, 0.0f);
    overlap_buffer_.assign(fft_size_, 0.0f);
    window_.assign(fft_size_, 0.0f);
    eq_response_.assign(fft_size_ / 2 + 1, {1.0f, 0.0f});

    // Hann window
    for (size_t i = 0; i < fft_size_; ++i) {
        window_[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (fft_size_ - 1)));
    }

    if (params.contains("bands")) {
        setupEQCurve(params["bands"]);
    }
    reset();
}

void LinearPhaseEQ::setupEQCurve(const json& bands) {
    std::fill(eq_response_.begin(), eq_response_.end(), std::complex<float>(1.0f, 0.0f));
    for (const auto& band_params : bands) {
        applyEQBand(
            band_params.value("freq", 1000.0),
            band_params.value("gain_db", 0.0),
            band_params.value("q", 1.0),
            band_params.value("type", "peaking")
        );
    }
}

void LinearPhaseEQ::applyEQBand(double freq, double gain_db, double q, const std::string& type) {
    float A = std::pow(10.0f, gain_db / 20.0f);
    double w0 = 2.0 * M_PI * freq / sample_rate_;

    for (size_t i = 0; i < eq_response_.size(); ++i) {
        double w = 2.0 * M_PI * i * sample_rate_ / fft_size_ / sample_rate_;
        float gain = 1.0f;

        if (type == "peaking") {
            double alpha = std::sin(w0) / (2.0 * q);
            double num = (A * A - 1) * std::cos(w) * alpha;
            double den = 1 + alpha * alpha * (std::cos(w) * std::cos(w));
            gain = std::sqrt(1 + num / den);
        }
        // 他のフィルタタイプ（low/high shelfなど）もここに追加可能
        eq_response_[i] *= gain;
    }
}

void LinearPhaseEQ::processBlock() {
    std::vector<float> processed_chunk(fft_size_);
    std::vector<std::complex<float>> fft_data(fft_size_ / 2 + 1);

    // 1. Windowing
    for (size_t i = 0; i < fft_size_; ++i) {
        processed_chunk[i] = input_buffer_[i] * window_[i];
    }

    // 2. FFT
    fftwf_plan p_fwd = fftwf_plan_dft_r2c_1d(fft_size_, processed_chunk.data(), reinterpret_cast<fftwf_complex*>(fft_data.data()), FFTW_ESTIMATE);
    fftwf_execute(p_fwd);
    fftwf_destroy_plan(p_fwd);

    // 3. Apply EQ
    for (size_t i = 0; i < fft_data.size(); ++i) {
        fft_data[i] *= eq_response_[i];
    }

    // 4. IFFT
    fftwf_plan p_bwd = fftwf_plan_dft_c2r_1d(fft_size_, reinterpret_cast<fftwf_complex*>(fft_data.data()), processed_chunk.data(), FFTW_ESTIMATE);
    fftwf_execute(p_bwd);
    fftwf_destroy_plan(p_bwd);

    // 5. Overlap-Add
    for (size_t i = 0; i < fft_size_; ++i) {
        output_buffer_[i] = (processed_chunk[i] / fft_size_) + overlap_buffer_[i];
    }
    
    // 6. Save overlap for next frame
    std::copy(output_buffer_.begin() + hop_size_, output_buffer_.end(), overlap_buffer_.begin());
    std::fill(overlap_buffer_.begin() + (fft_size_ - hop_size_), overlap_buffer_.end(), 0.0f);
}

std::vector<float> LinearPhaseEQ::process(const std::vector<float>& input) {
    if (!enabled_) {
        return input;
    }

    // 注意：この実装は、エフェクトチェーンの他の部分とは異なり、
    // ブロック単位での処理を想定しています。
    // リアルタイムのサンプル毎のループに統合するには、
    // 呼び出し側のアーキテクチャの変更が必要です。
    std::vector<float> result;
    result.reserve(input.size());
    size_t processed_count = 0;

    while (processed_count < input.size()) {
        size_t remaining_in_buffer = fft_size_ - buffer_pos_;
        size_t to_copy = std::min(remaining_in_buffer, input.size() - processed_count);

        std::copy(input.begin() + processed_count, input.begin() + processed_count + to_copy, input_buffer_.begin() + buffer_pos_);
        buffer_pos_ += to_copy;
        processed_count += to_copy;

        if (buffer_pos_ == fft_size_) {
            processBlock();
            result.insert(result.end(), output_buffer_.begin(), output_buffer_.begin() + hop_size_);
            
            std::move(input_buffer_.begin() + hop_size_, input_buffer_.end(), input_buffer_.begin());
            buffer_pos_ -= hop_size_;
        }
    }
    return result;
}


// --- SpectralGateの実装 ---
void SpectralGate::setup(double sr, const json& params) {
    sample_rate_ = sr;
    if (params.is_object()) {
        enabled_ = params.value("enabled", true);
        threshold_db_ = params.value("threshold_db", -60.0);
        attack_ms_ = params.value("attack_ms", 5.0);
        release_ms_ = params.value("release_ms", 100.0);
    }
    if (!enabled_) return;

    // Biquadをスムージング用のローパスフィルタとして設定
    // カットオフ周波数をアタック/リリースタイムに応じて設定
    double attack_freq = 1.0 / (2.0 * M_PI * attack_ms_ / 1000.0);
    double release_freq = 1.0 / (2.0 * M_PI * release_ms_ / 1000.0);
    attack_smoother_.set_lpf(sr, attack_freq, 0.707);
    release_smoother_.set_lpf(sr, release_freq, 0.707);

    reset();
}

float SpectralGate::process(float input) {
    if (!enabled_) return input;

    // 入力信号のレベルをdBに変換
    float input_level_db = 20.0f * std::log10(std::abs(input) + 1e-9f);

    float target_gain;
    if (input_level_db > threshold_db_) {
        // スレッショルドを超えたらゲートを開く (ゲイン=1)
        target_gain = 1.0f;
    } else {
        // 下回ったらゲートを閉じる (ゲイン=0)
        target_gain = 0.0f;
    }

    // アタックとリリースで異なるスムーザーを使用し、ゲインの変化を滑らかにする
    // 注意：この設計は、フィルタの状態が切り替え時にジャンプする可能性があるため、
    // 理想的ではないかもしれませんが、ヘッダーファイルの定義に従っています。
    if (target_gain > current_gain_) {
        // アタック（ゲインが上昇）
        current_gain_ = attack_smoother_.process(target_gain);
    } else {
        // リリース（ゲインが下降）
        current_gain_ = release_smoother_.process(target_gain);
    }
    
    // わずかなクリックノイズを防ぐため、ゲインをクリップ
    current_gain_ = std::max(0.0f, std::min(1.0f, current_gain_));

    return input * current_gain_;
}
