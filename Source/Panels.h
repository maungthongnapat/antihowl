// AntiHowl AI - control panels (header-only).
#pragma once
#include "AudioEngine.h"
#include "SpectrumEq.h"
#include "UiKit.h"

namespace panels {

using namespace juce;

// ------------------------------------------------------------------------------------------------
class GrBar : public Component {
public:
    explicit GrBar(Colour c, bool rightToLeft = false) : col(c), rtl(rightToLeft) {}
    void setValue(float v01) { v = jlimit(0.0f, 1.0f, v01); repaint(); }
    void paint(Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour(Colour(0xff0a1017)); g.fillRoundedRectangle(r, 4.0f);
        const float w = r.getWidth() * v;
        g.setColour(col); g.fillRoundedRectangle(rtl ? r.withLeft(r.getRight() - w) : r.withWidth(w), 4.0f);
        g.setColour(ui::kLine); g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
    }
private:
    Colour col; bool rtl; float v = 0;
};

// ------------------------------------------------------------------------------------------------
// EQ editor: pick a band (1-6 or HPF) and set frequency / gain / width. Width has quick presets.
class EqEditorPanel : public Component {
public:
    EqEditorPanel(AudioEngine& e, SpectrumEq& g)
        : eng(e), graph(g),
          freq(TH("ความถี่"), 20, 20000, 1000, 1000.0, 0.0, 84),
          gain(TH("เกน"), -15, 15, 0, 0.0, 0.1, 84),
          width(TH("ความกว้าง"), 0.3, 30, 1, 3.0, 0.0, 120)
    {
        for (int b = 0; b < 7; ++b) {
            auto& t = bandBtn[static_cast<size_t>(b)];
            t.setButtonText(b < 6 ? String(b + 1) : String("HPF"));
            t.setClickingTogglesState(true); t.setRadioGroupId(2001);
            const Colour c = b < 6 ? ui::kBand[b] : Colour(0xff9fb0c6);
            t.setColour(TextButton::textColourOnId, c); t.setColour(TextButton::buttonOnColourId, c.withAlpha(0.2f));
            t.onClick = [this, b] { graph.setSelectedBand(b < 6 ? b : SpectrumEq::kHpf); sync(); };
            addAndMakeVisible(t);
        }
        freq.format = [](double v) { return ui::fmtHz(v); };
        gain.format = [](double v) { return ui::fmtDb(v); };
        width.format = [this](double q) {
            const bool shelf = band() >= 0 && band() < ah::kBands && ah::kEqType[band()] != ah::FilterType::Peaking;
            return shelf ? "Q " + String(q, 2) : "Q" + String(q, 1) + TH(" · ") + String(ah::qToOctaves(q), 2) + " oct";
        };
        freq.onValue = [this](double v) {
            if (band() == SpectrumEq::kHpf) set(ah::P_HPF_F, v); else if (valid()) set(ah::eqF(band()), v);
            graph.repaint();
        };
        gain.onValue = [this](double v) { if (valid()) set(ah::eqG(band()), v); graph.repaint(); };
        width.onValue = [this](double v) { if (valid()) set(ah::eqQ(band()), v); graph.repaint(); };
        addAndMakeVisible(freq); addAndMakeVisible(gain); addAndMakeVisible(width);

        static const struct { const char* name; double q; } presets[4] = {
            { "แคบมาก Q20", 20 }, { "แคบ Q8", 8 },
            { "กลาง Q2", 2 }, { "กว้าง Q0.7", 0.7 } };
        for (int i = 0; i < 4; ++i) {
            auto& b = preset[static_cast<size_t>(i)];
            b.setButtonText(TH(presets[i].name));
            const double q = presets[i].q;
            b.onClick = [this, q] { if (valid()) { set(ah::eqQ(band()), q); sync(); graph.repaint(); } };
            addAndMakeVisible(b);
        }
        reset.setButtonText(TH("รีเซ็ต EQ"));
        reset.onClick = [this] {
            for (int b = 0; b < ah::kBands; ++b) for (int k = 0; k < 3; ++k) {
                const int idx = ah::eqF(b) + k; eng.sp[static_cast<size_t>(ch)].set(idx, ah::stripInfo()[static_cast<size_t>(idx)].def);
            }
            sync(); graph.repaint();
        };
        addAndMakeVisible(reset);
        hpfOn.setClickingTogglesState(true); hpfOn.setButtonText("HPF");
        hpfOn.onClick = [this] { eng.sp[static_cast<size_t>(ch)].set(ah::P_HPF_ON, hpfOn.getToggleState() ? 1.0f : 0.0f); graph.repaint(); };
        addAndMakeVisible(hpfOn);
        ui::styleLabel(info, "", 13.0f, ui::kMut);
        addAndMakeVisible(info);
        sync();
    }

    void setChannel(int c) { ch = c; sync(); }
    int band() const { return graph.getSelectedBand(); }

    void sync()
    {
        const int b = band();
        for (int i = 0; i < 7; ++i) bandBtn[static_cast<size_t>(i)].setToggleState((i < 6 ? i : SpectrumEq::kHpf) == b, dontSendNotification);
        const auto& P = eng.sp[static_cast<size_t>(ch)];
        hpfOn.setToggleState(P.get(ah::P_HPF_ON) > 0.5f, dontSendNotification);
        if (b == SpectrumEq::kHpf) {
            freq.setRange(20, 500, 90, 100.0); freq.setValue(P.get(ah::P_HPF_F));
            gain.setEnabledLook(false); width.setEnabledLook(false);
            info.setText(TH("HPF ตัดเสียงทุ้มต่ำกว่าความถี่ที่ตั้ง (ลดเสียงลมปาก/ไมค์ตก) · ดับเบิลคลิกจุด H เพื่อเปิด/ปิด"), dontSendNotification);
            for (auto& p : preset) p.setEnabled(false);
        } else if (b >= 0 && b < ah::kBands) {
            const bool shelf = ah::kEqType[b] != ah::FilterType::Peaking;
            freq.setRange(20, 20000, 1000, 1000.0); freq.setValue(P.get(ah::eqF(b)));
            gain.setEnabledLook(true); gain.setValue(P.get(ah::eqG(b)));
            width.setEnabledLook(true);
            if (shelf) width.setRange(0.3, 2.0, 0.7071); else width.setRange(0.3, 30.0, 1.0, 3.0);
            width.setName(shelf ? TH("ความชัน (Q)") : TH("ความกว้าง"));
            width.setValue(P.get(ah::eqQ(b)));
            for (auto& p : preset) p.setEnabled(!shelf);
            const char* type = ah::kEqType[b] == ah::FilterType::LowShelf ? "Low shelf (ปรับเสียงทุ้มทั้งย่าน)"
                             : shelf ? "High shelf (ปรับเสียงแหลมทั้งย่าน)"
                             : "Bell (ปรับเฉพาะย่านความถี่)";
            info.setText(TH("แบนด์ ") + String(b + 1) + TH(" · ") + TH(type) + TH(" · ลากจุดบนกราฟ หรือลากมือจับสี่เหลี่ยมสองข้างเพื่อปรับกว้าง/แคบ"), dontSendNotification);
        }
        freq.refreshText(); gain.refreshText(); width.refreshText();
    }

