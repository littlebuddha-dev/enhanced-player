# ./CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
project(RealtimeSoundEnhancer VERSION 2.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# コンパイラ最適化フラグ
# nlohmann/jsonで警告が出るため -ffast-math は削除
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native")
endif()

# 依存ライブラリを探す
find_package(PkgConfig REQUIRED)

# libsndfile
pkg_check_modules(SNDFILE REQUIRED sndfile)

# libsamplerate
pkg_check_modules(SAMPLERATE REQUIRED samplerate)

# PortAudio
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)

# nlohmann/json
# pkg-config を使って nlohmann_json のパスを見つける
pkg_check_modules(NLOHMANN_JSON REQUIRED nlohmann_json)

# 実行ファイル作成
add_executable(realtime_enhancer
    main.cpp
)

# インクルードディレクトリ
# プロジェクトのルートディレクトリをインクルードパスに追加することで、
# "advanced_dynamics.h" などローカルのヘッダーファイルを見つけられるようにします。
target_include_directories(realtime_enhancer PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${SNDFILE_INCLUDE_DIRS}
    ${SAMPLERATE_INCLUDE_DIRS}
    ${PORTAUDIO_INCLUDE_DIRS}
    ${NLOHMANN_JSON_INCLUDE_DIRS}
)

# ライブラリリンク
target_link_libraries(realtime_enhancer
    ${SNDFILE_LIBRARIES}
    ${SAMPLERATE_LIBRARIES}
    ${PORTAUDIO_LIBRARIES}
)

# コンパイルフラグ
target_compile_options(realtime_enhancer PRIVATE
    ${SNDFILE_CFLAGS_OTHER}
    ${SAMPLERATE_CFLAGS_OTHER}
    ${PORTAUDIO_CFLAGS_OTHER}
)

# インストール設定
install(TARGETS realtime_enhancer DESTINATION bin)

# サンプル設定ファイルのインストール
install(FILES
    params.json
    DESTINATION share/realtime_enhancer
)