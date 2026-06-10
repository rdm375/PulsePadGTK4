#include "Waveform.h"
#include "BoardConfig.h"
#include "Subprocess.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>

namespace pulsepad {

bool command_available(const char* name) {
    return name && executable_available(name);
}

bool ffmpeg_available() { return command_available("ffmpeg"); }
bool ffprobe_available() { return command_available("ffprobe"); }

double get_audio_duration_seconds_with_ffprobe(const std::filesystem::path& file) {
    SubprocessOptions options;
    options.maxStdoutBytes = 4096;
    options.maxStderrBytes = 64 * 1024;
    options.timeoutMs = 10000;
    auto result = run_subprocess({"ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "default=noprint_wrappers=1:nokey=1", file.string()}, options);
    if (!result.success()) return 0.0;
    try { return clamp_time_seconds(std::stod(result.stdoutText)); } catch (...) { return 0.0; }
}

std::vector<double> generate_waveform_peaks(const std::filesystem::path& file, double durationSeconds, int bins) {
    std::vector<double> peaks(static_cast<size_t>(std::max(1, bins)), 0.0);
    if (durationSeconds <= 0.0 || bins <= 0) return peaks;

    constexpr int sampleRate = 8000;
    const long long expectedSamples = std::max(1LL, static_cast<long long>(durationSeconds * sampleRate));
    SubprocessOptions options;
    options.maxStdoutBytes = static_cast<std::size_t>(std::min<long long>(expectedSamples * 2 + 4096, 64LL * 1024LL * 1024LL));
    options.maxStderrBytes = 128 * 1024;
    options.timeoutMs = 30000;
    auto result = run_subprocess({"ffmpeg", "-nostdin", "-hide_banner", "-v", "error", "-i", file.string(), "-vn", "-ac", "1", "-ar", "8000", "-f", "s16le", "-"}, options);
    if (!result.success()) {
        std::cerr << "waveform ffmpeg failed: " << subprocess_error_summary("ffmpeg", result) << std::endl;
        return {};
    }

    const std::string& data = result.stdoutText;
    long long sampleIndex = 0;
    for (size_t i = 0; i + 1 < data.size(); i += 2) {
        unsigned char lo = static_cast<unsigned char>(data[i]);
        unsigned char hi = static_cast<unsigned char>(data[i + 1]);
        int16_t sample = static_cast<int16_t>(static_cast<unsigned int>(lo) | (static_cast<unsigned int>(hi) << 8));
        int bin = static_cast<int>((sampleIndex * bins) / expectedSamples);
        if (bin < 0) bin = 0;
        if (bin >= bins) bin = bins - 1;
        double amp = std::min(1.0, std::abs(static_cast<int>(sample)) / 32768.0);
        peaks[static_cast<size_t>(bin)] = std::max(peaks[static_cast<size_t>(bin)], amp);
        ++sampleIndex;
    }
    return peaks;
}


std::string file_timestamp_for_analysis(const std::filesystem::path& file) {
    try {
        auto t = std::filesystem::last_write_time(file).time_since_epoch().count();
        std::ostringstream ss;
        ss << t;
        return ss.str();
    } catch (...) {
        return {};
    }
}

static bool parse_last_number_after_label(const std::string& text, const std::string& label, double& out) {
    std::regex pattern(label + R"(\s*:\s*(-?(?:\d+(?:\.\d*)?|\.\d+)))", std::regex::icase);
    bool found = false;
    for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it) {
        try {
            out = std::stod((*it)[1].str());
            found = true;
        } catch (...) {}
    }
    return found;
}

LoudnessAnalysisResult analyze_loudness_with_ffmpeg(const std::filesystem::path& file, double regionStart, double regionEnd, double targetLufs, double peakCeilingDb) {
    LoudnessAnalysisResult out;
    out.regionStart = clamp_time_seconds(regionStart);
    out.regionEnd = clamp_time_seconds(regionEnd);
    out.sourceFile = file.string();
    out.sourceTimestamp = file_timestamp_for_analysis(file);

    if (!ffmpeg_available()) {
        out.errorMessage = "ffmpeg is required for loudness analysis";
        return out;
    }
    if (!std::filesystem::exists(file)) {
        out.errorMessage = "audio file not found";
        return out;
    }

    std::vector<std::string> args{"ffmpeg", "-nostdin", "-hide_banner", "-nostats", "-v", "info"};
    if (out.regionStart > 0.0) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(3) << out.regionStart;
        args.push_back("-ss");
        args.push_back(ss.str());
    }
    if (out.regionEnd > out.regionStart) {
        std::ostringstream tt;
        tt << std::fixed << std::setprecision(3) << (out.regionEnd - out.regionStart);
        args.push_back("-t");
        args.push_back(tt.str());
    }
    args.push_back("-i");
    args.push_back(file.string());
    args.push_back("-vn");
    args.push_back("-filter_complex");
    args.push_back("ebur128=peak=sample");
    args.push_back("-f");
    args.push_back("null");
    args.push_back("-");

    SubprocessOptions options;
    options.maxStdoutBytes = 4096;
    options.maxStderrBytes = 512 * 1024;
    options.timeoutMs = 60000;
    auto result = run_subprocess(args, options);
    if (!result.success()) {
        out.errorMessage = subprocess_error_summary("ffmpeg", result);
        return out;
    }

    double integrated = 0.0;
    double peak = 0.0;
    const bool haveLufs = parse_last_number_after_label(result.stderrText, "I", integrated);
    bool havePeak = parse_last_number_after_label(result.stderrText, "Peak", peak);
    if (!havePeak) havePeak = parse_last_number_after_label(result.stderrText, "Sample peak", peak);
    if (!haveLufs || !havePeak || !std::isfinite(integrated) || !std::isfinite(peak)) {
        out.errorMessage = "ffmpeg did not report integrated loudness and sample peak";
        return out;
    }

    out.measuredLufs = integrated;
    out.measuredPeakDb = peak;
    out.normalizationGainDb = calculate_normalization_gain_db(targetLufs, integrated, peakCeilingDb, peak);
    out.ok = true;
    return out;
}

std::string format_seconds(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    int total = static_cast<int>(seconds + 0.5);
    int minutes = total / 60;
    int secs = total % 60;
    std::ostringstream ss;
    ss << minutes << ":" << std::setw(2) << std::setfill('0') << secs;
    return ss.str();
}

} // namespace pulsepad