    void paint(Graphics& g) override { ui::drawPanel(g, getLocalBounds(), TH("EQ พาราเมตริก · ปรับความกว้าง–แคบของแต่ละย่าน")); }
    void resized() override
    {
        auto r = getLocalBounds().reduced(14, 0).withTrimmedTop(44).withTrimmedBottom(8);
        auto top = r.removeFromTop(30);
        for (int i = 0; i < 7; ++i) bandBtn[static_cast<size_t>(i)].setBounds(top.removeFromLeft(i < 6 ? 38 : 56).reduced(2, 0));
        hpfOn.setBounds(top.removeFromLeft(60).reduced(4, 0));
        reset.setBounds(top.removeFromRight(100).reduced(2, 0));
        r.removeFromTop(4);
        info.setBounds(r.removeFromBottom(22));
        auto k = r.withTrimmedTop(2);
        freq.setBounds(k.removeFromLeft(96)); gain.setBounds(k.removeFromLeft(96)); width.setBounds(k.removeFromLeft(130));
        k.removeFromLeft(12);
        auto pr = k.withSizeKeepingCentre(k.getWidth(), 34);
        const int w = jmin(112, pr.getWidth() / 4);
        for (int i = 0; i < 4; ++i) preset[static_cast<size_t>(i)].setBounds(pr.removeFromLeft(w).reduced(3, 0));
    }

private:
    bool valid() const { return band() >= 0 && band() < ah::kBands; }
    void set(int param, double v)
    {
        const auto& inf = ah::stripInfo()[static_cast<size_t>(param)];
        eng.sp[static_cast<size_t>(ch)].set(param, jlimit(inf.lo, inf.hi, static_cast<float>(v)));
    }
    AudioEngine& eng; SpectrumEq& graph; int ch = 0;
    std::array<TextButton, 7> bandBtn; std::array<TextButton, 4> preset;
    TextButton reset, hpfOn; Label info;
    ui::Knob freq, gain, width;
};

// ------------------------------------------------------------------------------------------------
// Notch list + per-notch editor (frequency / depth / width).
class NotchPanel : public Component {
public:
    NotchPanel(AudioEngine& e, SpectrumEq& g)
        : eng(e), graph(g),
          freq(TH("ความถี่"), 90, 15000, 1000, 1000.0, 0.0, 84),
          depth(TH("ตัดลึก"), 2, 30, 12, 0.0, 0.5, 84),
          width(TH("ความกว้าง"), 8, 60, 30, 22.0, 0.0, 120)
    {
        for (int j = 0; j < ah::kNotches; ++j) {
            auto& c = chip[static_cast<size_t>(j)];
            c.setClickingTogglesState(false);
            c.onClick = [this, j] { select(j); };
            addChildComponent(c);
        }
        freq.format = [](double v) { return ui::fmtHz(v); };
        depth.format = [](double v) { return "-" + String(v, 1) + " dB"; };
        width.format = [](double q) { return "Q" + String(q, 0) + TH(" · ") + String(ah::qToOctaves(q), 2) + " oct"; };
        depth.setAccent(ui::kCut); freq.setAccent(ui::kCut); width.setAccent(ui::kCut);
        auto push = [this](double) { apply(); };
        freq.onValue = push; depth.onValue = push; width.onValue = push;
        addAndMakeVisible(freq); addAndMakeVisible(depth); addAndMakeVisible(width);
        del.setButtonText(TH("ลบ Notch นี้")); del.onClick = [this] { if (sel >= 0) { eng.afs[static_cast<size_t>(ch)].remove(sel); refresh(); graph.repaint(); } };
        clear.setButtonText(TH("ล้างทั้งหมด")); clear.onClick = [this] { eng.afs[static_cast<size_t>(ch)].clearAll(); sel = -1; refresh(); graph.repaint(); };
        addAndMakeVisible(del); addAndMakeVisible(clear);
        ui::styleLabel(empty, TH("ยังไม่มี Notch · AI จะเพิ่มให้เองเมื่อพบเสียงหอน หรือดับเบิลคลิกบนกราฟเพื่อเพิ่มเอง"), 13.0f, ui::kDim);
        addAndMakeVisible(empty);
        refresh();
    }

    void setChannel(int c) { ch = c; sel = -1; refresh(); }
    void select(int j) { sel = j; graph.setSelectedNotch(j); refresh(); }

    void refresh()
    {
        int count = 0;
        for (int j = 0; j < ah::kNotches; ++j) {
            const auto& s = graph.slots[static_cast<size_t>(j)];
            auto& c = chip[static_cast<size_t>(j)];
            c.setVisible(s.on);
            if (!s.on) continue;
            ++count;
            c.setButtonText(ui::fmtHz(s.f) + "  -" + String(roundToInt(s.depth)) + (s.manual ? TH("  ตั้งเอง") : String("  AI")));
            const Colour col = s.manual ? ui::kEq : ui::kCut;
            c.setColour(TextButton::buttonColourId, col.withAlpha(j == sel ? 0.35f : 0.12f));
            c.setColour(TextButton::textColourOffId, col.brighter(0.3f));
        }
        if (sel >= 0 && !graph.slots[static_cast<size_t>(sel)].on) sel = -1;
        if (sel < 0) for (int j = 0; j < ah::kNotches; ++j) if (graph.slots[static_cast<size_t>(j)].on) { sel = j; break; }
        empty.setVisible(count == 0);
        const bool has = sel >= 0;
        freq.setEnabledLook(has); depth.setEnabledLook(has); width.setEnabledLook(has);
        del.setEnabled(has);
        if (has && !isEditing()) {
            const auto& s = graph.slots[static_cast<size_t>(sel)];
            updating = true;
            freq.setValue(s.f); depth.setValue(s.depth); width.setValue(s.q);
            updating = false;
        }
        layoutChips();
        repaint();
    }

