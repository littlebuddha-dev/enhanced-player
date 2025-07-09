// ./MPG123Decoder.cpp - Final Corrected and Verified Version
#include "MPG123Decoder.h"
#include <iostream>
#include <stdexcept> // For std::runtime_error

// コンストラクタ: mpg123ライブラリを初期化し、新しいハンドルを作成
MPG123Decoder::MPG123Decoder() : mh_(nullptr) {
    int err;
    // ライブラリの初期化
    err = mpg123_init();
    if (err != MPG123_OK) {
        throw std::runtime_error("Failed to initialize mpg123 library.");
    }

    // デコーダーハンドルを作成
    mh_ = mpg123_new(NULL, &err);
    if (!mh_) {
        throw std::runtime_error("Failed to create mpg123 handle: " + std::string(mpg123_plain_strerror(err)));
    }

    // パラメータを設定：デコード速度よりも品質を優先
    mpg123_param(mh_, MPG123_ADD_FLAGS, MPG123_FORCE_FLOAT, 0.);
}

// デストラクタ: mpg123ハンドルをクリーンアップし、ライブラリを終了
MPG123Decoder::~MPG123Decoder() {
    if (mh_) {
        mpg123_close(mh_);
        mpg123_delete(mh_);
    }
    mpg123_exit();
}

// open: MP3ファイルを開き、フォーマットを設定し、情報を取得
bool MPG123Decoder::open(const std::string& filePath) {
    // 1. ファイルを開く
    if (mpg123_open(mh_, filePath.c_str()) != MPG123_OK) {
        std::cerr << "MPG123Decoder Error: Failed to open file '" << filePath << "'. " << mpg123_strerror(mh_) << std::endl;
        return false;
    }

    // 2. MP3のフォーマットを取得
    long rate = 0;
    int channels = 0;
    int encoding = 0;
    if (mpg123_getformat(mh_, &rate, &channels, &encoding) != MPG123_OK) {
        std::cerr << "MPG123Decoder Error: Failed to get format information. " << mpg123_strerror(mh_) << std::endl;
        return false;
    }

    // 3. 全てのチャンネル、全てのレートの32bit浮動小数点形式を受け入れるように設定
    mpg123_format_none(mh_);
    if (mpg123_format(mh_, rate, MPG123_MONO | MPG123_STEREO, MPG123_ENC_FLOAT_32) != MPG123_OK) {
        std::cerr << "MPG123Decoder Error: Failed to set output format. " << mpg123_strerror(mh_) << std::endl;
        return false;
    }

    // 4. ファイルをスキャンして正確な長さを取得
    if (mpg123_scan(mh_) != MPG123_OK) {
        std::cerr << "MPG123Decoder Warning: Failed to scan file. Seeking may be inaccurate." << std::endl;
    }
    
    // 5. 最終的なオーディオ情報を格納
    info_.sampleRate = rate;
    info_.channels = channels;
    off_t length = mpg123_length(mh_);
    info_.totalFrames = (length < 0) ? 0 : length;

    return true;
}

// getInfo: 格納されているオーディオ情報を返す
AudioInfo MPG123Decoder::getInfo() const {
    return info_;
}

// read: デコードされたオーディオフレームをバッファに読み込む
size_t MPG123Decoder::read(float* buffer, size_t frames) {
    if (!mh_ || !buffer || frames == 0) {
        return 0;
    }

    size_t bytes_to_read = frames * info_.channels * sizeof(float);
    size_t bytes_done = 0;
    int err = mpg123_read(mh_, reinterpret_cast<unsigned char*>(buffer), bytes_to_read, &bytes_done);

    if (err != MPG123_OK && err != MPG123_DONE) {
        // NEW_FORMATは正常ケースとして扱う
        if (err == MPG123_NEW_FORMAT) {
            long new_rate;
            int new_channels, new_encoding;
            mpg123_getformat(mh_, &new_rate, &new_channels, &new_encoding);
            info_.sampleRate = new_rate;
            info_.channels = new_channels;
        } else {
            // その他のエラー
            return 0;
        }
    }
    
    // 実際に読み込まれたバイト数からフレーム数を計算して返す
    return bytes_done / (info_.channels * sizeof(float));
}

// seek: トラックの指定したフレーム位置に移動
bool MPG123Decoder::seek(long long frame) {
    if (!mh_) {
        return false;
    }
    // mpg123_seekは成功すると移動先のフレーム位置を、失敗すると負の値を返す
    return mpg123_seek(mh_, frame, SEEK_SET) >= 0;
}