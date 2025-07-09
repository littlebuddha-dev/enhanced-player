# ./CMakeLists.txt - Final Robust Version with Modern FFTW Linking
cmake_minimum_required(VERSION 3.15)
project(RealtimeSoundEnhancer VERSION 4.3)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native")
endif()

# --- 依存関係の検索 ---
find_package(PkgConfig REQUIRED)

# pkg-config を使用してライブラリを検索
pkg_check_modules(SNDFILE REQUIRED sndfile)
pkg_check_modules(SAMPLERATE REQUIRED samplerate)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
pkg_check_modules(NLOHMANN_JSON REQUIRED nlohmann_json)

# FFTW: 単精度(float)コンポーネントを必須として検索
# これにより、インポートされたターゲット FFTW3::fftw3f が利用可能になる
find_package(FFTW3 COMPONENTS float REQUIRED)

# --- mpg123 を手動で、かつ確実に見つける (macOS Homebrew用) ---
if(APPLE)
    if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(HOMEBREW_PREFIX "/opt/homebrew")
    else()
        set(HOMEBREW_PREFIX "/usr/local")
    endif()

    find_path(MPG123_INCLUDE_DIR mpg123.h HINTS ${HOMEBREW_PREFIX}/include PATH_SUFFIXES mpg123)
    find_library(MPG123_LIBRARY NAMES mpg123 HINTS ${HOMEBREW_PREFIX}/lib)

    if(NOT MPG123_INCLUDE_DIR OR NOT MPG123_LIBRARY)
        message(FATAL_ERROR "mpg123 library not found in Homebrew paths. Please ensure 'brew install mpg123' was successful.")
    endif()
    
    set(MPG123_INCLUDE_DIRS ${MPG123_INCLUDE_DIR})
    set(MPG123_LIBRARIES ${MPG123_LIBRARY})
else()
    # macOS以外では引き続きpkg-configを利用
    pkg_check_modules(MPG123 REQUIRED mpg123)
endif()

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
    ${MPG123_INCLUDE_DIRS}
    # FFTWのインクルードディレクトリは、下の target_link_libraries で
    # インポートされたターゲットを使用することで自動的に追加される
)

# --- ライブラリのリンク ---
target_link_libraries(realtime_enhancer
    ${SNDFILE_LIBRARIES}
    ${SAMPLERATE_LIBRARIES}
    ${PORTAUDIO_LIBRARIES}
    # nlohmann::jsonはヘッダーオンリーなので、リンカーフラグは不要な場合が多いが念のため
    # ${NLOHMANN_JSON_LIBRARIES} 
    ${MPG123_LIBRARIES}
    # モダンなCMakeの作法に従い、インポートされたターゲットを使用する
    # これにより、ライブラリのパスとインクルードディレクトリが正しく設定される
    FFTW3::fftw3f
)

# --- ビルド後のカスタムコマンド ---
add_custom_command(
    TARGET realtime_enhancer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_SOURCE_DIR}/params.json
            $<TARGET_FILE_DIR:realtime_enhancer>/params.json
    COMMENT "Copying params.json to build directory"
)