    void paint(Graphics& g) override { ui::drawPanel(g, getLocalBounds(), TH("Notch กันหอน · AI ตัดให้อัตโนมัติ ปรับเองต่อตัวได้")); }
    void resized() override { layoutChips(); }

private:
    bool isEditing() const { return freq.isMouseButtonDown(true) || depth.isMouseButtonDown(true) || width.isMouseButtonDown(true); }
    void apply()
    {
        if (updating || sel < 0) return;
        eng.afs[static_cast<size_t>(ch)].edit(sel, freq.getValue(), static_cast<float>(depth.getValue()), static_cast<float>(width.getValue()));
        graph.slots[static_cast<size_t>(sel)].f = freq.getValue();
        graph.slots[static_cast<size_t>(sel)].depth = static_cast<float>(depth.getValue());
        graph.slots[static_cast<size_t>(sel)].q = static_cast<float>(width.getValue());
        graph.slots[static_cast<size_t>(sel)].manual = true;
        graph.repaint();
    }
    void layoutChips()
    {
        auto r = getLocalBounds().reduced(14, 0).withTrimmedTop(44).withTrimmedBottom(8);
        auto chips = r.removeFromTop(30);
        int x = chips.getX();
        for (int j = 0; j < ah::kNotches; ++j) {
            auto& c = chip[static_cast<size_t>(j)];
            if (!c.isVisible()) continue;
            const int w = jmin(150, 20 + GlyphArrangement::getStringWidthInt(ui::font(13.0f), c.getButtonText()));
            c.setBounds(x, chips.getY(), w, 28); x += w + 6;
        }
        empty.setBounds(chips);
        auto k = r.withTrimmedTop(2);
        freq.setBounds(k.removeFromLeft(96)); depth.setBounds(k.removeFromLeft(96)); width.setBounds(k.removeFromLeft(130));
        k.removeFromLeft(12);
        auto b = k.withSizeKeepingCentre(k.getWidth(), 34);
        del.setBounds(b.removeFromLeft(120).reduced(3, 0)); clear.setBounds(b.removeFromLeft(120).reduced(3, 0));
    }
    AudioEngine& eng; SpectrumEq& graph; int ch = 0, sel = -1; bool updating = false;
    std::array<TextButton, ah::kNotches> chip; TextButton del, clear; Label empty;
    ui::Knob freq, depth, width;
};

// ------------------------------------------------------------------------------------------------
class DynamicsPanel : public Component {
public:
    explicit DynamicsPanel(AudioEngine& e) : eng(e), gateBar(ui::kDyn), compBar(Colour(0xffff8f5c)), gateLed()
    {
        auto add = [this](int group, const String& name, int param, double lo, double hi, double skewMid, double step,
                          std::function<String(double)> fmt, Colour accent) {
            auto k = std::make_unique<ui::Knob>(name, lo, hi, ah::stripInfo()[static_cast<size_t>(param)].def, skewMid, step, 70);
            k->format = fmt; k->setAccent(accent);
            k->onValue = [this, param](double v) { eng.sp[static_cast<size_t>(ch)].set(param, static_cast<float>(v)); };
            addAndMakeVisible(*k);
            binds.push_back({ std::move(k), param, group });
        };
        auto ms = [](double v) { return ui::fmtMs(v); };
        auto db = [](double v) { return ui::fmtDb(v); };
        add(0, "Threshold", ah::P_GATE_THR, -80, -10, 0, 0.5, db, ui::kDyn);
        add(0, "Attack", ah::P_GATE_ATK, 0.1, 50, 2.2, 0, ms, ui::kDyn);
        add(0, "Hold", ah::P_GATE_HOLD, 0, 500, 0, 5, ms, ui::kDyn);
        add(0, "Release", ah::P_GATE_REL, 10, 1500, 120, 0, ms, ui::kDyn);
        add(0, "Range", ah::P_GATE_RANGE, -80, 0, 0, 1, db, ui::kDyn);
        add(1, "Threshold", ah::P_COMP_THR, -40, 0, 0, 0.5, db, Colour(0xffff8f5c));
        add(1, "Ratio", ah::P_COMP_RATIO, 1, 20, 4.5, 0, [](double v) { return String(v, 1) + ":1"; }, Colour(0xffff8f5c));
        add(1, "Attack", ah::P_COMP_ATK, 0.5, 100, 7, 0, ms, Colour(0xffff8f5c));
        add(1, "Release", ah::P_COMP_REL, 10, 1000, 100, 0, ms, Colour(0xffff8f5c));
        add(1, "Makeup", ah::P_COMP_MAKEUP, 0, 18, 0, 0.5, db, Colour(0xffff8f5c));
        add(2, "Ceiling", ah::P_LIM_CEIL, -12, 0, 0, 0.5, db, ui::kCut);

        ui::styleToggle(gateB, "Gate", ui::kDyn); ui::styleToggle(compB, "Comp", Colour(0xffff8f5c)); ui::styleToggle(limB, "Limiter", ui::kCut);
        gateB.onClick = [this] { eng.sp[static_cast<size_t>(ch)].set(ah::P_GATE_ON, gateB.getToggleState() ? 1.f : 0.f); };
        compB.onClick = [this] { eng.sp[static_cast<size_t>(ch)].set(ah::P_COMP_ON, compB.getToggleState() ? 1.f : 0.f); };
        limB.onClick = [this] { eng.sp[static_cast<size_t>(ch)].set(ah::P_LIM_ON, limB.getToggleState() ? 1.f : 0.f); };
        for (auto* c : std::initializer_list<Component*>{ &gateB, &compB, &limB, &gateBar, &compBar, &gateLed }) addAndMakeVisible(c);
        ui::styleLabel(note, TH("กันสัญญานเกินเพดานก่อนถึงลำโพงและแอมป์"), 12.0f, ui::kDim);
        ui::styleLabel(gl1, TH("ตัดลง"), 12.0f); ui::styleLabel(gl2, TH("ลดระดับ"), 12.0f);
        addAndMakeVisible(note); addAndMakeVisible(gl1); addAndMakeVisible(gl2);
        sync();
    }

