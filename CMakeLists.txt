# ./CMakeLists.txt - Intel/Apple Silicon 両対応の最終修正版
cmake_minimum_required(VERSION 3.15)
project(RealtimeSoundEnhancer VERSION 4.9)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
endif()

# --- 依存関係の検索 ---
find_package(PkgConfig REQUIRED)

# Part 1: pkg-configで安定して見つかるライブラリ
pkg_check_modules(SNDFILE REQUIRED sndfile)
pkg_check_modules(SAMPLERATE REQUIRED samplerate)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
pkg_check_modules(NLOHMANN_JSON REQUIRED nlohmann_json)

# Part 2: 手動で見つけるライブラリ (macOS/Homebrew向け)
if(APPLE)
    if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(HOMEBREW_PREFIX "/opt/homebrew")
    else()
        set(HOMEBREW_PREFIX "/usr/local")
    endif()
    set(ENV{PKG_CONFIG_PATH} "${HOMEBREW_PREFIX}/lib/pkgconfig")
endif()

find_path(MPG123_INCLUDE_DIR mpg123.h HINTS ${HOMEBREW_PREFIX}/include)
find_library(MPG123_LIBRARY NAMES mpg123 HINTS ${HOMEBREW_PREFIX}/lib)
if(NOT MPG123_INCLUDE_DIR OR NOT MPG123_LIBRARY)
    message(FATAL_ERROR "mpg123 library not found. Please run 'brew install mpg123'")
endif()

find_path(FFTW3F_INCLUDE_DIR fftw3.h HINTS ${HOMEBREW_PREFIX}/include)
find_library(FFTW3F_LIBRARY NAMES fftw3f HINTS ${HOMEBREW_PREFIX}/lib)
if(NOT FFTW3F_INCLUDE_DIR OR NOT FFTW3F_LIBRARY)
    message(FATAL_ERROR "fftw3f library not found. Please run 'brew install fftw'")
endif()

# --- 実行ファイルの定義 ---
add_executable(realtime_enhancer
    main.cpp
    AudioDecoderFactory.cpp
    MPG123Decoder.cpp
    SndfileDecoder.cpp
    advanced_dynamics.cpp
    advanced_eq_harmonics.cpp
    custom_effects.cpp
)

# --- インクルードディレクトリの指定 ---
target_include_directories(realtime_enhancer PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${SNDFILE_INCLUDE_DIRS}
    ${SAMPLERATE_INCLUDE_DIRS}
    ${PORTAUDIO_INCLUDE_DIRS}
    ${NLOHMANN_JSON_INCLUDE_DIRS}
    ${MPG123_INCLUDE_DIR}
    ${FFTW3F_INCLUDE_DIR}
)

# ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↓修正開始◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
# --- ライブラリのリンク ---

# pkg-configが見つけたライブラリのディレクトリをターゲットのリンカ検索パスに追加 (Modern CMake)
target_link_directories(realtime_enhancer PRIVATE
    ${SNDFILE_LIBRARY_DIRS}
    ${SAMPLERATE_LIBRARY_DIRS}
    ${PORTAUDIO_LIBRARY_DIRS}
)

# ライブラリ本体をリンク
target_link_libraries(realtime_enhancer PRIVATE
    ${SNDFILE_LIBRARIES}
    ${SAMPLERATE_LIBRARIES}
    ${PORTAUDIO_LIBRARIES}
    ${MPG123_LIBRARY}
    ${FFTW3F_LIBRARY}
)
# ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↑修正終わり◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️

# --- ビルド後のカスタムコマンド ---
add_custom_command(
    TARGET realtime_enhancer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_SOURCE_DIR}/params.json
            $<TARGET_FILE_DIR:realtime_enhancer>/params.json
    COMMENT "Copying params.json to build directory"
)
