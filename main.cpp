// ./main.cpp - Final Version with Corrected RingBuffer Implementation and Playback Fix
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
#include <filesystem>

#include <samplerate.h>
#include <portaudio.h>
#include <nlohmann/json.hpp>

#include "AudioDecoderFactory.h"
#include "SimpleBiquad.h"
#include "vocal_instrument_separator.h"
#include "advanced_dynamics.h"
#include "advanced_eq_harmonics.h"
#include "spatial_processing.h"

using json = nlohmann::json;

const double TARGET_SAMPLE_RATE = 48000.0;
const unsigned int FRAMES_PER_BUFFER = 1024;
const size_t RING_BUFFER_FRAMES = 8192; 

template<typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t frame_count, size_t channels) : 
        buffer_(frame_count * channels), 
        size_(frame_count * channels),
        channels_(channels) {}
    
    bool push(const T* data, size_t frames) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t samples_to_write = frames * channels_;
        if (available_write_samples() < samples_to_write) return false;
        
        for (size_t i = 0; i < samples_to_write; ++i) {
            buffer_[write_pos_] = data[i];
            write_pos_ = (write_pos_ + 1) % size_;
        }
        return true;
    }
    
    size_t pop(T* data, size_t frames) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t samples_to_read = frames * channels_;
        size_t available = available_read_samples();
        size_t to_read = std::min(samples_to_read, available);
        
        for (size_t i = 0; i < to_read; ++i) {
            data[i] = buffer_[read_pos_];
            read_pos_ = (read_pos_ + 1) % size_;
        }
        return to_read / channels_;
    }
    
    size_t available_read_frames() const {
        return available_read_samples() / channels_;
    }
    
    size_t available_write_frames() const {
        return available_write_samples() / channels_;
    }

    void clear() { std::lock_guard<std::mutex> lock(mutex_); read_pos_ = write_pos_ = 0; }
private:
    size_t available_read_samples() const {
        if (write_pos_ >= read_pos_) {
            return write_pos_ - read_pos_;
        } else {
            return size_ - read_pos_ + write_pos_;
        }
    }
    size_t available_write_samples() const {
        return size_ - available_read_samples() - 1;
    }

    std::vector<T> buffer_;
    size_t size_;
    size_t channels_;
    std::atomic<size_t> read_pos_{0};
    std::atomic<size_t> write_pos_{0};
    mutable std::mutex mutex_;
};

class EffectChain {
public:
    void setup(const json& params, int channels, double sr) {
        std::lock_guard<std::mutex> lock(mutex_); 
        channels_ = channels;
        separator_.setup(sr, params.value("separation", json({})));
        saturation_.setup(sr, params.value("analog_saturation", json({})));
        enhancer_.setup(sr, params.value("harmonic_enhancer", json({})));
    }
    void process(std::vector<float>& buffer) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (buffer.empty() || channels_ == 0) return;
        size_t frame_count = buffer.size() / channels_;
        for (size_t i = 0; i < frame_count; ++i) {
            float* frame = &buffer[i * channels_];
            if (channels_ == 2) {
                auto separated = separator_.process(frame[0], frame[1]);
                float left = separated.first, right = separated.second;
                float mid = (left + right) * 0.5f, side = (left - right) * 0.5f;
                float enhanced_mid = enhancer_.process(mid);
                left = enhanced_mid + side; right = enhanced_mid - side;
                frame[0] = saturation_.process(left); frame[1] = saturation_.process(right);
            } else {
                for(int ch = 0; ch < channels_; ++ch) {
                    float sample = frame[ch];
                    sample = enhancer_.process(sample); sample = saturation_.process(sample);
                    frame[ch] = sample;
                }
            }
        }
    }
    void reset() { std::lock_guard<std::mutex> lock(mutex_); separator_.reset(); saturation_.reset(); enhancer_.reset(); }
private:
    int channels_ = 0;
    MSVocalInstrumentSeparator separator_;
    AnalogSaturation saturation_;
    HarmonicEnhancer enhancer_;
    mutable std::mutex mutex_;
};

