// littlebuddha-dev/enhanced-player/enhanced-player-08b6ca5fd163116ffefc8ab54b1c2ab6ccda0410/main.cpp
// ./main.cpp - 安定性向上版リアルタイム再生エンジン
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
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>

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
const size_t RING_BUFFER_SIZE = 32768; // 32KB ring buffer
const size_t PREFILL_THRESHOLD = RING_BUFFER_SIZE / 4;

// --- スレッドセーフなリングバッファ ---
template<typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t size) : buffer_(size), size_(size) {}
    
    bool push(const T* data, size_t count) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (available_write() < count) return false;
        
        for (size_t i = 0; i < count; ++i) {
            buffer_[write_pos_] = data[i];
            write_pos_ = (write_pos_ + 1) % size_;
        }
        return true;
    }
    
    size_t pop(T* data, size_t count) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t available = available_read();
        size_t to_read = std::min(count, available);
        
        for (size_t i = 0; i < to_read; ++i) {
            data[i] = buffer_[read_pos_];
            read_pos_ = (read_pos_ + 1) % size_;
        }
        return to_read;
    }
    
    size_t available_read() const {
        if (write_pos_ >= read_pos_) return write_pos_ - read_pos_;
        return size_ - read_pos_ + write_pos_;
    }
    
    size_t available_write() const {
        return size_ - available_read() - 1;
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        read_pos_ = write_pos_ = 0;
    }

private:
    std::vector<T> buffer_;
    size_t size_;
    std::atomic<size_t> read_pos_{0};
    std::atomic<size_t> write_pos_{0};
    mutable std::mutex mutex_;
};

// --- 改良版オーディオファイルクラス ---
class AudioFile {
public:
    AudioFile(const std::string& path) : path_(path) {
        sfinfo_.format = 0;
        file_ = sf_open(path.c_str(), SFM_READ, &sfinfo_);
        if (!file_) {
            throw std::runtime_error("Could not open audio file: " + path + 
                ". Error: " + sf_strerror(nullptr) + 
                ". Check path and ensure format is supported (e.g., WAV, FLAC, AIFF, OGG).");
        }
    }
    
    ~AudioFile() { 
        if (file_) sf_close(file_); 
    }
    
    sf_count_t read(float* buffer, sf_count_t frames) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!file_) return 0;
        
        sf_count_t read_frames = sf_readf_float(file_, buffer, frames);
        if (read_frames < 0) {
            std::cerr << "Audio file read error: " << sf_strerror(file_) << std::endl;
            return 0;
        }
        return read_frames;
    }
    
    sf_count_t seek(sf_count_t frames, int whence) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!file_) return -1;
        return sf_seek(file_, frames, whence);
    }
    
    const SF_INFO& getInfo() const { return sfinfo_; }
    bool isValid() const { return file_ != nullptr; }
    
private:
    std::string path_;
    SNDFILE* file_ = nullptr;
    SF_INFO sfinfo_;
    mutable std::mutex mutex_;
};

// --- エンハンス処理チェイン（スレッドセーフ版） ---
class EffectChain {
public:
    void setup(const json& params, int channels, double sr) {
        std::lock_guard<std::mutex> lock(mutex_);
        channels_ = channels;
        separator_.setup(sr, params.value("separation", json({})));
        std::cout << "Effect Chain Initialized for " << channels << " channels at " << sr << " Hz." << std::endl;
    }
    
    void process(std::vector<float>& buffer) {
        std::lock_guard<std::mutex> lock(mutex_);
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
        std::lock_guard<std::mutex> lock(mutex_);
        separator_.reset();
    }
    
private:
    int channels_ = 0;
    MSVocalInstrumentSeparator separator_;
    mutable std::mutex mutex_;
};

// --- 改良版リアルタイムオーディオエンジン ---
class RealtimeAudioEngine {
public:
    enum class PlaybackState { STOPPED, PLAYING, PAUSED };

