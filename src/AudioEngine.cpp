#include "AudioEngine.h"

#include "Waveform.h"

#include <gst/gst.h>
#include <glib.h>
#include <glibmm/main.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <sstream>
#include <utility>

namespace fs = std::filesystem;

namespace pulsepad {

static bool playback_speed_is_zero(float v) {
    return std::abs(v) < 0.0001f;
}

static GstClockTime seconds_to_gst_time(double seconds) {
    if (seconds <= 0.0) return 0;
    return static_cast<GstClockTime>(seconds * static_cast<double>(GST_SECOND));
}

static float clamp_duck_db(float v) {
    return clampf(v, -36.0f, 0.0f);
}

static std::string filename_to_uri(const fs::path& file) {
    GError* error = nullptr;
    gchar* uri = g_filename_to_uri(file.string().c_str(), nullptr, &error);
    if (!uri) {
        if (error) g_error_free(error);
        return {};
    }
    std::string result(uri);
    g_free(uri);
    return result;
}

class AudioEngine::Impl {
public:
    struct PlayerHandle {
        int key = -1;
        GstElement* playbin = nullptr;
        GstElement* audioSink = nullptr;
        GstElement* ampElement = nullptr;
        GstElement* panElement = nullptr;
        PlaybackMode mode = PlaybackMode::PlayThrough;
        std::string filePath;
        bool reportedRuntimeIssue = false;
        std::string groupName;
        float buttonVolume = 1.0f;
        float duckDb = 0.0f;
        int duckFadeMs = 0;
        float speed = 1.0f;
        double trimStart = 0.0;
        double trimEnd = 0.0;
        int fadeInMs = 0;
        int fadeOutMs = 0;
        bool stopping = false;
        guint busWatchId = 0;
    };

    ~Impl() { stop_all(); }

    std::vector<pulsepad::PlayingInfo> currently_playing() const {
        std::vector<pulsepad::PlayingInfo> out;
        for (const auto& p : players) {
            if (!p || !p->playbin) continue;
            gint64 pos = 0;
            double seconds = 0.0;
            if (gst_element_query_position(p->playbin, GST_FORMAT_TIME, &pos) && pos >= 0) {
                seconds = static_cast<double>(pos) / static_cast<double>(GST_SECOND);
            }
            out.push_back({p->key, p->mode, p->groupName, p->buttonVolume, p->duckDb, p->speed, p->trimStart, p->trimEnd, seconds, p->stopping});
        }
        return out;
    }

    std::optional<double> position_for_key(int key) const {
        for (const auto& p : players) {
            if (!p || !p->playbin || p->key != key) continue;
            gint64 pos = 0;
            if (gst_element_query_position(p->playbin, GST_FORMAT_TIME, &pos) && pos >= 0) {
                return static_cast<double>(pos) / static_cast<double>(GST_SECOND);
            }
        }
        return std::nullopt;
    }

