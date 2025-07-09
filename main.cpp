// ./main.cpp - Final Corrected Version with Re-engineered Audio Engine and Enhanced Logging
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

// ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↓修正開始◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
#include "AudioDecoderFactory.h"
// ◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️↑修正終わり◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️◾️
#include "AudioEffectFactory.h"
#include "vocal_instrument_separator.h"
#include "advanced_dynamics.h"
#include "advanced_eq_harmonics.h"
#include "spatial_processing.h"
#include "custom_effects.h"

using json = nlohmann::json;

// --- 定数定義 ---
const double TARGET_SAMPLE_RATE = 48000.0;
const unsigned int PROCESSING_BLOCK_SIZE = 512;
const size_t RING_BUFFER_FRAMES = 8192;

// --- ログ出力用マクロ ---
#define LOG_INFO(msg) std::cout << "[INFO] " << msg << std::endl
#define LOG_WARN(msg) std::cerr << "[WARN] " << msg << std::endl
#define LOG_ERROR(msg) std::cerr << "[ERROR] " << msg << std::endl

// --- リングバッファクラス ---
template<typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t frame_count, size_t channels) :
        buffer_(frame_count * channels), size_(frame_count * channels), channels_(channels) {}

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
        std::lock_guard<std::mutex> lock(mutex_);
        return available_read_samples() / channels_;
    }
    size_t available_write_frames() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return available_write_samples() / channels_;
    }
    void clear() { std::lock_guard<std::mutex> lock(mutex_); read_pos_ = write_pos_ = 0; }

private:
    size_t available_read_samples() const {
        if (write_pos_ >= read_pos_) return write_pos_ - read_pos_;
        else return size_ - read_pos_ + write_pos_;
    }
    size_t available_write_samples() const { return size_ - available_read_samples() - 1; }

    std::vector<T> buffer_;
    size_t size_;
    size_t channels_;
    std::atomic<size_t> read_pos_{0};
    std::atomic<size_t> write_pos_{0};
    mutable std::mutex mutex_;
};

// --- エフェクトチェーンクラス ---
class EffectChain {
public:
    void setup(const json& params, int channels, double sr) {
        std::lock_guard<std::mutex> lock(mutex_);
        channels_ = channels;
        sample_rate_ = sr;

        effects_.clear();
        LOG_INFO("Building effect chain...");

        if (params.contains("effect_chain_order") && params["effect_chain_order"].is_array()) {
            const auto& order = params["effect_chain_order"];
            for (const auto& effect_key_json : order) {
                if (effect_key_json.is_string()) {
                    std::string effect_key = effect_key_json.get<std::string>();

                    auto effect = AudioEffectFactory::getInstance().createEffect(effect_key);

                    if (effect) {
                        effect->setup(sample_rate_, params.value(effect_key, json({})));
                        LOG_INFO("  -> Loaded: " << effect->getName());
                        effects_.push_back(std::move(effect));
                    } else {
                        LOG_WARN("  -> Unknown effect key '" << effect_key << "' in effect_chain_order. Skipping.");
                    }
                }
            }
        } else {
            LOG_WARN("'effect_chain_order' not found or not an array in params.json. No effects will be loaded.");
        }
        LOG_INFO("Effect chain built.");
    }

    void process(std::vector<float>& block) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (block.empty() || channels_ == 0) return;

        for (auto& effect : effects_) {
            effect->process(block, channels_);
        }
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& effect : effects_) {
            effect->reset();
        }
    }
private:
    int channels_ = 0;
    double sample_rate_ = 0.0;
    std::vector<std::unique_ptr<AudioEffect>> effects_;
    mutable std::mutex mutex_;
};

