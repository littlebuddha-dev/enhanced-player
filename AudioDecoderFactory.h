// ./AudioDecoderFactory.h
// オーディオデコーダーのインスタンスを生成するファクトリークラス
#pragma once

#include "AudioDecoder.h"
#include <string>
#include <memory> // std::unique_ptr を使用するため

class AudioDecoderFactory {
public:
    // ファイルパスを受け取り、適切なデコーダーのインスタンスを生成して返す
    static std::unique_ptr<AudioDecoder> createDecoder(const std::string& filePath);
};