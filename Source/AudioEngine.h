// AntiHowl AI - realtime engine. Connects JUCE's AudioDeviceManager (ASIO / WASAPI / CoreAudio) to the DSP core.
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>
#include "Afs.h"

class AudioEngine : public juce::AudioIODeviceCallback {
public:
    AudioEngine();
    ~AudioEngine() override;

    // ---- parameters (UI writes, audio thread reads) ----
    ah::ParamSet<ah::G_COUNT> gp;
    std::array<ah::ParamSet<ah::P_COUNT>, ah::kMaxStrips> sp;
    std::array<std::unique_ptr<ah::ChannelStrip>, ah::kMaxStrips> strips;
    std::array<ah::AfsController, ah::kMaxStrips> afs;

    // ---- read-only status for the UI ----
    std::atomic<float> masterPeakAcc{ 0.f }, masterRms{ 0.f }, masterGr{ 0.f };
    std::atomic<float> cpuAudioPct{ 0.f }, cpuAfsPct{ 0.f };
    std::atomic<double> sampleRate{ 48000.0 };
    std::atomic<int> numIn{ 0 }, numOut{ 0 }, bufferSize{ 0 };
    bool isRunning() const { return prepared.load(); }

    // ---- persistence ----
    void resetToDefaults();
    juce::var toVar() const;
    void fromVar(const juce::var& v);
    ah::AfsSettings afsSettings() const;

    // ---- juce::AudioIODeviceCallback ----
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                          float* const* outputChannelData, int numOutputChannels,
                                          int numSamples, const juce::AudioIODeviceCallbackContext& context) override;

private:
    class AnalysisThread;
    void processChunk(const float* const* in, int nIn, float* const* out, int nOut, int off, int n);
    void runFx(const float* bus, float* wetL, float* wetR, int n);
    void updateReverb(int type);

    std::atomic<bool> prepared{ false };
    int blockCap = 0;
    std::vector<float> zeros, tmp, bus, wetL, wetR, mixL, mixR;

    // effects
    juce::Reverb reverb;
    int rvType = -1; float rvDecay = -1, rvTone = -1;
    std::vector<float> preBuf, echoBuf;
    uint64_t preW = 0, echoW = 0; float echoLp = 0;

    // master
    ah::CompLimiter masterLim;
    float masterG = 1.0f;

    std::unique_ptr<AnalysisThread> thread;
    double lastTicks = 0;
};
