// ./SimpleBiquad.h
#pragma once

#include <string>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <nlohmann/json.hpp>

// jsonエイリアスを追加
using json = nlohmann::json;

// --- Utilities ---
inline double db_to_linear(double db) {
    if (!std::isfinite(db)) return 1.0;
    return std::pow(10.0, db / 20.0);
}

// --- Simple Biquad Filter ---
class SimpleBiquad {
public:
    SimpleBiquad(std::string name = "Unnamed") : filter_name_(name) { reset(); }
    void reset() { a1 = 0.0; a2 = 0.0; b0 = 1.0; b1 = 0.0; b2 = 0.0; z1 = 0.0; z2 = 0.0; is_bypassed_ = false; }
    
    void set_lpf(double sr, double freq, double q) {
        reset();
        q = std::max(0.1, q); freq = std::max(10.0, std::min(freq, sr / 2.2));
        double w0 = 2.0 * M_PI * freq / sr, cos_w0 = std::cos(w0), sin_w0 = std::sin(w0);
        double alpha = sin_w0 / (2.0 * q), a0 = 1.0 + alpha;
        b0 = (1.0 - cos_w0) / 2.0 / a0; b1 = (1.0 - cos_w0) / a0; b2 = b0;
        a1 = -2.0 * cos_w0 / a0; a2 = (1.0 - alpha) / a0;
    }
    void set_hpf(double sr, double freq, double q) {
        reset();
        q = std::max(0.1, q); freq = std::max(10.0, std::min(freq, sr / 2.2));
        double w0 = 2.0 * M_PI * freq / sr, cos_w0 = std::cos(w0), sin_w0 = std::sin(w0);
        double alpha = sin_w0 / (2.0 * q), a0 = 1.0 + alpha;
        b0 = (1.0 + cos_w0) / 2.0 / a0; b1 = -(1.0 + cos_w0) / a0; b2 = b0;
        a1 = -2.0 * cos_w0 / a0; a2 = (1.0 - alpha) / a0;
    }
    void set_peaking(double sr, double freq, double q, double gain_db) {
        reset();
        q = std::max(0.1, q); freq = std::max(10.0, std::min(freq, sr / 2.2));
        double A = db_to_linear(gain_db / 2.0);
        double w0 = 2.0 * M_PI * freq / sr, cos_w0 = std::cos(w0), sin_w0 = std::sin(w0);
        double alpha = sin_w0 / (2.0 * q), a0 = 1.0 + alpha / A;
        b0 = (1.0 + alpha * A) / a0; b1 = -2.0 * cos_w0 / a0; b2 = (1.0 - alpha * A) / a0;
        a1 = b1; a2 = (1.0 - alpha / A) / a0;
    }
    
    float process(float in) {
        if (is_bypassed_ || std::isnan(in) || std::isinf(in)) return in;
        if (std::isnan(z1) || std::isinf(z1) || std::isnan(z2) || std::isinf(z2)) reset();
        double out = b0 * in + z1;
        z1 = b1 * in - a1 * out + z2;
        z2 = b2 * in - a2 * out;
        return static_cast<float>(out);
    }
private:
    std::string filter_name_; bool is_bypassed_ = false;
    double a1, a2, b0, b1, b2, z1, z2;
};