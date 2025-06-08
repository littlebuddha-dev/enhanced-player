// littlebuddha-dev/enhanced-player/enhanced-player-08b6ca5fd163116ffefc8ab54b1c2ab6ccda0410/main.cpp
// ./main.cpp - リアルタイム再生エンジン (引数エラー修正版)
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <atomic>

// 3rd party libraries
#include <sndfile.h>
#include <samplerate.h>
#include <portaudio.h>
#include <nlohmann/json.hpp>

// プロジェクトのヘッダー
#include "SimpleBiquad.h"
#include "vocal_instrument_separator.h"
#include "advanced_dynamics.h"
#include "advanced_eq_harmonics.h"
#include "spatial_processing.h"

using json = nlohmann::json;

// --- グローバル定数 ---
const double TARGET_SAMPLE_RATE = 48000.0;
const unsigned int FRAMES_PER_BUFFER = 1024;

// --- オーディオファイル読み込みクラス ---
class AudioFile {
public:
    AudioFile(const std::string& path) {
        sfinfo_.format = 0;
        file_ = sf_open(path.c_str(), SFM_READ, &sfinfo_);
        if (!file_) {
            // ★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★
            // ★ ここを修正: エラーメッセージを具体的にする ★
            // ★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★
            throw std::runtime_error("Could not open audio file: " + path + ". Check path and ensure format is supported (e.g., WAV, FLAC, AIFF, OGG).");
        }
    }
    ~AudioFile() { if (file_) sf_close(file_); }
    sf_count_t read(float* buffer, sf_count_t frames) { return sf_readf_float(file_, buffer, frames); }
    sf_count_t seek(sf_count_t frames, int whence) { return sf_seek(file_, frames, whence); }
    const SF_INFO& getInfo() const { return sfinfo_; }
private:
    SNDFILE* file_ = nullptr;
    SF_INFO sfinfo_;
};

// --- エンハンス処理チェイン ---
class EffectChain {
public:
    void setup(const json& params, int channels, double sr) {
        channels_ = channels;
        separator_.setup(sr, params.value("separation", json({})));
        std::cout << "Effect Chain Initialized for " << channels << " channels at " << sr << " Hz." << std::endl;
        std::cout << "Vocal Separator: " << (params.value("separation", json({})).value("enabled", false) ? "On" : "Off") << std::endl;
        std::cout << "WARNING: Other effects are disabled until their implementations are provided." << std::endl;
    }
    void process(std::vector<float>& buffer) {
        if (buffer.empty() || channels_ != 2) return;
        size_t frame_count = buffer.size() / channels_;
        for (size_t i = 0; i < frame_count; ++i) {
            float* frame = &buffer[i * channels_];
            auto separated = separator_.process(frame[0], frame[1]);
            frame[0] = separated.first;
            frame[1] = separated.second;
        }
    }
    void reset() {
        separator_.reset();
    }
private:
    int channels_ = 0;
    MSVocalInstrumentSeparator separator_;
};

// --- リアルタイムオーディオ再生エンジン ---
class RealtimeAudioEngine {
public:
    enum class PlaybackState { STOPPED, PLAYING, PAUSED };

    RealtimeAudioEngine(const std::string& audio_file_path) : audio_file_(audio_file_path) {
        load_parameters();
        const SF_INFO& info = audio_file_.getInfo();
        channels_ = info.channels;
        source_sample_rate_ = static_cast<double>(info.samplerate);
        if (source_sample_rate_ != TARGET_SAMPLE_RATE && source_sample_rate_ != 0) {
            std::cout << "File sample rate (" << source_sample_rate_ << ") differs from engine rate (" << TARGET_SAMPLE_RATE << "). Initializing resampler." << std::endl;
            resampling_ratio_ = TARGET_SAMPLE_RATE / source_sample_rate_;
            int error = 0;
            resampler_state_ = src_new(SRC_SINC_FASTEST, channels_, &error);
            if (!resampler_state_) throw std::runtime_error(std::string("src_new failed: ") + src_strerror(error));
        }
        
        effect_chain_.setup(params_, channels_, TARGET_SAMPLE_RATE);
        
        init_portaudio();
    }
    ~RealtimeAudioEngine() {
        stop_stream();
        if (resampler_state_) src_delete(resampler_state_);
        Pa_Terminate();
    }
    void play() {
        if (playback_state_ != PlaybackState::PLAYING) {
            playback_state_ = PlaybackState::PLAYING;
            if (Pa_IsStreamActive(stream_) == 0) Pa_StartStream(stream_);
            std::cout << "Playback started." << std::endl;
        }
    }
    void pause() {
        if (playback_state_ == PlaybackState::PLAYING) {
            playback_state_ = PlaybackState::PAUSED;
            std::cout << "Playback paused." << std::endl;
        }
    }
    void stop() {
        if (playback_state_ != PlaybackState::STOPPED) {
            playback_state_ = PlaybackState::STOPPED;
            stop_stream();
            seek(0.0);
            std::cout << "Playback stopped." << std::endl;
        }
    }
    void seek(double seconds) {
        bool was_playing = (playback_state_ == PlaybackState::PLAYING);
        if (was_playing) playback_state_ = PlaybackState::PAUSED;
        sf_count_t frame_offset = static_cast<sf_count_t>(seconds * source_sample_rate_);
        audio_file_.seek(frame_offset, SEEK_SET);
        if (resampler_state_) src_reset(resampler_state_);
        effect_chain_.reset();
        std::cout << "Seek to " << seconds << " seconds." << std::endl;
        if (was_playing) playback_state_ = PlaybackState::PLAYING;
    }
    bool isPlaying() const { return playback_state_ != PlaybackState::STOPPED; }

private:
    AudioFile audio_file_;
    EffectChain effect_chain_;
    json params_;
    int channels_;
    double source_sample_rate_ = 0.0;
    PaStream* stream_ = nullptr;
    std::atomic<PlaybackState> playback_state_{PlaybackState::STOPPED};
    SRC_STATE* resampler_state_ = nullptr;
    double resampling_ratio_ = 1.0;
    std::vector<float> resampler_input_buffer_;

