#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pulsepad {

bool command_available(const char* name);
bool ffmpeg_available();
bool ffprobe_available();
double get_audio_duration_seconds_with_ffprobe(const std::filesystem::path& file);
struct WaveformData {
    std::vector<double> peaks;
    std::vector<double> monoSamples;
    int sampleRate = 0;
};

std::vector<double> generate_waveform_peaks(const std::filesystem::path& file, double durationSeconds, int bins = 420);
WaveformData generate_waveform_data(const std::filesystem::path& file, double durationSeconds, int bins = 420);
std::optional<double> find_nearest_zero_crossing(const std::vector<double>& monoSamples, int sampleRate, double requestedSeconds, double durationSeconds, double searchWindowSeconds = 0.010, double nearZeroThreshold = 0.001);

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
