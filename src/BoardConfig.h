#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace pulsepad {

inline constexpr int MAX_PAD_GROUPS = 6;

enum class AppThemeMode { Light, Dark };
enum class PlaybackMode { PlayThrough, Loop, Retrigger };
enum class PlaybackDirection { Forward, Reverse };
enum class PadGroupType { Play, Exclusive };
enum class GroupTransition { Stop, Fade, Crossfade };

struct PadGroup {
    std::string name;
    PadGroupType type = PadGroupType::Play;
    std::string color = "default";
    GroupTransition transition = GroupTransition::Stop;
    int fadeOutMs = 500;
    int fadeInMs = 500;
    bool duckEnabled = false;
    std::set<std::string> duckTargets;
    float duckAmountDb = 12.0f;
    int duckAttackMs = 100;
    int duckReleaseMs = 750;
};

struct SoundButton {
    int id = 0;
    std::string label;
    std::string originalFilename;
    std::string storedFilename;
    std::string relativePathOrAssetReference;
    std::string mimeType;
    std::string color = "default";
    float volume = 1.0f;
    float playbackSpeed = 1.0f;
    float playbackPitch = 1.0f;
    float pan = 0.0f;
    double trimStart = 0.0;
    double trimEnd = 0.0;
    double durationSeconds = 0.0;
    int fadeInMs = 0;
    int fadeOutMs = 0;
    std::string exclusiveGroup;
    std::uint32_t hotkeyKeyval = 0;
    std::string hotkeyLabel;
    int midiChannel = -1;
    int midiNote = -1;
    PlaybackMode playbackMode = PlaybackMode::PlayThrough;
    PlaybackDirection playbackDirection = PlaybackDirection::Forward;
    bool assigned = false;
    bool userRenamed = false;
};

struct BoardState {
    std::vector<SoundButton> buttons;
    std::vector<PadGroup> groups;
    int gridRows = 3;
    int gridColumns = 3;
    float masterVolumeDb = 0.0f;
    AppThemeMode themeMode = AppThemeMode::Light;
    std::string status = "Ready";
    bool midiEnabled = true;
    std::string midiPortName;
    std::string lastMidiEvent = "None";
    int lastMidiChannel = -1;
    int lastMidiNote = -1;
    int lastMidiVelocity = 0;
};

float clampf(float v, float lo, float hi);
float clamp_playback_speed(float v);
float clamp_pad_volume(float v);
float clamp_pan(float v);
int clamp_fade_ms(int v);
float clamp_duck_amount_db(float v);
double clamp_time_seconds(double v);
int clamp_grid_size(int v);
std::string trim_copy(std::string s);
std::string sanitize_filename(const std::string& name);
std::string basename_without_ext(const std::string& name);
const std::vector<std::string>& pad_color_ids();
std::string normalize_pad_color(const std::string& color);
std::string display_pad_color(const std::string& color);
std::string normalize_group_name(const std::string& name);
std::string to_string(AppThemeMode m);
std::string to_string(PlaybackMode m);
std::string to_string(PlaybackDirection d);
std::string display_label(PlaybackMode m);
std::string display_label(PlaybackDirection d);
std::string to_string(PadGroupType type);
std::string to_string(GroupTransition transition);
AppThemeMode theme_from_string(const std::string& s);
PlaybackMode playback_from_string(const std::string& s, bool loop_fallback = false);
PlaybackDirection direction_from_string(const std::string& s);
PadGroupType group_type_from_string(const std::string& value);
GroupTransition group_transition_from_string(const std::string& value);
std::string default_button_label(int id);
SoundButton default_button(int id);
std::vector<SoundButton> default_buttons(int count = 9);
void ensure_button_count(std::vector<SoundButton>& buttons, int desiredCount);
std::string encode_board_config(const BoardState& state);
BoardState decode_board_config(const std::string& text);
BoardState default_board_state();

struct PackageLimits {
    std::uint64_t maxEntries = 1024;
    std::uint64_t maxSingleFileBytes = 100ULL * 1024ULL * 1024ULL;
    std::uint64_t maxTotalBytes = 512ULL * 1024ULL * 1024ULL;
};

struct PackageEntryInfo {
    std::string path;
    std::uint64_t uncompressedSize = 0;
};

bool is_valid_package_path(const std::string& path);
bool is_valid_sound_entry(const std::string& path);
void validate_package_path_or_throw(const std::string& path);
void validate_package_entries_or_throw(const std::vector<PackageEntryInfo>& entries, const PackageLimits& limits = {});

} // namespace pulsepad
