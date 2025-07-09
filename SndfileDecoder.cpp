// ./SndfileDecoder.cpp
// libsndfileを使用したデコーダーの実装

#include "SndfileDecoder.h"
#include <iostream>

// --- コンストラクタ ---
// メンバ変数を初期化
SndfileDecoder::SndfileDecoder() : sndfile_(nullptr) {
    // sfinfo_構造体をゼロで初期化
    sfinfo_.frames = 0;
    sfinfo_.samplerate = 0;
    sfinfo_.channels = 0;
    sfinfo_.format = 0;
    sfinfo_.sections = 0;
    sfinfo_.seekable = 0;
}

// --- デストラクタ ---
// ファイルが開かれていれば閉じる
SndfileDecoder::~SndfileDecoder() {
    if (sndfile_) {
        sf_close(sndfile_);
    }
}

// --- open ---
// 音声ファイルを開く
bool SndfileDecoder::open(const std::string& filePath) {
    if (sndfile_) {
        sf_close(sndfile_);
    }
    // sf_open関数でファイルを開き、結果をメンバ変数に格納
    sndfile_ = sf_open(filePath.c_str(), SFM_READ, &sfinfo_);
    if (!sndfile_) {
        std::cerr << "SndfileDecoder Error: Could not open file " << filePath 
                  << " with libsndfile. Reason: " << sf_strerror(nullptr) << std::endl;
        return false;
    }
    return true;
}

// --- getInfo ---
// 音声ファイルの情報を取得して返す
AudioInfo SndfileDecoder::getInfo() const {
    AudioInfo info;
    if (sndfile_) {
        info.channels = sfinfo_.channels;
        info.sampleRate = sfinfo_.samplerate;
        info.totalFrames = sfinfo_.frames;
    }
    return info;
}

// --- read ---
// 指定されたフレーム数の音声データを読み込む
size_t SndfileDecoder::read(float* buffer, size_t frames) {
    if (!sndfile_) {
        return 0;
    }
    // libsndfileのsf_readf_float関数で読み込み
    return static_cast<size_t>(sf_readf_float(sndfile_, buffer, frames));
}

// --- seek ---
// 指定されたフレーム位置に移動する
bool SndfileDecoder::seek(long long frame) {
    if (!sndfile_) {
        return false;
    }
    // libsndfileのsf_seek関数でシーク
    return sf_seek(sndfile_, frame, SEEK_SET) != -1;
}