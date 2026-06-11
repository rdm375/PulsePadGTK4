#include "Waveform.h"

#include <cmath>
#include <iostream>
#include <vector>

static int failures = 0;
#define CHECK_TRUE(v) do { if (!(v)) { std::cerr << __FILE__ << ":" << __LINE__ << ": expected true\n"; ++failures; } } while (0)
#define CHECK_FALSE(v) do { if ((v)) { std::cerr << __FILE__ << ":" << __LINE__ << ": expected false\n"; ++failures; } } while (0)
#define CHECK_NEAR(a,b,eps) do { auto va=(a); auto vb=(b); if (std::abs(va - vb) > (eps)) { std::cerr << __FILE__ << ":" << __LINE__ << ": expected " << va << " near " << vb << "\n"; ++failures; } } while (0)

int main() {
    using pulsepad::find_nearest_zero_crossing;

    const int sr = 1000;
    const double duration = 0.010;

    // exact zero crossing at requested point
    std::vector<double> exact{1.0, 0.5, 0.0, -0.5, -1.0};
    auto z = find_nearest_zero_crossing(exact, sr, 0.002, duration, 0.004);
    CHECK_TRUE(z.has_value());
    if (z) CHECK_NEAR(*z, 0.002, 0.000001);

    // nearest crossing before requested point
    std::vector<double> before{1.0, 0.5, -0.2, -0.4, -0.6, -0.8};
    z = find_nearest_zero_crossing(before, sr, 0.004, duration, 0.004);
    CHECK_TRUE(z.has_value());
    if (z) CHECK_NEAR(*z, 0.002, 0.001);

    // nearest crossing after requested point
    std::vector<double> after{1.0, 0.8, 0.6, 0.3, -0.1, -0.4};
    z = find_nearest_zero_crossing(after, sr, 0.002, duration, 0.004);
    CHECK_TRUE(z.has_value());
    if (z) CHECK_NEAR(*z, 0.003, 0.001);

    // no crossing inside search window
    std::vector<double> none{1.0, 0.9, 0.8, 0.7, 0.6, -0.2};
    z = find_nearest_zero_crossing(none, sr, 0.001, duration, 0.001);
    CHECK_FALSE(z.has_value());

    // clamp at file start/end
    z = find_nearest_zero_crossing(exact, sr, -1.0, duration, 0.004);
    CHECK_TRUE(z.has_value());
    if (z) CHECK_NEAR(*z, 0.002, 0.000001);
    z = find_nearest_zero_crossing(exact, sr, 99.0, duration, 0.010);
    CHECK_TRUE(z.has_value());
    if (z) CHECK_NEAR(*z, 0.002, 0.000001);


    // deterministic golden case: one valid zero crossing in the entire synthetic sample.
    // 0.5 seconds of positive audio, one exact zero sample, then 0.5 seconds of negative audio.
    // Any request within the search window around the center has only one correct answer: 0.5s.
    const int goldenSr = 48000;
    std::vector<double> singleSolution(static_cast<size_t>(goldenSr) + 1, 1.0);
    singleSolution[static_cast<size_t>(goldenSr / 2)] = 0.0;
    for (size_t i = static_cast<size_t>(goldenSr / 2 + 1); i < singleSolution.size(); ++i) {
        singleSolution[i] = -1.0;
    }
    z = find_nearest_zero_crossing(singleSolution, goldenSr, 0.495, 1.0, 0.010);
    CHECK_TRUE(z.has_value());
    if (z) CHECK_NEAR(*z, 0.500, 0.000001);
    z = find_nearest_zero_crossing(singleSolution, goldenSr, 0.505, 1.0, 0.010);
    CHECK_TRUE(z.has_value());
    if (z) CHECK_NEAR(*z, 0.500, 0.000001);
    z = find_nearest_zero_crossing(singleSolution, goldenSr, 0.400, 1.0, 0.010);
    CHECK_FALSE(z.has_value());

    // stereo mixed-to-mono crossing represented by pre-mixed mono samples.
    std::vector<double> mixedMono{0.7, 0.35, 0.0, -0.35, -0.7};
    z = find_nearest_zero_crossing(mixedMono, sr, 0.002, duration, 0.004);
    CHECK_TRUE(z.has_value());
    if (z) CHECK_NEAR(*z, 0.002, 0.000001);

    if (failures) return 1;
    std::cout << "All waveform tests passed\n";
    return 0;
}
