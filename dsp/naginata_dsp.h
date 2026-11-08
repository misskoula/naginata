// Naginata DSP core: Unit 1 "Shillelagh-style" fuzz -> Unit 2 "WF-1-style" wavefolder.
// Portable C++17, header-only, no framework dependency. Wrappers (VST3/CLAP) call this.
// The nonlinear path runs at 4x the host rate. Latency is fixed at NaginataChain::kLatencySamples.
// Approximations: built from the Shillelagh netlist topology and the WF-1 public
// description (split +/- folding, Lim/0/Dist sum stage, Sym switch). Not circuit-exact.
#pragma once
#include <algorithm>
#include <cmath>

namespace naginata {

constexpr double kPi = 3.14159265358979323846;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float dbToLin(float db) { return std::pow(10.0f, db / 20.0f); }

// Soft knee at 0.8, asymptotic to 1.0. Replaces a hard clamp, so peaks compress instead of clipping.
inline float softLimit(float x) {
    constexpr float knee = 0.8f;
    const float a = std::fabs(x);
    if (a <= knee) return x;
    const float y = knee + (1.0f - knee) * std::tanh((a - knee) / (1.0f - knee));
    return x < 0.0f ? -y : y;
}

// One-pole smoother for a continuous control. Removes zipper noise from per-block updates.
struct Smoothed {
    float cur = 0.0f, target = 0.0f, coef = 1.0f;

