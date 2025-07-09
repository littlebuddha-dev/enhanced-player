// ./SndfileDecoder.h
// libsndfileを使用したデコーダー
#pragma once

#include "AudioDecoder.h"
#include <sndfile.h>
#include <memory>

class SndfileDecoder : public AudioDecoder {
public:
    SndfileDecoder();
    ~SndfileDecoder() override;

    // AudioDecoderインターフェースの実装
    bool open(const std::string& filePath) override;
    AudioInfo getInfo() const override;
    size_t read(float* buffer, size_t frames) override;
    bool seek(long long frame) override;

private:
    // libsndfileを扱うためのポインタ
    SNDFILE* sndfile_ = nullptr;
    // ファイル情報を保持する構造体
    SF_INFO sfinfo_;
};