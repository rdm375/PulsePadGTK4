#pragma once

#include "BoardConfig.h"

#include <atomic>
#include <filesystem>
#include <string>

namespace pulsepad {

struct FileDialogMemory {
    std::string lastAudioImportDir;
    std::string lastBoardLoadDir;
    std::string lastBoardSaveDir;
};

class BoardRepository {
public:
    explicit BoardRepository(std::filesystem::path rootDir = default_config_root());

    static std::filesystem::path default_config_root();

    const std::filesystem::path& root_dir() const { return rootDir_; }
    std::filesystem::path sounds_dir() const;
    std::filesystem::path config_path() const;
    std::filesystem::path preferences_path() const;
    std::filesystem::path sound_file(const SoundButton& button) const;
    std::filesystem::path reverse_sound_file(const SoundButton& button) const;
    std::filesystem::path playback_file(const SoundButton& button) const;

    BoardState load() const;
    void save(const BoardState& state) const;
    FileDialogMemory load_file_dialog_memory() const;
    void save_file_dialog_memory(const FileDialogMemory& memory) const;
    BoardState default_state() const;

    bool reverse_audio_is_ready(const SoundButton& button) const;
    void normalize_trim_to_duration(SoundButton& button) const;
    void clear_all_audio_assets() const;
    std::string default_label_for(const SoundButton& button) const;
    SoundButton import_audio_for_button(const SoundButton& button, const std::filesystem::path& selectedFile) const;
    SoundButton duplicate_button_audio_for_pad(const SoundButton& source, int targetId) const;
    std::filesystem::path ensure_reverse_audio_for_button(const SoundButton& button, const std::atomic<bool>* cancelFlag = nullptr) const;
    void remove_audio_assets(const SoundButton& button) const;
    SoundButton clear_audio(const SoundButton& button) const;

private:
    static std::string unique_token();

    std::filesystem::path rootDir_;
};

std::string lower_extension(const std::filesystem::path& path);

} // namespace pulsepad
