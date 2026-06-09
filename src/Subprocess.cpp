#include "Subprocess.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>

#include <filesystem>
#include <sstream>
#include <thread>

extern char** environ;

namespace pulsepad {
namespace {

bool set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void append_limited(std::string& out, const char* data, std::size_t n, std::size_t limit) {
    if (out.size() >= limit) return;
    std::size_t take = std::min(n, limit - out.size());
    out.append(data, take);
}

std::string trim_for_message(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    while (!s.empty() && (s.front() == '\n' || s.front() == '\r' || s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    if (s.size() > 400) s = s.substr(0, 400) + "...";
    return s;
}

} // namespace

bool executable_available(const std::string& name) {
    if (name.empty() || name.find('/') != std::string::npos) return false;
    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv) return false;
    std::string path(pathEnv);
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find(':', start);
        std::string dir = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (dir.empty()) dir = ".";
        std::filesystem::path candidate = std::filesystem::path(dir) / name;
        if (access(candidate.c_str(), X_OK) == 0) return true;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

SubprocessResult run_subprocess(const std::vector<std::string>& argv, const SubprocessOptions& options) {
    SubprocessResult result;
    if (argv.empty() || argv.front().empty()) {
        result.errorMessage = "No executable specified";
        return result;
    }

    int outPipe[2] = {-1, -1};
    int errPipe[2] = {-1, -1};
    if (pipe(outPipe) != 0 || pipe(errPipe) != 0) {
        result.errorMessage = std::strerror(errno);
        if (outPipe[0] >= 0) close(outPipe[0]);
        if (outPipe[1] >= 0) close(outPipe[1]);
        if (errPipe[0] >= 0) close(errPipe[0]);
        if (errPipe[1] >= 0) close(errPipe[1]);
        return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    // GUI-launched subprocesses must not inherit the app/terminal stdin.
    // Tools such as ffmpeg can read commands from stdin and appear to hang
    // forever, leaving UI state stuck at "pending".
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, outPipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, errPipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, outPipe[0]);
    posix_spawn_file_actions_addclose(&actions, errPipe[0]);
    posix_spawn_file_actions_addclose(&actions, outPipe[1]);
    posix_spawn_file_actions_addclose(&actions, errPipe[1]);

    posix_spawnattr_t attrs;
    posix_spawnattr_init(&attrs);
    // Put the child in its own process group so cancellation/timeouts can
    // terminate shell-launched grandchildren that may still hold pipe fds open.
    posix_spawnattr_setflags(&attrs, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attrs, 0);

    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (const auto& arg : argv) cargv.push_back(const_cast<char*>(arg.c_str()));
    cargv.push_back(nullptr);

    pid_t pid = -1;
    int spawnRc = posix_spawnp(&pid, argv.front().c_str(), &actions, &attrs, cargv.data(), environ);
    posix_spawnattr_destroy(&attrs);
    posix_spawn_file_actions_destroy(&actions);
    close(outPipe[1]);
    close(errPipe[1]);

    if (spawnRc != 0) {
        close(outPipe[0]);
        close(errPipe[0]);
        result.errorMessage = std::strerror(spawnRc);
        return result;
    }

    result.started = true;
    set_nonblock(outPipe[0]);
    set_nonblock(errPipe[0]);

    auto start = std::chrono::steady_clock::now();
    bool outOpen = true;
    bool errOpen = true;
    bool childExited = false;
    bool terminateSent = false;
    bool killSent = false;
    int status = 0;
    std::array<char, 4096> buffer{};

    while (outOpen || errOpen || !childExited) {
        if (options.cancelFlag && options.cancelFlag->load()) {
            result.cancelled = true;
        }
        if (options.timeoutMs > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            if (elapsed > options.timeoutMs) {
                result.timedOut = true;
            }
        }

        struct pollfd fds[2];
        nfds_t nfds = 0;
        if (outOpen) fds[nfds++] = {outPipe[0], POLLIN | POLLHUP, 0};
        if (errOpen) fds[nfds++] = {errPipe[0], POLLIN | POLLHUP, 0};
        if (nfds > 0) poll(fds, nfds, 50);

        auto read_fd = [&](int fd, bool& open, std::string& dest, std::size_t limit) {
            while (open) {
                ssize_t n = read(fd, buffer.data(), buffer.size());
                if (n > 0) append_limited(dest, buffer.data(), static_cast<std::size_t>(n), limit);
                else if (n == 0) { close(fd); open = false; }
                else if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                else { close(fd); open = false; break; }
            }
        };
        if (outOpen) read_fd(outPipe[0], outOpen, result.stdoutText, options.maxStdoutBytes);
        if (errOpen) read_fd(errPipe[0], errOpen, result.stderrText, options.maxStderrBytes);

        if (!childExited) {
            pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == pid) childExited = true;
            else if (w < 0 && errno == ECHILD) childExited = true;
        }

        if ((result.cancelled || result.timedOut) && !childExited) {
            if (!terminateSent) {
                if (kill(-pid, SIGTERM) != 0) kill(pid, SIGTERM);
                terminateSent = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (!killSent) {
                pid_t w = waitpid(pid, &status, WNOHANG);
                if (w == pid || (w < 0 && errno == ECHILD)) {
                    childExited = true;
                } else if (w == 0) {
                    if (kill(-pid, SIGKILL) != 0) kill(pid, SIGKILL);
                    killSent = true;
                }
            }
        }

        if ((result.cancelled || result.timedOut) && (killSent || childExited)) {
            if (outOpen) { close(outPipe[0]); outOpen = false; }
            if (errOpen) { close(errPipe[0]); errOpen = false; }
        }
    }

    if (WIFEXITED(status)) result.exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) result.exitCode = 128 + WTERMSIG(status);
    else result.exitCode = -1;

    if (!result.success() && result.errorMessage.empty()) {
        if (result.cancelled) result.errorMessage = "Process cancelled";
        else if (result.timedOut) result.errorMessage = "Process timed out";
        else result.errorMessage = trim_for_message(result.stderrText);
    }
    return result;
}

std::string subprocess_error_summary(const std::string& tool, const SubprocessResult& result) {
    if (!result.started) return tool + " could not be started: " + (result.errorMessage.empty() ? "executable not found" : result.errorMessage);
    if (result.cancelled) return tool + " was cancelled";
    if (result.timedOut) return tool + " timed out";
    std::ostringstream ss;
    ss << tool << " exited with status " << result.exitCode;
    std::string detail = trim_for_message(result.stderrText.empty() ? result.errorMessage : result.stderrText);
    if (!detail.empty()) ss << ": " << detail;
    return ss.str();
}

} // namespace pulsepad
