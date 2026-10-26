#include "AudioEngine.h"

// ------------------------------------------------------------------------------------------------
// Background analysis thread: FFT + AI detector for every active channel. Never touches the audio thread
// except through the lock-free command queue inside each ChannelStrip.
class AudioEngine::AnalysisThread : public juce::Thread {
public:
    explicit AnalysisThread(AudioEngine& e) : juce::Thread("AntiHowl analysis"), eng(e) {}

    void run() override
    {
        double busy = 0, t0 = juce::Time::getMillisecondCounterHiRes();
        while (!threadShouldExit()) {
            if (!eng.prepared.load()) { wait(40); continue; }
            const double now = ah::nowSec();
            const int nch = juce::jlimit(1, ah::kMaxStrips, static_cast<int>(eng.gp.get(ah::G_NCH)));
            const bool glob = eng.gp.get(ah::G_AFS) > 0.5f;
            const ah::AfsSettings st = eng.afsSettings();
            const double a = juce::Time::getMillisecondCounterHiRes();
            if (glob)
                for (int s = 0; s < nch; ++s)
                    if (eng.sp[static_cast<size_t>(s)].get(ah::P_AFS) > 0.5f)
                        eng.afs[static_cast<size_t>(s)].analyze(now, st, eng.strips[static_cast<size_t>(s)]->detRing);
            busy += juce::Time::getMillisecondCounterHiRes() - a;
            const double t1 = juce::Time::getMillisecondCounterHiRes();
            if (t1 - t0 >= 1000.0) {
                eng.cpuAfsPct.store(static_cast<float>(busy / (t1 - t0) * 100.0));
                busy = 0; t0 = t1;
            }
            wait(2);
        }
    }
private:
    AudioEngine& eng;
};

// ------------------------------------------------------------------------------------------------
AudioEngine::AudioEngine()
{
    for (auto& s : strips) s = std::make_unique<ah::ChannelStrip>();
    resetToDefaults();
    thread = std::make_unique<AnalysisThread>(*this);
    thread->startThread(juce::Thread::Priority::low);
}

AudioEngine::~AudioEngine()
{
    prepared = false;
    thread->stopThread(2000);
}

void AudioEngine::resetToDefaults()
{
    for (int i = 0; i < ah::G_COUNT; ++i) gp.set(i, ah::globalInfo()[static_cast<size_t>(i)].def);
    for (int s = 0; s < ah::kMaxStrips; ++s) {
        for (int i = 0; i < ah::P_COUNT; ++i) sp[static_cast<size_t>(s)].set(i, ah::stripInfo()[static_cast<size_t>(i)].def);
        sp[static_cast<size_t>(s)].set(ah::P_SRC, static_cast<float>(s));
    }
}

ah::AfsSettings AudioEngine::afsSettings() const
{
    ah::AfsSettings s;
    s.on = gp.get(ah::G_AFS) > 0.5f;
    s.sens = static_cast<int>(std::lround(gp.get(ah::G_SENS)));
    s.depthMax = gp.get(ah::G_DEPTH);
    s.q = gp.get(ah::G_Q);
    s.relSec = gp.get(ah::G_RELSEC);
    s.lock = gp.get(ah::G_LOCK) > 0.5f;
    return s;
}

// ------------------------------------------------------------------------------------------------
juce::var AudioEngine::toVar() const
{
    auto* root = new juce::DynamicObject();
    auto* g = new juce::DynamicObject();
    for (int i = 0; i < ah::G_COUNT; ++i) g->setProperty(ah::globalInfo()[static_cast<size_t>(i)].key, static_cast<double>(gp.get(i)));
    root->setProperty("global", juce::var(g));
    juce::Array<juce::var> list;
    for (int s = 0; s < ah::kMaxStrips; ++s) {
        auto* o = new juce::DynamicObject();
        for (int i = 0; i < ah::P_COUNT; ++i) o->setProperty(ah::stripInfo()[static_cast<size_t>(i)].key, static_cast<double>(sp[static_cast<size_t>(s)].get(i)));
        list.add(juce::var(o));
    }
    root->setProperty("strips", list);
    return juce::var(root);
}

void AudioEngine::fromVar(const juce::var& v)
{
    auto* root = v.getDynamicObject();
    if (root == nullptr) return;
    if (auto* g = root->getProperty("global").getDynamicObject())
        for (int i = 0; i < ah::G_COUNT; ++i) {
            const char* key = ah::globalInfo()[static_cast<size_t>(i)].key;
            if (g->hasProperty(key)) gp.set(i, static_cast<float>(static_cast<double>(g->getProperty(key))));
        }
    gp.set(ah::G_SIM_ON, 0.f);   // never start with the howl simulator running
    const juce::var strs = root->getProperty("strips");
    if (auto* arr = strs.getArray())
        for (int s = 0; s < juce::jmin(arr->size(), ah::kMaxStrips); ++s)
            if (auto* o = (*arr)[s].getDynamicObject())
                for (int i = 0; i < ah::P_COUNT; ++i) {
                    const char* key = ah::stripInfo()[static_cast<size_t>(i)].key;
                    if (o->hasProperty(key)) sp[static_cast<size_t>(s)].set(i, static_cast<float>(static_cast<double>(o->getProperty(key))));
                }
}