class RealtimeAudioEngine {
public:
    enum class PlaybackState { STOPPED, PLAYING, PAUSED, FINISHED };
    RealtimeAudioEngine(const std::string& audio_file_path, const std::string& executable_path) 
        : executable_path_(executable_path) {
        decoder_ = AudioDecoderFactory::createDecoder(audio_file_path);
        if (!decoder_) { throw std::runtime_error("Failed to create a suitable decoder for the file."); }
        const AudioInfo info = decoder_->getInfo();
        channels_ = info.channels;
        source_sample_rate_ = static_cast<double>(info.sampleRate);
        total_frames_ = info.totalFrames;
        
        ring_buffer_ = std::make_unique<RingBuffer<float>>(RING_BUFFER_FRAMES, channels_);

        if (channels_ <= 0 || source_sample_rate_ <= 0) { throw std::runtime_error("Audio file properties are invalid."); }
        if (source_sample_rate_ != TARGET_SAMPLE_RATE) {
            resampling_ratio_ = TARGET_SAMPLE_RATE / source_sample_rate_;
            int error = 0;
            resampler_state_ = src_new(SRC_SINC_BEST_QUALITY, channels_, &error);
            if (!resampler_state_) { throw std::runtime_error(std::string("src_new failed: ") + src_strerror(error)); }
        }
        init_portaudio();
        reloadParameters();
        loading_thread_ = std::thread(&RealtimeAudioEngine::loading_thread_func, this);
    }
    ~RealtimeAudioEngine() {
        should_exit_ = true; loading_cv_.notify_all();
        if (loading_thread_.joinable()) { loading_thread_.join(); }
        if (stream_) { Pa_StopStream(stream_); Pa_CloseStream(stream_); }
        if (resampler_state_) { src_delete(resampler_state_); }
        Pa_Terminate();
    }
// ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↓修正開始◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
    void play() { 
        std::lock_guard<std::mutex> lock(state_mutex_); 
        if (playback_state_ != PlaybackState::PLAYING) { 
            if (playback_state_ == PlaybackState::FINISHED) { 
                seek_to_frame(0); 
            } 
            playback_state_ = PlaybackState::PLAYING; 
            loading_cv_.notify_all(); 
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
// ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↑修正終わり◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
    void pause() { std::lock_guard<std::mutex> lock(state_mutex_); if (playback_state_ == PlaybackState::PLAYING) { playback_state_ = PlaybackState::PAUSED; std::cout << "Playback paused." << std::endl; } }
    void stop() { { std::lock_guard<std::mutex> lock(state_mutex_); if (playback_state_ != PlaybackState::STOPPED) { playback_state_ = PlaybackState::STOPPED; std::cout << "Playback stopped." << std::endl; } } if (stream_ && Pa_IsStreamActive(stream_) == 1) { Pa_StopStream(stream_); } ring_buffer_->clear(); seek_to_frame(0); }
    void seek(double seconds) { long long target_frame = static_cast<long long>(seconds * source_sample_rate_); target_frame = std::max((long long)0, std::min(target_frame, total_frames_ - 1)); seek_to_frame(target_frame); std::cout << "Seek to " << seconds << " seconds." << std::endl; }
    void reloadParameters() { json new_params; try { std::filesystem::path exe_path(executable_path_); std::filesystem::path config_path = exe_path.parent_path() / "params.json"; std::ifstream f(config_path); if (f.is_open()) { new_params = json::parse(f, nullptr, false); if (new_params.is_discarded()) { std::cerr << "Warning: Invalid JSON, keeping previous settings." << std::endl; return; } } else { std::cerr << "Warning: Could not open params.json, keeping previous settings." << std::endl; return; } } catch (const std::exception& e) { std::cerr << "Warning: Failed to load params.json: " << e.what() << std::endl; return; } params_ = new_params; effect_chain_.setup(params_, channels_, TARGET_SAMPLE_RATE); }
    bool isPlaying() const { std::lock_guard<std::mutex> lock(state_mutex_); return playback_state_ == PlaybackState::PLAYING; }
private:
    std::unique_ptr<AudioDecoder> decoder_;
    std::unique_ptr<RingBuffer<float>> ring_buffer_;
    EffectChain effect_chain_;
    json params_;
    std::string executable_path_;
    int channels_;
    double source_sample_rate_ = 0.0;
    long long total_frames_ = 0;
    PaStream* stream_ = nullptr;
    std::atomic<PlaybackState> playback_state_{PlaybackState::STOPPED};
    mutable std::mutex state_mutex_;
    SRC_STATE* resampler_state_ = nullptr;
    double resampling_ratio_ = 1.0;
    std::thread loading_thread_;
    std::atomic<bool> should_exit_{false};
// ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↓修正開始◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
    std::atomic<bool> end_of_input_{false};
// ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↑修正終わり◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
    std::condition_variable loading_cv_;
    std::mutex loading_mutex_;
    
    void init_portaudio() { PaError err = Pa_Initialize(); if (err != paNoError) { throw std::runtime_error(std::string("PortAudio init failed: ") + Pa_GetErrorText(err)); } PaStreamParameters output_parameters; output_parameters.device = Pa_GetDefaultOutputDevice(); if (output_parameters.device == paNoDevice) { throw std::runtime_error("No default audio output device found."); } const PaDeviceInfo* device_info = Pa_GetDeviceInfo(output_parameters.device); if (!device_info) { throw std::runtime_error("Failed to get device info"); } output_parameters.channelCount = channels_; output_parameters.sampleFormat = paFloat32; output_parameters.suggestedLatency = device_info->defaultLowOutputLatency; output_parameters.hostApiSpecificStreamInfo = nullptr; err = Pa_OpenStream(&stream_, nullptr, &output_parameters, TARGET_SAMPLE_RATE, paFramesPerBufferUnspecified, paClipOff, audioCallback, this); if (err != paNoError) { throw std::runtime_error(std::string("Failed to open audio stream: ") + Pa_GetErrorText(err)); } }
// ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↓修正開始◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
    void seek_to_frame(long long frame) { 
        decoder_->seek(frame); 
        ring_buffer_->clear(); 
        if (resampler_state_) { src_reset(resampler_state_); } 
        effect_chain_.reset(); 
        end_of_input_ = false;
        { 
            std::lock_guard<std::mutex> lock(state_mutex_); 
            if(playback_state_ == PlaybackState::FINISHED) playback_state_ = PlaybackState::PAUSED; 
        }
        loading_cv_.notify_all();
    }
    
    void loading_thread_func() {
        std::vector<float> read_buffer(FRAMES_PER_BUFFER * channels_);
        std::vector<float> resampled_buffer(static_cast<size_t>(FRAMES_PER_BUFFER * channels_ * resampling_ratio_ * 2.0));

        while (!should_exit_) {
            {
                std::unique_lock<std::mutex> lock(loading_mutex_);
                loading_cv_.wait(lock, [this] {
                    return should_exit_ || (playback_state_ == PlaybackState::PLAYING && ring_buffer_->available_write_frames() >= FRAMES_PER_BUFFER && !end_of_input_);
                });
            }

            if (should_exit_) break;
            if (playback_state_ != PlaybackState::PLAYING || end_of_input_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            size_t frames_read = decoder_->read(read_buffer.data(), FRAMES_PER_BUFFER);
            if (frames_read == 0) {
                end_of_input_ = true;
                continue;
            }

            if (resampler_state_) {
                SRC_DATA src_data;
                src_data.data_in = read_buffer.data();
                src_data.input_frames = frames_read;
                src_data.data_out = resampled_buffer.data();
                src_data.output_frames = resampled_buffer.size() / channels_;
                src_data.src_ratio = resampling_ratio_;
                src_data.end_of_input = (frames_read < FRAMES_PER_BUFFER);
                if (src_process(resampler_state_, &src_data) != 0) { continue; }
                ring_buffer_->push(resampled_buffer.data(), src_data.output_frames_gen);
            } else {
                ring_buffer_->push(read_buffer.data(), frames_read);
            }
        }
    }
// ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↑修正終わり◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️

    int processAudio(float* output_buffer, unsigned long frames_per_buffer) {
        if (playback_state_ == PlaybackState::PAUSED || playback_state_ == PlaybackState::STOPPED) {
            std::fill(output_buffer, output_buffer + (frames_per_buffer * channels_), 0.0f);
            return paContinue;
        }

        size_t frames_read = ring_buffer_->pop(output_buffer, frames_per_buffer);

        if (frames_read < frames_per_buffer) {
            std::fill(output_buffer + (frames_read * channels_), output_buffer + (frames_per_buffer * channels_), 0.0f);
// ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↓修正開始◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
            if (end_of_input_ && ring_buffer_->available_read_frames() == 0) {
                 std::lock_guard<std::mutex> lock(state_mutex_);
                 if (playback_state_ == PlaybackState::PLAYING) {
                    playback_state_ = PlaybackState::FINISHED;
                    std::cout << "Playback finished." << std::endl;
                 }
            }
        }
// ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↑修正終わり◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️

        if(frames_read > 0) {
            std::vector<float> processing_vec(output_buffer, output_buffer + (frames_read * channels_));
            effect_chain_.process(processing_vec);
            std::copy(processing_vec.begin(), processing_vec.end(), output_buffer);
        }
        
        if (playback_state_ == PlaybackState::PLAYING) {
            loading_cv_.notify_one();
        }

        return paContinue;
    }
    
    static int audioCallback(const void*, void* out, unsigned long frames, const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* data) {
        return static_cast<RealtimeAudioEngine*>(data)->processAudio(static_cast<float*>(out), frames);
    }
};

void print_help() { std::cout << "Commands: play, pause, stop, reload, seek <sec>, exit, help\n"; }

int main(int argc, char* argv[]) {
    if (argc < 2) { std::cerr << "Usage: " << argv[0] << " <input_audio_file> [start_seconds]" << std::endl; return 1; }
    double start_time = 0.0;
    if (argc >= 3) { try { start_time = std::stod(argv[2]); } catch (const std::exception&) { std::cerr << "Invalid start time: " << argv[2] << std::endl; return 1; } }
    std::cout << "=== Real-Time Sound Enhancer Engine v4.5 (Playback Fix) ===" << std::endl;
    try {
        RealtimeAudioEngine engine(argv[1], argv[0]);
        if (start_time > 0.0) { engine.seek(start_time); }
        print_help();
        engine.play();
        std::string line;
        while (true) {
            std::cout << "> ";
            if (!std::getline(std::cin, line)) { if (engine.isPlaying()) engine.stop(); break; }
            std::stringstream ss(line);
            std::string command;
            ss >> command;
            if (command == "exit" || command == "quit") { if (engine.isPlaying()) engine.stop(); break; }
            try {
                if (command == "play") { engine.play(); } 
                else if (command == "pause") { engine.pause(); } 
                else if (command == "stop") { engine.stop(); } 
                else if (command == "reload") { engine.reloadParameters(); }
                else if (command == "seek") { double sec; if (ss >> sec) { engine.seek(sec); } else { std::cout << "Usage: seek <seconds>\n"; } } 
                else if (command == "help") { print_help(); } 
                else if (!command.empty()) { std::cout << "Unknown command: '" << command << "'. Type 'help' for available commands.\n"; }
            } catch (const std::exception& e) { std::cerr << "Command error: " << e.what() << std::endl; }
        }
    } catch (const std::exception& e) { std::cerr << "\nFatal error: " << e.what() << std::endl; return 1; }
    std::cout << "\nGoodbye!" << std::endl;
    return 0;
}