    void setTime(double fs, double seconds) { coef = float(1.0 - std::exp(-1.0 / (seconds * fs))); }
    void snap(float v) { cur = target = v; }
    void set(float v) { target = v; }
    float next() { cur += coef * (target - cur); return cur; }
};

// Second-order section (RBJ cookbook)
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    float process(float x) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }

    void setLowpass(double fs, double f, double q) {
        double w = 2 * kPi * f / fs, c = std::cos(w), al = std::sin(w) / (2 * q);
        set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void setHighpass(double fs, double f, double q) {
        double w = 2 * kPi * f / fs, c = std::cos(w), al = std::sin(w) / (2 * q);
        set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void setPeaking(double fs, double f, double q, double gainDb) {
        double A = std::pow(10.0, gainDb / 40.0), w = 2 * kPi * f / fs;
        double c = std::cos(w), al = std::sin(w) / (2 * q);
        set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }

private:
    void set(double b0n, double b1n, double b2n, double a0, double a1n, double a2n) {
        b0 = float(b0n / a0); b1 = float(b1n / a0); b2 = float(b2n / a0);
        a1 = float(a1n / a0); a2 = float(a2n / a0);
    }
};

// 4x oversampler: zero-stuffed upsampling and FIR decimation, 65-tap Blackman-windowed sinc.
// Latency is (taps - 1) / 2 high-rate samples per stage. With both stages that is 16 host-rate samples.
class Oversampler4 {
public:
    static constexpr int kOs = 4;
    static constexpr int kTaps = 65;                       // odd: integer group delay
    // Group delay per stage is (kTaps-1)/2 high-rate samples; two stages, converted to host rate
    static constexpr int kLatency = ((kTaps - 1) / 2 / kOs) * 2;  // 16 host-rate samples

    void prepare() {
        designFir();
        reset();
    }
    void reset() {
        std::fill(upHist_, upHist_ + kTaps, 0.0f);
        std::fill(downHist_, downHist_ + kTaps, 0.0f);
    }

    // One host-rate sample in, kOs high-rate samples out
    void upsample(float x, float* out) {
        for (int k = 0; k < kOs; ++k) {
            push(upHist_, k == 0 ? x * float(kOs) : 0.0f);   // compensate for zero-stuffing
            out[k] = dot(upHist_);
        }
    }

    // kOs high-rate samples in, one host-rate sample out. The output is taken at the first sample
    // of the group, so the decimation phase matches the upsampler and the latency is an integer.
    float downsample(const float* in) {
        push(downHist_, in[0]);
        const float y = dot(downHist_);
        for (int k = 1; k < kOs; ++k) push(downHist_, in[k]);
        return y;
    }

private:
    // Each stage has its own history. Sharing one buffer mixes the upsampled and decimated streams.
    void push(float* hist, float v) {
        for (int i = kTaps - 1; i > 0; --i) hist[i] = hist[i - 1];
        hist[0] = v;
    }
    float dot(const float* hist) const {
        float s = 0.0f;
        for (int i = 0; i < kTaps; ++i) s += fir_[i] * hist[i];
        return s;
    }
    void designFir() {
        // Cutoff at 0.45 x host Nyquist, expressed relative to the high rate
        const double fc = 0.45 / kOs;
        const int mid = (kTaps - 1) / 2;
        double sum = 0.0;
        for (int n = 0; n < kTaps; ++n) {
            const int t = n - mid;
            const double sinc = (t == 0) ? 2.0 * fc : std::sin(2.0 * kPi * fc * t) / (kPi * t);
            const double w = 0.42 - 0.5 * std::cos(2.0 * kPi * n / (kTaps - 1))
                           + 0.08 * std::cos(4.0 * kPi * n / (kTaps - 1));
            fir_[n] = float(sinc * w);
            sum += fir_[n];
        }
        for (int n = 0; n < kTaps; ++n) fir_[n] = float(fir_[n] / sum);   // unity DC gain
    }

    float fir_[kTaps] = {};
    float upHist_[kTaps] = {};
    float downHist_[kTaps] = {};
};

// ---------------------------------------------------------------- Unit 1: fuzz
struct FuzzParams {
    bool engaged = false;
    float level = 0.7f;   // 0..1 -> -30..+6 dB
    float tone = 0.5f;    // 0..1 -> tone low-pass 1.5k..12k Hz (log)
    float gain = 0.6f;    // 0..1 -> pre-gain 0..+50 dB
    float harmonics = 0.4f; // 0..1 -> clip asymmetry: 0 = odd harmonics only, 1 = strongly even-rich
    float harmDepth = 1.0f; // 0..1 -> how much of the shaped path is mixed into the clip
    float wet = 1.0f;     // 0..1 dry/wet
    int midMode = 0;      // 3-way switch: 0 = off (flat), 1 = mid boost, 2 = mid scoop
    float hpfKnob = 0.0f; // 0 = off, 1 = 800 Hz
    float lpfKnob = 1.0f; // 1 = 20 kHz, 0 = 1 kHz
};

class FuzzUnit {
public:
    // fs is the rate the unit runs at (the oversampled rate)
    void prepare(double fs) {
        fs_ = fs;
        dcb_.setHighpass(fs, 20.0, 0.707);
        hpf_.setHighpass(fs, 20.0, 0.707);
        lpf_.setLowpass(fs, 20000.0, 0.707);
        toneLp_.setLowpass(fs, 12000.0, 0.707);
        mid_.reset();
        engageSm_ = 0.0f;
        engageCoef_ = float(1.0 - std::exp(-1.0 / (0.005 * fs)));  // 5 ms click-free switch
        sGain_.setTime(fs, 0.02);
        sLevel_.setTime(fs, 0.02);
        sWet_.setTime(fs, 0.02);
        sHarm_.setTime(fs, 0.02);
        sDepth_.setTime(fs, 0.02);
        sHarm_.snap(p_.harmonics);
        sDepth_.snap(p_.harmDepth);
        sGain_.snap(p_.gain);
        sLevel_.snap(p_.level);
        sWet_.snap(p_.wet);
        updateFilters();
        reset();
    }

    void reset() {
        dcb_.reset(); hpf_.reset(); lpf_.reset(); toneLp_.reset(); mid_.reset(); analogLp_.reset();
    }

    void setParams(const FuzzParams& p) {
        p_ = p;
        sGain_.set(p.gain);
        sLevel_.set(p.level);
        sWet_.set(p.wet);
        sHarm_.set(p.harmonics);
        sDepth_.set(p.harmDepth);
        updateFilters();
    }

    float process(float x) {
        // Engage crossfade: dry when off, fully processed when on
        engageSm_ += engageCoef_ * ((p_.engaged ? 1.0f : 0.0f) - engageSm_);
        if (engageSm_ < 1e-5f) return x;

        float y = hpf_.process(x);
        y *= dbToLin(sGain_.next() * 50.0f);             // pre-gain (0 dB .. +50 dB)
        y = mid_.process(y);                             // boost / scoop, pre-clip
        y = clip(y, sHarm_.next(), sDepth_.next());
        y = analogLp_.process(y);                        // op-amp bandwidth roll-off
        y = toneLp_.process(y);
        y = lpf_.process(y);
        y *= dbToLin(-30.0f + sLevel_.next() * 36.0f);   // -30 .. +6 dB
        y = dcb_.process(y);

        const float wet = sWet_.next();
        float out = x * (1.0f - wet) + y * wet;
        return x * (1.0f - engageSm_) + out * engageSm_;
    }

private:
    // Clip stage. The plain tanh gives odd harmonics. The shaped path adds a DC bias, which raises
    // the even harmonics (2nd, 4th). harmonics sets the bias, depth sets how much of the shaped path
    // is mixed in. Subtracting tanh(bias) keeps silence at zero.
    static float clip(float v, float harmonics, float depth) {
        const float bias = 1.0f * harmonics;   // up to 1.0, twice the earlier range
        const float shaped = std::tanh(v + bias) - std::tanh(bias);
        return (1.0f - depth) * std::tanh(v) + depth * shaped;
    }

    void updateFilters() {
        // HPF knob: 0 => ~5 Hz (effectively off), 1 => 800 Hz
        float hf = 5.0f * std::pow(160.0f, p_.hpfKnob);
        hpf_.setHighpass(fs_, hf, 0.707);
        // Tone knob: 1.5k .. 12k log
        float tf = 1500.0f * std::pow(8.0f, p_.tone);
        toneLp_.setLowpass(fs_, tf, 0.707);
        // LPF knob: 1k .. 20k log
        float lf = 1000.0f * std::pow(20.0f, p_.lpfKnob);
        lpf_.setLowpass(fs_, lf, 0.707);
        // Mid switch: boost +6 dB @ 800 Hz, scoop -12 dB @ 500 Hz
        // Mid switch: 0 = off (0 dB, identity), 1 = boost +6 dB @ 800 Hz, 2 = scoop -12 dB @ 500 Hz
        if (p_.midMode == 1)      mid_.setPeaking(fs_, 800.0, 1.0, 6.0);
        else if (p_.midMode == 2) mid_.setPeaking(fs_, 500.0, 0.8, -12.0);
        else                      mid_.setPeaking(fs_, 800.0, 1.0, 0.0);
        // Analog voicing: fixed op-amp bandwidth limit at 7 kHz
        analogLp_.setLowpass(fs_, 7000.0, 0.707);
    }

    double fs_ = 48000.0;
    FuzzParams p_;
    Biquad dcb_, hpf_, lpf_, toneLp_, mid_, analogLp_;
    float engageSm_ = 0.0f, engageCoef_ = 0.0f;
    Smoothed sGain_, sLevel_, sWet_, sHarm_, sDepth_;
};

// ------------------------------------------------------------ Unit 2: wavefolder
struct FoldParams {
    bool engaged = false;
    float clean = 0.0f;      // share of the unfolded signal kept in the output
    float posFold = 0.4f;    // positive-half fold amount
    float negFold = 0.4f;    // negative-half fold amount
    float posThr = 0.7f;     // positive fold threshold
    float negThr = 0.7f;     // negative fold threshold
    float wet = 1.0f;
    int sumMode = 1;         // 0 = Limiter, 1 = Neutral (0), 2 = Distortion
    bool symmetrical = true; // false = asymmetrical (independent +/- controls)
};

// Triangle fold of v into [-t, t] (reflects at +/-t, repeating every 4t)
inline float foldTo(float v, float t) {
    float period = 4.0f * t;
    float u = std::fmod(v + t, period);
    if (u < 0.0f) u += period;
    return (u < 2.0f * t) ? (u - t) : (3.0f * t - u);
}

class FoldUnit {
public:
    // fs is the rate the unit runs at (the oversampled rate)
    void prepare(double fs) {
        fs_ = fs;
        engageCoef_ = float(1.0 - std::exp(-1.0 / (0.005 * fs)));
        engageSm_ = 0.0f;
        for (Smoothed* s : {&sClean_, &sWet_, &sPosFold_, &sNegFold_, &sPosThr_, &sNegThr_}) s->setTime(fs, 0.02);
        sClean_.snap(p_.clean);
        sWet_.snap(p_.wet);
        sPosFold_.snap(p_.posFold);
        sNegFold_.snap(p_.negFold);
        sPosThr_.snap(p_.posThr);
        sNegThr_.snap(p_.negThr);
    }

    void setParams(const FoldParams& p) {
        p_ = p;
        sClean_.set(p.clean);
        sWet_.set(p.wet);
        sPosFold_.set(p.posFold);
        sNegFold_.set(p.negFold);
        sPosThr_.set(p.posThr);
        sNegThr_.set(p.negThr);
    }

    float process(float x) {
        engageSm_ += engageCoef_ * ((p_.engaged ? 1.0f : 0.0f) - engageSm_);
        if (engageSm_ < 1e-5f) return x;

        const float clean = sClean_.next();
        const float wet = sWet_.next();
        const float posFold = sPosFold_.next();
        const float negFold = sNegFold_.next();
        const float posThr = sPosThr_.next();
        const float negThr = sNegThr_.next();

        const bool pos = x >= 0.0f;
        const float amount = pos ? posFold : (p_.symmetrical ? posFold : negFold);
        const float thr01 = pos ? posThr : (p_.symmetrical ? posThr : negThr);
        const float thr = 0.05f + 0.95f * clampf(thr01, 0.0f, 1.0f);   // threshold 0.05 .. 1.0
        const float drive = 1.0f + 9.0f * clampf(amount, 0.0f, 1.0f);  // fold amount 1x .. 10x

        // Fold ceiling is thr: output never exceeds the input's own level in neutral mode,
        // so lowering the threshold reduces level predictably instead of boosting it.
        const float folded = foldTo(x * drive, thr);

        float summed;
        switch (p_.sumMode) {
            case 0:  summed = std::tanh(2.0f * folded) * 0.7f; break;   // limiter
            case 2:  summed = std::tanh(8.0f * folded) * 0.6f; break;   // distortion
            default: summed = folded; break;                            // neutral
        }

        const float processed = clean * x + (1.0f - clean) * summed;
        const float out = x * (1.0f - wet) + processed * wet;
        return x * (1.0f - engageSm_) + out * engageSm_;
    }

private:
    double fs_ = 48000.0;
    FoldParams p_;
    float engageSm_ = 0.0f, engageCoef_ = 0.0f;
    Smoothed sClean_, sWet_, sPosFold_, sNegFold_, sPosThr_, sNegThr_;
};

// ------------------------------------------------------------------ Full chain
struct ChainParams {
    FuzzParams fuzz;
    FoldParams fold;
};

// Series: input -> 4x upsample -> unit 1 -> unit 2 -> 4x downsample -> soft limit.
// Latency is constant whether or not the units are engaged, so host delay compensation stays correct.
class NaginataChain {
public:
    static constexpr int kLatencySamples = Oversampler4::kLatency;

    void prepare(double fs) {
        os_.prepare();
        fuzz_.prepare(fs * Oversampler4::kOs);
        fold_.prepare(fs * Oversampler4::kOs);
    }
    void setParams(const ChainParams& p) { fuzz_.setParams(p.fuzz); fold_.setParams(p.fold); }

    float process(float x) {
        float up[Oversampler4::kOs];
        float y[Oversampler4::kOs];
        os_.upsample(x, up);
        for (int k = 0; k < Oversampler4::kOs; ++k) y[k] = fold_.process(fuzz_.process(up[k]));
        return softLimit(os_.downsample(y));
    }

private:
    Oversampler4 os_;
    FuzzUnit fuzz_;
    FoldUnit fold_;
};

}  // namespace naginata
