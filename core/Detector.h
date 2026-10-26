// AntiHowl AI - feedback (howl) detector.
// Finds narrow spectral peaks that persist and grow, scores them with a small logistic model and rejects
// harmonic series (voice / instruments). Runs on the analysis thread only (allocates, never on the audio thread).
#pragma once
#include "Dsp.h"
#include <utility>

namespace ah {

struct DetCfg {
    int fft;           // FFT size
    int intervalMs;    // analysis period
    float prom;        // min prominence over local background (dB)
    float thr;         // decision threshold for the AI score
    float sm;          // spectrum smoothing
    float floorDb;     // ignore peaks below this level
    const char* name;  // UTF-8 label
};

// Higher sensitivity => bigger FFT, more frequent analysis, lower thresholds => more CPU.
inline const DetCfg& sensCfg(int level)
{
    static const DetCfg t[5] = {
        { 2048, 60, 14.0f, 0.85f, 0.60f, -80.0f, "\xE0\xB8\x9B\xE0\xB8\xA3\xE0\xB8\xB0\xE0\xB8\xAB\xE0\xB8\xA2\xE0\xB8\xB1\xE0\xB8\x94" },          // ประหยัด
        { 4096, 40, 12.0f, 0.78f, 0.55f, -80.0f, "\xE0\xB8\x9B\xE0\xB8\x81\xE0\xB8\x95\xE0\xB8\xB4" },                                         // ปกติ
        { 4096, 25, 10.5f, 0.70f, 0.50f, -80.0f, "\xE0\xB9\x84\xE0\xB8\xA7" },                                                                 // ไว
        { 8192, 16, 9.0f, 0.62f, 0.40f, -82.0f, "\xE0\xB9\x84\xE0\xB8\xA7\xE0\xB8\xA1\xE0\xB8\xB2\xE0\xB8\x81" },                              // ไวมาก
        { 8192, 10, 8.0f, 0.55f, 0.30f, -85.0f, "\xE0\xB8\xAA\xE0\xB8\xB9\xE0\xB8\x87\xE0\xB8\xAA\xE0\xB8\xB8\xE0\xB8\x94" },                  // สูงสุด
    };
    return t[clampv(level, 1, 5) - 1];
}

struct Track {
    int id = 0;
    double f = 0, born = 0;
    int hits = 0, miss = 0, missTot = 0, width = 1, k = 0;
    std::vector<std::pair<double, float>> hist;  // (time, level dB)
    std::vector<double> fh;                      // refined frequency history
    float p = 0, growth = 0, wander = 99, harm = 0, hm = 0, prom = 0, v = 0;
};

struct HowlEvent {
    double f; float p, prom, growth; int id;
};

class Detector {
public:
    std::vector<Track> tracks;
    float maxP = 0;

    void reset() { tracks.clear(); maxP = 0; }

    static float score(const Track& t, double persist)
    {
        const float x1 = clampv((t.prom - 6.0f) / 18.0f, 0.0f, 1.5f);
        const float x2 = clampv(t.growth / 10.0f, -1.0f, 1.5f) * (t.growth > 0 ? clampv(static_cast<float>(persist) / 0.25f, 0.0f, 1.0f) : 1.0f);
        const float x3 = clampv(static_cast<float>(persist) / 0.4f, 0.0f, 2.0f);
        const float x4 = clampv(1.0f - t.wander / 40.0f, 0.0f, 1.0f);
        const float x5 = t.width <= 3 ? 1.0f : (t.width <= 5 ? 0.5f : 0.0f);
        const float z = -4.0f + 2.0f * x1 + 2.4f * x2 + 1.8f * x3 + 1.4f * x4 + 1.0f * x5 - 9.0f * t.harm;
        return 1.0f / (1.0f + std::exp(-z));
    }

    // spec: N dB values (N = fft/2). now: seconds.
    std::vector<HowlEvent> run(const float* spec, int N, double sr, int fft, const DetCfg& cfg, double now)
    {
        const double binHz = sr / fft;
        pre.resize(static_cast<size_t>(N) + 1);
        pre[0] = 0;
        for (int i = 0; i < N; ++i) {
            double v = spec[i];
            if (!(v > -140.0)) v = -140.0;
            pre[static_cast<size_t>(i) + 1] = pre[static_cast<size_t>(i)] + v;
        }
        const int W = clampv(static_cast<int>(std::lround(220.0 / binHz)), 8, 40), GAP = 3;
        auto bg = [&](int k) -> double {
            const int a0 = std::max(0, k - W), a1 = k - GAP, b0 = k + GAP, b1 = std::min(N - 1, k + W);
            double sum = 0; int cnt = 0;
            if (a1 >= a0) { sum += pre[a1 + 1] - pre[a0]; cnt += a1 - a0 + 1; }
            if (b1 >= b0) { sum += pre[b1 + 1] - pre[b0]; cnt += b1 - b0 + 1; }
            return cnt ? sum / cnt : -140.0;
        };
        const int kLo = std::max(GAP + 2, static_cast<int>(std::ceil(90.0 / binHz)));
        const int kHi = std::min(N - 3, static_cast<int>(std::floor(std::min(sr * 0.46, 15000.0) / binHz)));

        struct Cand { int k; float v, pr; double f; };
        std::vector<Cand> cands;
        for (int k = kLo; k <= kHi; ++k) {
            const float v = spec[k];
            if (!(v > cfg.floorDb)) continue;
            if (v > spec[k - 1] && v >= spec[k + 1]) {
                const float pr = v - static_cast<float>(bg(k));
                if (pr >= cfg.prom) cands.push_back({ k, v, pr, 0.0 });
            }
        }
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.pr > b.pr; });
        if (cands.size() > 12) cands.resize(12);
        for (auto& c : cands) {
            const double a = spec[c.k - 1] > -140.f ? spec[c.k - 1] : -140.0, d = spec[c.k + 1] > -140.f ? spec[c.k + 1] : -140.0;
            const double den = a - 2.0 * c.v + d;
            const double off = den < 0 ? clampv(0.5 * (a - d) / den, -0.5, 0.5) : 0.0;
            c.f = (c.k + off) * binHz;
        }

