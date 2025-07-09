// ./MPG123Decoder.h - Final Corrected and Verified Version
#pragma once
#include "AudioDecoder.h"
#include <mpg123.h>
#include <string>
#include <vector>

class MPG123Decoder : public AudioDecoder {
public:
    MPG123Decoder();
    ~MPG123Decoder() override;

    bool open(const std::string& filePath) override;
    AudioInfo getInfo() const override;
    size_t read(float* buffer, size_t frames) override;
    bool seek(long long frame) override;

private:
    mpg123_handle *mh_;
    AudioInfo info_;
};