    void setChannel(int c) { ch = c; sync(); }
    void sync()
    {
        const auto& P = eng.sp[static_cast<size_t>(ch)];
        for (auto& b : binds) b.knob->setValue(P.get(b.param));
        gateB.setToggleState(P.get(ah::P_GATE_ON) > 0.5f, dontSendNotification);
        compB.setToggleState(P.get(ah::P_COMP_ON) > 0.5f, dontSendNotification);
        limB.setToggleState(P.get(ah::P_LIM_ON) > 0.5f, dontSendNotification);
    }
    void updateMeters()
    {
        const auto& s = *eng.strips[static_cast<size_t>(ch)];
        const bool run = eng.isRunning();
        gateBar.setValue(run ? s.gateAttDb.load() / 60.0f : 0.f);
        compBar.setValue(run ? s.compGrDb.load() / 20.0f : 0.f);
        gateLed.setOn(run && s.gateOpen.load());
    }

    void paint(Graphics& g) override
    {
        ui::drawPanel(g, getLocalBounds(), TH("Gate · Compressor · Limiter ของช่อง ") + String(ch + 1));
        g.setColour(ui::kLine);
        for (int x : { div1, div2 }) g.drawVerticalLine(x, 46.0f, static_cast<float>(getHeight() - 8));
    }
    void resized() override
    {
        auto r = getLocalBounds().withTrimmedTop(44).reduced(8, 4);
        const int w = r.getWidth(), gw = static_cast<int>(w * 0.40f), cw = static_cast<int>(w * 0.40f);
        auto g1 = r.removeFromLeft(gw), g2 = r.removeFromLeft(cw), g3 = r;
        div1 = getX() * 0 + g1.getRight() + 4; div2 = g2.getRight() + 4;
        auto head = [](Rectangle<int> a) { return a.removeFromTop(28); };
        auto h1 = head(g1), h2 = head(g2), h3 = head(g3);
        gateB.setBounds(h1.removeFromLeft(64)); gateLed.setBounds(h1.removeFromLeft(24).withSizeKeepingCentre(10, 10)); gl1.setBounds(h1.removeFromLeft(44)); gateBar.setBounds(h1.reduced(4, 9));
        compB.setBounds(h2.removeFromLeft(64)); h2.removeFromLeft(24); gl2.setBounds(h2.removeFromLeft(50)); compBar.setBounds(h2.reduced(4, 9));
        limB.setBounds(h3.removeFromLeft(80));
        auto place = [&](int group, Rectangle<int> a) {
            a.removeFromTop(32);
            for (auto& b : binds) if (b.group == group) b.knob->setBounds(a.removeFromLeft(72).withHeight(92));
        };
        place(0, g1); place(1, g2); place(2, g3);
        note.setBounds(g3.withTrimmedTop(128).withHeight(36));
    }

private:
    struct LedDot : Component {
        void setOn(bool b) { if (b != on) { on = b; repaint(); } }
        void paint(Graphics& g) override { g.setColour(on ? ui::kSig : Colour(0xff2a394c)); g.fillEllipse(getLocalBounds().toFloat()); }
        bool on = false;
    };
    struct Bind { std::unique_ptr<ui::Knob> knob; int param; int group; };
    AudioEngine& eng; int ch = 0, div1 = 0, div2 = 0;
    std::vector<Bind> binds; TextButton gateB, compB, limB; GrBar gateBar, compBar; LedDot gateLed; Label note, gl1, gl2;
};

// ------------------------------------------------------------------------------------------------
class AfsPanel : public Component {
public:
    explicit AfsPanel(AudioEngine& e, SpectrumEq& g)
        : eng(e), graph(g), riskBar(ui::kCut), estBar(ui::kEq), cpuBar(ui::kDyn),
          depth(TH("ตัดลึกสุด"), 6, 30, 18, 0.0, 1.0, 84), qk(TH("ความแคบ (Q)"), 8, 60, 30, 22.0, 0.0, 84), rel(TH("คืนค่าเอง"), 5, 180, 45, 30.0, 0.0, 84)
    {
        ui::styleLabel(riskL, TH("ความเสี่ยงหอนตอนนี้"), 13.0f); ui::styleLabel(riskT, "", 14.0f, ui::kTxt, Justification::centredRight, true);
        ui::styleLabel(sensL, TH("ความไวในการตรวจจับ"), 13.0f); ui::styleLabel(sensT, "", 14.0f, ui::kTxt, Justification::centredRight, true);
        ui::styleLabel(estL, TH("โหลดโดยประมาณ"), 12.0f); ui::styleLabel(cpuL, TH("CPU จริงของ AI"), 12.0f);
        ui::styleLabel(info, "", 12.0f, ui::kDim);
        sens.setSliderStyle(Slider::LinearHorizontal); sens.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
        sens.setRange(1, 5, 1); sens.setColour(Slider::trackColourId, ui::kSig);
        sens.onValueChange = [this] { eng.gp.set(ah::G_SENS, static_cast<float>(sens.getValue())); updateSens(); };
        depth.format = [](double v) { return "-" + String(v, 0) + " dB"; };
        qk.format = [](double v) { return "Q " + String(v, 0); };
        rel.format = [](double v) { return String(roundToInt(v)) + TH(" วิ"); };
        depth.onValue = [this](double v) { eng.gp.set(ah::G_DEPTH, static_cast<float>(v)); };
        qk.onValue = [this](double v) { eng.gp.set(ah::G_Q, static_cast<float>(v)); };
        rel.onValue = [this](double v) { eng.gp.set(ah::G_RELSEC, static_cast<float>(v)); };
        ui::styleToggle(lock, TH("ล็อก Notch ถาวร"));
        lock.onClick = [this] { eng.gp.set(ah::G_LOCK, lock.getToggleState() ? 1.f : 0.f); };
        logBox.setMultiLine(true); logBox.setReadOnly(true); logBox.setScrollbarsShown(true); logBox.setFont(ui::font(12.5f));
        logBox.setText(TH("รอเริ่มระบบ…"), false);
        for (auto* c : std::initializer_list<Component*>{ &riskL, &riskT, &riskBar, &sensL, &sensT, &sens, &estL, &cpuL, &estBar, &cpuBar, &info, &depth, &qk, &rel, &lock, &logBox })
            addAndMakeVisible(c);
        sync();
    }