// --- オーディオエンジンクラス ---
class RealtimeAudioEngine {
public:
    enum class PlaybackState { STOPPED, PLAYING, PAUSED, FINISHED };
    RealtimeAudioEngine(const std::string& audio_file_path, const std::string& executable_path)
        : executable_path_(executable_path) {
        LOG_INFO("Initializing RealtimeAudioEngine...");
        decoder_ = AudioDecoderFactory::createDecoder(audio_file_path);
        if (!decoder_) throw std::runtime_error("Failed to create a suitable decoder.");

        const AudioInfo info = decoder_->getInfo();
        channels_ = info.channels;
        source_sample_rate_ = static_cast<double>(info.sampleRate);
        total_frames_ = info.totalFrames;

        LOG_INFO("Audio file properties: " << channels_ << " channels, " << source_sample_rate_ << " Hz, " << total_frames_ << " frames.");
        if (channels_ <= 0 || source_sample_rate_ <= 0) throw std::runtime_error("Invalid audio file properties.");

        processed_ring_buffer_ = std::make_unique<RingBuffer<float>>(RING_BUFFER_FRAMES, channels_);

        if (source_sample_rate_ != TARGET_SAMPLE_RATE) {
            LOG_INFO("Resampling required: " << source_sample_rate_ << " Hz -> " << TARGET_SAMPLE_RATE << " Hz");
            resampling_ratio_ = TARGET_SAMPLE_RATE / source_sample_rate_;
            int error = 0;
            resampler_state_ = src_new(SRC_SINC_BEST_QUALITY, channels_, &error);
            if (!resampler_state_) throw std::runtime_error(std::string("src_new failed: ") + src_strerror(error));
        }

        init_portaudio();
        reloadParameters();
        processing_thread_ = std::thread(&RealtimeAudioEngine::processing_thread_func, this);
    }

    ~RealtimeAudioEngine() {
        LOG_INFO("Shutting down RealtimeAudioEngine...");
        should_exit_ = true;
        processing_cv_.notify_all();
        if (processing_thread_.joinable()) processing_thread_.join();
        if (stream_) { Pa_StopStream(stream_); Pa_CloseStream(stream_); }
        if (resampler_state_) src_delete(resampler_state_);
        Pa_Terminate();
        LOG_INFO("Shutdown complete.");
    }

    void play() {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (playback_state_ != PlaybackState::PLAYING) {
            if (playback_state_ == PlaybackState::FINISHED) {
                LOG_INFO("Playback finished. Resetting to beginning.");
                seek_to_frame(0);
            }
            playback_state_ = PlaybackState::PLAYING;
            processing_cv_.notify_all();

            LOG_INFO("Attempting to start audio stream...");
            if (Pa_IsStreamStopped(stream_) == 1) {
                PaError err = Pa_StartStream(stream_);
                if (err != paNoError) {
                    LOG_ERROR("Failed to start PortAudio stream: " << Pa_GetErrorText(err));
                    playback_state_ = PlaybackState::STOPPED;
                } else {
                    LOG_INFO("Audio stream started successfully.");
                }
            } else {
                 LOG_INFO("Audio stream is already active.");
            }
        }
    }
    void pause() { std::lock_guard<std::mutex> lock(state_mutex_); if (playback_state_ == PlaybackState::PLAYING) { playback_state_ = PlaybackState::PAUSED; LOG_INFO("Playback paused."); } }
    void stop() { { std::lock_guard<std::mutex> lock(state_mutex_); if (playback_state_ != PlaybackState::STOPPED) { playback_state_ = PlaybackState::STOPPED; LOG_INFO("Playback stopped."); } } if (stream_ && Pa_IsStreamActive(stream_) == 1) { Pa_StopStream(stream_); } seek_to_frame(0); }
    void seek(double seconds) { long long target_frame = static_cast<long long>(seconds * source_sample_rate_); target_frame = std::max((long long)0, std::min(target_frame, total_frames_ - 1)); LOG_INFO("Seeking to " << seconds << "s (frame " << target_frame << ")"); seek_to_frame(target_frame); }
    void reloadParameters() {
        json new_params;
        try {
            std::filesystem::path exe_path(executable_path_);
            std::filesystem::path config_path = exe_path.parent_path() / "params.json";
            LOG_INFO("Loading parameters from: " << config_path);
            std::ifstream f(config_path);
            if (f.is_open()) new_params = json::parse(f, nullptr, false);
            else { LOG_WARN("Could not open params.json. Using defaults."); }
        } catch (const std::exception& e) { LOG_WARN("Failed to load or parse params.json: " << e.what()); return; }

        std::lock_guard<std::mutex> lock(processing_mutex_);
        params_ = new_params;
        effect_chain_.setup(params_, channels_, TARGET_SAMPLE_RATE);
    }
    bool isPlaying() const { std::lock_guard<std::mutex> lock(state_mutex_); return playback_state_ == PlaybackState::PLAYING; }

private:
    std::unique_ptr<AudioDecoder> decoder_;
    std::unique_ptr<RingBuffer<float>> processed_ring_buffer_;
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
    std::thread processing_thread_;
    std::atomic<bool> should_exit_{false};
    std::atomic<bool> end_of_input_{false};
    std::condition_variable processing_cv_;
    std::mutex processing_mutex_;

