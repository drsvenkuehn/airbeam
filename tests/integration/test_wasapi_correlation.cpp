/// Integration test: WASAPI loopback capture cross-correlation.
/// Plays a known 1 kHz sine wave to the default render device, captures via
/// WASAPI loopback, and asserts Pearson r > 0.99 between source and captured.
///
/// Disabled until WasapiCapture (T044) is implemented.
#include <gtest/gtest.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cmath>
#include <vector>

// TODO(T044): include "audio/WasapiCapture.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<int16_t> Gen1kHzStereo(int durationMs) {
    const int sr = 44100;
    const int n  = sr * durationMs / 1000;
    std::vector<int16_t> buf;
    buf.reserve(static_cast<size_t>(n) * 2);
    for (int i = 0; i < n; ++i) {
        auto s = static_cast<int16_t>(16000.0 * std::sin(2.0 * kPi * 1000.0 * i / sr));
        buf.push_back(s); buf.push_back(s);
    }
    return buf;
}

double PearsonR(const std::vector<int16_t>& a, const std::vector<int16_t>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0;
    const size_t n = a.size();
    double sa = 0, sb = 0;
    for (size_t i = 0; i < n; ++i) { sa += a[i]; sb += b[i]; }
    const double ma = sa/n, mb = sb/n;
    double cov = 0, va = 0, vb = 0;
    for (size_t i = 0; i < n; ++i) {
        double da = a[i]-ma, db = b[i]-mb;
        cov += da*db; va += da*da; vb += db*db;
    }
    return (va == 0 || vb == 0) ? 0.0 : cov / std::sqrt(va * vb);
}

} // namespace

TEST(WasapiCorrelation, DISABLED_CrossCorrelationAbove0_99) {
    auto source = Gen1kHzStereo(5000);

    // TODO(T044): start WasapiCapture, play source, stop after 5s
    std::vector<int16_t> captured; // placeholder

    if (captured.size() < source.size() / 2)
        GTEST_SKIP() << "WasapiCapture not yet implemented";

    const size_t len = std::min(source.size(), captured.size());
    source.resize(len); captured.resize(len);
    EXPECT_GT(PearsonR(source, captured), 0.99);
}

TEST(WasapiCorrelation, PearsonRSameSignalIsOne) {
    auto sig = Gen1kHzStereo(100);
    EXPECT_NEAR(PearsonR(sig, sig), 1.0, 1e-9);
}

TEST(WasapiCorrelation, PearsonROppositeIsMinusOne) {
    auto a = Gen1kHzStereo(100);
    std::vector<int16_t> b;
    b.reserve(a.size());
    for (auto s : a) b.push_back(static_cast<int16_t>(-s));
    EXPECT_NEAR(PearsonR(a, b), -1.0, 1e-3);
}

/// T067: After 2s of capture, change default audio device and verify capture resumes
/// within 1s with cross-correlation > 0.99.
/// Disabled: requires two audio devices and admin privileges.
TEST(WasapiCorrelation, DISABLED_DeviceChangeResumesCapture) {
    GTEST_SKIP() << "Requires two audio render devices and IMMDeviceEnumerator::SetDefaultEndpoint";
}