// ------------------------------------------------------------------------------------------------
void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* d)
{
    prepared = false;
    const double sr = d->getCurrentSampleRate();
    const int bs = d->getCurrentBufferSizeSamples();
    sampleRate = sr; bufferSize = bs;
    numIn = d->getActiveInputChannels().countNumberOfSetBits();
    numOut = d->getActiveOutputChannels().countNumberOfSetBits();
    blockCap = juce::jmax(2048, bs);
    for (auto* v : { &zeros, &tmp, &bus, &wetL, &wetR, &mixL, &mixR }) v->assign(static_cast<size_t>(blockCap), 0.f);
    for (int s = 0; s < ah::kMaxStrips; ++s) {
        strips[static_cast<size_t>(s)]->prepare(sr, blockCap);
        afs[static_cast<size_t>(s)].bind(&strips[static_cast<size_t>(s)]->cmds, sr);
        afs[static_cast<size_t>(s)].resendAll();
    }
    reverb.setSampleRate(sr); reverb.reset(); rvType = -1;
    preBuf.assign(1u << 15, 0.f); echoBuf.assign(1u << 18, 0.f); preW = echoW = 0; echoLp = 0;
    masterLim.prepare(sr); masterLim.compOn = false; masterLim.limOn = true; masterLim.ceil = -1.0f; masterLim.update();
    masterG = 1.0f;
    prepared = true;
}

void AudioEngine::audioDeviceStopped() { prepared = false; }

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const* in, int nIn, float* const* out, int nOut,
                                                   int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    for (int c = 0; c < nOut; ++c)
        if (out[c] != nullptr) juce::FloatVectorOperations::clear(out[c], numSamples);
    if (!prepared.load()) return;

    int done = 0;
    while (done < numSamples) {
        const int m = juce::jmin(numSamples - done, blockCap);
        processChunk(in, nIn, out, nOut, done, m);
        done += m;
    }
    const double used = juce::Time::getMillisecondCounterHiRes() - t0;
    const double avail = 1000.0 * numSamples / sampleRate.load();
    const float load = static_cast<float>(used / avail * 100.0);
    cpuAudioPct.store(cpuAudioPct.load() * 0.93f + load * 0.07f);
}

void AudioEngine::processChunk(const float* const* in, int nIn, float* const* out, int nOut, int off, int n)
{
    const int nch = juce::jlimit(1, ah::kMaxStrips, static_cast<int>(gp.get(ah::G_NCH)));
    const bool afsGlobal = gp.get(ah::G_AFS) > 0.5f;
    bool anySolo = false;
    for (int s = 0; s < nch; ++s) if (sp[static_cast<size_t>(s)].get(ah::P_SOLO) > 0.5f) anySolo = true;
    const bool simOn = gp.get(ah::G_SIM_ON) > 0.5f;
    const int simCh = static_cast<int>(gp.get(ah::G_SIM_CH));
    const float simF = gp.get(ah::G_SIM_F);

    std::fill(bus.begin(), bus.begin() + n, 0.f);
    for (int s = 0; s < nch; ++s) {
        auto& P = sp[static_cast<size_t>(s)];
        int src = static_cast<int>(P.get(ah::P_SRC));
        if (nIn > 0 && src >= nIn) src = nIn - 1;      // saved mapping from a bigger interface: clamp instead of going silent
        const float* inp = (src >= 0 && src < nIn && in[src] != nullptr) ? in[src] + off : zeros.data();
        const bool muted = P.get(ah::P_MUTE) > 0.5f || (anySolo && P.get(ah::P_SOLO) <= 0.5f);
        const bool afsOn = afsGlobal && P.get(ah::P_AFS) > 0.5f;
        strips[static_cast<size_t>(s)]->process(inp, tmp.data(), n, P, afsOn, muted ? 0.f : 1.f, simOn && s == simCh, simF);
        for (int i = 0; i < n; ++i) bus[static_cast<size_t>(i)] += tmp[static_cast<size_t>(i)];
    }

    runFx(bus.data(), wetL.data(), wetR.data(), n);

    const float gDb = gp.get(ah::G_MASTER);
    const float gTarget = gDb <= -59.5f ? 0.f : ah::dbToLin(gDb);
    const float step = (gTarget - masterG) / static_cast<float>(n);
    masterLim.limOn = gp.get(ah::G_MLIM) > 0.5f;
    float pk = 0, ss = 0;
    for (int i = 0; i < n; ++i) {
        masterG += step;
        const size_t k = static_cast<size_t>(i);
        float l = (bus[k] + wetL[k]) * masterG, r = (bus[k] + wetR[k]) * masterG;
        const float gl = masterLim.gain(std::max(std::fabs(l), std::fabs(r)));
        l = juce::jlimit(-1.0f, 1.0f, l * gl); r = juce::jlimit(-1.0f, 1.0f, r * gl);
        mixL[k] = l; mixR[k] = r;
        pk = std::max(pk, std::max(std::fabs(l), std::fabs(r)));
        ss += 0.5f * (l * l + r * r);
    }
    masterG = gTarget;
    if (pk > masterPeakAcc.load(std::memory_order_relaxed)) masterPeakAcc.store(pk, std::memory_order_relaxed);
    masterRms.store(std::sqrt(ss / static_cast<float>(n)), std::memory_order_relaxed);
    masterGr.store(masterLim.takeReductionDb(), std::memory_order_relaxed);

    if (nOut <= 0) return;
    const int pair = static_cast<int>(gp.get(ah::G_OUTPAIR));
    const int oL = juce::jmin(pair * 2, nOut - 1), oR = juce::jmin(pair * 2 + 1, nOut - 1);
    if (oL == oR) {
        if (out[oL] != nullptr) for (int i = 0; i < n; ++i) out[oL][off + i] += 0.5f * (mixL[static_cast<size_t>(i)] + mixR[static_cast<size_t>(i)]);
    } else {
        if (out[oL] != nullptr) for (int i = 0; i < n; ++i) out[oL][off + i] += mixL[static_cast<size_t>(i)];
        if (out[oR] != nullptr) for (int i = 0; i < n; ++i) out[oR][off + i] += mixR[static_cast<size_t>(i)];
    }
}