    void init_portaudio();
    void seek_to_frame(long long frame);
    void processing_thread_func();
    int audioCallback(float* output_buffer, unsigned long frames_per_buffer);
    static int paCallback(const void*, void* out, unsigned long frames, const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* data) {
        return static_cast<RealtimeAudioEngine*>(data)->audioCallback(static_cast<float*>(out), frames);
    }
};

void RealtimeAudioEngine::init_portaudio() {
    LOG_INFO("Initializing PortAudio...");
    if (Pa_Initialize() != paNoError) throw std::runtime_error("PortAudio init failed.");
    PaStreamParameters output_parameters;
    output_parameters.device = Pa_GetDefaultOutputDevice();
    if (output_parameters.device == paNoDevice) throw std::runtime_error("No default audio output device.");
    const PaDeviceInfo* device_info = Pa_GetDeviceInfo(output_parameters.device);
    LOG_INFO("Using output device: " << device_info->name);
    output_parameters.channelCount = channels_;
    output_parameters.sampleFormat = paFloat32;
    output_parameters.suggestedLatency = device_info->defaultLowOutputLatency;
    output_parameters.hostApiSpecificStreamInfo = nullptr;

    LOG_INFO("Opening PortAudio stream with " << channels_ << " channels at " << TARGET_SAMPLE_RATE << " Hz.");
    PaError err = Pa_OpenStream(&stream_, nullptr, &output_parameters, TARGET_SAMPLE_RATE, paFramesPerBufferUnspecified, paClipOff, paCallback, this);
    if (err != paNoError) {
        throw std::runtime_error("Failed to open audio stream: " + std::string(Pa_GetErrorText(err)));
    }
}

void RealtimeAudioEngine::seek_to_frame(long long frame) {
    std::lock_guard<std::mutex> lock(processing_mutex_);
    LOG_INFO("Seeking decoder to frame " << frame);
    decoder_->seek(frame);
    processed_ring_buffer_->clear();
    if (resampler_state_) src_reset(resampler_state_);
    effect_chain_.reset();
    end_of_input_ = false;

    {
        std::lock_guard<std::mutex> state_lock(state_mutex_);
        if(playback_state_ == PlaybackState::FINISHED) playback_state_ = PlaybackState::STOPPED;
    }
    processing_cv_.notify_all();
}

