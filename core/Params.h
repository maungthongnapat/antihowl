// AntiHowl AI - parameter identifiers, defaults and ranges.
#pragma once
#include "Dsp.h"
#include <vector>

namespace ah {

constexpr int kMaxStrips = 8;
constexpr int kBands = 6;
constexpr int kNotches = 12;

enum StripParam : int {
    P_TRIM, P_FADER, P_MUTE, P_SOLO, P_AFS, P_SRC,
    P_HPF_ON, P_HPF_F,
    P_EQ0_F, P_EQ0_G, P_EQ0_Q, P_EQ1_F, P_EQ1_G, P_EQ1_Q, P_EQ2_F, P_EQ2_G, P_EQ2_Q,
    P_EQ3_F, P_EQ3_G, P_EQ3_Q, P_EQ4_F, P_EQ4_G, P_EQ4_Q, P_EQ5_F, P_EQ5_G, P_EQ5_Q,
    P_GATE_ON, P_GATE_THR, P_GATE_ATK, P_GATE_HOLD, P_GATE_REL, P_GATE_RANGE,
    P_COMP_ON, P_COMP_THR, P_COMP_RATIO, P_COMP_ATK, P_COMP_REL, P_COMP_MAKEUP,
    P_LIM_ON, P_LIM_CEIL,
    P_COUNT
};
constexpr int eqF(int b) { return P_EQ0_F + b * 3; }
constexpr int eqG(int b) { return P_EQ0_F + b * 3 + 1; }
constexpr int eqQ(int b) { return P_EQ0_F + b * 3 + 2; }

constexpr FilterType kEqType[kBands] = {
    FilterType::LowShelf, FilterType::Peaking, FilterType::Peaking,
    FilterType::Peaking, FilterType::Peaking, FilterType::HighShelf };

enum GlobalParam : int {
    G_AFS, G_SENS, G_DEPTH, G_Q, G_RELSEC, G_LOCK,
    G_FX_TYPE, G_FX_MIX, G_FX_DECAY, G_FX_PRE, G_FX_TONE,
    G_MASTER, G_MLIM, G_OUTPAIR, G_NCH,
    G_SIM_ON, G_SIM_F, G_SIM_CH,
    G_COUNT
};

struct ParamInfo { const char* key = nullptr; float def = 0, lo = 0, hi = 1; };

inline const std::vector<ParamInfo>& stripInfo()
{
    static const std::vector<ParamInfo> t = [] {
        std::vector<ParamInfo> v(P_COUNT);
        auto S = [&](int i, const char* k, float d, float lo, float hi) { v[static_cast<size_t>(i)] = { k, d, lo, hi }; };
        S(P_TRIM, "trim", 0, -20, 30); S(P_FADER, "fader", 0, -60, 12);
        S(P_MUTE, "mute", 0, 0, 1); S(P_SOLO, "solo", 0, 0, 1); S(P_AFS, "afs", 1, 0, 1); S(P_SRC, "src", 0, 0, 31);
        S(P_HPF_ON, "hpfOn", 1, 0, 1); S(P_HPF_F, "hpfF", 90, 20, 500);
        static const char* eqk[kBands][3] = { {"eq0f","eq0g","eq0q"}, {"eq1f","eq1g","eq1q"}, {"eq2f","eq2g","eq2q"},
                                              {"eq3f","eq3g","eq3q"}, {"eq4f","eq4g","eq4q"}, {"eq5f","eq5g","eq5q"} };
        const float ef[kBands] = { 120, 400, 1000, 2500, 5000, 10000 };
        const float eqd[kBands] = { 0.7071f, 1.0f, 1.0f, 1.0f, 1.0f, 0.7071f };
        for (int b = 0; b < kBands; ++b) {
            const bool shelf = kEqType[b] != FilterType::Peaking;
            S(eqF(b), eqk[b][0], ef[b], 20, 20000);
            S(eqG(b), eqk[b][1], 0, -15, 15);
            S(eqQ(b), eqk[b][2], eqd[b], 0.3f, shelf ? 2.0f : 30.0f);   // Q up to 30 => very narrow bell
        }
        S(P_GATE_ON, "gateOn", 0, 0, 1); S(P_GATE_THR, "gateThr", -55, -80, -10); S(P_GATE_ATK, "gateAtk", 2, 0.1f, 50);
        S(P_GATE_HOLD, "gateHold", 120, 0, 500); S(P_GATE_REL, "gateRel", 250, 10, 1500); S(P_GATE_RANGE, "gateRange", -60, -80, 0);
        S(P_COMP_ON, "compOn", 1, 0, 1); S(P_COMP_THR, "compThr", -16, -40, 0); S(P_COMP_RATIO, "compRatio", 3, 1, 20);
        S(P_COMP_ATK, "compAtk", 10, 0.5f, 100); S(P_COMP_REL, "compRel", 150, 10, 1000); S(P_COMP_MAKEUP, "compMakeup", 0, 0, 18);
        S(P_LIM_ON, "limOn", 1, 0, 1); S(P_LIM_CEIL, "limCeil", -2, -12, 0);
        return v;
    }();
    return t;
}

inline const std::vector<ParamInfo>& globalInfo()
{
    static const std::vector<ParamInfo> t = [] {
        std::vector<ParamInfo> v(G_COUNT);
        auto S = [&](int i, const char* k, float d, float lo, float hi) { v[static_cast<size_t>(i)] = { k, d, lo, hi }; };
        S(G_AFS, "afs", 1, 0, 1); S(G_SENS, "sens", 3, 1, 5); S(G_DEPTH, "depth", 18, 6, 30); S(G_Q, "notchQ", 30, 8, 60);
        S(G_RELSEC, "relSec", 45, 5, 180); S(G_LOCK, "lock", 0, 0, 1);
        S(G_FX_TYPE, "fxType", 2, 0, 5); S(G_FX_MIX, "fxMix", 0.18f, 0, 1); S(G_FX_DECAY, "fxDecay", 2.4f, 0.3f, 6);
        S(G_FX_PRE, "fxPre", 20, 0, 120); S(G_FX_TONE, "fxTone", 6500, 1500, 16000);
        S(G_MASTER, "master", 0, -60, 12); S(G_MLIM, "mlim", 1, 0, 1); S(G_OUTPAIR, "outPair", 0, 0, 15); S(G_NCH, "nch", 2, 1, 8);
        S(G_SIM_ON, "simOn", 0, 0, 1); S(G_SIM_F, "simF", 2500, 300, 8000); S(G_SIM_CH, "simCh", 0, 0, 7);
        return v;
    }();
    return t;
}

// Bandwidth in octaves for a peaking filter of a given Q.
inline double qToOctaves(double q) { return 2.0 * std::asinh(1.0 / (2.0 * std::max(q, 0.05))) / std::log(2.0); }
inline double octavesToQ(double oct) { return 1.0 / (2.0 * std::sinh(std::log(2.0) / 2.0 * std::max(oct, 0.01))); }

} // namespace ah
