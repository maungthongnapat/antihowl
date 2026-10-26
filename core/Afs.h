// AntiHowl AI - automatic feedback suppressor controller (one per channel).
// analyze() runs on the analysis thread; the UI calls the snapshot/edit functions (mutex protected).
// The audio thread is only ever touched through ChannelStrip::cmds (lock-free queue).
#pragma once
#include "Detector.h"
#include "Strip.h"
#include <chrono>
#include <deque>
#include <mutex>
#include <string>

namespace ah {

struct AfsSettings {
    bool on = true;
    int sens = 3;
    float depthMax = 18, q = 30, relSec = 45;
    bool lock = false;
};

struct SlotView {
    bool on = false, manual = false;
    double f = 1000, lastEvent = -1e9;
    float q = 30, depth = 0, lastP = 0;
};

struct TrackView { double f; float p, level, growth; };

inline double nowSec()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

class AfsController {
public:
    void bind(SpscQueue<NotchCmd, 256>* queue, double sampleRate) { q = queue; sr = sampleRate; }

    // ---- analysis thread -------------------------------------------------
    void analyze(double now, const AfsSettings& st, const AudioRing& ring)
    {
        const DetCfg& cfg = sensCfg(st.sens);
        if (now - lastRun < cfg.intervalMs / 1000.0) return;
        if (now - lastRun > 1.0) { std::lock_guard<std::mutex> lk(m); det.reset(); }
        lastRun = now;
        samples.resize(static_cast<size_t>(cfg.fft));
        spec.resize(static_cast<size_t>(cfg.fft / 2));
        ring.copyLatest(samples.data(), cfg.fft);
        sa.setup(cfg.fft, cfg.sm);
        sa.process(samples.data(), spec.data());
        std::lock_guard<std::mutex> lk(m);
        const auto ev = det.run(spec.data(), cfg.fft / 2, sr, cfg.fft, cfg, now);
        for (const auto& e : ev) onHowl(e, now, st);
        if (now - lastExpire > 0.4) { lastExpire = now; expire(now, st); }
    }

    // ---- UI thread ---------------------------------------------------------
    void snapshot(std::array<SlotView, kNotches>& s, std::vector<TrackView>& t, float& risk)
    {
        std::lock_guard<std::mutex> lk(m);
        for (int i = 0; i < kNotches; ++i) s[static_cast<size_t>(i)] = slots[static_cast<size_t>(i)];
        t.clear();
        for (const auto& k : det.tracks)
            if (k.p >= 0.25f && k.miss == 0) t.push_back({ k.f, k.p, k.v, k.growth });
        risk = det.maxP;
    }
    bool addManual(double f, float depth, float qv)
    {
        std::lock_guard<std::mutex> lk(m);
        for (int j = 0; j < kNotches; ++j)
            if (!slots[static_cast<size_t>(j)].on) {
                auto& x = slots[static_cast<size_t>(j)];
                x.on = true; x.manual = true; x.f = f; x.depth = depth; x.q = qv; x.lastEvent = -1e9;
                push(j); return true;
            }
        return false;
    }
    void edit(int j, double f, float depth, float qv)   // user changes an existing notch (becomes manual)
    {
        std::lock_guard<std::mutex> lk(m);
        auto& x = slots[static_cast<size_t>(j)];
        if (!x.on) return;
        x.f = f; x.depth = depth; x.q = qv; x.manual = true; push(j);
    }
    void remove(int j)
    {
        std::lock_guard<std::mutex> lk(m);
        auto& x = slots[static_cast<size_t>(j)];
        if (!x.on) return;
        x.on = false; x.depth = 0; push(j);
    }
    void clearAll()
    {
        std::lock_guard<std::mutex> lk(m);
        for (int j = 0; j < kNotches; ++j)
            if (slots[static_cast<size_t>(j)].on) { slots[static_cast<size_t>(j)].on = false; slots[static_cast<size_t>(j)].depth = 0; push(j); }
    }
    // After the audio device restarts the strip is reset: push the authoritative slot state again.
    void resendAll()
    {
        std::lock_guard<std::mutex> lk(m);
        for (int j = 0; j < kNotches; ++j) if (slots[static_cast<size_t>(j)].on) push(j);
    }
    std::vector<std::string> takeLogs()
    {
        std::lock_guard<std::mutex> lk(m);
        std::vector<std::string> out(logs.begin(), logs.end());
        logs.clear();
        return out;
    }
    int activeCount()
    {
        std::lock_guard<std::mutex> lk(m);
        int c = 0; for (const auto& s : slots) if (s.on) ++c;
        return c;
    }

private:
    // caller holds m
    void push(int j)
    {
        const auto& x = slots[static_cast<size_t>(j)];
        NotchCmd c; c.slot = static_cast<uint8_t>(j); c.on = x.on;
        c.f = static_cast<float>(x.f); c.q = x.q; c.depthDb = x.depth;
        if (q) q->push(c);
    }
    void log(const std::string& s) { logs.push_back(s); if (logs.size() > 64) logs.pop_front(); }

