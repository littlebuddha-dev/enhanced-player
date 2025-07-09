#!/bin/bash

# エラーが発生したらすぐにスクリプトを終了する
set -e

# 依存関係のインストール (macOS)
if command -v brew >/dev/null 2>&1;
then
    echo "Checking and installing dependencies for macOS..."
    brew install libsndfile libsamplerate nlohmann-json fftw cmake pkg-config portaudio mpg123
fi

# 依存関係のインストール (Ubuntu/Debian)
if command -v apt-get >/dev/null 2>&1;
then
    echo "Checking and installing dependencies for Ubuntu/Debian..."
    sudo apt-get update
    sudo apt-get install -y libsndfile1-dev libsamplerate0-dev nlohmann-json3-dev libfftw3-dev cmake build-essential pkg-config portaudio19-dev libmpg123-dev
fi

# 古いビルドディレクトリを削除してクリーンな状態から始める
if [ -d "build" ]; then
    echo "Cleaning up previous build..."
    rm -rf build
fi

# ビルドディレクトリを作成
mkdir -p build
cd build

# CMakeを実行してビルドファイルを作成
echo "Configuring project with CMake..."
cmake ..

# プロジェクトをビルド
echo "Building the project..."
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo ""
echo "Build complete! Executable is in the 'build' directory."
echo ""
echo "Usage examples:"
echo "  ./realtime_enhancer ../your_audio_file.wav"