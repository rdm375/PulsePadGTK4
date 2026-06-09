#pragma once

#include "BoardConfig.h"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pulsepad {

struct PlaybackRequest {
    int key = -1;
    std::filesystem::path file;
    float volume = 1.0f;
    float speed = 1.0f;
    float pitch = 1.0f;
    PlaybackMode mode = PlaybackMode::PlayThrough;
    float pan = 0.0f;
    std::string groupName;
    double trimStart = 0.0;
    double trimEnd = 0.0;
    int fadeInMs = 0;
    int fadeOutMs = 0;
};


struct AudioRuntimeCapabilities {
    bool ffmpegAvailable = false;
    bool ffprobeAvailable = false;
    bool panoramaAvailable = false;
    bool audioAmplifyAvailable = false;
};

struct PlayingInfo {
    int key = -1;
    PlaybackMode mode = PlaybackMode::PlayThrough;
    std::string groupName;
    float volume = 1.0f;
    float duckDb = 0.0f;
    float speed = 1.0f;
    double trimStart = 0.0;
    double trimEnd = 0.0;
    double position = 0.0;
    bool stopping = false;
};

class AudioEngine {
public:
    static bool initialize(int* argc, char*** argv, std::string* errorMessage = nullptr);
    static AudioRuntimeCapabilities runtime_capabilities();
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    AudioEngine(AudioEngine&&) noexcept;
    AudioEngine& operator=(AudioEngine&&) noexcept;

    void play(int key, const std::filesystem::path& file, float volume, float speed, float pitch,
              PlaybackMode mode, float pan, const std::string& groupName, double trimStart,
              double trimEnd, int fadeInMs, int fadeOutMs,
              std::function<void(std::string)> onFailure);

    void stop_all();
    void stop_all_with_fade();
    void stop_key_with_fade(int key);
    void stop_key_with_fade_ms(int key, int fadeMs);
    void stop_key_immediate(int key);

    void set_master_volume(float volume);
    float master_volume() const;
    void set_button_volume(int key, float value);
    void set_playback_speed(int key, float value);
    void set_playback_pitch(int key, float value);
    void set_button_pan(int key, float value);
    void set_group_ducks(const std::map<std::string, float>& ducksDb, int fadeMs);

    bool is_key_playing(int key) const;
    std::vector<PlayingInfo> currently_playing() const;
    std::optional<double> position_for_key(int key) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pulsepad
