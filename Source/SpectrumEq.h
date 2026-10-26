// AntiHowl AI - live spectrum + parametric EQ + howl markers for the selected channel.
#pragma once
#include "AudioEngine.h"
#include "UiKit.h"

class SpectrumEq : public juce::Component, private juce::Timer {
public:
    static constexpr int kHpf = 100;

    explicit SpectrumEq(AudioEngine& e);
    ~SpectrumEq() override { stopTimer(); }

    void setChannel(int c);
    int getChannel() const { return ch; }
    void setSelectedBand(int b) { selBand = b; repaint(); }
    int getSelectedBand() const { return selBand; }
    void setSelectedNotch(int j) { selNotch = j; repaint(); }
    int getSelectedNotch() const { return selNotch; }

    // callbacks into the surrounding panels
    std::function<void()> onEqEdited;                 // a band/HPF value changed from the graph
    std::function<void(int)> onBandSelected;          // 0..5 or kHpf
    std::function<void(int)> onNotchSelected;         // slot index
    std::function<void()> onNotchesChanged;           // notch added/removed/edited from the graph

    // latest data (UI thread only) for other panels
    double dominantHz = 0; float dominantDb = -200.f, risk = 0.f;
    std::array<ah::SlotView, ah::kNotches> slots;
    std::vector<ah::TrackView> tracks;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;

private:
    struct Hit { enum Kind { None, Node, Handle, NotchLine, NotchDepth } kind = None; int idx = -1; int side = 0; };

    void timerCallback() override;
    void computeCurve();
    Hit hitTest(juce::Point<float> p) const;
    void setStrip(int param, float v);
    float getStrip(int param) const { return eng.sp[static_cast<size_t>(ch)].get(param); }
    std::pair<float, float> handleX(int band) const;
    float fx(double f) const { return plot.getX() + static_cast<float>(std::log(f / 20.0) / std::log(1000.0)) * plot.getWidth(); }
    double xf(float x) const { return 20.0 * std::pow(1000.0, static_cast<double>((x - plot.getX()) / plot.getWidth())); }
    float ySp(float db) const { return plot.getY() + juce::jlimit(0.0f, 1.0f, -db / 90.0f) * plot.getHeight(); }
    float yEq(float g) const { return plot.getCentreY() - g / 24.0f * plot.getHeight() * 0.5f; }
    float gEq(float y) const { return (plot.getCentreY() - y) / (plot.getHeight() * 0.5f) * 24.0f; }

    AudioEngine& eng;
    int ch = 0, selBand = 2, selNotch = -1, hovBand = -1;
    juce::Rectangle<float> plot;

    static constexpr int kFft = 4096;
    ah::SpectrumAnalyzer sa;
    std::vector<float> samples, spec, disp, hold, curve;
    bool live = false; int tick = 0;

    Hit drag; float dragStartQ = 1.f; float dragStartY = 0.f;
    juce::Point<float> mouse{ -1.f, -1.f };
};
