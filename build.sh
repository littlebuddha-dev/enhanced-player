#!/bin/bash

# 依存関係のインストール (Ubuntu/Debian)
if command -v apt-get >/dev/null 2>&1;
then
    echo "Installing dependencies for Ubuntu/Debian..."
    sudo apt-get update
    # portaudio19-dev を追加
    sudo apt-get install -y libsndfile1-dev libsamplerate0-dev nlohmann-json3-dev libfftw3-dev cmake build-essential pkg-config portaudio19-dev
fi

# 依存関係のインストール (macOS)
if command -v brew >/dev/null 2>&1;
then
    echo "Installing dependencies for macOS..."
    # portaudio を追加
    brew install libsndfile libsamplerate nlohmann-json fftw cmake pkg-config portaudio
fi

# ビルドディレクトリ作成
mkdir -p build
cd build

# CMake設定
cmake .. -DCMAKE_BUILD_TYPE=Release

# ビルド実行
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo "Build complete! Executable: ./realtime_enhancer"
echo ""
echo "Usage examples:"
echo "  ./realtime_enhancer input.wav"