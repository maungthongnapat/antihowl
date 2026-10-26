// AntiHowl AI - one mono channel strip. Everything on the audio path is zero-latency (no look-ahead).
// Stage order: trim -> [sim loop] -> gate -> (tap for AI) -> HPF -> 12 notches -> 6-band EQ -> (tap for RTA)
//              -> compressor/limiter -> fader -> (meter) -> mute
#pragma once
#include "Params.h"

namespace ah {

struct NotchCmd {
    uint8_t slot = 0;
    bool on = false;
    float f = 1000, q = 30, depthDb = 0;
};

class ChannelStrip {
public:
    ChannelStrip() : detRing(1 << 16), rtaRing(1 << 15), simBuf(1 << 15, 0.f) {}

    void prepare(double sampleRate, int maxBlockSize)
    {
        sr = sampleRate; maxBlock = maxBlockSize;
        gate.prepare(sr); comp.prepare(sr);
        first = true;
        for (auto& n : notch) { n = NotchRt(); }
        for (auto& b : bands) b = Band();
        hpf.reset();
        std::fill(simBuf.begin(), simBuf.end(), 0.f);
        simW = 0; simPrev = false;
    }

    // Processes n samples. 'in' may alias 'out'. muteTarget: 0 = silent, 1 = pass.
    void process(const float* in, float* out, int n, const ParamSet<P_COUNT>& p,
                 bool afsActive, float muteTarget, bool simOn, float simF)
    {
        if (n > maxBlock) n = maxBlock;
        if (out != in) std::memcpy(out, in, sizeof(float) * static_cast<size_t>(n));

        NotchCmd c;
        while (cmds.pop(c)) applyCmd(c);
        syncParams(p);
        if (first) { trimG = trimTarget; faderG = faderTarget; muteG = muteTarget; first = false; }

        ramp(out, n, trimG, trimTarget);
        if (simOn) simPre(out, n, simF); else simPrev = false;

        gate.processBlock(out, n);
        detRing.pushBlock(out, n);

        if (hpfOn) hpf.processBlock(out, n);
        notchStage(out, n, afsActive);
        for (int b = 0; b < kBands; ++b)
            if (bands[b].active) bands[b].bq.processBlock(out, n);

        rtaRing.pushBlock(out, n);
        if (simOn) simPost(out, n);

        for (int i = 0; i < n; ++i) out[i] *= comp.gain(out[i] < 0 ? -out[i] : out[i]);
        ramp(out, n, faderG, faderTarget);

        float pk = 0, ss = 0;
        for (int i = 0; i < n; ++i) { const float a = out[i] < 0 ? -out[i] : out[i]; if (a > pk) pk = a; ss += out[i] * out[i]; }
        if (pk > peakAcc.load(std::memory_order_relaxed)) peakAcc.store(pk, std::memory_order_relaxed);
        rmsNow.store(std::sqrt(ss / static_cast<float>(std::max(n, 1))), std::memory_order_relaxed);
        compGrDb.store(comp.takeReductionDb(), std::memory_order_relaxed);
        gateOpen.store(gate.open, std::memory_order_relaxed);
        gateAttDb.store(gate.on ? -linToDb(gate.g) : 0.f, std::memory_order_relaxed);

        ramp(out, n, muteG, muteTarget);
    }

    // ---- shared with other threads
    SpscQueue<NotchCmd, 256> cmds;            // AFS controller -> audio thread
    AudioRing detRing;                         // after gate, before notches (what the AI listens to)
    AudioRing rtaRing;                         // after EQ, before dynamics (what the spectrum shows)
    std::atomic<float> peakAcc{ 0.f };         // max |x| since last UI read (UI does exchange(0))
    std::atomic<float> rmsNow{ 0.f };
    std::atomic<float> compGrDb{ 0.f }, gateAttDb{ 0.f };
    std::atomic<bool> gateOpen{ true };

private:
    struct Band { Biquad bq; bool active = false; };
    struct NotchRt {
        double f = 1000; float q = 30, target = 0, cur = 0;
        Biquad bq; float lastCur = 1e9f; double lastF = 0; float lastQ = 0;
    };

    void applyCmd(const NotchCmd& c)
    {
        if (c.slot >= kNotches) return;
        NotchRt& s = notch[c.slot];
        s.f = c.f; s.q = c.q;
        s.target = c.on ? -std::fabs(c.depthDb) : 0.f;
    }

