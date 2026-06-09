#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace pulsepad {

bool command_available(const char* name);
bool ffmpeg_available();
bool ffprobe_available();
double get_audio_duration_seconds_with_ffprobe(const std::filesystem::path& file);
std::vector<double> generate_waveform_peaks(const std::filesystem::path& file, double durationSeconds, int bins = 420);
std::string format_seconds(double seconds);

} // namespace pulsepad
