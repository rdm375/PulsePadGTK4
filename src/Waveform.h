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

struct LoudnessAnalysisResult {
    bool ok = false;
    double measuredLufs = 0.0;
    double measuredPeakDb = 0.0;
    double normalizationGainDb = 0.0;
    double regionStart = 0.0;
    double regionEnd = 0.0;
    std::string sourceFile;
    std::string sourceTimestamp;
    std::string errorMessage;
};

LoudnessAnalysisResult analyze_loudness_with_ffmpeg(const std::filesystem::path& file, double regionStart, double regionEnd, double targetLufs = -18.0, double peakCeilingDb = -1.0);
std::string file_timestamp_for_analysis(const std::filesystem::path& file);
std::string format_seconds(double seconds);

} // namespace pulsepad