    void play(int key, const fs::path& file, float volume, float speed, float pitch, PlaybackMode mode, float pan, const std::string& groupName, double trimStart, double trimEnd, int fadeInMs, int fadeOutMs, std::function<void(std::string)> onFailure) {
        if (std::abs(clampf(pitch, 0.5f, 2.0f) - 1.0f) > 0.001f) {
            onFailure("Playback pitch is preserved for config compatibility but is not supported at runtime; playing at normal pitch");
        }
        if (!fs::exists(file)) {
            onFailure("Missing stored audio asset");
            return;
        }
        if (mode == PlaybackMode::Retrigger) stop_key(key);

        auto handle = std::make_shared<PlayerHandle>();
        handle->key = key;
        handle->mode = mode;
        handle->filePath = file.string();
        handle->groupName = normalize_group_name(groupName);
        handle->buttonVolume = clamp_pad_volume(volume);
        handle->speed = clamp_playback_speed(speed);
        handle->trimStart = clamp_time_seconds(trimStart);
        handle->trimEnd = clamp_time_seconds(trimEnd);
        handle->fadeInMs = clamp_fade_ms(fadeInMs);
        handle->fadeOutMs = clamp_fade_ms(fadeOutMs);
        if (handle->trimEnd > 0.0 && handle->trimEnd <= handle->trimStart) handle->trimEnd = 0.0;
        handle->playbin = gst_element_factory_make("playbin", nullptr);
        if (!handle->playbin) {
            onFailure("Could not create GStreamer playbin");
            return;
        }
        GError* sinkError = nullptr;
        GstElement* audioSink = gst_parse_bin_from_description(
            "audioconvert ! audioamplify name=padamp amplification=1.0 clipping-method=soft-clip ! audiopanorama name=padpan ! autoaudiosink",
            TRUE, &sinkError);
        if (!audioSink) {
            if (sinkError) { g_error_free(sinkError); sinkError = nullptr; }
            audioSink = gst_parse_bin_from_description(
                "audioconvert ! audioamplify name=padamp amplification=1.0 ! audiopanorama name=padpan ! autoaudiosink",
                TRUE, &sinkError);
        }
        if (!audioSink) {
            if (sinkError) { g_error_free(sinkError); sinkError = nullptr; }
            audioSink = gst_parse_bin_from_description(
                "audioconvert ! audiopanorama name=padpan ! autoaudiosink", TRUE, &sinkError);
        }
        if (!audioSink) {
            std::string msg = sinkError && sinkError->message ? sinkError->message : "unknown error";
            if (sinkError) g_error_free(sinkError);
            gst_object_unref(handle->playbin);
            handle->playbin = nullptr;
            onFailure("Could not create audio sink: " + msg);
            return;
        }
        gst_object_ref_sink(audioSink);
        handle->audioSink = audioSink;
        handle->ampElement = gst_bin_get_by_name(GST_BIN(audioSink), "padamp");
        handle->panElement = gst_bin_get_by_name(GST_BIN(audioSink), "padpan");
        if (handle->panElement) g_object_set(handle->panElement, "panorama", clamp_pan(pan), nullptr);

        const auto uri = filename_to_uri(file);
        if (uri.empty()) {
            gst_object_unref(handle->playbin);
            handle->playbin = nullptr;
            onFailure("Could not convert audio path to URI");
            return;
        }
        float initialVolume = handle->fadeInMs > 0 ? 0.0f : static_cast<float>(effective_volume(handle->buttonVolume));
        g_object_set(handle->playbin, "uri", uri.c_str(), "volume", handle->ampElement ? 1.0 : initialVolume, "audio-sink", audioSink, nullptr);
        set_handle_gain(handle, initialVolume);
        GstBus* bus = gst_element_get_bus(handle->playbin);
        if (!bus) {
            gst_object_unref(handle->playbin);
            handle->playbin = nullptr;
            onFailure("Audio playback failed: GStreamer did not provide a message bus for the playback pipeline.");
            return;
        }
        handle->busWatchId = gst_bus_add_watch_full(bus, G_PRIORITY_DEFAULT, &AudioEngine::Impl::bus_callback, new CallbackData{this, handle, onFailure}, &AudioEngine::Impl::destroy_callback_data);
        gst_object_unref(bus);
        players.push_back(handle);
        if (mode == PlaybackMode::Retrigger) retriggerPlayers[key] = handle;

        const bool needsInitialSeek = handle->mode == PlaybackMode::Loop || handle->speed != 1.0f || handle->trimStart > 0.0 || handle->trimEnd > 0.0;

        // Do not let the pipeline audibly start at position 0 and then seek a moment later.
        // That made one click sound like two quick triggers whenever trim/rate logic queued
        // an initial seek.  Preroll paused, then apply the seek; apply_rate() switches to
        // PLAYING after the seek is issued.
        const GstState targetState = (playback_speed_is_zero(handle->speed) || needsInitialSeek) ? GST_STATE_PAUSED : GST_STATE_PLAYING;
        const GstStateChangeReturn stateResult = gst_element_set_state(handle->playbin, targetState);
        if (stateResult == GST_STATE_CHANGE_FAILURE) {
            onFailure("Audio playback failed: GStreamer could not start the playback pipeline. Check your audio device and installed GStreamer plugins.");
            finish_handle(handle);
            return;
        }
        if (needsInitialSeek) {
            // playbin often cannot accept a rate/segment seek until it has prerolled.
            auto weakHandle = std::weak_ptr<PlayerHandle>(handle);
            Glib::signal_timeout().connect_once([weakHandle, this]() {
                if (auto h = weakHandle.lock()) {
                    AudioEngine::Impl::apply_rate(h, static_cast<gint64>(-1), true);
                    start_fade_in(h);
                }
            }, 150);
        } else {
            start_fade_in(handle);
        }
    }

