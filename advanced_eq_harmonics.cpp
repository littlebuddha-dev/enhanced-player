// ./advanced_eq_harmonics.cpp
// Full and Corrected Implementation of Audio Effects with Memory-Safe FFT and Stable Overlap-Save
#include "advanced_eq_harmonics.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <numeric>

// --- HarmonicEnhancerクラスのメソッド実装 ---

void HarmonicEnhancer::setup(double sr, const json& params) {
    sample_rate_ = sr;
    if (params.is_object() && !params.empty()) {
        enabled_ = params.value("enabled", true);
        drive_ = params.value("drive", 0.3);
        even_harmonics_ = params.value("even_harmonics", 0.2);
        odd_harmonics_ = params.value("odd_harmonics", 0.3);
        mix_ = params.value("mix", 0.25);
    }
    dc_blocker_.set_hpf(sr, 15.0, 0.707);
    lowpass_.set_lpf(sr, sr / 2.2, 0.707);
}

void HarmonicEnhancer::reset() {
    dc_blocker_.reset();
    lowpass_.reset();
}

float HarmonicEnhancer::generateHarmonics(float input) {
    float processed = 0.0f;
    float abs_input = std::fabs(input);
    if (even_harmonics_ > 0) processed += (input * input - abs_input) * even_harmonics_;
    if (odd_harmonics_ > 0) processed += (std::tanh(input * 1.5f) - input) * odd_harmonics_;
    return input + processed * drive_;
}

float HarmonicEnhancer::processSample(float input) {
    if (!enabled_) return input;
    float dry_signal = input;
    input = dc_blocker_.process(input);
    float wet_signal = generateHarmonics(input);
    wet_signal = lowpass_.process(wet_signal);
    return (1.0f - mix_) * dry_signal + mix_ * wet_signal;
}

void HarmonicEnhancer::process(std::vector<float>& block, int channels) {
    if (!enabled_) return;
    for (size_t i = 0; i < block.size(); ++i) {
        block[i] = processSample(block[i]);
    }
}

// --- LinearPhaseEQの実装 ---

LinearPhaseEQ::LinearPhaseEQ()
    : fft_plan_fwd_L_(nullptr), fft_plan_fwd_R_(nullptr),
      fft_plan_bwd_L_(nullptr), fft_plan_bwd_R_(nullptr) {}

LinearPhaseEQ::~LinearPhaseEQ() {
    if (fft_plan_fwd_L_) fftwf_destroy_plan(fft_plan_fwd_L_);
    if (fft_plan_fwd_R_) fftwf_destroy_plan(fft_plan_fwd_R_);
    if (fft_plan_bwd_L_) fftwf_destroy_plan(fft_plan_bwd_L_);
    if (fft_plan_bwd_R_) fftwf_destroy_plan(fft_plan_bwd_R_);
}

void LinearPhaseEQ::setup(double sr, const json& params) {
    sample_rate_ = sr;
    if (params.is_object()) {
        enabled_ = params.value("enabled", true);
        fft_size_ = params.value("fft_size", 2048);
        hop_size_ = params.value("hop_size", fft_size_ / 4);
    }
    if (!enabled_) return;

    if (fft_size_ == 0 || hop_size_ == 0 || fft_size_ < hop_size_) {
        throw std::runtime_error("Invalid FFT/hop size for LinearPhaseEQ.");
    }

    // バッファを確保
    time_domain_buffer_L_.assign(fft_size_, 0.0f);
    time_domain_buffer_R_.assign(fft_size_, 0.0f);
    freq_domain_buffer_L_.assign(fft_size_ / 2 + 1, {0.0f, 0.0f});
    freq_domain_buffer_R_.assign(fft_size_ / 2 + 1, {0.0f, 0.0f});
    input_buffer_L_.assign(fft_size_, 0.0f);
    input_buffer_R_.assign(fft_size_, 0.0f);
    eq_curve_.assign(fft_size_ / 2 + 1, {1.0f, 0.0f});
    
    // FFTW plans
    fft_plan_fwd_L_ = fftwf_plan_dft_r2c_1d(fft_size_, time_domain_buffer_L_.data(), reinterpret_cast<fftwf_complex*>(freq_domain_buffer_L_.data()), FFTW_ESTIMATE);
    fft_plan_bwd_L_ = fftwf_plan_dft_c2r_1d(fft_size_, reinterpret_cast<fftwf_complex*>(freq_domain_buffer_L_.data()), time_domain_buffer_L_.data(), FFTW_ESTIMATE);
    fft_plan_fwd_R_ = fftwf_plan_dft_r2c_1d(fft_size_, time_domain_buffer_R_.data(), reinterpret_cast<fftwf_complex*>(freq_domain_buffer_R_.data()), FFTW_ESTIMATE);
    fft_plan_bwd_R_ = fftwf_plan_dft_c2r_1d(fft_size_, reinterpret_cast<fftwf_complex*>(freq_domain_buffer_R_.data()), time_domain_buffer_R_.data(), FFTW_ESTIMATE);

    if (params.contains("bands")) {
        setupEQCurve(params["bands"]);
    }
    reset();
}

