// Core tests: build with   g++ -std=c++17 -O2 -Icore tests/core_tests.cpp -o core_tests && ./core_tests
// or via CMake option -DAH_BUILD_TESTS=ON
#include "../core/Afs.h"
#include <cstdio>
#include <functional>
#include <set>

using namespace ah;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, msg) do { if (cond) { ++g_pass; std::printf("  PASS  %s\n", msg); } else { ++g_fail; std::printf("  FAIL  %s\n", msg); } } while (0)

struct Rng {
    uint32_t s = 12345u;
    float uni() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFF) / 16777216.0f; }
    float gauss() { return std::sqrt(-2.0f * std::log(uni() + 1e-9f)) * std::cos(2.0f * 3.14159265f * uni()); }
};

static const double SR = 48000.0;

// ----------------------------------------------------------------------------------------------
static void testParams()
{
    std::printf("[params]\n");
    bool ok = true;
    for (int i = 0; i < P_COUNT; ++i) if (!stripInfo()[static_cast<size_t>(i)].key) ok = false;
    for (int i = 0; i < G_COUNT; ++i) if (!globalInfo()[static_cast<size_t>(i)].key) ok = false;
    CHECK(ok, "every strip/global parameter has a key");
    CHECK(stripInfo()[static_cast<size_t>(eqQ(2))].hi >= 30.0f, "peaking EQ Q goes up to 30 (very narrow)");
}

static void testFilters()
{
    std::printf("[filters]\n");
    auto c = makeBiquad(FilterType::Peaking, SR, 1000, 6, 1);
    CHECK(std::fabs(biquadMagDb(c, SR, 1000) - 6.0) < 0.01, "peaking +6 dB at centre");
    auto n = makeBiquad(FilterType::Peaking, SR, 2500, -18, 30);
    CHECK(std::fabs(biquadMagDb(n, SR, 2500) + 18.0) < 0.01, "notch -18 dB at centre (Q 30)");
    CHECK(biquadMagDb(n, SR, 2575) > -8.0, "notch Q30 is narrow: only ~-5 dB at +3%");
    // narrower Q => narrower bandwidth: find -3 dB width of a +12 dB bell
    auto bw = [&](double Q) {
        auto b = makeBiquad(FilterType::Peaking, SR, 1000, 12, Q);
        double lo = 1000, hi = 1000;
        while (lo > 20 && biquadMagDb(b, SR, lo) > 9.0) lo *= 0.999;
        while (hi < 20000 && biquadMagDb(b, SR, hi) > 9.0) hi *= 1.001;
        return std::log2(hi / lo);
    };
    const double w1 = bw(0.7), w2 = bw(4), w3 = bw(20);
    std::printf("        bandwidth (oct, -3 dB from peak): Q0.7=%.2f  Q4=%.2f  Q20=%.3f\n", w1, w2, w3);
    CHECK(w1 > w2 && w2 > w3 && w3 < 0.1, "Q control changes the bell width monotonically down to <0.1 octave");
    auto hp = makeBiquad(FilterType::HighPass, SR, 90, 0, 0.7071);
    CHECK(std::fabs(biquadMagDb(hp, SR, 90) + 3.01) < 0.05, "HPF is -3 dB at its corner");
    auto ls = makeBiquad(FilterType::LowShelf, SR, 1000, 6, 0.7071);
    CHECK(std::fabs(biquadMagDb(ls, SR, 50) - 6.0) < 0.15, "low shelf +6 dB below corner");
    CHECK(std::fabs(qToOctaves(octavesToQ(0.5)) - 0.5) < 1e-6, "Q <-> octave conversion round-trips");
}

static void testDynamics()
{
    std::printf("[dynamics]\n");
    Gate g; g.prepare(SR); g.set(true, -40, 1, 50, 100, -60);
    float lastLoud = 0, lastQuiet = 0;
    for (int i = 0; i < 48000 * 0.3; ++i) lastLoud = g.process(0.3f * std::sin(2 * 3.14159265f * 1000.0f * i / 48000.f));
    for (int i = 0; i < 48000 * 0.6; ++i) lastQuiet = g.process(0.003f * std::sin(2 * 3.14159265f * 1000.0f * i / 48000.f));
    CHECK(g.open == false, "gate closes on quiet signal");
    CHECK(std::fabs(lastQuiet) < 0.003f * 0.002f, "gate attenuates >50 dB below range");
    (void)lastLoud;
    CompLimiter c; c.prepare(SR); c.compOn = true; c.limOn = false; c.thr = -20; c.ratio = 4; c.atk = 5; c.rel = 100; c.makeup = 0; c.update();
    float pk = 0;
    for (int i = 0; i < 48000 * 0.5; ++i) {
        const float x = 0.5f * std::sin(2 * 3.14159265f * 1000.0f * i / 48000.f);
        const float y = x * c.gain(std::fabs(x));
        if (i > 48000 * 0.4) pk = std::max(pk, std::fabs(y));
    }
    std::printf("        comp: in -6.0 dBFS thr -20 ratio 4 -> out %.1f dBFS (expect about -16)\n", linToDb(pk));
    CHECK(linToDb(pk) < -14.0f && linToDb(pk) > -18.0f, "compressor reduces as expected");
    CompLimiter l; l.prepare(SR); l.compOn = false; l.limOn = true; l.ceil = -6; l.update();
    float lp = 0;
    for (int i = 0; i < 48000 * 0.3; ++i) { const float x = 1.8f * std::sin(2 * 3.14159265f * 200.0f * i / 48000.f); lp = std::max(lp, std::fabs(x * l.gain(std::fabs(x)))); }
    CHECK(lp <= dbToLin(-6.0f) * 1.001f, "limiter never exceeds its ceiling");
}

