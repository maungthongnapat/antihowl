// AntiHowl AI - DSP primitives (no JUCE dependency, real-time safe in process paths)
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ah {

constexpr double kPi = 3.14159265358979323846;

inline float dbToLin(float d) { return std::pow(10.0f, d * 0.05f); }
inline float linToDb(float l) { return 20.0f * std::log10(std::max(l, 1e-9f)); }
template <class T> inline T clampv(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---------------------------------------------------------------- Biquad
enum class FilterType { Peaking, LowShelf, HighShelf, HighPass, BandPass };

struct BiquadCoef {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;  // a0 normalised to 1
};

// RBJ Audio-EQ-Cookbook. Q is the true Q for peaking/bandpass/highpass; for shelves it is the shelf Q.
inline BiquadCoef makeBiquad(FilterType t, double fs, double f0, double gainDb, double Q)
{
    f0 = clampv(f0, 10.0, fs * 0.49);
    Q = std::max(Q, 0.1);
    const double w0 = 2.0 * kPi * f0 / fs, c = std::cos(w0), s = std::sin(w0);
    const double A = std::pow(10.0, gainDb / 40.0), al = s / (2.0 * Q);
    double b0, b1, b2, a0, a1, a2;
    switch (t) {
    case FilterType::Peaking:
        b0 = 1 + al * A; b1 = -2 * c; b2 = 1 - al * A;
        a0 = 1 + al / A; a1 = -2 * c; a2 = 1 - al / A;
        break;
    case FilterType::LowShelf: {
        const double q = 2 * std::sqrt(A) * al;
        b0 = A * ((A + 1) - (A - 1) * c + q);
        b1 = 2 * A * ((A - 1) - (A + 1) * c);
        b2 = A * ((A + 1) - (A - 1) * c - q);
        a0 = (A + 1) + (A - 1) * c + q;
        a1 = -2 * ((A - 1) + (A + 1) * c);
        a2 = (A + 1) + (A - 1) * c - q;
        break; }
    case FilterType::HighShelf: {
        const double q = 2 * std::sqrt(A) * al;
        b0 = A * ((A + 1) + (A - 1) * c + q);
        b1 = -2 * A * ((A - 1) + (A + 1) * c);
        b2 = A * ((A + 1) + (A - 1) * c - q);
        a0 = (A + 1) - (A - 1) * c + q;
        a1 = 2 * ((A - 1) - (A + 1) * c);
        a2 = (A + 1) - (A - 1) * c - q;
        break; }
    case FilterType::HighPass:
        b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2;
        a0 = 1 + al; a1 = -2 * c; a2 = 1 - al;
        break;
    case FilterType::BandPass:  // constant 0 dB peak gain
    default:
        b0 = al; b1 = 0; b2 = -al;
        a0 = 1 + al; a1 = -2 * c; a2 = 1 - al;
        break;
    }
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

inline double biquadMagDb(const BiquadCoef& c, double fs, double f)
{
    const double w = 2.0 * kPi * f / fs;
    const std::complex<double> z1(std::cos(w), -std::sin(w)), z2 = z1 * z1;
    const std::complex<double> num = c.b0 + c.b1 * z1 + c.b2 * z2;
    const std::complex<double> den = 1.0 + c.a1 * z1 + c.a2 * z2;
    return 20.0 * std::log10(std::max(std::abs(num / den), 1e-12));
}

struct Biquad {
    BiquadCoef c;
    double z1 = 0, z2 = 0;
    inline float process(float x)
    {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return static_cast<float>(y);
    }
    void processBlock(float* x, int n) { for (int i = 0; i < n; ++i) x[i] = process(x[i]); }
    void reset() { z1 = z2 = 0; }
};

// ---------------------------------------------------------------- Gate (no look-ahead => zero added latency)
struct Gate {
    bool on = false;
    float thr = -55, atk = 2, hold = 120, rel = 250, range = -60;

    void prepare(double sampleRate) { sr = sampleRate; update(); }
    void set(bool on_, float thr_, float atk_, float hold_, float rel_, float range_)
    {
        on = on_; thr = thr_; atk = atk_; hold = hold_; rel = rel_; range = range_;
        update();
    }
    void update()
    {
        tO = dbToLin(thr); tC = dbToLin(thr - 3.0f);
        hS = static_cast<int>(std::lround(hold * sr / 1000.0));
        cA = coef(atk); cR = coef(rel); rg = dbToLin(range); eD = coef(15.0f);
    }
    inline float process(float s)
    {
        if (!on) { g = 1; open = true; hc = hS; return s; }
        const float a = s < 0 ? -s : s;
        env = a > env ? a : env * eD;
        if (env > tO) { open = true; hc = hS; }
        else if (open) {
            if (env > tC) hc = hS;
            else if (hc > 0) --hc;
            else open = false;
        }
        const float tg = open ? 1.0f : rg, c = tg > g ? cA : cR;
        g = tg + (g - tg) * c;
        return s * g;
    }
    void processBlock(float* x, int n) { for (int i = 0; i < n; ++i) x[i] = process(x[i]); }

    float g = 1; bool open = true;
private:
    float coef(float ms) const { return ms <= 0 ? 0.f : static_cast<float>(std::exp(-1.0 / (sr * ms / 1000.0))); }
    double sr = 48000;
    float env = 0, tO = 0, tC = 0, cA = 0, cR = 0, rg = 0, eD = 0;
    int hc = 0, hS = 0;
};

// ---------------------------------------------------------------- Compressor + brick-wall limiter (no look-ahead)
struct CompLimiter {
    bool compOn = true, limOn = true;
    float thr = -16, ratio = 3, atk = 10, rel = 150, knee = 6, makeup = 0, ceil = -2;

    void prepare(double sampleRate) { sr = sampleRate; update(); }
    void update()
    {
        cA = coef(atk); cR = coef(rel); mk = dbToLin(makeup); ceilL = dbToLin(ceil);
        lr = 1.0f - coef(60.0f); slope = 1.0f - 1.0f / std::max(1.0f, ratio);
    }
    // a = detector level (linear peak of the sample / linked channels). returns total linear gain to apply.
    inline float gain(float a)
    {
        float gc = 1;
        if (compOn) {
            const float cf = a > env ? cA : cR;
            env = a + cf * (env - a);
            const float over = 20.0f * std::log10(env + 1e-9f) - thr;
            float gr = 0;
            if (2 * over > knee) gr = over * slope;
            else if (2 * over > -knee) gr = slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2 * knee);
            if (gr > 0) gc = std::pow(10.0f, -gr * 0.05f);
        }
        float gt = gc * mk, glim = 1;
        if (limOn) {
            const float pk = a * gt, need = pk > ceilL ? ceilL / pk : 1.0f;
            gl += (1.0f - gl) * lr;
            if (need < gl) gl = need;
            gt *= gl; glim = gl;
        } else gl = 1;
        const float red = gc * glim;
        if (red < minRed) minRed = red;
        return gt;
    }
    float takeReductionDb() { const float r = -linToDb(minRed); minRed = 1; return r; }
private:
    float coef(float ms) const { return ms <= 0 ? 0.f : static_cast<float>(std::exp(-1.0 / (sr * ms / 1000.0))); }
    double sr = 48000;
    float cA = 0, cR = 0, mk = 1, ceilL = 1, lr = 0, slope = 0, env = 0, gl = 1, minRed = 1;
};

// ---------------------------------------------------------------- FFT / spectrum
class Fft {
public:
    explicit Fft(int n = 0) { setSize(n); }
    void setSize(int n)
    {
        size = n; rev.assign(n > 0 ? n : 0, 0); tw.assign(n > 1 ? n / 2 : 0, std::complex<float>());
        if (n < 2) return;
        int bits = 0; while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[i] = r;
        }
        for (int k = 0; k < n / 2; ++k) {
            const double a = -2.0 * kPi * k / n;
            tw[k] = std::complex<float>(static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a)));
        }
    }
    int getSize() const { return size; }
    void forward(std::complex<float>* x) const
    {
        for (int i = 0; i < size; ++i) if (i < rev[i]) std::swap(x[i], x[rev[i]]);
        for (int len = 2; len <= size; len <<= 1) {
            const int half = len / 2, step = size / len;
            for (int i = 0; i < size; i += len)
                for (int k = 0; k < half; ++k) {
                    const std::complex<float> u = x[i + k], v = x[i + k + half] * tw[k * step];
                    x[i + k] = u + v; x[i + k + half] = u - v;
                }
        }
    }