        std::vector<char> used(tracks.size(), 0);
        for (const auto& c : cands) {
            const double f = c.f;
            int w = 1, l = c.k - 1, r = c.k + 1;
            const float lim = c.v - 6.0f;
            while (l > 0 && spec[l] >= lim && w < 14) { ++w; --l; }
            while (r < N - 1 && spec[r] >= lim && w < 14) { ++w; ++r; }
            int best = -1; double bd = 1e9;
            const double tol = std::max(2.5 * binHz, f * 0.006);
            for (size_t i = 0; i < tracks.size(); ++i) {
                if (used[i]) continue;
                const double dd = std::fabs(tracks[i].f - f);
                if (dd < tol && dd < bd) { bd = dd; best = static_cast<int>(i); }
            }
            if (best < 0) {
                Track t; t.id = nextId++; t.f = f; t.born = now; t.width = w;
                tracks.push_back(std::move(t)); used.push_back(0);
                best = static_cast<int>(tracks.size()) - 1;
            }
            Track& t = tracks[static_cast<size_t>(best)];
            used[static_cast<size_t>(best)] = 1;
            t.f = f; t.k = c.k; t.prom = c.pr; t.v = c.v; t.width = w; ++t.hits; t.miss = 0;
            t.hist.emplace_back(now, c.v); t.fh.push_back(f);
            if (t.hist.size() > 16) { t.hist.erase(t.hist.begin()); t.fh.erase(t.fh.begin()); }
        }
        for (int i = static_cast<int>(tracks.size()) - 1; i >= 0; --i) {
            if (!used[static_cast<size_t>(i)]) {
                Track& t = tracks[static_cast<size_t>(i)];
                ++t.miss; ++t.missTot;
                if (t.miss > 3) { tracks.erase(tracks.begin() + i); used.erase(used.begin() + i); }
            }
        }

        // Harmonic-series test: other peaks in this frame at integer multiples (2..6) or evenly spaced.
        std::vector<Cand> sorted = cands;
        std::sort(sorted.begin(), sorted.end(), [](const Cand& a, const Cand& b) { return a.f < b.f; });
        auto ap = [&](double f) -> bool {
            const double tol = 0.8 * binHz + 0.003 * f;
            std::vector<double> os;
            for (const auto& o : sorted) if (std::fabs(o.f - f) >= 3 * binHz) os.push_back(o.f);
            for (size_t i = 0; i < os.size(); ++i)
                for (size_t j = i + 1; j < os.size(); ++j) {
                    const double a = os[i], b = os[j]; double d;
                    if (f < a) { d = a - f; if (d >= 60 && std::fabs((b - a) - d) < tol) return true; }
                    else if (f < b) { d = f - a; if (d >= 60 && std::fabs((b - f) - d) < tol) return true; }
                    else { d = b - a; if (d >= 60 && std::fabs((f - b) - d) < tol) return true; }
                }
            return false;
        };
        auto rel = [&](double f, float v) -> float {
            int sup = 0; bool dom = false;
            for (const auto& o : cands) {
                if (std::fabs(o.f - f) < 3 * binHz) continue;
                const double lo = std::min(f, o.f), hi = std::max(f, o.f);
                for (int m = 2; m <= 6; ++m)
                    if (std::fabs(hi - m * lo) < 0.2 * binHz * m + 0.002 * hi) { ++sup; if (o.f < f && o.v >= v + 10.0f) dom = true; break; }
            }
            if (dom) return 2.0f;
            if (sup >= 2 || ap(f)) return 1.0f;
            return sup == 1 ? 0.15f : 0.0f;
        };

        std::vector<HowlEvent> events;
        float mp = 0;
        for (size_t ti = 0; ti < tracks.size(); ++ti) {
            Track& t = tracks[ti];
            if (!used[ti]) { t.p *= 0.85f; continue; }
            const size_t n = t.hist.size();
            float g = 0;
            if (n >= 4) {
                double mt = 0, mv = 0;
                for (const auto& h : t.hist) { mt += h.first; mv += h.second; }
                mt /= n; mv /= n;
                double num = 0, dn = 0;
                for (const auto& h : t.hist) { const double dt = h.first - mt; num += dt * (h.second - mv); dn += dt * dt; }
                g = dn > 1e-9 ? static_cast<float>(num / dn) : 0.f;
                double m = 0; for (double f : t.fh) m += f; m /= n;
                double s = 0; for (double f : t.fh) { const double c = 1200.0 * std::log2(f / m); s += c * c; }
                t.wander = static_cast<float>(std::sqrt(s / n));
            } else t.wander = 99;
            t.growth = g;
            const double persist = (now - t.born) * (static_cast<double>(t.hits) / (t.hits + t.missTot));
            t.hm = std::max(rel(t.f, t.v), t.hm * 0.9f);
            t.harm = t.hm;
            t.p = score(t, persist);
            if (t.p > mp) mp = t.p;
            if (t.p >= cfg.thr && t.hits >= 4 && persist >= 0.10)
                events.push_back({ t.f, t.p, t.prom, g, t.id });
        }
        maxP = mp;
        return events;
    }

private:
    std::vector<double> pre;
    int nextId = 1;
};

} // namespace ah
