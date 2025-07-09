// ./AudioDecoder.h
// オーディオデコーダーのインターフェース（抽象基底クラス）
#pragma once

#include <string>
#include <cstddef> // for size_t

// --- オーディオファイル情報 ---
struct AudioInfo {
    int channels = 0;
    int sampleRate = 0;
    long long totalFrames = 0;
};

// --- デコーダーのインターフェース ---
class AudioDecoder {
public:
    // デストラクタは仮想（virtual）にするのがルール
    virtual ~AudioDecoder() = default;

    // 純粋仮想関数（実装は派生クラスで行う）
    virtual bool open(const std::string& filePath) = 0;
    virtual AudioInfo getInfo() const = 0;
    virtual size_t read(float* buffer, size_t frames) = 0;
    virtual bool seek(long long frame) = 0;
};