    void stop_all() {
        auto copy = players;
        for (auto& p : copy) stop_handle(p, false);
    }

    void stop_all_with_fade() {
        auto copy = players;
        for (auto& p : copy) stop_handle(p, true);
    }

    void stop_key_with_fade(int key) {
        stop_key(key, true);
    }

    void stop_key_with_fade_ms(int key, int fadeMs) {
        auto copy = players;
        for (auto& p : copy) {
            if (p && p->key == key) {
                p->fadeOutMs = clamp_fade_ms(fadeMs);
                stop_handle(p, true);
            }
        }
    }

    void stop_key_immediate(int key) {
        stop_key(key, false);
    }

    void set_master_volume(float value) {
        masterVolume = clampf(value, 0.0f, 1.0f);
        for (auto& p : players) set_handle_gain(p, effective_volume(p->buttonVolume, p->duckDb));
    }

    float master_volume() const { return masterVolume; }

    void set_button_volume(int key, float value) {
        float v = clamp_pad_volume(value);
        for (auto& p : players) {
            if (p->key == key) {
                p->buttonVolume = v;
                set_handle_gain(p, effective_volume(v, p->duckDb));
            }
        }
    }

    void set_playback_speed(int key, float value) {
        float rate = clamp_playback_speed(value);
        for (auto& p : players) {
            if (p->key == key) {
                p->speed = rate;
                gint64 pos = 0;
                gst_element_query_position(p->playbin, GST_FORMAT_TIME, &pos);
                apply_rate(p, pos, true);
            }
        }
    }

    void set_playback_pitch(int key, float value) { (void)key; (void)value; }

    void set_button_pan(int key, float value) {
        float pan = clamp_pan(value);
        for (auto& p : players) {
            if (p->key == key && p->panElement) {
                g_object_set(p->panElement, "panorama", pan, nullptr);
            }
        }
    }

    void set_group_ducks(const std::map<std::string, float>& ducksDb, int fadeMs) {
        for (auto& p : players) {
            if (!p) continue;
            float target = 0.0f;
            auto it = ducksDb.find(p->groupName);
            if (it != ducksDb.end()) target = clamp_duck_db(it->second);
            if (std::abs(target - p->duckDb) < 0.01f) continue;
            start_duck_fade(p, target, clamp_fade_ms(fadeMs));
        }
    }

    bool is_key_playing(int key) const {
        for (const auto& p : players) {
            if (p && p->key == key && p->playbin && !p->stopping) return true;
        }
        return false;
    }

private:
    struct CallbackData {
        AudioEngine::Impl* engine;
        std::shared_ptr<PlayerHandle> handle;
        std::function<void(std::string)> onFailure;
    };

    float masterVolume = 1.0f;
    std::vector<std::shared_ptr<PlayerHandle>> players;
    std::map<int, std::shared_ptr<PlayerHandle>> retriggerPlayers;

    double effective_volume(float buttonVolume, float duckDb = 0.0f) const {
        double duckLinear = std::pow(10.0, clamp_duck_db(duckDb) / 20.0);
        return clampf(static_cast<float>(masterVolume * clamp_pad_volume(buttonVolume) * duckLinear), 0.0f, 4.0f);
    }

    void set_handle_gain(const std::shared_ptr<PlayerHandle>& handle, double gain) {
        if (!handle || !handle->playbin) return;
        gain = clampf(static_cast<float>(gain), 0.0f, 4.0f);
        if (handle->ampElement) g_object_set(handle->ampElement, "amplification", gain, nullptr);
        else g_object_set(handle->playbin, "volume", gain, nullptr);
    }

    void stop_key(int key, bool allowFade = false) {
        auto copy = players;
        for (auto& p : copy) {
            if (p && p->key == key) stop_handle(p, allowFade);
        }
    }

