#include "UserMessages.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace pulsepad::user_message {
namespace {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string clean_detail(const std::string& detail) {
    if (detail.empty()) return {};
    if (detail.size() > 160) return detail.substr(0, 157) + "...";
    return detail;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return lower(haystack).find(lower(needle)) != std::string::npos;
}
} // namespace

std::string dependency_warning(bool ffmpegAvailable, bool ffprobeAvailable) {
    std::string out;
    if (!ffmpegAvailable) out += "ffmpeg not found in PATH: waveform display and reverse audio are unavailable. Install ffmpeg and restart PulsePad.";
    if (!ffprobeAvailable) {
        if (!out.empty()) out += " ";
        out += "ffprobe not found in PATH: duration, trim, and waveform setup may be unavailable. Install ffmpeg/ffprobe and restart PulsePad.";
    }
    return out;
}

std::string waveform_unavailable_missing_ffmpeg() { return "Waveform unavailable: ffmpeg could not be found in PATH. Install ffmpeg and restart PulsePad."; }
std::string trim_unavailable_missing_ffprobe() { return "Trim unavailable: ffprobe could not be found in PATH. Install ffmpeg/ffprobe and restart PulsePad."; }
std::string audio_missing() { return "Audio file missing. Load the sound again or re-import the board package that contains it."; }
std::string duration_unknown(bool ffprobeAvailable) { return ffprobeAvailable ? "Duration could not be determined for this audio file." : trim_unavailable_missing_ffprobe(); }
std::string waveform_failed(const std::string& detail) { return "Waveform generation failed" + (clean_detail(detail).empty() ? std::string(".") : std::string(": ") + clean_detail(detail)); }
std::string reverse_failed(const std::string& detail) { return "Reverse audio generation failed" + (clean_detail(detail).empty() ? std::string(". You can retry.") : std::string(": ") + clean_detail(detail) + ". You can retry."); }
std::string reverse_unavailable_missing_ffmpeg() { return "Reverse unavailable: ffmpeg could not be found in PATH. Install ffmpeg and restart PulsePad."; }
std::string reverse_generating() { return "Generating reverse audio..."; }
std::string reverse_ready() { return "Reverse audio ready."; }
std::string reverse_invalidated() { return "Reverse audio will be regenerated because the source changed."; }

std::string board_import_failed(const std::string& detail) {
    if (contains(detail, "unsupported version")) return "Import failed: unsupported board version.";
    if (contains(detail, "missing manifest") || contains(detail, "could not open import package") || contains(detail, "unsupported file") || contains(detail, "unsafe archive")) return "Import failed: invalid board package.";
    if (contains(detail, "missing imported audio") || contains(detail, "missing audio")) return "Import failed: the board package is missing audio files.";
    if (contains(detail, "write") || contains(detail, "copy") || contains(detail, "temporary")) return "Import failed: could not write board files.";
    return "Import failed" + (clean_detail(detail).empty() ? std::string(".") : std::string(": ") + clean_detail(detail));
}

std::string board_export_failed(const std::string& detail) {
    if (contains(detail, "create export") || contains(detail, "destination") || contains(detail, "permission") || contains(detail, "writable")) return "Export failed: destination is not writable.";
    if (contains(detail, "missing source audio") || contains(detail, "missing stored audio") || contains(detail, "audio asset")) return "Export failed: a source audio file is missing.";
    if (contains(detail, "finalize") || contains(detail, "package") || contains(detail, "zip") || contains(detail, "write failure")) return "Export failed: could not package the board.";
    return "Export failed" + (clean_detail(detail).empty() ? std::string(".") : std::string(": ") + clean_detail(detail));
}

} // namespace pulsepad::user_message
