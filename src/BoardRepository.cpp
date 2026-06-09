#include "BoardRepository.h"

#include "Subprocess.h"
#include "Waveform.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <stdexcept>
#include <unistd.h>

namespace fs = std::filesystem;

namespace pulsepad {


static std::string json_escape_local(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char ch : value) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += ch; break;
        }
    }
    return out;
}

static std::optional<std::string> json_string_field_local(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = text.find(needle);
    if (pos == std::string::npos) return std::nullopt;
    pos = text.find(':', pos + needle.size());
    if (pos == std::string::npos) return std::nullopt;
    pos = text.find('"', pos + 1);
    if (pos == std::string::npos) return std::nullopt;
    ++pos;
    std::string out;
    while (pos < text.size()) {
        char ch = text[pos++];
        if (ch == '"') return out;
        if (ch != '\\') {
            out += ch;
            continue;
        }
        if (pos >= text.size()) return std::nullopt;
        char esc = text[pos++];
        switch (esc) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            default: return std::nullopt;
        }
    }
    return std::nullopt;
}

static std::string directory_if_valid_local(const std::string& value) {
    if (value.empty()) return {};
    std::error_code ec;
    fs::path path(value);
    if (!fs::exists(path, ec) || !fs::is_directory(path, ec)) return {};
    return path.string();
}

fs::path BoardRepository::default_config_root() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return fs::path(xdg) / "pulsepad-gtk";
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : ".") / ".config" / "pulsepad-gtk";
}

BoardRepository::BoardRepository(fs::path rootDir) : rootDir_(std::move(rootDir)) { fs::create_directories(sounds_dir()); }

fs::path BoardRepository::sounds_dir() const { return rootDir_ / "sounds"; }
fs::path BoardRepository::config_path() const { return rootDir_ / "board_config_v2.json"; }
fs::path BoardRepository::preferences_path() const { return rootDir_ / "app_preferences.json"; }
fs::path BoardRepository::sound_file(const SoundButton& button) const { return sounds_dir() / button.storedFilename; }
fs::path BoardRepository::reverse_sound_file(const SoundButton& button) const { return sounds_dir() / (button.storedFilename + ".reverse.wav"); }
fs::path BoardRepository::playback_file(const SoundButton& button) const {
    return button.playbackDirection == PlaybackDirection::Reverse ? reverse_sound_file(button) : sound_file(button);
}

BoardState BoardRepository::default_state() const { return default_board_state(); }

BoardState BoardRepository::load() const {
    BoardState state = default_state();
    const auto file = config_path();
    if (!fs::exists(file)) return state;
    try {
        std::ifstream in(file, std::ios::binary);
        if (!in) throw std::runtime_error("Could not open config");
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        state = decode_board_config(text);
        for (auto& button : state.buttons) normalize_trim_to_duration(button);
        return state;
    } catch (const std::exception&) {
        try { fs::copy_file(file, rootDir_ / "corrupt-config-backup.json", fs::copy_options::overwrite_existing); } catch (...) {}
        state.status = "Corrupt configuration reset to defaults";
        return state;
    }
}

void BoardRepository::save(const BoardState& state) const {
    fs::create_directories(rootDir_);
    const auto target = config_path();
    const auto tmp = target.string() + ".tmp-" + unique_token();
    try {
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) throw std::runtime_error("Could not open temporary config for writing");
            const auto encoded = encode_board_config(state);
            out.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
            out.flush();
            if (!out) throw std::runtime_error("Could not write temporary config");
        }
        fs::rename(tmp, target);
    } catch (...) {
        std::error_code ignored;
        fs::remove(tmp, ignored);
        throw std::runtime_error("Could not save configuration");
    }
}


FileDialogMemory BoardRepository::load_file_dialog_memory() const {
    FileDialogMemory memory;
    const auto file = preferences_path();
    if (!fs::exists(file)) return memory;
    try {
        std::ifstream in(file, std::ios::binary);
        if (!in) return memory;
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (auto value = json_string_field_local(text, "last_audio_import_dir")) memory.lastAudioImportDir = directory_if_valid_local(*value);
        if (auto value = json_string_field_local(text, "last_board_load_dir")) memory.lastBoardLoadDir = directory_if_valid_local(*value);
        if (auto value = json_string_field_local(text, "last_board_save_dir")) memory.lastBoardSaveDir = directory_if_valid_local(*value);
    } catch (...) {
        return {};
    }
    return memory;
}

void BoardRepository::save_file_dialog_memory(const FileDialogMemory& memory) const {
    fs::create_directories(rootDir_);
    const auto target = preferences_path();
    const auto tmp = target.string() + ".tmp-" + unique_token();
    try {
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) throw std::runtime_error("Could not open temporary preferences for writing");
            out << "{\n";
            out << "  \"last_audio_import_dir\": \"" << json_escape_local(memory.lastAudioImportDir) << "\",\n";
            out << "  \"last_board_load_dir\": \"" << json_escape_local(memory.lastBoardLoadDir) << "\",\n";
            out << "  \"last_board_save_dir\": \"" << json_escape_local(memory.lastBoardSaveDir) << "\"\n";
            out << "}\n";
            out.flush();
            if (!out) throw std::runtime_error("Could not write temporary preferences");
        }
        fs::rename(tmp, target);
    } catch (...) {
        std::error_code ignored;
        fs::remove(tmp, ignored);
        throw std::runtime_error("Could not save preferences");
    }
}