    void stop_handle(const std::shared_ptr<PlayerHandle>& handle, bool allowFade) {
        if (!handle || handle->stopping) return;
        if (allowFade && handle->fadeOutMs > 0 && handle->playbin) {
            handle->stopping = true;
            start_fade_out(handle);
            return;
        }
        finish_handle(handle);
    }

    void finish_handle(const std::shared_ptr<PlayerHandle>& handle) {
        // Remove from the active list before doing any potentially slow GStreamer
        // teardown.  This makes UI state update immediately when a mixer Stop
        // button is pressed, even if the backend takes a moment to release.
        players.erase(std::remove(players.begin(), players.end(), handle), players.end());
        auto it = retriggerPlayers.find(handle ? handle->key : -1);
        if (it != retriggerPlayers.end() && it->second == handle) retriggerPlayers.erase(it);
        release(handle);
    }

    void release(const std::shared_ptr<PlayerHandle>& handle) {
        if (!handle || !handle->playbin) return;

        GstElement* playbin = handle->playbin;
        GstElement* audioSink = handle->audioSink;
        GstElement* amp = handle->ampElement;
        GstElement* pan = handle->panElement;
        guint busWatchId = handle->busWatchId;

        handle->playbin = nullptr;
        handle->audioSink = nullptr;
        handle->ampElement = nullptr;
        handle->panElement = nullptr;
        handle->busWatchId = 0;

        // Manual stops must also remove the bus watch.  Otherwise its
        // CallbackData keeps a shared_ptr<PlayerHandle> alive after the player
        // has been removed from players/retriggerPlayers.
        if (busWatchId != 0) g_source_remove(busWatchId);

        if (amp) g_object_set(amp, "amplification", 0.0, nullptr);
        else g_object_set(playbin, "volume", 0.0, nullptr);
        g_object_set(playbin, "volume", 0.0, nullptr);
        gst_element_set_state(playbin, GST_STATE_NULL);

        if (amp) gst_object_unref(amp);
        if (pan) gst_object_unref(pan);

        // Detach the sink while our explicit sink ref is still alive.
        g_object_set(playbin, "audio-sink", nullptr, nullptr);
        gst_object_unref(playbin);
        if (audioSink) gst_object_unref(audioSink);
    }

    void start_duck_fade(const std::shared_ptr<PlayerHandle>& handle, float targetDb, int fadeMs) {
        if (!handle || !handle->playbin) return;
        targetDb = clamp_duck_db(targetDb);
        if (fadeMs <= 0) {
            handle->duckDb = targetDb;
            set_handle_gain(handle, effective_volume(handle->buttonVolume, handle->duckDb));
            return;
        }
        auto weakHandle = std::weak_ptr<PlayerHandle>(handle);
        const int intervalMs = 30;
        const int steps = std::max(1, fadeMs / intervalMs);
        float startDb = handle->duckDb;
        auto step = std::make_shared<int>(0);
        Glib::signal_timeout().connect([this, weakHandle, step, steps, startDb, targetDb]() mutable {
            auto h = weakHandle.lock();
            if (!h || !h->playbin) return false;
            ++(*step);
            float t = std::min(1.0f, static_cast<float>(*step) / static_cast<float>(steps));
            h->duckDb = startDb + (targetDb - startDb) * t;
            set_handle_gain(h, effective_volume(h->buttonVolume, h->duckDb));
            return *step < steps;
        }, intervalMs);
    }

    void start_fade_in(const std::shared_ptr<PlayerHandle>& handle) {
        if (!handle || !handle->playbin || handle->fadeInMs <= 0) return;
        auto weakHandle = std::weak_ptr<PlayerHandle>(handle);
        const int intervalMs = 30;
        const int steps = std::max(1, handle->fadeInMs / intervalMs);
        auto step = std::make_shared<int>(0);
        Glib::signal_timeout().connect([this, weakHandle, step, steps]() mutable {
            auto h = weakHandle.lock();
            if (!h || !h->playbin || h->stopping) return false;
            ++(*step);
            float t = std::min(1.0f, static_cast<float>(*step) / static_cast<float>(steps));
            set_handle_gain(h, effective_volume(h->buttonVolume, h->duckDb) * t);
            return *step < steps;
        }, intervalMs);
    }

