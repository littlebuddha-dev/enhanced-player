# enhanced-player

はい、承知いたしました。提供されたファイルの内容に基づいて、プロジェクトの README.md ファイルを作成します。

Realtime Sound Enhancer
Realtime Sound Enhancer は、オーディオファイルに対してリアルタイムで高度な音響効果を適用するC++製のプレイヤーです。このプロジェクトは、ボーカルと楽器の分離、パラメトリックEQ、ハーモニックエンハンスメント、ステレオイメージングなど、多彩なエフェクトをリアルタイムに処理し再生することを目的としています。

設定は外部の params.json ファイルによって管理されており、ユーザーは柔軟にパラメータを調整できます。

主な機能
本プレイヤーは、モジュール化されたエフェクトチェインを特徴としています。

リアルタイムオーディオエンジン:
libsndfile を用いたオーディオファイルの読み込み。
PortAudio を用いたクロスプラットフォームなオーディオ出力。
libsamplerate を用いたサンプルレート変換機能。
エフェクトモジュール (一部実装中):
ボーカル・楽器分離: M/S (Mid-Side) 処理に基づき、ボーカル成分と楽器成分を動的に分離・強調します。
高度なダイナミクス処理:
マルチバンドコンプレッサー
アナログ風サチュレーション（Tube, Tape, Transformer）
EQとハーモニクス:
FFTベースの線形位相EQ
ハーモニックエンハンサー
スペクトラルゲート（ノイズ除去）
空間処理:
ステレオエンハンサー（ステレオ幅調整、低域のモノラル化）
注: 現在の main.cpp の実装では、主に MSVocalInstrumentSeparator が有効化されています。他の多くのエフェクトはヘッダーファイルとして定義されていますが、エフェクトチェインへの完全な統合は今後の開発項目となります。

依存ライブラリ
このプロジェクトは以下のライブラリに依存しています。

libsndfile: オーディオファイルの読み書き用
libsamplerate: 高品質なサンプルレート変換用
PortAudio: クロスプラットフォームのリアルタイムオーディオI/O用
nlohmann/json: 設定ファイル (params.json) のパース用
FFTW3: 線形位相EQなどのFFT処理用
CMake: ビルドシステム
PkgConfig: ライブラリの検出用
セットアップとビルド
付属の build.sh スクリプトを使用することで、依存関係のインストールからビルドまでを自動的に行うことができます。

1. 依存関係のインストール
Ubuntu/Debian の場合:

Bash

sudo apt-get update
sudo apt-get install -y libsndfile1-dev libsamplerate0-dev nlohmann-json3-dev libfftw3-dev cmake build-essential pkg-config portaudio19-dev
macOS (Homebrew) の場合:

Bash

brew install libsndfile libsamplerate nlohmann-json fftw cmake pkg-config portaudio
2. ビルド
プロジェクトのルートディレクトリで以下のコマンドを実行してください。

Bash

# ビルドディレクトリを作成して移動
mkdir -p build
cd build

# CMake を実行してビルドファイルを生成
cmake .. -DCMAKE_BUILD_TYPE=Release

# ビルドを実行 (CPUコア数に応じて並列ビルド)
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
ビルドが完了すると、build ディレクトリ内に realtime_enhancer という実行ファイルが生成されます。

使用方法
実行ファイルはコマンドラインから使用します。

基本コマンド:

Bash

./build/realtime_enhancer <input_audio_file> [start_seconds]
<input_audio_file>: 再生したいオーディオファイルのパスを指定します（例: input.wav）。
[start_seconds] (オプション): 再生を開始する時間を秒で指定します。
対話コマンド:
プログラム実行後、以下のコマンドを入力することで再生をコントロールできます。

play: 再生を開始します。
pause: 再生を一時停止します。
stop: 再生を停止し、曲の先頭に戻ります。
seek <sec>: 指定した秒数にシークします。
exit: プログラムを終了します。
help: コマンドの一覧を表示します。
設定
エフェクトのパラメータは params.json ファイルで詳細に設定できます。このファイルを編集することで、ボーカル分離の強度、EQの各バンド設定、ハーモニックエンハンサーのドライブ量などをカスタマイズすることが可能です。
