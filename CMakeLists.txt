# ./CMakeLists.txt - Final Robust Version with Hybrid Library Finding
cmake_minimum_required(VERSION 3.15)
project(RealtimeSoundEnhancer VERSION 4.5)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native")
endif()

# --- 依存関係の検索 ---
find_package(PkgConfig REQUIRED)

# Part 1: pkg-configで安定して見つかるライブラリ
pkg_check_modules(SNDFILE REQUIRED sndfile)
pkg_check_modules(SAMPLERATE REQUIRED samplerate)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
pkg_check_modules(NLOHMANN_JSON REQUIRED nlohmann_json)

# ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↓修正開始◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
# Part 2: 手動で見つけるライブラリ (macOS/Homebrew向け)
# Homebrewのインストールプレフィックスをアーキテクチャに応じて設定
if(APPLE)
    if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(HOMEBREW_PREFIX "/opt/homebrew")
    else()
        set(HOMEBREW_PREFIX "/usr/local")
    endif()
    # pkg-configがHomebrewのパスを見るように設定
    set(ENV{PKG_CONFIG_PATH} "${HOMEBREW_PREFIX}/lib/pkgconfig")
endif()

# 手動でmpg123を検索
find_path(MPG123_INCLUDE_DIR mpg123.h HINTS ${HOMEBREW_PREFIX}/include)
find_library(MPG123_LIBRARY NAMES mpg123 HINTS ${HOMEBREW_PREFIX}/lib)
if(NOT MPG123_INCLUDE_DIR OR NOT MPG123_LIBRARY)
    message(FATAL_ERROR "mpg123 library not found. Please run 'brew install mpg123'")
endif()

# 手動でfftw3f (単精度float版) を検索
find_path(FFTW3F_INCLUDE_DIR fftw3.h HINTS ${HOMEBREW_PREFIX}/include)
find_library(FFTW3F_LIBRARY NAMES fftw3f HINTS ${HOMEBREW_PREFIX}/lib)
if(NOT FFTW3F_INCLUDE_DIR OR NOT FFTW3F_LIBRARY)
    message(FATAL_ERROR "fftw3f library not found. Please run 'brew install fftw'")
endif()
# ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↑修正終わり◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️


# --- ビルドターゲットの定義 ---
add_executable(realtime_enhancer
    main.cpp
    advanced_dynamics.cpp
    advanced_eq_harmonics.cpp
    SndfileDecoder.cpp
    MPG123Decoder.cpp
    AudioDecoderFactory.cpp
)

# --- インクルードディレクトリの指定 ---
target_include_directories(realtime_enhancer PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${SNDFILE_INCLUDE_DIRS}
    ${SAMPLERATE_INCLUDE_DIRS}
    ${PORTAUDIO_INCLUDE_DIRS}
    ${NLOHMANN_JSON_INCLUDE_DIRS}
    # ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↓修正開始◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
    ${MPG123_INCLUDE_DIR}
    ${FFTW3F_INCLUDE_DIR}
    # ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↑修正終わり◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
)

# --- ライブラリのリンク ---
target_link_libraries(realtime_enhancer
    ${SNDFILE_LIBRARIES}
    ${SAMPLERATE_LIBRARIES}
    ${PORTAUDIO_LIBRARIES}
    # ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↓修正開始◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
    ${MPG123_LIBRARY}
    ${FFTW3F_LIBRARY}
    # ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↑修正終わり◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
)

# --- ビルド後のカスタムコマンド ---
add_custom_command(
    TARGET realtime_enhancer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_SOURCE_DIR}/params.json
            $<TARGET_FILE_DIR:realtime_enhancer>/params.json
    COMMENT "Copying params.json to build directory"
)
