#pragma once

#include <string>

namespace pulsepad::user_message {

std::string dependency_warning(bool ffmpegAvailable, bool ffprobeAvailable);
std::string waveform_unavailable_missing_ffmpeg();
std::string trim_unavailable_missing_ffprobe();
std::string audio_missing();
std::string duration_unknown(bool ffprobeAvailable);
std::string waveform_failed(const std::string& detail);
std::string reverse_failed(const std::string& detail);
std::string reverse_unavailable_missing_ffmpeg();
std::string reverse_generating();
std::string reverse_ready();
std::string reverse_invalidated();
std::string board_import_failed(const std::string& detail);
std::string board_export_failed(const std::string& detail);

} // namespace pulsepad::user_message