bool BoardRepository::reverse_audio_is_ready(const SoundButton& button) const {
    if (!button.assigned || button.storedFilename.empty()) return false;
    const auto input = sound_file(button);
    const auto output = reverse_sound_file(button);
    return fs::exists(input) && fs::exists(output) && fs::file_size(output) > 0 && fs::last_write_time(output) >= fs::last_write_time(input);
}

void BoardRepository::normalize_trim_to_duration(SoundButton& button) const {
    if (!button.assigned || button.storedFilename.empty()) {
        button.trimStart = 0.0;
        button.trimEnd = 0.0;
        return;
    }
    const double duration = button.durationSeconds > 0.0 ? button.durationSeconds : get_audio_duration_seconds_with_ffprobe(sound_file(button));
    button.durationSeconds = duration;
    if (duration <= 0.0) {
        button.trimStart = clamp_time_seconds(button.trimStart);
        button.trimEnd = clamp_time_seconds(button.trimEnd);
        if (button.trimEnd > 0.0 && button.trimEnd <= button.trimStart) button.trimEnd = 0.0;
        return;
    }
    button.trimStart = std::min(clamp_time_seconds(button.trimStart), duration);
    button.trimEnd = clamp_time_seconds(button.trimEnd);
    if (button.trimEnd <= 0.0 || button.trimEnd > duration) button.trimEnd = duration;
    if (button.trimEnd <= button.trimStart) {
        button.trimStart = 0.0;
        button.trimEnd = duration;
    }
}

void BoardRepository::clear_all_audio_assets() const {
    fs::remove_all(sounds_dir());
    fs::create_directories(sounds_dir());
}

std::string BoardRepository::default_label_for(const SoundButton& button) const {
    if (button.assigned && !button.originalFilename.empty()) return basename_without_ext(button.originalFilename);
    return default_button_label(button.id);
}

SoundButton BoardRepository::import_audio_for_button(const SoundButton& button, const fs::path& selectedFile) const {
    static const std::set<std::string> supported = {"mp3", "wav", "ogg", "flac", "m4a"};
    const auto ext = lower_extension(selectedFile);
    if (supported.find(ext) == supported.end()) throw std::runtime_error("Unsupported audio format");
    const auto name = selectedFile.filename().string();
    const auto stored = "button-" + std::to_string(button.id) + "-" + unique_token() + "-" + sanitize_filename(name);
    const auto outFile = sounds_dir() / stored;
    fs::create_directories(sounds_dir());
    fs::copy_file(selectedFile, outFile, fs::copy_options::none);
    if (!fs::exists(outFile) || fs::file_size(outFile) == 0) throw std::runtime_error("Imported audio could not be opened");
    SoundButton b = button;
    b.label = basename_without_ext(name);
    b.userRenamed = false;
    b.originalFilename = name;
    b.storedFilename = stored;
    b.relativePathOrAssetReference = "sounds/" + stored;
    b.mimeType = "";
    b.durationSeconds = get_audio_duration_seconds_with_ffprobe(outFile);
    b.trimStart = 0.0;
    b.trimEnd = b.durationSeconds;
    b.assigned = true;
    return b;
}

fs::path BoardRepository::ensure_reverse_audio_for_button(const SoundButton& button, const std::atomic<bool>* cancelFlag) const {
    if (!button.assigned || button.storedFilename.empty()) throw std::runtime_error("No audio assigned");
    const auto input = sound_file(button);
    const auto output = reverse_sound_file(button);
    if (!fs::exists(input)) throw std::runtime_error("Missing stored audio asset");
    if (fs::exists(output) && fs::last_write_time(output) >= fs::last_write_time(input) && fs::file_size(output) > 0) return output;

    fs::path temp = output;
    temp += ".tmp.wav";
    fs::remove(temp);
    SubprocessOptions options;
    options.maxStderrBytes = 256 * 1024;
    options.timeoutMs = 120000;
    options.cancelFlag = cancelFlag;
    auto result = run_subprocess({"ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-i", input.string(), "-af", "areverse", "-c:a", "pcm_s16le", temp.string()}, options);
    if (!result.success() || !fs::exists(temp) || fs::file_size(temp) == 0) {
        fs::remove(temp);
        if (result.cancelled) throw std::runtime_error("Reverse audio generation cancelled");
        throw std::runtime_error("Could not create reversed audio: " + subprocess_error_summary("ffmpeg", result));
    }
    fs::rename(temp, output);
    return output;
}

void BoardRepository::remove_audio_assets(const SoundButton& button) const {
    if (!button.assigned || button.storedFilename.empty()) return;
    std::error_code ignored;
    fs::remove(sound_file(button), ignored);
    fs::remove(reverse_sound_file(button), ignored);
}

SoundButton BoardRepository::clear_audio(const SoundButton& button) const {
    remove_audio_assets(button);
    return default_button(button.id);
}

std::string BoardRepository::unique_token() {
    return std::to_string(static_cast<unsigned long long>(::getpid())) + "-" + std::to_string(static_cast<unsigned long long>(std::time(nullptr)));
}

std::string lower_extension(const fs::path& path) {
    std::string ext = path.extension().string();
    if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

} // namespace pulsepad