    void onHowl(const HowlEvent& ev, double t, const AfsSettings& st)
    {
        for (int j = 0; j < kNotches; ++j) {
            auto& x = slots[static_cast<size_t>(j)];
            if (x.on && std::fabs(std::log2(ev.f / x.f)) < 0.045) {
                x.lastEvent = t; x.lastP = ev.p;
                if (!x.manual) x.exp = t + st.relSec;
                if (ev.growth > -0.5f && t - x.t > 0.7 && x.depth < st.depthMax) {
                    x.depth = std::min(st.depthMax, x.depth + 3.0f); x.t = t; push(j);
                    log("DEEPEN " + std::to_string(static_cast<int>(x.f)) + " " + std::to_string(static_cast<int>(x.depth)));
                }
                return;
            }
        }
        int j = -1;
        for (int k = 0; k < kNotches; ++k) if (!slots[static_cast<size_t>(k)].on) { j = k; break; }
        if (j < 0) {
            double bt = 1e18;
            for (int k = 0; k < kNotches; ++k)
                if (!slots[static_cast<size_t>(k)].manual && slots[static_cast<size_t>(k)].t < bt) { bt = slots[static_cast<size_t>(k)].t; j = k; }
            if (j < 0) return;
        }
        auto& x = slots[static_cast<size_t>(j)];
        x.on = true; x.f = ev.f; x.q = st.q; x.manual = false;
        x.depth = std::min(st.depthMax, clampv(std::round(ev.prom * 0.6f), 6.0f, 12.0f));
        x.t = t; x.exp = t + st.relSec; x.lastEvent = t; x.lastP = ev.p;
        push(j);
        log("HOWL " + std::to_string(static_cast<int>(ev.f)) + " " + std::to_string(static_cast<int>(ev.p * 100)) + " " + std::to_string(static_cast<int>(x.depth)));
    }

    void expire(double now, const AfsSettings& st)
    {
        if (st.lock) return;
        for (int j = 0; j < kNotches; ++j) {
            auto& x = slots[static_cast<size_t>(j)];
            if (x.on && !x.manual && now > x.exp) {
                x.on = false; x.depth = 0; push(j);
                log("RELEASE " + std::to_string(static_cast<int>(x.f)));
            }
        }
    }

    struct SlotExt : SlotView { double t = 0, exp = 0; };
    std::array<SlotExt, kNotches> slots{};

    SpscQueue<NotchCmd, 256>* q = nullptr;
    double sr = 48000, lastRun = -10, lastExpire = 0;
    std::mutex m;
    Detector det;
    SpectrumAnalyzer sa;
    std::vector<float> samples, spec;
    std::deque<std::string> logs;

};

} // namespace ah