private:
    int size = 0; std::vector<int> rev; std::vector<std::complex<float>> tw;
};

// Same scaling as the Web Audio AnalyserNode: Blackman window, |X|/N, exponential smoothing on magnitude, dB output.
class SpectrumAnalyzer {
public:
    void setup(int fftSize, float smoothing)
    {
        sm = smoothing;
        if (fftSize == n) return;
        n = fftSize; fft.setSize(n);
        win.resize(n); buf.resize(n); prev.assign(n / 2, 0.f);
        for (int i = 0; i < n; ++i) {
            const double a = 0.16;
            win[i] = static_cast<float>((1 - a) / 2 - 0.5 * std::cos(2 * kPi * i / n) + a / 2 * std::cos(4 * kPi * i / n));
        }
    }
    int fftSize() const { return n; }
    int bins() const { return n / 2; }
    void process(const float* samples, float* outDb)
    {
        for (int i = 0; i < n; ++i) buf[i] = std::complex<float>(samples[i] * win[i], 0.f);
        fft.forward(buf.data());
        for (int k = 0; k < n / 2; ++k) {
            const float m = std::abs(buf[k]) / static_cast<float>(n);
            prev[k] = sm * prev[k] + (1 - sm) * m;
            outDb[k] = 20.0f * std::log10(prev[k] + 1e-12f);
        }
    }
private:
    int n = 0; float sm = 0.5f; Fft fft; std::vector<float> win, prev; std::vector<std::complex<float>> buf;
};