    RealtimeAudioEngine(const std::string& audio_file_path) 
        : audio_file_(audio_file_path), ring_buffer_(RING_BUFFER_SIZE) {
        
        if (!audio_file_.isValid()) {
            throw std::runtime_error("Failed to initialize audio file");
        }
        
        load_parameters();
        
        const SF_INFO& info = audio_file_.getInfo();
        channels_ = info.channels;
        source_sample_rate_ = static_cast<double>(info.samplerate);
        total_frames_ = info.frames;
        
        // リサンプラー初期化
        if (source_sample_rate_ != TARGET_SAMPLE_RATE && source_sample_rate_ != 0) {
            std::cout << "File sample rate (" << source_sample_rate_ << 
                ") differs from engine rate (" << TARGET_SAMPLE_RATE << 
                "). Initializing resampler." << std::endl;
            resampling_ratio_ = TARGET_SAMPLE_RATE / source_sample_rate_;
            
            int error = 0;
            resampler_state_ = src_new(SRC_SINC_MEDIUM_QUALITY, channels_, &error);
            if (!resampler_state_) {
                throw std::runtime_error(std::string("src_new failed: ") + src_strerror(error));
            }
        }
        
        effect_chain_.setup(params_, channels_, TARGET_SAMPLE_RATE);
        init_portaudio();
        
        // バックグラウンドローディングスレッド開始
        loading_thread_ = std::thread(&RealtimeAudioEngine::loading_thread_func, this);
    }
    
    ~RealtimeAudioEngine() {
        stop();
        should_exit_ = true;
        loading_cv_.notify_all();
        if (loading_thread_.joinable()) {
            loading_thread_.join();
        }
        
        if (stream_) {
            Pa_CloseStream(stream_);
        }
        if (resampler_state_) {
            src_delete(resampler_state_);
        }
        Pa_Terminate();
    }
    
    void play() {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (playback_state_ != PlaybackState::PLAYING) {
            playback_state_ = PlaybackState::PLAYING;
            loading_cv_.notify_all(); // ローディングスレッドを起動
            
            if (Pa_IsStreamActive(stream_) == 0) {
                PaError err = Pa_StartStream(stream_);
                if (err != paNoError) {
                    std::cerr << "Failed to start audio stream: " << Pa_GetErrorText(err) << std::endl;
                    playback_state_ = PlaybackState::STOPPED;
                    return;
                }
            }
            std::cout << "Playback started." << std::endl;
        }
    }
    
    void pause() {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (playback_state_ == PlaybackState::PLAYING) {
            playback_state_ = PlaybackState::PAUSED;
            std::cout << "Playback paused." << std::endl;
        }
    }
    
    void stop() {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (playback_state_ != PlaybackState::STOPPED) {
                playback_state_ = PlaybackState::STOPPED;
                std::cout << "Playback stopped." << std::endl;
            }
        }
        
        if (stream_ && Pa_IsStreamActive(stream_) == 1) {
            Pa_StopStream(stream_);
        }
        
        ring_buffer_.clear();
        seek_to_frame(0);
    }
    
    void seek(double seconds) {
        sf_count_t target_frame = static_cast<sf_count_t>(seconds * source_sample_rate_);
        target_frame = std::max(sf_count_t(0), std::min(target_frame, total_frames_ - 1));
        
        seek_to_frame(target_frame);
        std::cout << "Seek to " << seconds << " seconds." << std::endl;
    }
    
    bool isPlaying() const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return playback_state_ != PlaybackState::STOPPED;
    }

