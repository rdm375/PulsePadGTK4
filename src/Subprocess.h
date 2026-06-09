#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace pulsepad {

struct SubprocessResult {
    bool started = false;
    bool timedOut = false;
    bool cancelled = false;
    int exitCode = -1;
    std::string stdoutText;
    std::string stderrText;
    std::string errorMessage;

    bool success() const { return started && !timedOut && !cancelled && exitCode == 0; }
};

struct SubprocessOptions {
    std::size_t maxStdoutBytes = 4 * 1024 * 1024;
    std::size_t maxStderrBytes = 256 * 1024;
    int timeoutMs = 0;
    const std::atomic<bool>* cancelFlag = nullptr;
};

bool executable_available(const std::string& name);
SubprocessResult run_subprocess(const std::vector<std::string>& argv, const SubprocessOptions& options = {});
std::string subprocess_error_summary(const std::string& tool, const SubprocessResult& result);

} // namespace pulsepad
