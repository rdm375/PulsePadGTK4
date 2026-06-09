#include "UserMessages.h"

#include <iostream>
#include <string>

static int failures = 0;
#define CHECK_CONTAINS(s, sub) do { auto value=(s); if (value.find(sub) == std::string::npos) { std::cerr << __FILE__ << ":" << __LINE__ << ": expected '" << value << "' to contain '" << sub << "'\n"; ++failures; } } while (0)
#define CHECK_EQ(a,b) do { auto va=(a); auto vb=(b); if (!(va == vb)) { std::cerr << __FILE__ << ":" << __LINE__ << ": expected '" << va << "' == '" << vb << "'\n"; ++failures; } } while (0)

int main() {
    using namespace pulsepad::user_message;
    CHECK_CONTAINS(dependency_warning(false, true), "ffmpeg not found in PATH");
    CHECK_CONTAINS(dependency_warning(true, false), "ffprobe not found in PATH");
    CHECK_CONTAINS(waveform_unavailable_missing_ffmpeg(), "ffmpeg could not be found in PATH");
    CHECK_CONTAINS(duration_unknown(false), "ffprobe could not be found in PATH");
    CHECK_CONTAINS(reverse_failed("ffmpeg exited"), "You can retry");
    CHECK_CONTAINS(board_import_failed("Missing imported audio asset: kick.wav"), "missing audio files");
    CHECK_CONTAINS(board_import_failed("Unsupported version 99"), "unsupported board version");
    CHECK_CONTAINS(board_export_failed("Missing source audio file"), "source audio file is missing");
    CHECK_CONTAINS(board_export_failed("Could not create export package"), "destination is not writable");
    if (failures) return 1;
    std::cout << "All user message tests passed\n";
    return 0;
}