private:
    AudioFile audio_file_;
    EffectChain effect_chain_;
    json params_;
    
    int channels_;
    double source_sample_rate_ = 0.0;
    sf_count_t total_frames_ = 0;
    sf_count_t current_frame_ = 0;
    
    PaStream* stream_ = nullptr;
    std::atomic<PlaybackState> playback_state_{PlaybackState::STOPPED};
    mutable std::mutex state_mutex_;
    
    // リサンプリング関連
    SRC_STATE* resampler_state_ = nullptr;
    double resampling_ratio_ = 1.0;
    
    // バッファリング関連
    RingBuffer<float> ring_buffer_;
    std::thread loading_thread_;
    std::atomic<bool> should_exit_{false};
    std::condition_variable loading_cv_;
    std::mutex loading_mutex_;
    
    void load_parameters() {
        try {
            std::ifstream f("params.json");
            if (f.is_open()) {
                params_ = json::parse(f, nullptr, false);
                if (params_.is_discarded()) {
                    std::cerr << "Warning: Invalid JSON in params.json, using defaults" << std::endl;
                    params_ = json::object();
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Warning: Failed to load params.json: " << e.what() << std::endl;
            params_ = json::object();
        }
    }
    
    void init_portaudio() {
        PaError err = Pa_Initialize();
        if (err != paNoError) {
            throw std::runtime_error(std::string("PortAudio init failed: ") + Pa_GetErrorText(err));
        }
        
        PaStreamParameters output_parameters;
        output_parameters.device = Pa_GetDefaultOutputDevice();
        if (output_parameters.device == paNoDevice) {
            throw std::runtime_error("No default audio output device found.");
        }
        
        const PaDeviceInfo* device_info = Pa_GetDeviceInfo(output_parameters.device);
        if (!device_info) {
            throw std::runtime_error("Failed to get device info");
        }
        
        output_parameters.channelCount = channels_;
        output_parameters.sampleFormat = paFloat32;
        output_parameters.suggestedLatency = device_info->defaultHighOutputLatency;
        output_parameters.hostApiSpecificStreamInfo = nullptr;
        
        err = Pa_OpenStream(&stream_, nullptr, &output_parameters, TARGET_SAMPLE_RATE, 
                          FRAMES_PER_BUFFER, paClipOff, audioCallback, this);
        if (err != paNoError) {
            throw std::runtime_error(std::string("Failed to open audio stream: ") + Pa_GetErrorText(err));
        }
    }
    
    void seek_to_frame(sf_count_t frame) {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            current_frame_ = frame;
        }
        
        audio_file_.seek(frame, SEEK_SET);
        ring_buffer_.clear();
        
        if (resampler_state_) {
            src_reset(resampler_state_);
        }
        effect_chain_.reset();
    }
    
    void loading_thread_func() {
        std::vector<float> temp_buffer(FRAMES_PER_BUFFER * channels_);
        std::vector<float> resampled_buffer(FRAMES_PER_BUFFER * channels_ * 2); // 余裕を持ったサイズ
        
        while (!should_exit_) {
            std::unique_lock<std::mutex> lock(loading_mutex_);
            loading_cv_.wait(lock, [this] { 
                return should_exit_ || 
                       (playback_state_ == PlaybackState::PLAYING && 
                        ring_buffer_.available_read() < PREFILL_THRESHOLD * channels_);
            });
            
            if (should_exit_) break;
            
            if (playback_state_ == PlaybackState::PLAYING) {
                lock.unlock();
                
                // ファイルからデータ読み込み
                sf_count_t frames_read = audio_file_.read(temp_buffer.data(), FRAMES_PER_BUFFER);
                if (frames_read <= 0) {
                    // ファイル終端に達した
                    std::lock_guard<std::mutex> state_lock(state_mutex_);
                    if (playback_state_ == PlaybackState::PLAYING) {
                        playback_state_ = PlaybackState::STOPPED;
                    }
                    continue;
                }
                
                // リサンプリング処理
                if (resampler_state_) {
                    SRC_DATA src_data;
                    src_data.data_in = temp_buffer.data();
                    src_data.input_frames = frames_read;
                    src_data.data_out = resampled_buffer.data();
                    src_data.output_frames = resampled_buffer.size() / channels_;
                    src_data.src_ratio = resampling_ratio_;
                    src_data.end_of_input = 0;
                    
                    int error = src_process(resampler_state_, &src_data);
                    if (error != 0) {
                        std::cerr << "Resampling error: " << src_strerror(error) << std::endl;
                        continue;
                    }
                    
                    // リサンプル済みデータをリングバッファにプッシュ
                    size_t samples_to_push = src_data.output_frames_gen * channels_;
                    if (!ring_buffer_.push(resampled_buffer.data(), samples_to_push)) {
                        // バッファがフル - 少し待つ
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                } else {
                    // リサンプリング不要
                    size_t samples_to_push = frames_read * channels_;
                    if (!ring_buffer_.push(temp_buffer.data(), samples_to_push)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                }
            }
        }
    }
    
    int processAudio(float* output_buffer, unsigned long frames_per_buffer) {
        // 再生停止時は無音を出力
        if (playback_state_ != PlaybackState::PLAYING) {
            std::fill(output_buffer, output_buffer + (frames_per_buffer * channels_), 0.0f);
            return paContinue;
        }
        
        size_t samples_needed = frames_per_buffer * channels_;
        std::vector<float> processing_buffer(samples_needed);
        
        // リングバッファからデータを取得
        size_t samples_read = ring_buffer_.pop(processing_buffer.data(), samples_needed);
        
        if (samples_read == 0) {
            // アンダーラン - 無音を出力してローディングを促す
            std::fill(output_buffer, output_buffer + samples_needed, 0.0f);
            loading_cv_.notify_all();
            return paContinue;
        }
        
        // 不足分は無音で埋める
        if (samples_read < samples_needed) {
            std::fill(processing_buffer.data() + samples_read, 
                     processing_buffer.data() + samples_needed, 0.0f);
        }
        
        // エフェクト処理
        try {
            effect_chain_.process(processing_buffer);
        } catch (const std::exception& e) {
            std::cerr << "Effect processing error: " << e.what() << std::endl;
        }
        
        // 出力バッファにコピー
        std::copy(processing_buffer.begin(), processing_buffer.end(), output_buffer);
        
        // ローディングスレッドに通知
        loading_cv_.notify_all();
        
        return paContinue;
    }
    
    static int audioCallback(const void*, void* out, unsigned long frames, 
                           const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* data) {
        return static_cast<RealtimeAudioEngine*>(data)->processAudio(static_cast<float*>(out), frames);
    }
};

void print_help() {
    std::cout << "Commands: play, pause, stop, seek <sec>, exit, help\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) { 
        std::cerr << "Usage: " << argv[0] << " <input_audio_file> [start_seconds]" << std::endl; 
        return 1; 
    }
    
    double start_time = 0.0;
    if (argc >= 3) {
        try { 
            start_time = std::stod(argv[2]); 
        } catch (const std::exception&) { 
            std::cerr << "Invalid start time: " << argv[2] << std::endl; 
            return 1; 
        }
    }
    
    std::cout << "=== Real-Time Sound Enhancer Engine v2.0 (Stability Enhanced) ===" << std::endl;
    
    try {
        RealtimeAudioEngine engine(argv[1]);
        
        if (start_time > 0.0) {
            engine.seek(start_time);
        }
        
        print_help();
        engine.play();
        
        std::string line;
        while (engine.isPlaying() || line != "exit") {
            std::cout << "> ";
            if (!std::getline(std::cin, line)) {
                // EOF reached
                engine.stop();
                break;
            }
            
            std::stringstream ss(line);
            std::string command;
            ss >> command;
            
            try {
                if (command == "play") {
                    engine.play();
                } else if (command == "pause") {
                    engine.pause();
                } else if (command == "stop") {
                    engine.stop();
                } else if (command == "seek") {
                    double sec;
                    if (ss >> sec) {
                        engine.seek(sec);
                    } else {
                        std::cout << "Usage: seek <seconds>\n";
                    }
                } else if (command == "exit" || command == "quit") {
                    engine.stop();
                    break;
                } else if (command == "help") {
                    print_help();
                } else if (!command.empty()) {
                    std::cout << "Unknown command: '" << command << "'. Type 'help' for available commands.\n";
                }
            } catch (const std::exception& e) {
                std::cerr << "Command error: " << e.what() << std::endl;
            }
            
            if (!engine.isPlaying() && command != "play" && command != "seek" && 
                command != "exit" && command != "quit" && command != "help") {
                std::cout << "Playback finished. Type 'play' to restart or 'exit' to quit." << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "\nFatal error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "Goodbye!" << std::endl;
    return 0;
}
