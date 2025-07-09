// ./AudioEffectFactory.h
// エフェクト名（文字列）からAudioEffectのインスタンスを生成するファクトリー
#pragma once

#include "AudioEffect.h"
#include <string>
#include <memory>
#include <functional>
#include <map>
#include <stdexcept>

class AudioEffectFactory {
public:
    // シングルトンインスタンスを取得
    static AudioEffectFactory&getInstance() {
        static AudioEffectFactory instance;
        return instance;
    }

    // ファクトリーに関数（コンストラクタ）を登録する
    template<typename T>
    void registerEffect(const std::string&name) {
        creators_[name] = []() { return std::make_unique<T>(); };
    }

    // 名前を指定してエフェクトのインスタンスを作成する
    std::unique_ptr<AudioEffect>createEffect(const std::string&name) {
        auto it = creators_.find(name);
        if (it == creators_.end()) {
            // 見つからない場合はエラーメッセージを出すか、nullptrを返す
            // ここではnullptrを返し、呼び出し元でログ出力などをハンドルする
            return nullptr;
        }
        return it->second();
    }

private:
    // プライベートコンストラクタ（シングルトンパターン）
    AudioEffectFactory() = default;
    ~AudioEffectFactory() = default;
    
    // コピーとムーブを禁止
    AudioEffectFactory(const AudioEffectFactory&) = delete;
    AudioEffectFactory&operator=(const AudioEffectFactory&) = delete;

    // 文字列名と、AudioEffectを生成する関数のマップ
    std::map<std::string, std::function<std::unique_ptr<AudioEffect>()>> creators_;
};