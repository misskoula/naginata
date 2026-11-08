// Smoke test: builds the chain, renders a decaying 110 Hz tone, checks for NaN/clipping,
// and verifies that both units bypass cleanly when disengaged.
#include "naginata_dsp.h"
#include <cstdio>
#include <cmath>
#include <vector>

int main() {
    const double fs = 48000.0;
    const int n = int(fs * 3.0);
    naginata::NaginataChain chain;
    chain.prepare(fs);

    // Bypass check: both units off => output equals the input delayed by the fixed latency,
    // within the oversampling filter's passband error.
    const int L = naginata::NaginataChain::kLatencySamples;
    naginata::ChainParams off;
    chain.setParams(off);
    float maxErrBypass = 0.0f;
    std::vector<float> xs(4800);
    for (int i = 0; i < 4800; ++i) xs[i] = 0.5f * std::sin(2 * naginata::kPi * 110.0 * i / fs);
    for (int i = 0; i < 4800; ++i) {
        float y = chain.process(xs[i]);
        if (i >= L) maxErrBypass = std::fmax(maxErrBypass, std::fabs(y - xs[i - L]));
    }
    std::printf("latency: %d samples  bypass max error (vs delayed input): %g\n", L, maxErrBypass);

    // Full chain: fuzz boost + neutral fold, both engaged.
    naginata::ChainParams on;
    on.fuzz.engaged = true; on.fuzz.gain = 0.6f; on.fuzz.midMode = 1; on.fuzz.wet = 1.0f;
    on.fold.engaged = true; on.fold.posFold = 0.5f; on.fold.negFold = 0.3f;
    on.fold.posThr = 0.6f; on.fold.negThr = 0.7f; on.fold.sumMode = 1; on.fold.symmetrical = false;
    chain.setParams(on);

    double sumSq = 0.0; float peak = 0.0f; bool finite = true;
    for (int i = 0; i < n; ++i) {
        float env = std::exp(-1.5f * i / float(fs));
        float x = 0.4f * env * std::sin(2 * naginata::kPi * 110.0 * i / fs);
        float y = chain.process(x);
        if (!std::isfinite(y)) finite = false;
        peak = std::fmax(peak, std::fabs(y));
        sumSq += double(y) * y;
    }
    std::printf("finite: %s  peak: %.3f  rms: %.3f\n", finite ? "yes" : "NO", peak,
                std::sqrt(sumSq / n));
    return (finite && maxErrBypass < 1e-3f) ? 0 : 1;
}