void LinearPhaseEQ::reset() {
    channels_ = 0;
    std::fill(input_buffer_L_.begin(), input_buffer_L_.end(), 0.0f);
    std::fill(input_buffer_R_.begin(), input_buffer_R_.end(), 0.0f);
}

// ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↓修正開始◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
void LinearPhaseEQ::process(std::vector<float>& block, int channels) {
    if (!enabled_ || block.empty()) return;

    if (channels_ != channels) {
        channels_ = channels;
        reset();
    }

    size_t num_frames_in = block.size() / channels;
    size_t frames_processed = 0;

    while (frames_processed < num_frames_in) {
        size_t frames_to_process_now = std::min(num_frames_in - frames_processed, hop_size_);

        // 1. 入力バッファをシフトし、新しいデータを追加
        std::move(input_buffer_L_.begin() + frames_to_process_now, input_buffer_L_.end(), input_buffer_L_.begin());
        if (channels > 1) {
            std::move(input_buffer_R_.begin() + frames_to_process_now, input_buffer_R_.end(), input_buffer_R_.begin());
        }

        size_t copy_start_index = fft_size_ - frames_to_process_now;
        for (size_t i = 0; i < frames_to_process_now; ++i) {
            input_buffer_L_[copy_start_index + i] = block[(frames_processed + i) * channels];
            if (channels > 1) {
                input_buffer_R_[copy_start_index + i] = block[(frames_processed + i) * channels + 1];
            }
        }
        
        // 2. 入力データを時間領域バッファにコピー（窓関数は適用しない）
        time_domain_buffer_L_ = input_buffer_L_;
        if (channels > 1) {
            time_domain_buffer_R_ = input_buffer_R_;
        }

        // 3. FFT -> EQ適用 -> IFFT
        fftwf_execute(fft_plan_fwd_L_);
        for(size_t i = 0; i < eq_curve_.size(); ++i) freq_domain_buffer_L_[i] *= eq_curve_[i];
        fftwf_execute(fft_plan_bwd_L_);

        if(channels > 1) {
            fftwf_execute(fft_plan_fwd_R_);
            for(size_t i = 0; i < eq_curve_.size(); ++i) freq_domain_buffer_R_[i] *= eq_curve_[i];
            fftwf_execute(fft_plan_bwd_R_);
        }

        // 4. 結果の有効な部分を出力ブロックに書き戻す (Overlap-Save)
        float norm_factor = 1.0f / fft_size_; // FFTWのIFFTは正規化されないため、手動で正規化
        size_t result_start_index = fft_size_ - hop_size_;
        for (size_t i = 0; i < frames_to_process_now; ++i) {
            block[(frames_processed + i) * channels] = time_domain_buffer_L_[result_start_index + i] * norm_factor;
            if (channels > 1) {
                block[(frames_processed + i) * channels + 1] = time_domain_buffer_R_[result_start_index + i] * norm_factor;
            }
        }

        frames_processed += frames_to_process_now;
    }
}
// ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↑修正終わり◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️


void LinearPhaseEQ::setupEQCurve(const json& bands) {
    std::fill(eq_curve_.begin(), eq_curve_.end(), std::complex<float>(1.0f, 0.0f));
    if (!bands.is_array()) return;

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
    if (q <= 0) return;
    float gain_linear = std::pow(10.0f, gain_db / 20.0f);
    float nyquist = sample_rate_ / 2.0;

    for (size_t i = 0; i < eq_curve_.size(); ++i) {
        double current_freq = static_cast<double>(i) * nyquist / (eq_curve_.size() - 1);
        float gain = 1.0f;

        if (type == "peaking") {
            double w = (current_freq - freq) / (freq / q);
            gain = 1.0f + (gain_linear - 1.0f) * std::exp(-0.5 * w * w);
        } else if (type == "lowshelf") {
             gain = (current_freq <= freq) ? gain_linear : 1.0f; // Simplified
        } else if (type == "highshelf") {
             gain = (current_freq >= freq) ? gain_linear : 1.0f; // Simplified
        }

        eq_curve_[i] *= gain;
    }
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

    double attack_samples = sr * (attack_ms_ / 1000.0);
    double release_samples = sr * (release_ms_ / 1000.0);
    attack_coeff_ = (attack_samples > 0) ? std::exp(-1.0 / attack_samples) : 0.0;
    release_coeff_ = (release_samples > 0) ? std::exp(-1.0 / release_samples) : 0.0;
    reset();
}

void SpectralGate::reset() {
    current_gain_ = 0.0f;
}

float SpectralGate::processSample(float input) {
    if (!enabled_) return input;

    float input_level_db = 20.0f * std::log10(std::abs(input) + 1e-12f);
    float target_gain = (input_level_db > threshold_db_) ? 1.0f : 0.0f;

    if (target_gain > current_gain_) {
        current_gain_ = attack_coeff_ * current_gain_ + (1.0f - attack_coeff_) * target_gain;
    } else {
        current_gain_ = release_coeff_ * current_gain_ + (1.0f - release_coeff_) * target_gain;
    }

    current_gain_ = std::max(0.0f, std::min(1.0f, current_gain_));
    return input * current_gain_;
}

void SpectralGate::process(std::vector<float>& block, int channels) {
    if (!enabled_) return;
    for (size_t i = 0; i < block.size(); ++i) {
        block[i] = processSample(block[i]);
    }
}