// ----------------------------------------------------------------------------------------------
struct ScenResult { double first = -1; int events = 0; std::set<int> freqs; float maxP = 0; };

static ScenResult scenario(int sens, double secs, const std::function<float(double, Rng&)>& gen)
{
    const DetCfg& cfg = sensCfg(sens);
    Detector det; SpectrumAnalyzer sa; sa.setup(cfg.fft, cfg.sm);
    Rng rng;
    const size_t total = static_cast<size_t>(secs * SR);
    std::vector<float> buf(total);
    for (size_t i = 0; i < total; ++i) buf[i] = gen(static_cast<double>(i) / SR, rng);
    std::vector<float> spec(static_cast<size_t>(cfg.fft / 2));
    ScenResult r;
    for (double t = cfg.intervalMs / 1000.0 + cfg.fft / SR; t < secs; t += cfg.intervalMs / 1000.0) {
        const size_t end = static_cast<size_t>(t * SR);
        sa.process(buf.data() + end - static_cast<size_t>(cfg.fft), spec.data());
        const auto ev = det.run(spec.data(), cfg.fft / 2, SR, cfg.fft, cfg, t);
        r.maxP = std::max(r.maxP, det.maxP);
        for (const auto& e : ev) { ++r.events; if (r.first < 0) r.first = t; r.freqs.insert(static_cast<int>(std::lround(e.f / 10.0)) * 10); }
    }
    return r;
}

static void testDetector()
{
    std::printf("[detector]\n");
    const double TP = 2 * kPi;
    auto howl = [&](double f) { return [=](double t, Rng& r) { return 0.00018f * r.gauss() + static_cast<float>((t < 0.5 ? 0.0003 : 0.0003 * std::pow(10.0, std::min(t - 0.5, 2.5))) * std::sin(TP * f * t)); }; };
    for (int s : { 1, 3, 5 }) {
        auto r = scenario(s, 4, howl(2500));
        std::printf("        growing howl 2.5k sens %d: first=%.2fs events=%d\n", s, r.first, r.events);
        char m[96]; std::snprintf(m, sizeof m, "howl detected at sensitivity %d within 1.3 s, only at 2.5 kHz", s);
        CHECK(r.first > 0 && r.first < 1.3 && r.freqs.size() == 1 && *r.freqs.begin() == 2500, m);
    }
    {   // higher sensitivity reacts sooner
        auto a = scenario(1, 4, howl(2500)), b = scenario(5, 4, howl(2500));
        CHECK(b.first < a.first, "higher sensitivity detects earlier");
    }
    for (int s : { 1, 3, 5 }) {
        auto r = scenario(s, 6, [](double, Rng& g) { return 0.002f * g.gauss(); });
        char m[96]; std::snprintf(m, sizeof m, "pure noise: no false alarms (sens %d)", s);
        CHECK(r.events == 0, m);
    }
    auto voice = [&](bool vib, int harmonics) {
        return [=](double t, Rng& g) {
            const double ph = TP * 220 * t + (vib ? (220 * 0.012 / 5.5) * (-std::cos(TP * 5.5 * t)) : 0.0);
            double v = 0; for (int h = 1; h <= harmonics; ++h) v += std::sin(h * ph) / h;
            return static_cast<float>(0.05 * v) + 0.0005f * g.gauss(); };
    };
    for (int s : { 3, 5 }) {
        auto a = scenario(s, 6, voice(true, 12)), b = scenario(s, 6, voice(false, 12));
        char m[96]; std::snprintf(m, sizeof m, "sung vowel with harmonics is not howl (sens %d)", s);
        CHECK(a.events == 0 && b.events == 0, m);
    }
    {
        auto r = scenario(3, 5, [&](double t, Rng& g) {
            double v = 0; for (int h = 1; h <= 12; ++h) v += std::sin(TP * 220 * h * t) / h;
            return static_cast<float>(0.05 * v) + 0.0005f * g.gauss()
                + static_cast<float>(0.0004 * std::pow(10.0, std::min(t, 3.5) * 0.9) * std::sin(TP * 3150 * t)); });
        std::printf("        voice + howl 3.15k: first=%.2fs freqs=", r.first); for (int f : r.freqs) std::printf("%d ", f); std::printf("\n");
        CHECK(r.first > 0 && r.freqs.size() == 1 && *r.freqs.begin() == 3150, "howl found even while someone sings; voice partials ignored");
    }
    {
        auto r = scenario(3, 5, [&](double t, Rng& g) {
            const double a = 0.0004 * std::pow(10.0, std::min(t, 3.0));
            return 0.00018f * g.gauss() + static_cast<float>(a * std::sin(TP * 2000 * t) + a * 0.018 * std::sin(TP * 6000 * t)); });
        CHECK(r.freqs.size() == 1 && *r.freqs.begin() == 2000, "3rd harmonic of a howl is not treated as a second howl");
    }
    {
        auto r = scenario(5, 7, [&](double t, Rng& g) {
            const double ph = std::fmod(t, 0.75); const int n = static_cast<int>(t / 0.75);
            const int semis[6] = { 0, 2, 4, 5, 7, 9 };
            const double f = 196.0 * std::pow(2.0, semis[n % 6] / 12.0);
            double v = 0; if (ph < 0.5) { const double e = std::min(1.0, ph / 0.03); for (int h = 1; h <= 10; ++h) v += std::sin(TP * f * h * t) / h * e; }
            return static_cast<float>(0.04 * v) + 0.0005f * g.gauss(); });
        CHECK(r.events == 0, "repeated sung note onsets do not trigger (max sensitivity)");
    }
}

