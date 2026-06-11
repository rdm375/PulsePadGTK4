#include "Waveform.h"
#include "BoardConfig.h"
#include "Subprocess.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <regex>
#include <sstream>

namespace pulsepad {

static bool debug_zero_crossing_enabled() {
    const char* value = std::getenv("PULSEPAD_DEBUG_ZERO_CROSSING");
    return value && *value && std::string(value) != "0";
}

static void debug_zero_crossing_log(const std::string& message) {
    if (debug_zero_crossing_enabled()) std::cerr << "[zero-cross] " << message << std::endl;
}

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

WaveformData generate_waveform_data(const std::filesystem::path& file, double durationSeconds, int bins) {
    WaveformData out;
    out.peaks.assign(static_cast<size_t>(std::max(1, bins)), 0.0);
    if (durationSeconds <= 0.0 || bins <= 0) return out;

    constexpr int sampleRate = 8000;
    out.sampleRate = sampleRate;
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
    out.monoSamples.reserve(data.size() / 2);
    long long sampleIndex = 0;
    for (size_t i = 0; i + 1 < data.size(); i += 2) {
        unsigned char lo = static_cast<unsigned char>(data[i]);
        unsigned char hi = static_cast<unsigned char>(data[i + 1]);
        int16_t sample = static_cast<int16_t>(static_cast<unsigned int>(lo) | (static_cast<unsigned int>(hi) << 8));
        int bin = static_cast<int>((sampleIndex * bins) / expectedSamples);
        if (bin < 0) bin = 0;
        if (bin >= bins) bin = bins - 1;
        double normalized = std::max(-1.0, std::min(1.0, static_cast<double>(sample) / 32768.0));
        double amp = std::min(1.0, std::abs(normalized));
        out.peaks[static_cast<size_t>(bin)] = std::max(out.peaks[static_cast<size_t>(bin)], amp);
        out.monoSamples.push_back(normalized);
        ++sampleIndex;
    }
    {
        std::ostringstream zs;
        zs << "generate_waveform_data file=" << file.string()
           << " duration=" << durationSeconds
           << " bins=" << bins
           << " decodedSamples=" << out.monoSamples.size()
           << " sampleRate=" << out.sampleRate;
        debug_zero_crossing_log(zs.str());
    }
    return out;
}

std::vector<double> generate_waveform_peaks(const std::filesystem::path& file, double durationSeconds, int bins) {
    return generate_waveform_data(file, durationSeconds, bins).peaks;
}

std::optional<double> find_nearest_zero_crossing(const std::vector<double>& monoSamples, int sampleRate, double requestedSeconds, double durationSeconds, double searchWindowSeconds, double nearZeroThreshold) {
    if (monoSamples.size() < 2 || sampleRate <= 0 || durationSeconds <= 0.0 || !std::isfinite(requestedSeconds)) return std::nullopt;
    const double duration = std::min(durationSeconds, static_cast<double>(monoSamples.size() - 1) / static_cast<double>(sampleRate));
    if (duration <= 0.0) return std::nullopt;
    const double requested = std::max(0.0, std::min(duration, requestedSeconds));
    const double window = std::max(0.0, searchWindowSeconds);
    const double threshold = std::max(0.0, nearZeroThreshold);
    const long long target = std::max(0LL, std::min(static_cast<long long>(monoSamples.size() - 1), static_cast<long long>(std::llround(requested * sampleRate))));
    const long long radius = std::max(1LL, static_cast<long long>(std::ceil(window * sampleRate)));
    const long long first = std::max(0LL, target - radius);
    const long long last = std::min(static_cast<long long>(monoSamples.size() - 1), target + radius);

    long long bestIndex = -1;
    long long bestDistance = std::numeric_limits<long long>::max();
    auto consider = [&](long long index) {
        if (index < first || index > last) return;
        const long long distance = std::llabs(index - target);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = index;
        }
    };

    for (long long i = first; i <= last; ++i) {
        if (std::abs(monoSamples[static_cast<size_t>(i)]) <= threshold) consider(i);
    }
    for (long long i = first; i < last; ++i) {
        const double a = monoSamples[static_cast<size_t>(i)];
        const double b = monoSamples[static_cast<size_t>(i + 1)];
        if ((a < 0.0 && b > 0.0) || (a > 0.0 && b < 0.0)) {
            consider(std::llabs(i - target) <= std::llabs(i + 1 - target) ? i : i + 1);
        }
    }

    if (bestIndex < 0) {
        std::ostringstream zs;
        zs << "find_nearest_zero_crossing none requested=" << requestedSeconds
           << " targetSample=" << target
           << " first=" << first
           << " last=" << last
           << " sampleRate=" << sampleRate
           << " samples=" << monoSamples.size();
        debug_zero_crossing_log(zs.str());
        return std::nullopt;
    }
    const double result = std::max(0.0, std::min(durationSeconds, static_cast<double>(bestIndex) / static_cast<double>(sampleRate)));
    {
        std::ostringstream zs;
        zs << "find_nearest_zero_crossing requested=" << requestedSeconds
           << " targetSample=" << target
           << " bestSample=" << bestIndex
           << " result=" << result
           << " distanceSamples=" << bestDistance
           << " first=" << first
           << " last=" << last
           << " sampleRate=" << sampleRate;
        debug_zero_crossing_log(zs.str());
    }
    return result;
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