    void sync()
    {
        sens.setValue(eng.gp.get(ah::G_SENS), dontSendNotification);
        depth.setValue(eng.gp.get(ah::G_DEPTH)); qk.setValue(eng.gp.get(ah::G_Q)); rel.setValue(eng.gp.get(ah::G_RELSEC));
        lock.setToggleState(eng.gp.get(ah::G_LOCK) > 0.5f, dontSendNotification);
        updateSens();
    }
    void update()
    {
        const bool afsOn = eng.gp.get(ah::G_AFS) > 0.5f, run = eng.isRunning();
        risk += (graph.risk - risk) * 0.4f;
        riskBar.setValue(afsOn && run ? jmax(0.03f, risk) : 0.f);
        riskT.setText(!afsOn ? TH("ระบบกันหอนปิดอยู่") : !run ? TH("—") : risk > 0.6f ? TH("พบเสียงหอน") : risk > 0.3f ? TH("เฝ้าระวัง") : TH("ปลอดภัย"), dontSendNotification);
        riskT.setColour(Label::textColourId, risk > 0.6f && afsOn ? ui::kCut : ui::kTxt);
        const float cpu = eng.cpuAfsPct.load();
        cpuBar.setValue(jlimit(0.02f, 1.0f, cpu * 0.2f));
        cpuL.setText(TH("CPU จริงของ AI ") + String(cpu, 1) + "%", dontSendNotification);
        // collect log lines from all channels
        bool added = false;
        for (int s = 0; s < ah::kMaxStrips; ++s)
            for (const auto& line : eng.afs[static_cast<size_t>(s)].takeLogs()) { lines.insert(0, stamp() + " CH" + String(s + 1) + " " + describe(String(line))); added = true; }
        if (added) {
            while (lines.size() > 60) lines.remove(lines.size() - 1);
            logBox.setText(lines.joinIntoString("\n"), false);
        }
    }

