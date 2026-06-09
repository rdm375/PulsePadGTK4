#include "Subprocess.h"

#include <iostream>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK_TRUE(expr) do { if (!(expr)) { std::cerr << __FILE__ << ":" << __LINE__ << ": check failed: " #expr "\n"; ++failures; } } while (0)
#define CHECK_EQ(a,b) do { auto av=(a); auto bv=(b); if (!(av == bv)) { std::cerr << __FILE__ << ":" << __LINE__ << ": check failed: " #a " == " #b << " got '" << av << "' vs '" << bv << "'\n"; ++failures; } } while (0)

int main() {
    using namespace pulsepad;

    auto ok = run_subprocess({"/bin/echo", "hello;not-shell", "two words"});
    CHECK_TRUE(ok.success());
    CHECK_TRUE(ok.stdoutText.find("hello;not-shell two words") != std::string::npos);

    auto missing = run_subprocess({"definitely-not-a-pulsepad-tool-xyz"});
    CHECK_TRUE(!missing.started);
    CHECK_TRUE(!missing.success());
    CHECK_TRUE(!subprocess_error_summary("missing-tool", missing).empty());

    auto sh = run_subprocess({"/bin/sh", "-c", "printf out; printf err >&2; exit 7"});
    CHECK_TRUE(sh.started);
    CHECK_EQ(sh.exitCode, 7);
    CHECK_TRUE(sh.stdoutText.find("out") != std::string::npos);
    CHECK_TRUE(sh.stderrText.find("err") != std::string::npos);

    std::atomic<bool> cancel{true};
    SubprocessOptions options;
    options.cancelFlag = &cancel;
    options.timeoutMs = 5000;
    auto cancelled = run_subprocess({"/bin/sh", "-c", "sleep 5"}, options);
    CHECK_TRUE(cancelled.cancelled || cancelled.exitCode != 0);

    return failures == 0 ? 0 : 1;
}