    void syncParams(const ParamSet<P_COUNT>& p)
    {
        float cur[P_COUNT];
        for (int i = 0; i < P_COUNT; ++i) cur[i] = p.get(i);
        auto ch = [&](int i) { return first || cur[i] != cache[static_cast<size_t>(i)]; };

        if (ch(P_HPF_ON) || ch(P_HPF_F)) {
            hpfOn = cur[P_HPF_ON] > 0.5f;
            hpf.c = makeBiquad(FilterType::HighPass, sr, cur[P_HPF_F], 0.0, 0.7071);
        }
        for (int b = 0; b < kBands; ++b)
            if (ch(eqF(b)) || ch(eqG(b)) || ch(eqQ(b))) {
                bands[b].bq.c = makeBiquad(kEqType[b], sr, cur[eqF(b)], cur[eqG(b)], cur[eqQ(b)]);
                bands[b].active = std::fabs(cur[eqG(b)]) > 0.01f;
            }
        if (ch(P_GATE_ON) || ch(P_GATE_THR) || ch(P_GATE_ATK) || ch(P_GATE_HOLD) || ch(P_GATE_REL) || ch(P_GATE_RANGE))
            gate.set(cur[P_GATE_ON] > 0.5f, cur[P_GATE_THR], cur[P_GATE_ATK], cur[P_GATE_HOLD], cur[P_GATE_REL], cur[P_GATE_RANGE]);
        if (ch(P_COMP_ON) || ch(P_COMP_THR) || ch(P_COMP_RATIO) || ch(P_COMP_ATK) || ch(P_COMP_REL) || ch(P_COMP_MAKEUP)
            || ch(P_LIM_ON) || ch(P_LIM_CEIL)) {
            comp.compOn = cur[P_COMP_ON] > 0.5f; comp.thr = cur[P_COMP_THR]; comp.ratio = cur[P_COMP_RATIO];
            comp.atk = cur[P_COMP_ATK]; comp.rel = cur[P_COMP_REL]; comp.makeup = cur[P_COMP_MAKEUP];
            comp.limOn = cur[P_LIM_ON] > 0.5f; comp.ceil = cur[P_LIM_CEIL];
            comp.update();
        }
        trimTarget = dbToLin(cur[P_TRIM]);
        faderTarget = cur[P_FADER] <= -59.5f ? 0.f : dbToLin(cur[P_FADER]);
        for (int i = 0; i < P_COUNT; ++i) cache[static_cast<size_t>(i)] = cur[i];
    }

    static void ramp(float* x, int n, float& g, float target)
    {
        if (g == target) {
            if (target != 1.0f) for (int i = 0; i < n; ++i) x[i] *= target;
            return;
        }
        const float step = (target - g) / static_cast<float>(n);
        float v = g;
        for (int i = 0; i < n; ++i) { v += step; x[i] *= v; }
        g = target;
    }

    void notchStage(float* x, int n, bool afsActive)
    {
        const float alpha = 1.0f - std::exp(-32.0f / static_cast<float>(sr * 0.006));
        for (auto& s : notch) {
            const float tgt = afsActive ? s.target : 0.f;
            if (tgt == 0.f && s.cur == 0.f) continue;
            for (int off = 0; off < n; off += 32) {
                const int m = std::min(32, n - off);
                s.cur += (tgt - s.cur) * alpha;
                if (std::fabs(tgt - s.cur) < 0.01f) s.cur = tgt;
                if (s.cur == 0.f) { s.bq.reset(); continue; }
                if (s.cur != s.lastCur || s.f != s.lastF || s.q != s.lastQ) {
                    s.bq.c = makeBiquad(FilterType::Peaking, sr, s.f, s.cur, s.q);
                    s.lastCur = s.cur; s.lastF = s.f; s.lastQ = s.q;
                }
                s.bq.processBlock(x + off, m);
            }
        }
    }

    // ---- howl simulator: acoustic loop = (EQ output) -> 12 ms delay -> resonance (Q 35) -> gain 1.05 -> soft clip -> input
    void simPre(float* x, int n, float f)
    {
        if (!simPrev) {
            std::fill(simBuf.begin(), simBuf.end(), 0.f); simBp.reset();
            simNoise = static_cast<int>(sr * 0.15); simPrev = true; simFcur = 0;
        }
        if (f != simFcur) { simBp.c = makeBiquad(FilterType::BandPass, sr, f, 0.0, 35.0); simFcur = f; }
        const uint64_t mask = simBuf.size() - 1;
        const int D = std::max(static_cast<int>(sr * 0.012), n + 1);
        for (int i = 0; i < n; ++i) {
            float d = simBuf[(simW + static_cast<uint64_t>(i) - static_cast<uint64_t>(D)) & mask];
            if (simNoise > 0) {
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                d += 0.02f * (static_cast<float>(rng & 0xFFFF) / 32768.0f - 1.0f);
                --simNoise;
            }
            float y = simBp.process(d) * 1.05f;
            y = 0.12f * std::tanh(y / 0.12f);
            x[i] += y;
        }
    }
    void simPost(const float* x, int n)
    {
        const uint64_t mask = simBuf.size() - 1;
        for (int i = 0; i < n; ++i) simBuf[(simW + static_cast<uint64_t>(i)) & mask] = x[i];
        simW += static_cast<uint64_t>(n);
    }

    double sr = 48000; int maxBlock = 4096; bool first = true;
    Gate gate; CompLimiter comp; Biquad hpf; bool hpfOn = true;
    Band bands[kBands]; NotchRt notch[kNotches];
    float trimTarget = 1, faderTarget = 1, trimG = 1, faderG = 1, muteG = 1;
    std::array<float, P_COUNT> cache{};
    std::vector<float> simBuf; uint64_t simW = 0; bool simPrev = false; Biquad simBp; float simFcur = 0; int simNoise = 0;
    uint32_t rng = 2463534242u;
};

} // namespace ah