// ------------------------------------------------------------------------------------------------
void AudioEngine::updateReverb(int type)
{
    const float decay = gp.get(ah::G_FX_DECAY), tone = gp.get(ah::G_FX_TONE);
    if (type == rvType && decay == rvDecay && tone == rvTone) return;
    rvType = type; rvDecay = decay; rvTone = tone;
    float room = juce::jmap(decay, 0.3f, 6.0f, 0.25f, 0.97f);
    float damp = 1.0f - std::log(tone / 1500.0f) / std::log(16000.0f / 1500.0f);
    switch (type) {
    case 1: room *= 0.8f; break;                       // room
    case 3: damp *= 0.4f; break;                       // plate: brighter
    case 4: room = juce::jmin(0.98f, room + 0.05f); break;   // church
    default: break;                                    // hall
    }
    juce::Reverb::Parameters p;
    p.roomSize = juce::jlimit(0.0f, 1.0f, room);
    p.damping = juce::jlimit(0.0f, 1.0f, damp);
    p.wetLevel = 0.33f; p.dryLevel = 0.0f; p.width = 1.0f; p.freezeMode = 0.0f;
    reverb.setParameters(p);
}

void AudioEngine::runFx(const float* in, float* wl, float* wr, int n)
{
    const int type = static_cast<int>(gp.get(ah::G_FX_TYPE));    // 0 off, 1 room, 2 hall, 3 plate, 4 church, 5 echo
    const float mix = gp.get(ah::G_FX_MIX);
    const uint64_t pm = preBuf.size() - 1;
    const double sr = sampleRate.load();
    const int pd = juce::jlimit(0, static_cast<int>(pm) - 1, static_cast<int>(gp.get(ah::G_FX_PRE) * 0.001 * sr));
    for (int i = 0; i < n; ++i) preBuf[(preW + static_cast<uint64_t>(i)) & pm] = in[i];

    if (type >= 1 && type <= 4 && mix > 0.001f) {
        updateReverb(type);
        for (int i = 0; i < n; ++i) wl[i] = wr[i] = preBuf[(preW + static_cast<uint64_t>(i) - static_cast<uint64_t>(pd)) & pm];
        reverb.processStereo(wl, wr, n);
        const float k = mix * 1.4f;
        for (int i = 0; i < n; ++i) { wl[i] *= k; wr[i] *= k; }
    } else if (type == 5 && mix > 0.001f) {
        const uint64_t em = echoBuf.size() - 1;
        const uint64_t d = static_cast<uint64_t>(0.28 * sr);
        const float lpA = 1.0f - std::exp(-2.0f * 3.14159265f * 3200.0f / static_cast<float>(sr));
        for (int i = 0; i < n; ++i) {
            const float x = echoBuf[(echoW + static_cast<uint64_t>(i) - d) & em];
            echoLp += lpA * (x - echoLp);
            echoBuf[(echoW + static_cast<uint64_t>(i)) & em] = in[i] + echoLp * 0.38f;
            wl[i] = wr[i] = echoLp * mix * 1.2f;
        }
        echoW += static_cast<uint64_t>(n);
    } else {
        std::fill(wl, wl + n, 0.f); std::fill(wr, wr + n, 0.f);
    }
    preW += static_cast<uint64_t>(n);
}