// ---------------------------------------------------------------- lock-free helpers
// Audio thread writes, analysis/UI threads read the most recent samples.
class AudioRing {
public:
    explicit AudioRing(int sizePow2 = 1 << 16) : buf(sizePow2, 0.f), mask(static_cast<uint64_t>(sizePow2) - 1) {}
    void pushBlock(const float* x, int n)
    {
        const uint64_t w = wr.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) buf[(w + static_cast<uint64_t>(i)) & mask] = x[i];
        wr.store(w + static_cast<uint64_t>(n), std::memory_order_release);
    }
    void copyLatest(float* dst, int n) const
    {
        const int64_t w = static_cast<int64_t>(wr.load(std::memory_order_acquire));
        for (int i = 0; i < n; ++i) {
            const int64_t idx = w - n + i;
            dst[i] = idx < 0 ? 0.f : buf[static_cast<uint64_t>(idx) & mask];
        }
    }
private:
    std::vector<float> buf; uint64_t mask; std::atomic<uint64_t> wr{ 0 };
};

template <class T, unsigned N> class SpscQueue {
    static_assert((N & (N - 1)) == 0, "N must be a power of two");
public:
    bool push(const T& x)
    {
        const unsigned w = wi.load(std::memory_order_relaxed), r = ri.load(std::memory_order_acquire);
        if (w - r >= N) return false;
        items[w & (N - 1)] = x; wi.store(w + 1, std::memory_order_release); return true;
    }
    bool pop(T& x)
    {
        const unsigned r = ri.load(std::memory_order_relaxed);
        if (r == wi.load(std::memory_order_acquire)) return false;
        x = items[r & (N - 1)]; ri.store(r + 1, std::memory_order_release); return true;
    }
private:
    std::array<T, N> items{}; std::atomic<unsigned> wi{ 0 }, ri{ 0 };
};

// Atomic parameter bank shared between UI thread (writer) and audio thread (reader).
template <int N> struct ParamSet {
    std::array<std::atomic<float>, N> v;
    ParamSet() { for (auto& a : v) a.store(0.f, std::memory_order_relaxed); }
    float get(int i) const { return v[i].load(std::memory_order_relaxed); }
    void set(int i, float x) { v[i].store(x, std::memory_order_relaxed); }
};

} // namespace ah