// ----------------------------------------------------------------------------------------------
static float bandPeakDb(const AudioRing& ring, double f0, double f1)
{
    SpectrumAnalyzer sa; sa.setup(8192, 0.0f);
    std::vector<float> s(8192), d(4096);
    ring.copyLatest(s.data(), 8192);
    sa.process(s.data(), d.data());
    float m = -200;
    for (int k = 0; k < 4096; ++k) { const double f = k * SR / 8192; if (f >= f0 && f <= f1) m = std::max(m, d[static_cast<size_t>(k)]); }
    return m;
}

static void setDefaults(ParamSet<P_COUNT>& p) { for (int i = 0; i < P_COUNT; ++i) p.set(i, stripInfo()[static_cast<size_t>(i)].def); }

static void testStripEq()
{
    std::printf("[strip EQ]\n");
    ChannelStrip st; st.prepare(SR, 512);
    ParamSet<P_COUNT> p; setDefaults(p);
    p.set(P_COMP_ON, 0); p.set(P_LIM_ON, 0); p.set(P_HPF_ON, 0);
    p.set(eqF(2), 1000); p.set(eqG(2), 12); p.set(eqQ(2), 20);       // narrow +12 dB bell at 1 kHz
    Rng rng; std::vector<float> in(256), out(256);
    auto measure = [&](double f) {
        double sumIn = 0, sumOut = 0; double ph = 0;
        for (int blk = 0; blk < 200; ++blk) {
            for (auto& x : in) { x = 0.01f * static_cast<float>(std::sin(ph)); ph += 2 * kPi * f / SR; }
            st.process(in.data(), out.data(), 256, p, false, 1.f, false, 0.f);
            if (blk > 100) for (int i = 0; i < 256; ++i) { sumIn += in[static_cast<size_t>(i)] * in[static_cast<size_t>(i)]; sumOut += out[static_cast<size_t>(i)] * out[static_cast<size_t>(i)]; }
        }
        return 10 * std::log10(sumOut / sumIn);
    };
    const double a = measure(1000), b = measure(1100), c = measure(500);
    std::printf("        gain at 1000 Hz=%.1f dB, 1100 Hz=%.1f dB, 500 Hz=%.1f dB\n", a, b, c);
    CHECK(std::fabs(a - 12.0) < 0.5, "narrow band gives +12 dB at centre");
    CHECK(b < 5.0 && std::fabs(c) < 0.3, "narrow band leaves neighbours (1.1 kHz, 500 Hz) mostly untouched");
}

