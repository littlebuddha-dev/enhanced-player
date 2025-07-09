// ./AudioEffect.h
// 全てのオーディオエフェクトクラスが継承する抽象基底クラス
#pragma once

#include <vector>
#include <string>
#include <nlohmann/json.hpp>

// jsonエイリアス
using json = nlohmann::json;

class AudioEffect {
public:
    // デストラクタは仮想にする
    virtual ~AudioEffect() = default;

    /**
     * @brief エフェクトのパラメータを設定する
     * @param sr サンプリングレート
     * @param params JSONオブジェクト形式のパラメータ
     */
    virtual void setup(double sr, const json& params) = 0;

    /**
     * @brief オーディオデータをブロック単位で処理する
     * @param block 処理対象のオーディオデータブロック（インターリーブ形式）
     * @param channels チャンネル数
     */
    virtual void process(std::vector<float>& block, int channels) = 0;

    /**
     * @brief エフェクトの内部状態をリセットする
     */
    virtual void reset() = 0;

    /**
     * @brief エフェクトの名前を取得する
     * @return エフェクト名
     */
    virtual const std::string& getName() const = 0;
};