// AntiHowl AI - shared UI building blocks (JUCE 8)
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

#define TH(s) juce::String::fromUTF8(s)   // all Thai literals must go through this (JUCE asserts on non-ASCII const char*)

namespace ui {

inline const juce::Colour kBg    { 0xff0b1017 };
inline const juce::Colour kPanel { 0xff121a24 };
inline const juce::Colour kPanel2{ 0xff17212e };
inline const juce::Colour kLine  { 0xff233143 };
inline const juce::Colour kLine2 { 0xff33465c };
inline const juce::Colour kTxt   { 0xffe8eef6 };
inline const juce::Colour kMut   { 0xff8c9db3 };
inline const juce::Colour kDim   { 0xff5a6b80 };
inline const juce::Colour kSig   { 0xff38e0c4 };   // live signal / suppressor active
inline const juce::Colour kEq    { 0xff8ea8ff };   // EQ
inline const juce::Colour kDyn   { 0xffffb347 };   // dynamics, warnings
inline const juce::Colour kCut   { 0xffff5c70 };   // notch / howl / clip
inline const juce::Colour kFx    { 0xffc79bff };   // effects

inline const juce::Colour kBand[6] = {
    juce::Colour(0xffff7a90), juce::Colour(0xffffb347), juce::Colour(0xffd6e66a),
    juce::Colour(0xff38e0c4), juce::Colour(0xff6aa5ff), juce::Colour(0xffc79bff) };

inline juce::String fontName()
{
#if JUCE_WINDOWS
    return "Leelawadee UI";      // ships with Windows 10/11, has Thai glyphs
#elif JUCE_MAC
    return "Thonburi";
#else
    return "Noto Sans Thai";
#endif
}
inline juce::Font font(float h, bool bold = false)
{
    return juce::Font(juce::FontOptions().withName(fontName()).withHeight(h).withStyle(bold ? "Bold" : "Regular"));
}
inline juce::Font mono(float h)
{
    return juce::Font(juce::FontOptions().withName(juce::Font::getDefaultMonospacedFontName()).withHeight(h));
}

inline juce::String fmtHz(double f)
{
    if (f >= 1000.0) {
        juce::String s(f / 1000.0, f >= 10000.0 ? 1 : 2);
        if (s.contains(".")) s = s.trimCharactersAtEnd("0").trimCharactersAtEnd(".");
        return s + " kHz";
    }
    return juce::String(juce::roundToInt(f)) + " Hz";
}
inline juce::String fmtDb(double d, int dec = 1) { return (d > 0 ? "+" : "") + juce::String(d, dec) + " dB"; }
inline juce::String fmtMs(double ms) { return ms < 10 ? juce::String(ms, 1) + " ms" : juce::String(juce::roundToInt(ms)) + " ms"; }

// ------------------------------------------------------------------------------------------------
class AhLookAndFeel : public juce::LookAndFeel_V4 {
public:
    AhLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, kBg);
        setColour(juce::Label::textColourId, kTxt);
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a2431));
        setColour(juce::TextButton::buttonOnColourId, kSig.withAlpha(0.16f));
        setColour(juce::TextButton::textColourOffId, kTxt);
        setColour(juce::TextButton::textColourOnId, kSig);
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0d141d));
        setColour(juce::ComboBox::outlineColourId, kLine2);
        setColour(juce::ComboBox::textColourId, kTxt);
        setColour(juce::ComboBox::arrowColourId, kMut);
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff111a25));
        setColour(juce::PopupMenu::textColourId, kTxt);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, kSig.withAlpha(0.25f));
        setColour(juce::Slider::backgroundColourId, juce::Colour(0xff25354a));
        setColour(juce::Slider::trackColourId, kEq);
        setColour(juce::Slider::thumbColourId, juce::Colour(0xffeaf3ff));
        setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0a1017));
        setColour(juce::TextEditor::textColourId, kMut);
        setColour(juce::TextEditor::outlineColourId, kLine);
        setColour(juce::TextEditor::focusedOutlineColourId, kLine2);
        setColour(juce::ScrollBar::thumbColourId, kLine2);
        setDefaultSansSerifTypefaceName(fontName());
    }
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override { return font(juce::jmin(14.0f, buttonHeight * 0.55f)); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return font(13.0f); }
    juce::Font getPopupMenuFont() override { return font(14.0f); }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool hl, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced(0.5f);
        auto c = bg;
        if (down) c = c.brighter(0.12f); else if (hl) c = c.brighter(0.06f);
        g.setColour(c);
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(b.getToggleState() ? b.findColour(juce::TextButton::textColourOnId).withAlpha(0.9f) : kLine2);
        g.drawRoundedRectangle(r, 8.0f, 1.0f);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)).reduced(3.0f);
        const float r = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f, thick = juce::jmax(3.5f, r * 0.17f);
        const auto c = bounds.getCentre();
        const float rr = r - thick * 0.5f;
        juce::Path track; track.addCentredArc(c.x, c.y, rr, rr, 0.0f, a0, a1, true);
        g.setColour(juce::Colour(0xff223044));
        g.strokePath(track, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float ang = a0 + pos * (a1 - a0);
        if (pos > 0.003f) {
            juce::Path val; val.addCentredArc(c.x, c.y, rr, rr, 0.0f, a0, ang, true);
            g.setColour(s.findColour(juce::Slider::rotarySliderFillColourId));
            g.strokePath(val, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour(juce::Colour(0xff0d141d)); g.fillEllipse(c.x - rr * 0.62f, c.y - rr * 0.62f, rr * 1.24f, rr * 1.24f);
        const juce::Point<float> dir(std::sin(ang), -std::cos(ang));
        g.setColour(juce::Colours::white);
        g.drawLine(juce::Line<float>(c + dir * (rr * 0.18f), c + dir * (rr * 0.58f)), 2.4f);
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s) override
    {
        const float cy = y + h * 0.5f;
        const juce::Colour fill = s.findColour(juce::Slider::trackColourId);
        g.setColour(juce::Colour(0xff25354a));
        g.fillRoundedRectangle(static_cast<float>(x), cy - 3.0f, static_cast<float>(w), 6.0f, 3.0f);
        g.setColour(fill);
        g.fillRoundedRectangle(static_cast<float>(x), cy - 3.0f, pos - x, 6.0f, 3.0f);
        g.setColour(juce::Colour(0xffeaf3ff)); g.fillEllipse(pos - 8.0f, cy - 8.0f, 16.0f, 16.0f);
        g.setColour(fill); g.drawEllipse(pos - 8.0f, cy - 8.0f, 16.0f, 16.0f, 3.0f);
    }
};

// ------------------------------------------------------------------------------------------------
// Rotary knob with name + value text. Double-click resets, wheel adjusts, drag vertically.
class Knob : public juce::Component {
public:
    Knob(const juce::String& name, double lo, double hi, double def, double skewMid = 0.0, double step = 0.0, int width = 74)
        : defWidth(width)
    {
        nameL.setText(name, juce::dontSendNotification);
        nameL.setJustificationType(juce::Justification::centred);
        nameL.setFont(font(12.0f)); nameL.setColour(juce::Label::textColourId, kMut);
        valL.setJustificationType(juce::Justification::centred);
        valL.setFont(mono(12.0f));
        addAndMakeVisible(s); addAndMakeVisible(nameL); addAndMakeVisible(valL);
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setRange(lo, hi, step);
        if (skewMid > lo && skewMid < hi) s.setSkewFactorFromMidPoint(skewMid);
        s.setDoubleClickReturnValue(true, def);
        s.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        s.setMouseDragSensitivity(170);
        s.setScrollWheelEnabled(false);   // the page scrolls with the wheel; knobs change by drag / double-click reset
        s.setValue(def, juce::dontSendNotification);
        s.onValueChange = [this] { refreshText(); if (onValue) onValue(s.getValue()); };
        setAccent(kSig);
        refreshText();
    }
    void setAccent(juce::Colour c) { s.setColour(juce::Slider::rotarySliderFillColourId, c); valL.setColour(juce::Label::textColourId, kTxt); repaint(); }
    void setValue(double v, bool notify = false) { s.setValue(v, notify ? juce::sendNotificationSync : juce::dontSendNotification); refreshText(); }
    double getValue() const { return s.getValue(); }
    void setRange(double lo, double hi, double def, double skewMid = 0.0)
    {
        s.setRange(lo, hi, 0.0);
        if (skewMid > lo && skewMid < hi) s.setSkewFactorFromMidPoint(skewMid); else s.setSkewFactor(1.0);
        s.setDoubleClickReturnValue(true, def);
    }
    void setName(const juce::String& n) { nameL.setText(n, juce::dontSendNotification); }
    void setEnabledLook(bool e) { setAlpha(e ? 1.0f : 0.4f); setInterceptsMouseClicks(e, e); }
    void refreshText()
    {
        const double v = s.getValue();
        valL.setText(format ? format(v) : juce::String(v, 1), juce::dontSendNotification);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        const int nh = nameL.getText().isEmpty() ? 0 : 16;
        nameL.setBounds(r.removeFromBottom(nh));
        valL.setBounds(r.removeFromBottom(16));
        s.setBounds(r);
    }
    int preferredWidth() const { return defWidth; }

    std::function<void(double)> onValue;
    std::function<juce::String(double)> format;
private:
    juce::Slider s; juce::Label nameL, valL; int defWidth;
};

// ------------------------------------------------------------------------------------------------
class LevelMeter : public juce::Component {
public:
    // peak/rms are linear; call at UI rate (~30 Hz)
    void setLevel(float peakLin, float rmsLin)
    {
        const float pd = juce::Decibels::gainToDecibels(peakLin, -90.0f), rd = juce::Decibels::gainToDecibels(rmsLin, -90.0f);
        pk = juce::jmax(pd, pk - 2.2f);
        rms = rd > rms ? rd : rms - 1.6f;
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (pd >= hold) { hold = pd; holdT = now; } else if (now - holdT > 1200.0) hold -= 0.8f;
        if (peakLin >= 0.995f) clipT = now;
        repaint();
    }
    void paint(juce::Graphics& g) override
    {
        const int N = 40;
        auto r = getLocalBounds().toFloat().withTrimmedRight(12.0f);
        const float gap = 1.5f, sw = (r.getWidth() - (N - 1) * gap) / N;
        for (int k = 0; k < N; ++k) {
            const float db = -60.0f + (k + 1) * 60.0f / N, x = r.getX() + k * (sw + gap);
            juce::Colour c = db < -24 ? kSig : db < -12 ? juce::Colour(0xff9be86a) : db < -4 ? kDyn : kCut;
            float a = 0.14f;
            if (db <= rms) a = 1.0f; else if (db <= pk) a = 0.5f;
            if (hold > -60.0f && db >= hold && db - 60.0f / N < hold) a = 1.0f;
            g.setColour(a < 0.2f ? juce::Colour(0xff2a3646) : c.withAlpha(a));
            g.fillRect(x, r.getY(), sw, r.getHeight());
        }
        const bool clip = juce::Time::getMillisecondCounterHiRes() - clipT < 1500.0;
        g.setColour(clip ? kCut : juce::Colour(0xff2a2230));
        g.fillEllipse(r.getRight() + 3.0f, r.getCentreY() - 4.0f, 8.0f, 8.0f);
    }
private:
    float pk = -90, rms = -90, hold = -90; double holdT = 0, clipT = -1e9;
};

// Rounded panel with a title; children are laid out by the owner.
inline void drawPanel(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, bool header = true)
{
    auto f = r.toFloat().reduced(0.5f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff141d29), 0, f.getY(), juce::Colour(0xff101821), 0, f.getBottom(), false));
    g.fillRoundedRectangle(f, 12.0f);
    g.setColour(kLine); g.drawRoundedRectangle(f, 12.0f, 1.0f);
    if (header && title.isNotEmpty()) {
        g.setColour(kTxt); g.setFont(font(15.0f, true));
        g.drawText(title, r.getX() + 16, r.getY() + 8, r.getWidth() - 32, 24, juce::Justification::centredLeft, false);
        g.setColour(kLine); g.drawHorizontalLine(r.getY() + 38, f.getX() + 1.0f, f.getRight() - 1.0f);
    }
}

inline void styleLabel(juce::Label& l, const juce::String& t, float h = 13.0f, juce::Colour c = kMut, juce::Justification j = juce::Justification::centredLeft, bool bold = false)
{
    l.setText(t, juce::dontSendNotification); l.setFont(font(h, bold)); l.setColour(juce::Label::textColourId, c); l.setJustificationType(j);
}

inline void styleToggle(juce::TextButton& b, const juce::String& text, juce::Colour on = kSig)
{
    b.setButtonText(text); b.setClickingTogglesState(true);
    b.setColour(juce::TextButton::textColourOnId, on);
    b.setColour(juce::TextButton::buttonOnColourId, on.withAlpha(0.16f));
}

} // namespace ui
