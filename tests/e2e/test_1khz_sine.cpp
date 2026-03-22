/// End-to-end test: stream 1 kHz sine through the full AirBeam pipeline to
/// shairport-sync Docker, capture output, and assert spectral peak at 1000 Hz ±5 Hz.
///
/// Disabled until ConnectionController (T049–T053) is implemented.
#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include <algorithm>

// TODO(T049): include "core/ConnectionController.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<int16_t> Gen1kHz(int durationMs) {
    const int sr = 44100, n = sr * durationMs / 1000;
    std::vector<int16_t> buf; buf.reserve(static_cast<size_t>(n)*2);
    for (int i = 0; i < n; ++i) {
        auto s = static_cast<int16_t>(16000.0 * std::sin(2.0*kPi*1000.0*i/sr));
        buf.push_back(s); buf.push_back(s);
    }
    return buf;
}

// Goertzel algorithm: O(N) magnitude at a single frequency bin.
double Goertzel(const std::vector<double>& x, double hz, double sr) {
    const int n = (int)x.size();
    const double w = 2.0*kPi*hz/sr, c = 2.0*std::cos(w);
    double s0=0, s1=0, s2=0;
    for (int i=0;i<n;++i){s0=x[i]+c*s1-s2;s2=s1;s1=s0;}
    return std::sqrt(s1*s1+s2*s2-c*s1*s2);
}

double PeakFreq(const std::vector<double>& s, double sr, double lo, double hi) {
    double best=-1, freq=lo;
    for (double f=lo;f<=hi;f+=1.0){double m=Goertzel(s,f,sr);if(m>best){best=m;freq=f;}}
    return freq;
}

} // namespace

TEST(E2E1kHz, GoertzelSelfCheck) {
    auto pcm = Gen1kHz(200);
    std::vector<double> mono; mono.reserve(pcm.size()/2);
    for (size_t i=0;i<pcm.size();i+=2) mono.push_back(pcm[i]);
    EXPECT_NEAR(PeakFreq(mono, 44100.0, 800.0, 1200.0), 1000.0, 5.0);
}

TEST(E2E1kHz, DISABLED_FullPipeline1kHzAtReceiver) {
    // TODO(T049–T053): connect pipeline, stream Gen1kHz(2000), capture at receiver
    std::vector<double> captured;
    if (captured.empty())
        GTEST_SKIP() << "Pipeline not implemented yet";
    EXPECT_NEAR(PeakFreq(captured, 44100.0, 800.0, 1200.0), 1000.0, 5.0);
}