    void start_fade_out(const std::shared_ptr<PlayerHandle>& handle) {
        if (!handle || !handle->playbin) return;
        auto weakHandle = std::weak_ptr<PlayerHandle>(handle);
        const int intervalMs = 30;
        const int steps = std::max(1, handle->fadeOutMs / intervalMs);
        auto step = std::make_shared<int>(0);
        Glib::signal_timeout().connect([this, weakHandle, step, steps]() mutable {
            auto h = weakHandle.lock();
            if (!h || !h->playbin) return false;
            ++(*step);
            float t = 1.0f - std::min(1.0f, static_cast<float>(*step) / static_cast<float>(steps));
            set_handle_gain(h, effective_volume(h->buttonVolume, h->duckDb) * t);
            if (*step >= steps) {
                finish_handle(h);
                return false;
            }
            return true;
        }, intervalMs);
    }

    static bool apply_rate(const std::shared_ptr<PlayerHandle>& handle, gint64 position, bool flush) {
        if (!handle || !handle->playbin) return false;

        if (playback_speed_is_zero(handle->speed)) {
            gst_element_set_state(handle->playbin, GST_STATE_PAUSED);
            return true;
        }

        GstSeekFlags flags = static_cast<GstSeekFlags>(GST_SEEK_FLAG_ACCURATE);
        if (flush) flags = static_cast<GstSeekFlags>(flags | GST_SEEK_FLAG_FLUSH);
        if (handle->mode == PlaybackMode::Loop) flags = static_cast<GstSeekFlags>(flags | GST_SEEK_FLAG_SEGMENT);

        const gint64 trimStart = static_cast<gint64>(seconds_to_gst_time(handle->trimStart));
        const gint64 trimEnd = handle->trimEnd > 0.0 ? static_cast<gint64>(seconds_to_gst_time(handle->trimEnd)) : -1;
        if (position < 0 || position < trimStart) position = trimStart;
        if (trimEnd >= 0 && position >= trimEnd) position = trimStart;
        if (gst_element_set_state(handle->playbin, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) return false;
        return gst_element_seek(handle->playbin, handle->speed, GST_FORMAT_TIME, flags,
                                GST_SEEK_TYPE_SET, position,
                                trimEnd < 0 ? GST_SEEK_TYPE_NONE : GST_SEEK_TYPE_SET, trimEnd);
    }

    static bool restart_loop_segment(const std::shared_ptr<PlayerHandle>& handle) {
        if (!handle || handle->stopping) return false;
        return apply_rate(handle, static_cast<gint64>(seconds_to_gst_time(handle->trimStart)), false);
    }

    static void destroy_callback_data(gpointer user_data) {
        delete static_cast<CallbackData*>(user_data);
    }

    static gboolean bus_callback(GstBus*, GstMessage* message, gpointer user_data) {
        auto* data = static_cast<CallbackData*>(user_data);
        auto* engine = data->engine;
        auto handle = data->handle;
        switch (GST_MESSAGE_TYPE(message)) {
            case GST_MESSAGE_SEGMENT_DONE:
                if (handle->mode == PlaybackMode::Loop) {
                    AudioEngine::Impl::restart_loop_segment(handle);
                    return TRUE;
                }
                return TRUE;
            case GST_MESSAGE_EOS:
                if (handle->mode == PlaybackMode::Loop) {
                    // Fallback for demuxers/decoders that do not post SEGMENT_DONE reliably.
                    AudioEngine::Impl::restart_loop_segment(handle);
                    return TRUE;
                }
                if (handle) handle->busWatchId = 0;
                engine->finish_handle(handle);
                return FALSE;
            case GST_MESSAGE_ERROR: {
                GError* err = nullptr;
                gchar* debug = nullptr;
                gst_message_parse_error(message, &err, &debug);
                std::string msg = err && err->message ? err->message : "unknown error";
                std::string detail = debug ? debug : "";
                if (err) g_error_free(err);
                if (debug) g_free(debug);
                std::string file = handle && !handle->filePath.empty() ? " for " + handle->filePath : std::string();
                if (!handle || !handle->reportedRuntimeIssue) {
                    if (handle) handle->reportedRuntimeIssue = true;
                    data->onFailure("Audio playback failed" + file + ": " + msg + (detail.empty() ? std::string() : " (" + detail + ")"));
                }
                if (handle) handle->busWatchId = 0;
                engine->finish_handle(handle);
                return FALSE;
            }
            case GST_MESSAGE_WARNING: {
                GError* err = nullptr;
                gchar* debug = nullptr;
                gst_message_parse_warning(message, &err, &debug);
                std::string msg = err && err->message ? err->message : "unknown warning";
                if (err) g_error_free(err);
                if (debug) g_free(debug);
                if (handle && !handle->reportedRuntimeIssue) {
                    handle->reportedRuntimeIssue = true;
                    std::string file = !handle->filePath.empty() ? " for " + handle->filePath : std::string();
                    data->onFailure("Audio playback warning" + file + ": " + msg);
                }
                return TRUE;
            }
            default:
                return TRUE;
        }
    }
};

bool AudioEngine::initialize(int* argc, char*** argv, std::string* errorMessage) {
    GError* error = nullptr;
    const gboolean ok = gst_init_check(argc, argv, &error);
    if (!ok) {
        if (errorMessage) {
            *errorMessage = "Unable to initialize GStreamer audio support.";
            if (error && error->message) *errorMessage += std::string(" ") + error->message;
            *errorMessage += " Install or repair GStreamer 1.0 and its audio plugins, then restart PulsePad.";
        }
        if (error) g_error_free(error);
        return false;
    }
    if (error) g_error_free(error);
    return true;
}

AudioRuntimeCapabilities AudioEngine::runtime_capabilities() {
    AudioRuntimeCapabilities caps;
    caps.ffmpegAvailable = ffmpeg_available();
    caps.ffprobeAvailable = ffprobe_available();
    if (auto* factory = gst_element_factory_find("audiopanorama")) {
        caps.panoramaAvailable = true;
        gst_object_unref(factory);
    }
    if (auto* factory = gst_element_factory_find("audioamplify")) {
        caps.audioAmplifyAvailable = true;
        gst_object_unref(factory);
    }
    return caps;
}

AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() = default;
AudioEngine::AudioEngine(AudioEngine&&) noexcept = default;
AudioEngine& AudioEngine::operator=(AudioEngine&&) noexcept = default;

void AudioEngine::play(int key, const std::filesystem::path& file, float volume, float speed, float pitch,
                       PlaybackMode mode, float pan, const std::string& groupName, double trimStart,
                       double trimEnd, int fadeInMs, int fadeOutMs,
                       std::function<void(std::string)> onFailure) {
    impl_->play(key, file, volume, speed, pitch, mode, pan, groupName, trimStart, trimEnd, fadeInMs, fadeOutMs, std::move(onFailure));
}

void AudioEngine::stop_all() { impl_->stop_all(); }
void AudioEngine::stop_all_with_fade() { impl_->stop_all_with_fade(); }
void AudioEngine::stop_key_with_fade(int key) { impl_->stop_key_with_fade(key); }
void AudioEngine::stop_key_with_fade_ms(int key, int fadeMs) { impl_->stop_key_with_fade_ms(key, fadeMs); }
void AudioEngine::stop_key_immediate(int key) { impl_->stop_key_immediate(key); }
void AudioEngine::set_master_volume(float volume) { impl_->set_master_volume(volume); }
float AudioEngine::master_volume() const { return impl_->master_volume(); }
void AudioEngine::set_button_volume(int key, float value) { impl_->set_button_volume(key, value); }
void AudioEngine::set_playback_speed(int key, float value) { impl_->set_playback_speed(key, value); }
void AudioEngine::set_playback_pitch(int key, float value) { impl_->set_playback_pitch(key, value); }
void AudioEngine::set_button_pan(int key, float value) { impl_->set_button_pan(key, value); }
void AudioEngine::set_group_ducks(const std::map<std::string, float>& ducksDb, int fadeMs) { impl_->set_group_ducks(ducksDb, fadeMs); }
bool AudioEngine::is_key_playing(int key) const { return impl_->is_key_playing(key); }
std::vector<PlayingInfo> AudioEngine::currently_playing() const { return impl_->currently_playing(); }
std::optional<double> AudioEngine::position_for_key(int key) const { return impl_->position_for_key(key); }

} // namespace pulsepad