    void paint(Graphics& g) override
    {
        ui::drawPanel(g, getLocalBounds(), TH("AI ตรวจจับเสียงหอน"));
        auto r = getLocalBounds().reduced(16, 0);
        g.setColour(ui::kLine); g.drawHorizontalLine(sepY1, static_cast<float>(r.getX()), static_cast<float>(r.getRight()));
        g.drawHorizontalLine(sepY2, static_cast<float>(r.getX()), static_cast<float>(r.getRight()));
        g.setColour(ui::kDim); g.setFont(ui::font(11.0f));
        static const char* names[5] = { "ประหยัด", "ปกติ", "ไว",
                                        "ไวมาก", "สูงสุด" };
        for (int i = 0; i < 5; ++i)
            g.drawText(TH(names[i]), Rectangle<int>(ticks.getX() + i * ticks.getWidth() / 5, ticks.getY(), ticks.getWidth() / 5, ticks.getHeight()), Justification::centred, false);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced(16, 0).withTrimmedTop(46);
        auto a = r.removeFromTop(22); riskL.setBounds(a.removeFromLeft(a.getWidth() / 2)); riskT.setBounds(a);
        riskBar.setBounds(r.removeFromTop(14).reduced(0, 2)); r.removeFromTop(8);
        sepY1 = r.getY(); r.removeFromTop(10);
        auto b = r.removeFromTop(22); sensL.setBounds(b.removeFromLeft(b.getWidth() / 2)); sensT.setBounds(b);
        sens.setBounds(r.removeFromTop(26));
        ticks = r.removeFromTop(16);
        auto c = r.removeFromTop(16); estL.setBounds(c.removeFromLeft(c.getWidth() / 2)); cpuL.setBounds(c);
        auto d = r.removeFromTop(12); estBar.setBounds(d.removeFromLeft(d.getWidth() / 2 - 6)); d.removeFromLeft(12); cpuBar.setBounds(d);
        info.setBounds(r.removeFromTop(20));
        r.removeFromTop(6); sepY2 = r.getY(); r.removeFromTop(8);
        auto k = r.removeFromTop(96);
        depth.setBounds(k.removeFromLeft(k.getWidth() / 3)); qk.setBounds(k.removeFromLeft(k.getWidth() / 2)); rel.setBounds(k);
        lock.setBounds(r.removeFromTop(30).reduced(40, 0)); r.removeFromTop(8);
        logBox.setBounds(r.withTrimmedBottom(12));
    }

private:
    void updateSens()
    {
        const int lv = static_cast<int>(std::lround(eng.gp.get(ah::G_SENS)));
        const auto& cfg = ah::sensCfg(lv);
        sensT.setText(TH(cfg.name), dontSendNotification);
        info.setText("FFT " + String(cfg.fft) + TH(" จุด · วิเคราะห์ ") + String(1000 / cfg.intervalMs) + TH(" ครั้ง/วินาที/ช่อง"), dontSendNotification);
        auto cost = [](int v) { const auto& c = ah::sensCfg(v); return c.fft * std::log2(static_cast<double>(c.fft)) / c.intervalMs; };
        estBar.setValue(static_cast<float>(jmax(0.04, cost(lv) / cost(5))));
    }
    static String stamp() { return Time::getCurrentTime().formatted("%H:%M:%S"); }
    static String describe(const String& line)
    {
        const StringArray t = StringArray::fromTokens(line, " ", "");
        if (t.size() >= 4 && t[0] == "HOWL")
            return TH("พบหอนที่ ") + ui::fmtHz(t[1].getDoubleValue()) + " (AI " + t[2] + TH("%) → ตัด -") + t[3] + " dB";
        if (t.size() >= 3 && t[0] == "DEEPEN")
            return TH("ยังหอนที่ ") + ui::fmtHz(t[1].getDoubleValue()) + TH(" → ลึกขึ้นเป็น -") + t[2] + " dB";
        if (t.size() >= 2 && t[0] == "RELEASE")
            return TH("คืนค่า Notch ") + ui::fmtHz(t[1].getDoubleValue());
        return line;
    }
    AudioEngine& eng; SpectrumEq& graph; float risk = 0; int sepY1 = 0, sepY2 = 0; Rectangle<int> ticks;
    Label riskL, riskT, sensL, sensT, estL, cpuL, info; GrBar riskBar, estBar, cpuBar; Slider sens; ui::Knob depth, qk, rel;
    TextButton lock; TextEditor logBox; StringArray lines;
};

// ------------------------------------------------------------------------------------------------
class FxPanel : public Component {
public:
    explicit FxPanel(AudioEngine& e)
        : eng(e), mix(TH("Mix"), 0, 1, 0.18, 0.0, 0.01, 84), decay("Decay", 0.3, 6, 2.4, 1.5, 0.0, 84),
          pre("Pre-delay", 0, 120, 20, 0.0, 1.0, 84), tone("Tone", 1500, 16000, 6500, 4900.0, 0.0, 84)
    {
        type.addItem(TH("ปิด"), 1); type.addItem("Room", 2); type.addItem("Hall", 3); type.addItem("Plate", 4); type.addItem("Church", 5); type.addItem("Echo", 6);
        type.onChange = [this] {
            const int t = type.getSelectedId() - 1;
            eng.gp.set(ah::G_FX_TYPE, static_cast<float>(t));
            static const float dec[6] = { 2.4f, 0.9f, 2.4f, 1.6f, 4.8f, 2.4f };
            eng.gp.set(ah::G_FX_DECAY, dec[t]); decay.setValue(dec[t]);
        };
        mix.format = [](double v) { return String(roundToInt(v * 100)) + "%"; };
        decay.format = [](double v) { return String(v, 1) + TH(" วิ"); };
        pre.format = [](double v) { return String(roundToInt(v)) + " ms"; };
        tone.format = [](double v) { return ui::fmtHz(v); };
        mix.onValue = [this](double v) { eng.gp.set(ah::G_FX_MIX, static_cast<float>(v)); };
        decay.onValue = [this](double v) { eng.gp.set(ah::G_FX_DECAY, static_cast<float>(v)); };
        pre.onValue = [this](double v) { eng.gp.set(ah::G_FX_PRE, static_cast<float>(v)); };
        tone.onValue = [this](double v) { eng.gp.set(ah::G_FX_TONE, static_cast<float>(v)); };
        for (auto* k : { &mix, &decay, &pre, &tone }) { k->setAccent(ui::kFx); addAndMakeVisible(*k); }
        addAndMakeVisible(type);
        sync();
    }
    void sync()
    {
        type.setSelectedId(static_cast<int>(eng.gp.get(ah::G_FX_TYPE)) + 1, dontSendNotification);
        mix.setValue(eng.gp.get(ah::G_FX_MIX)); decay.setValue(eng.gp.get(ah::G_FX_DECAY));
        pre.setValue(eng.gp.get(ah::G_FX_PRE)); tone.setValue(eng.gp.get(ah::G_FX_TONE));
    }
    void paint(Graphics& g) override { ui::drawPanel(g, getLocalBounds(), TH("เอฟเฟกต์ (Hall / Room / Plate / Church / Echo)")); }
    void resized() override
    {
        auto r = getLocalBounds().reduced(16, 0).withTrimmedTop(46);
        type.setBounds(r.removeFromTop(28)); r.removeFromTop(6);
        auto k = r.removeFromTop(96); const int w = k.getWidth() / 4;
        mix.setBounds(k.removeFromLeft(w)); decay.setBounds(k.removeFromLeft(w)); pre.setBounds(k.removeFromLeft(w)); tone.setBounds(k);
    }
private:
    AudioEngine& eng; ComboBox type; ui::Knob mix, decay, pre, tone;
};

// ------------------------------------------------------------------------------------------------
class TestPanel : public Component {
public:
    explicit TestPanel(AudioEngine& e, std::function<int()> selectedChannel) : eng(e), selCh(std::move(selectedChannel))
    {
        for (int f : { 800, 1250, 2000, 2500, 3150, 4000, 6300 }) freqBox.addItem(ui::fmtHz(f), f);
        freqBox.setSelectedId(2500, dontSendNotification);
        freqBox.onChange = [this] { eng.gp.set(ah::G_SIM_F, static_cast<float>(freqBox.getSelectedId())); };
        ui::styleToggle(sim, TH("จำลองเสียงหอน"), ui::kCut);
        sim.onClick = [this] {
            eng.gp.set(ah::G_SIM_CH, static_cast<float>(selCh()));
            eng.gp.set(ah::G_SIM_F, static_cast<float>(freqBox.getSelectedId()));
            eng.gp.set(ah::G_SIM_ON, sim.getToggleState() ? 1.f : 0.f);
        };
        ui::styleLabel(txt, TH("จำลองลูปเสียงสะท้อนบนช่องที่เลือก เพื่อดู AI จับหอนและตัด Notch สดๆ โดยไม่ต้องใช้ลำโพงจริง (เสียงจำลองดังประมาณ -18 dBFS)"), 12.5f, ui::kMut);
        txt.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(freqBox); addAndMakeVisible(sim); addAndMakeVisible(txt);
        eng.gp.set(ah::G_SIM_F, 2500.f);
    }
    void paint(Graphics& g) override { ui::drawPanel(g, getLocalBounds(), TH("ทดสอบระบบ")); }
    void resized() override
    {
        auto r = getLocalBounds().reduced(16, 0).withTrimmedTop(44);
        txt.setBounds(r.removeFromTop(50)); r.removeFromTop(4);
        auto row = r.removeFromTop(30);
        freqBox.setBounds(row.removeFromLeft(row.getWidth() / 2 - 4)); row.removeFromLeft(8); sim.setBounds(row);
    }
    void syncOff() { sim.setToggleState(false, dontSendNotification); }
private:
    AudioEngine& eng; std::function<int()> selCh; ComboBox freqBox; TextButton sim; Label txt;
};

// ------------------------------------------------------------------------------------------------
class MixerPanel : public Component {
public:
    MixerPanel(AudioEngine& e, std::function<void(int)> onSelect, std::function<void()> onNchChanged)
        : eng(e), selectCb(std::move(onSelect)), nchCb(std::move(onNchChanged))
    {
        for (int i = 1; i <= ah::kMaxStrips; ++i) nchBox.addItem(String(i) + TH(" ช่อง"), i);
        nchBox.onChange = [this] { eng.gp.set(ah::G_NCH, static_cast<float>(nchBox.getSelectedId())); if (nchCb) nchCb(); };
        addAndMakeVisible(nchBox);
        ui::styleLabel(nchL, TH("จำนวนช่องไมค์"), 13.0f); addAndMakeVisible(nchL);
        rebuild();
    }