void RealtimeAudioEngine::processing_thread_func() {
    LOG_INFO("Processing thread started.");
    std::vector<float> read_buffer(PROCESSING_BLOCK_SIZE * channels_);

    const size_t resampled_buffer_max_frames = static_cast<size_t>(ceil(PROCESSING_BLOCK_SIZE * (resampling_ratio_ > 1.0 ? resampling_ratio_ : 1.0)));
    std::vector<float> resampled_buffer(resampled_buffer_max_frames * channels_);

    while (!should_exit_) {
        {
            std::unique_lock<std::mutex> lock(processing_mutex_);
            processing_cv_.wait(lock, [this, resampled_buffer_max_frames] {
                const size_t required_frames = resampler_state_ ? resampled_buffer_max_frames : PROCESSING_BLOCK_SIZE;
                bool has_enough_space = processed_ring_buffer_->available_write_frames() >= required_frames;

                bool should_run = (playback_state_ == PlaybackState::PLAYING &&
                                   has_enough_space &&
                                   !end_of_input_);
                return should_exit_ || should_run;
            });

            if (should_exit_) break;

            if (playback_state_ == PlaybackState::PLAYING && !end_of_input_) {
                size_t frames_read = decoder_->read(read_buffer.data(), PROCESSING_BLOCK_SIZE);
                if (frames_read == 0) {
                    if (!end_of_input_) {
                        LOG_INFO("End of input file reached.");
                        end_of_input_ = true;
                    }
                    continue;
                }

                std::vector<float> block_to_process;
                size_t frames_to_process = 0;

                if(resampler_state_) {
                    SRC_DATA src_data;
                    src_data.data_in = read_buffer.data();
                    src_data.input_frames = frames_read;
                    src_data.data_out = resampled_buffer.data();
                    src_data.output_frames = resampled_buffer.size() / channels_;
                    src_data.src_ratio = resampling_ratio_;
                    src_data.end_of_input = (frames_read < PROCESSING_BLOCK_SIZE);

                    if (src_process(resampler_state_, &src_data) == 0) {
                        frames_to_process = src_data.output_frames_gen;
                        block_to_process.assign(resampled_buffer.data(), resampled_buffer.data() + frames_to_process * channels_);
                    }
                } else {
                    frames_to_process = frames_read;
                    block_to_process.assign(read_buffer.data(), read_buffer.data() + frames_to_process * channels_);
                }

                if(frames_to_process > 0) {
                    effect_chain_.process(block_to_process);
                    if(!processed_ring_buffer_->push(block_to_process.data(), frames_to_process)) {
                        LOG_WARN("Ring buffer push failed (overflow).");
                    }
                }
            }
        }

        if(playback_state_ != PlaybackState::PLAYING) {
             std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    LOG_INFO("Processing thread finished.");
}

int RealtimeAudioEngine::audioCallback(float* output_buffer, unsigned long frames_per_buffer) {
    size_t frames_popped = processed_ring_buffer_->pop(output_buffer, frames_per_buffer);

    if (frames_popped < frames_per_buffer) {
        std::fill_n(output_buffer + frames_popped * channels_, (frames_per_buffer - frames_popped) * channels_, 0.0f);

        std::lock_guard<std::mutex> lock(state_mutex_);
        if (end_of_input_ && processed_ring_buffer_->available_read_frames() == 0 && playback_state_ == PlaybackState::PLAYING) {
            playback_state_ = PlaybackState::FINISHED;
            LOG_INFO("Playback finished (callback).");
        }
    }

    if(playback_state_ == PlaybackState::PLAYING) {
        processing_cv_.notify_one();
    }

    return paContinue;
}

// --- main関数とヘルパー ---
void print_help() { std::cout << "Commands: play, pause, stop, reload, seek <sec>, exit, help\n"; }

void registerAllEffects() {
    auto& factory = AudioEffectFactory::getInstance();
    // Dynamics
    factory.registerEffect<AnalogSaturation>("analog_saturation");
    factory.registerEffect<MasteringLimiter>("mastering_limiter");

    // EQ and Harmonics
    factory.registerEffect<HarmonicEnhancer>("harmonic_enhancer");
    factory.registerEffect<LinearPhaseEQ>("linear_phase_eq");
    factory.registerEffect<ParametricEQ>("parametric_eq");
    factory.registerEffect<SpectralGate>("spectral_gate");

    // Spatial and Separation
    factory.registerEffect<MSVocalInstrumentSeparator>("ms_separator");
    factory.registerEffect<StereoEnhancer>("stereo_enhancer");

    // Custom Enhancement
    factory.registerEffect<Exciter>("exciter");
    factory.registerEffect<GlossEnhancer>("gloss_enhancer");
}

int main(int argc, char* argv[]) {
    if (argc < 2) { std::cerr << "Usage: " << argv[0] << " <audio_file> [start_sec]" << std::endl; return 1; }

    LOG_INFO("Application starting...");
    registerAllEffects();

    try {
        RealtimeAudioEngine engine(argv[1], argv[0]);
        if (argc >= 3) {
            try {
                engine.seek(std::stod(argv[2]));
            } catch (const std::invalid_argument&) {
                LOG_WARN("Invalid start time provided. Starting from beginning.");
            }
        }

        print_help();
        engine.play();

        for (std::string line; std::cout << "> " && std::getline(std::cin, line);) {
            std::stringstream ss(line);
            std::string command;
            ss >> command;
            if (command == "exit" || command == "quit") break;

            try {
                if (command == "play") engine.play();
                else if (command == "pause") engine.pause();
                else if (command == "stop") engine.stop();
                else if (command == "reload") {
                    LOG_INFO("Reloading parameters and rebuilding effect chain...");
                    engine.reloadParameters();
                }
                else if (command == "seek") {
                    double sec;
                    if (ss >> sec) engine.seek(sec);
                    else std::cout << "Usage: seek <seconds>\n";
                }
                else if (command == "help") print_help();
                else if (!command.empty()) std::cout << "Unknown command: '" << command << "'\n";
            } catch (const std::exception& e) {
                LOG_ERROR("Command error: " << e.what());
            }
        }
        engine.stop();
    } catch (const std::exception& e) { LOG_ERROR("\nFatal error: " << e.what()); return 1; }
    LOG_INFO("\nGoodbye!");
    return 0;
}