static void testClosedLoop()
{
    std::printf("[closed loop: simulated howl -> AI -> notch]\n");
    for (int sens : { 3, 1, 5 }) {
        ChannelStrip st; st.prepare(SR, 512);
        AfsController afs; afs.bind(&st.cmds, SR);
        ParamSet<P_COUNT> p; setDefaults(p);
        AfsSettings set; set.sens = sens; set.depthMax = 18; set.q = 30;
        Rng rng; std::vector<float> in(128), out(128);
        double t = 0, levelBefore = -200, levelAfter = -200, tFirst = -1;
        const int blocks = static_cast<int>(8.0 * SR / 128);
        for (int b = 0; b < blocks; ++b) {
            for (auto& x : in) x = 0.0001f * rng.gauss();
            st.process(in.data(), out.data(), 128, p, true, 1.f, true, 2500.f);
            t += 128 / SR;
            afs.analyze(t, set, st.detRing);
            if (tFirst < 0 && afs.activeCount() > 0) { tFirst = t; levelBefore = bandPeakDb(st.rtaRing, 2000, 3000); }
        }
        levelAfter = bandPeakDb(st.rtaRing, 2000, 3000);
        std::array<SlotView, kNotches> sv; std::vector<TrackView> tv; float risk;
        afs.snapshot(sv, tv, risk);
        int on = 0; double f0 = 0; float depth = 0;
        for (const auto& s : sv) if (s.on) { ++on; f0 = s.f; depth = s.depth; }
        std::printf("        sens %d: first notch at %.2fs  f=%.0f Hz depth=%.0f dB  level at 2.5k: %.1f -> %.1f dBFS(spectrum)  notches=%d\n",
                    sens, tFirst, f0, depth, levelBefore, levelAfter, on);
        char m[128];
        std::snprintf(m, sizeof m, "sens %d: AI places a notch near 2.5 kHz", sens);
        CHECK(on >= 1 && on <= 3 && std::fabs(f0 - 2500) < 80, m);
        std::snprintf(m, sizeof m, "sens %d: howl is suppressed by >= 20 dB after the notch", sens);
        CHECK(levelAfter < levelBefore - 20.0f, m);
    }
    {   // control: with the suppressor disabled the howl stays at full level
        ChannelStrip st; st.prepare(SR, 512);
        ParamSet<P_COUNT> p; setDefaults(p);
        Rng rng; std::vector<float> in(128), out(128);
        for (int b = 0; b < static_cast<int>(6.0 * SR / 128); ++b) {
            for (auto& x : in) x = 0.0001f * rng.gauss();
            st.process(in.data(), out.data(), 128, p, false, 1.f, true, 2500.f);
        }
        const float lvl = bandPeakDb(st.rtaRing, 2000, 3000);
        std::printf("        control (AFS off): level at 2.5k = %.1f dB\n", lvl);
        CHECK(lvl > -40.0f, "without AFS the simulated howl saturates (control)");
    }
    {   // manual notch edit + queue
        ChannelStrip st; st.prepare(SR, 512);
        AfsController afs; afs.bind(&st.cmds, SR);
        afs.addManual(1000.0, 12.0f, 10.0f);
        ParamSet<P_COUNT> p; setDefaults(p); p.set(P_COMP_ON, 0); p.set(P_LIM_ON, 0); p.set(P_HPF_ON, 0);
        std::vector<float> in(256), out(256); double ph = 0, ss = 0, si = 0;
        for (int b = 0; b < 300; ++b) {
            for (auto& x : in) { x = 0.05f * static_cast<float>(std::sin(ph)); ph += 2 * kPi * 1000 / SR; }
            st.process(in.data(), out.data(), 256, p, true, 1.f, false, 0.f);
            if (b > 200) for (int i = 0; i < 256; ++i) { ss += out[static_cast<size_t>(i)] * out[static_cast<size_t>(i)]; si += in[static_cast<size_t>(i)] * in[static_cast<size_t>(i)]; }
        }
        CHECK(std::fabs(10 * std::log10(ss / si) + 12.0) < 0.6, "manual notch applies -12 dB through the lock-free queue");
        afs.remove(0);
        for (int b = 0; b < 300; ++b) { for (auto& x : in) { x = 0.05f * static_cast<float>(std::sin(ph)); ph += 2 * kPi * 1000 / SR; } st.process(in.data(), out.data(), 256, p, true, 1.f, false, 0.f); }
        double s2 = 0, i2 = 0;
        for (int b = 0; b < 50; ++b) { for (auto& x : in) { x = 0.05f * static_cast<float>(std::sin(ph)); ph += 2 * kPi * 1000 / SR; } st.process(in.data(), out.data(), 256, p, true, 1.f, false, 0.f);
            for (int i = 0; i < 256; ++i) { s2 += out[static_cast<size_t>(i)] * out[static_cast<size_t>(i)]; i2 += in[static_cast<size_t>(i)] * in[static_cast<size_t>(i)]; } }
        CHECK(std::fabs(10 * std::log10(s2 / i2)) < 0.3, "removing the notch restores unity gain");
    }
}

int main()
{
    testParams(); testFilters(); testDynamics(); testStripEq(); testDetector(); testClosedLoop();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