    int neededHeight() const { return 44 + static_cast<int>(rows.size()) * 50 + 58 + 8; }

    void rebuild()
    {
        rows.clear(); master.reset();
        nchBox.setSelectedId(jlimit(1, ah::kMaxStrips, static_cast<int>(eng.gp.get(ah::G_NCH))), dontSendNotification);
        const int n = jlimit(1, ah::kMaxStrips, static_cast<int>(eng.gp.get(ah::G_NCH)));
        for (int i = 0; i < n; ++i) {
            rows.push_back(std::make_unique<Row>(eng, i, inputNames, [this](int c) { if (selectCb) selectCb(c); }));
            addAndMakeVisible(*rows.back());
        }
        master = std::make_unique<MasterRow>(eng);
        addAndMakeVisible(*master);
        setSelected(sel);
        resized();
    }
    void setInputNames(const StringArray& names) { inputNames = names; for (auto& r : rows) r->setInputs(names); }
    void setOutputNames(const StringArray& names) { outputNames = names; if (master) master->setOutputs(names); }
    void setSelected(int c) { sel = c; for (size_t i = 0; i < rows.size(); ++i) rows[i]->setSelected(static_cast<int>(i) == c); }
    void updateMeters()
    {
        for (auto& r : rows) r->updateMeter();
        if (master) master->updateMeter();
        for (auto& r : rows) r->refreshNotchCount();
    }

    void paint(Graphics& g) override { ui::drawPanel(g, getLocalBounds(), TH("มิกเซอร์ช่องไมค์ · เลือกช่องอินพุตของแต่ละไมค์ได้อิสระ · S = ฟังเฉพาะช่องนั้น · M = ปิดเสียง")); }
    void resized() override
    {
        auto r = getLocalBounds();
        auto hd = r.removeFromTop(40).reduced(14, 6);
        nchBox.setBounds(hd.removeFromRight(110)); nchL.setBounds(hd.removeFromRight(110));
        r.removeFromTop(4);
        for (auto& row : rows) row->setBounds(r.removeFromTop(50).reduced(10, 2));
        if (master) master->setBounds(r.removeFromTop(58).reduced(10, 2));
    }

private:
    // ---- one channel strip row
    class Row : public Component {
    public:
        Row(AudioEngine& e, int idx, const StringArray& inputs, std::function<void(int)> sel)
            : eng(e), i(idx), trim("", -20, 30, 0, 0.0, 0.5, 58), onSel(std::move(sel))
        {
            name.setButtonText("CH " + String(i + 1)); name.onClick = [this] { onSel(i); };
            name.setColour(TextButton::buttonColourId, Colour(0xff1b2634));
            addAndMakeVisible(name);
            setInputs(inputs);
            src.onChange = [this] { eng.sp[static_cast<size_t>(i)].set(ah::P_SRC, static_cast<float>(src.getSelectedId() - 1)); };
            addAndMakeVisible(src);
            trim.setAccent(ui::kEq); trim.format = [](double v) { return ui::fmtDb(v, 0); };
            trim.setValue(eng.sp[static_cast<size_t>(i)].get(ah::P_TRIM));
            trim.onValue = [this](double v) { eng.sp[static_cast<size_t>(i)].set(ah::P_TRIM, static_cast<float>(v)); };
            addAndMakeVisible(trim); addAndMakeVisible(meter);
            fader.setSliderStyle(Slider::LinearHorizontal); fader.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
            fader.setRange(-60, 12, 0.5); fader.setDoubleClickReturnValue(true, 0.0);
            fader.setValue(eng.sp[static_cast<size_t>(i)].get(ah::P_FADER), dontSendNotification);
            fader.onValueChange = [this] { eng.sp[static_cast<size_t>(i)].set(ah::P_FADER, static_cast<float>(fader.getValue())); showVal(); };
            addAndMakeVisible(fader);
            ui::styleLabel(val, "", 12.0f, ui::kMut, Justification::centredRight); val.setFont(ui::mono(12.0f)); addAndMakeVisible(val); showVal();
            ui::styleToggle(afsB, "AFS"); ui::styleToggle(soloB, "S", ui::kDyn); ui::styleToggle(muteB, "M", ui::kCut);
            afsB.setToggleState(eng.sp[static_cast<size_t>(i)].get(ah::P_AFS) > 0.5f, dontSendNotification);
            soloB.setToggleState(eng.sp[static_cast<size_t>(i)].get(ah::P_SOLO) > 0.5f, dontSendNotification);
            muteB.setToggleState(eng.sp[static_cast<size_t>(i)].get(ah::P_MUTE) > 0.5f, dontSendNotification);
            afsB.onClick = [this] { eng.sp[static_cast<size_t>(i)].set(ah::P_AFS, afsB.getToggleState() ? 1.f : 0.f); };
            soloB.onClick = [this] { eng.sp[static_cast<size_t>(i)].set(ah::P_SOLO, soloB.getToggleState() ? 1.f : 0.f); };
            muteB.onClick = [this] { eng.sp[static_cast<size_t>(i)].set(ah::P_MUTE, muteB.getToggleState() ? 1.f : 0.f); };
            afsB.setTooltip(TH("เปิด/ปิดระบบกันหอนเฉพาะช่องนี้"));
            for (auto* b : { &afsB, &soloB, &muteB }) addAndMakeVisible(*b);
            ui::styleLabel(nc, "0", 12.0f, ui::kDim, Justification::centred, true); addAndMakeVisible(nc);
        }
        void setInputs(const StringArray& names)
        {
            src.clear(dontSendNotification);
            const int n = jmax(names.size(), jmax(2, i + 1));
            for (int k = 0; k < n; ++k) src.addItem(k < names.size() ? names[k] : TH("อินพุต ") + String(k + 1), k + 1);
            src.setSelectedId(jlimit(0, n - 1, static_cast<int>(eng.sp[static_cast<size_t>(i)].get(ah::P_SRC))) + 1, dontSendNotification);
        }
        void setSelected(bool s) { selected = s; repaint(); }
        void updateMeter()
        {
            auto& st = *eng.strips[static_cast<size_t>(i)];
            const float pk = st.peakAcc.exchange(0.f, std::memory_order_relaxed);
            meter.setLevel(eng.isRunning() ? pk : 0.f, eng.isRunning() ? st.rmsNow.load() : 0.f);
        }
        void refreshNotchCount()
        {
            const int c = eng.afs[static_cast<size_t>(i)].activeCount();
            nc.setText(String(c), dontSendNotification);
            nc.setColour(Label::textColourId, c ? ui::kCut : ui::kDim);
        }
        void paint(Graphics& g) override
        {
            if (selected) { g.setColour(ui::kEq.withAlpha(0.10f)); g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f); }
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced(4, 2);
            name.setBounds(r.removeFromLeft(86)); r.removeFromLeft(8);
            src.setBounds(r.removeFromLeft(130).withSizeKeepingCentre(130, 28)); r.removeFromLeft(8);
            trim.setBounds(r.removeFromLeft(60)); r.removeFromLeft(8);
            nc.setBounds(r.removeFromRight(30)); r.removeFromRight(4);
            muteB.setBounds(r.removeFromRight(36).withSizeKeepingCentre(34, 28)); soloB.setBounds(r.removeFromRight(36).withSizeKeepingCentre(34, 28));
            afsB.setBounds(r.removeFromRight(52).withSizeKeepingCentre(48, 28)); r.removeFromRight(8);
            val.setBounds(r.removeFromRight(62)); fader.setBounds(r.removeFromRight(170).withSizeKeepingCentre(170, 26)); r.removeFromRight(8);
            meter.setBounds(r.withSizeKeepingCentre(r.getWidth(), 18));
        }
    private:
        void showVal() { const double v = fader.getValue(); val.setText(v <= -59.5 ? TH("-∞ dB") : ui::fmtDb(v), dontSendNotification); }
        AudioEngine& eng; int i; bool selected = false;
        TextButton name, afsB, soloB, muteB; ComboBox src; ui::Knob trim; ui::LevelMeter meter; Slider fader; Label val, nc;
        std::function<void(int)> onSel;
    };