    void load_parameters() {
        std::ifstream f("params.json");
        if (f.is_open()) { params_ = json::parse(f, nullptr, false); }
    }
    void init_portaudio() {
        PaError err = Pa_Initialize();
        if (err != paNoError) throw std::runtime_error(Pa_GetErrorText(err));
        PaStreamParameters output_parameters;
        output_parameters.device = Pa_GetDefaultOutputDevice();
        if (output_parameters.device == paNoDevice) throw std::runtime_error("No default audio output device found.");
        output_parameters.channelCount = channels_;
        output_parameters.sampleFormat = paFloat32;
        output_parameters.suggestedLatency = Pa_GetDeviceInfo(output_parameters.device)->defaultHighOutputLatency;
        output_parameters.hostApiSpecificStreamInfo = nullptr;
        err = Pa_OpenStream(&stream_, nullptr, &output_parameters, TARGET_SAMPLE_RATE, FRAMES_PER_BUFFER, paClipOff, audioCallback, this);
        if (err != paNoError) throw std::runtime_error(Pa_GetErrorText(err));
    }
    void start_stream() {
        if (Pa_IsStreamActive(stream_) == 0) Pa_StartStream(stream_);
    }
    void stop_stream() {
        if (Pa_IsStreamActive(stream_) == 1) Pa_StopStream(stream_);
    }
    int processAudio(float* output_buffer, unsigned long frames_per_buffer);
    static int audioCallback(const void*, void* out, unsigned long frames, const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* data) {
        return static_cast<RealtimeAudioEngine*>(data)->processAudio(static_cast<float*>(out), frames);
    }
};

int RealtimeAudioEngine::processAudio(float* output_buffer, unsigned long frames_per_buffer) {
    if (playback_state_ != PlaybackState::PLAYING) {
        std::fill(output_buffer, output_buffer + (frames_per_buffer * channels_), 0.0f);
        return paContinue;
    }
    std::vector<float> processing_buffer(frames_per_buffer * channels_);
    long frames_to_process = 0;
    if (resampler_state_) {
        long input_frames_needed = static_cast<long>(ceil(static_cast<double>(frames_per_buffer) / resampling_ratio_));
        resampler_input_buffer_.resize(input_frames_needed * channels_);
        sf_count_t frames_read = audio_file_.read(resampler_input_buffer_.data(), input_frames_needed);
        SRC_DATA src_data;
        src_data.data_in = resampler_input_buffer_.data();
        src_data.input_frames = frames_read;
        src_data.data_out = processing_buffer.data();
        src_data.output_frames = frames_per_buffer;
        src_data.src_ratio = resampling_ratio_;
        src_data.end_of_input = (frames_read < input_frames_needed) ? 1 : 0;
        if (src_process(resampler_state_, &src_data) != 0) { std::cerr << "Resampling error\n"; stop(); }
        frames_to_process = src_data.output_frames_gen;
        if (src_data.end_of_input && frames_to_process == 0) { playback_state_ = PlaybackState::STOPPED; }
    } else {
        frames_to_process = audio_file_.read(processing_buffer.data(), frames_per_buffer);
        if (frames_to_process < (sf_count_t)frames_per_buffer) playback_state_ = PlaybackState::STOPPED;
    }
    if (frames_to_process > 0) {
        processing_buffer.resize(frames_to_process * channels_);
        effect_chain_.process(processing_buffer);
        std::copy(processing_buffer.begin(), processing_buffer.end(), output_buffer);
    }
    if ((size_t)frames_to_process < frames_per_buffer) {
         std::fill(output_buffer + (frames_to_process * channels_), output_buffer + (frames_per_buffer * channels_), 0.0f);
    }
    return paContinue;
}

void print_help() {
    std::cout << "Commands: play, pause, stop, seek <sec>, exit, help\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) { std::cerr << "Usage: " << argv[0] << " <input_audio_file> [start_seconds]" << std::endl; return 1; }
    double start_time = 0.0;
    if (argc >= 3) {
        try { start_time = std::stod(argv[2]); } catch (const std::exception&) { std::cerr << "Invalid start time.\n"; return 1; }
    }
    std::cout << "=== Real-Time Sound Enhancer Engine v1.7 (Multi-format support) ===" << std::endl;
    try {
        RealtimeAudioEngine engine(argv[1]);
        if (start_time > 0.0) engine.seek(start_time);
        print_help();
        engine.play();
        std::string line;
        while (engine.isPlaying() || line != "exit") {
            std::cout << "> ";
            std::getline(std::cin, line);
            std::stringstream ss(line);
            std::string command;
            ss >> command;
            if (command == "play") engine.play();
            else if (command == "pause") engine.pause();
            else if (command == "stop") engine.stop();
            else if (command == "seek") { double sec; if (ss >> sec) engine.seek(sec); else std::cout << "Usage: seek <seconds>\n"; }
            else if (command == "exit" || command == "quit" || std::cin.eof()) { engine.stop(); break; }
            else if (command == "help") print_help();
            else if (!command.empty()) std::cout << "Unknown command. Type 'help'.\n";
            if (!engine.isPlaying() && command != "play" && command != "seek" && command != "exit" && command != "quit") {
                std::cout << "Playback finished. Type 'play' to restart or 'exit' to quit." << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "\nAn error occurred: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}