    class MasterRow : public Component {
    public:
        explicit MasterRow(AudioEngine& e) : eng(e)
        {
            ui::styleLabel(title, "MASTER", 15.0f, ui::kDyn, Justification::centredLeft, true); addAndMakeVisible(title);
            out.onChange = [this] { eng.gp.set(ah::G_OUTPAIR, static_cast<float>(out.getSelectedId() - 1)); };
            addAndMakeVisible(out); addAndMakeVisible(meter);
            fader.setSliderStyle(Slider::LinearHorizontal); fader.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
            fader.setRange(-60, 12, 0.5); fader.setDoubleClickReturnValue(true, 0.0); fader.setColour(Slider::trackColourId, ui::kDyn);
            fader.setValue(eng.gp.get(ah::G_MASTER), dontSendNotification);
            fader.onValueChange = [this] { eng.gp.set(ah::G_MASTER, static_cast<float>(fader.getValue())); showVal(); };
            addAndMakeVisible(fader);
            ui::styleLabel(val, "", 12.0f, ui::kMut, Justification::centredRight); val.setFont(ui::mono(12.0f)); addAndMakeVisible(val); showVal();
            ui::styleToggle(lim, "Limiter", ui::kCut); lim.setToggleState(eng.gp.get(ah::G_MLIM) > 0.5f, dontSendNotification);
            lim.onClick = [this] { eng.gp.set(ah::G_MLIM, lim.getToggleState() ? 1.f : 0.f); };
            addAndMakeVisible(lim);
            ui::styleLabel(gr, "", 11.5f, ui::kDim, Justification::centred); addAndMakeVisible(gr);
            setOutputs({});
        }
        void setOutputs(const StringArray& names)
        {
            out.clear(dontSendNotification);
            const int pairs = jmax(1, (names.size() + 1) / 2);
            for (int p = 0; p < pairs; ++p) {
                String a = 2 * p < names.size() ? names[2 * p] : String(2 * p + 1), b = 2 * p + 1 < names.size() ? names[2 * p + 1] : "";
                out.addItem(TH("ส่งออก ") + (b.isEmpty() ? a : a + " + " + b), p + 1);
            }
            out.setSelectedId(jlimit(0, pairs - 1, static_cast<int>(eng.gp.get(ah::G_OUTPAIR))) + 1, dontSendNotification);
        }
        void updateMeter()
        {
            const float pk = eng.masterPeakAcc.exchange(0.f, std::memory_order_relaxed);
            meter.setLevel(eng.isRunning() ? pk : 0.f, eng.isRunning() ? eng.masterRms.load() : 0.f);
            const float g = eng.masterGr.load();
            gr.setText(g > 0.3f ? "GR " + String(g, 1) : String(), dontSendNotification);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced(4, 6);
            title.setBounds(r.removeFromLeft(86)); r.removeFromLeft(8);
            out.setBounds(r.removeFromLeft(250)); r.removeFromLeft(8);
            lim.setBounds(r.removeFromRight(74)); r.removeFromRight(8);
            gr.setBounds(r.removeFromRight(60));
            val.setBounds(r.removeFromRight(62)); fader.setBounds(r.removeFromRight(170).withSizeKeepingCentre(170, 26)); r.removeFromRight(8);
            meter.setBounds(r.withSizeKeepingCentre(r.getWidth(), 20));
        }
        void paint(Graphics& g) override { g.setColour(Colour(0xff0e151d)); g.fillRoundedRectangle(getLocalBounds().toFloat(), 10.0f); }
    private:
        void showVal() { const double v = fader.getValue(); val.setText(v <= -59.5 ? TH("-∞ dB") : ui::fmtDb(v), dontSendNotification); }
        AudioEngine& eng; Label title, val, gr; ComboBox out; ui::LevelMeter meter; Slider fader; TextButton lim;
    };

    AudioEngine& eng; std::function<void(int)> selectCb; std::function<void()> nchCb; int sel = 0;
    StringArray inputNames, outputNames; ComboBox nchBox; Label nchL;
    std::vector<std::unique_ptr<Row>> rows; std::unique_ptr<MasterRow> master;
};

} // namespace panels
