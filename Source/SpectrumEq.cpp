#include "SpectrumEq.h"

using namespace juce;

SpectrumEq::SpectrumEq(AudioEngine& e) : eng(e)
{
    samples.assign(static_cast<size_t>(kFft), 0.f);
    spec.assign(static_cast<size_t>(kFft / 2), -120.f);
    sa.setup(kFft, 0.72f);
    for (auto& s : slots) s = ah::SlotView();
    setMouseCursor(MouseCursor::CrosshairCursor);
    startTimerHz(30);
}

void SpectrumEq::setChannel(int c)
{
    ch = jlimit(0, ah::kMaxStrips - 1, c);
    std::fill(disp.begin(), disp.end(), -120.f); std::fill(hold.begin(), hold.end(), -120.f);
    selNotch = -1; repaint();
}

void SpectrumEq::resized()
{
    plot = getLocalBounds().toFloat().withTrimmedLeft(44.0f).withTrimmedRight(48.0f).withTrimmedTop(10.0f).withTrimmedBottom(24.0f);
    const size_t n = static_cast<size_t>(jmax(1.0f, plot.getWidth())) + 4;
    disp.assign(n, -120.f); hold.assign(n, -120.f);
}

void SpectrumEq::setStrip(int param, float v)
{
    const auto& info = ah::stripInfo()[static_cast<size_t>(param)];
    eng.sp[static_cast<size_t>(ch)].set(param, jlimit(info.lo, info.hi, v));
}

// ------------------------------------------------------------------------------------------------
void SpectrumEq::timerCallback()
{
    if (plot.isEmpty()) return;
    live = eng.isRunning();
    if (live) {
        eng.strips[static_cast<size_t>(ch)]->rtaRing.copyLatest(samples.data(), kFft);
        sa.process(samples.data(), spec.data());
    }
    const int W = static_cast<int>(plot.getWidth());
    const double binHz = eng.sampleRate.load() / kFft;
    double domF = 0; float domV = -200.f;
    for (int px = 0; px <= W; ++px) {
        float v = -120.f;
        if (live) {
            const double b0 = xf(plot.getX() + px - 0.5f) / binHz, b1 = xf(plot.getX() + px + 0.5f) / binHz;
            if (b1 - b0 < 1.0) {
                const double bc = 0.5 * (b0 + b1);
                const int i0 = jlimit(0, kFft / 2 - 2, static_cast<int>(bc));
                const float fr = static_cast<float>(bc - i0);
                v = spec[static_cast<size_t>(i0)] + (spec[static_cast<size_t>(i0) + 1] - spec[static_cast<size_t>(i0)]) * fr;
            } else {
                const int i0 = jmax(0, static_cast<int>(b0)), i1 = jmin(kFft / 2 - 1, static_cast<int>(std::ceil(b1)));
                for (int k = i0; k <= i1; ++k) v = jmax(v, spec[static_cast<size_t>(k)]);
            }
            v = jmax(v, -120.f);
        }
        float& d = disp[static_cast<size_t>(px)]; d += (v - d) * (v > d ? 0.55f : 0.14f);
        float& h = hold[static_cast<size_t>(px)]; h = v > h ? v : h - 0.2f;
        if (d > domV && xf(plot.getX() + static_cast<float>(px)) > 70.0) { domV = d; domF = xf(plot.getX() + static_cast<float>(px)); }
    }
    dominantHz = (live && domV > -85.f) ? domF : 0.0; dominantDb = domV;
    if ((tick++ & 1) == 0) eng.afs[static_cast<size_t>(ch)].snapshot(slots, tracks, risk);
    computeCurve();
    repaint();
}

void SpectrumEq::computeCurve()
{
    const double fs = eng.sampleRate.load();
    const auto& P = eng.sp[static_cast<size_t>(ch)];
    std::vector<ah::BiquadCoef> cf;
    if (P.get(ah::P_HPF_ON) > 0.5f) cf.push_back(ah::makeBiquad(ah::FilterType::HighPass, fs, P.get(ah::P_HPF_F), 0.0, 0.7071));
    if (eng.gp.get(ah::G_AFS) > 0.5f && P.get(ah::P_AFS) > 0.5f)
        for (const auto& s : slots) if (s.on) cf.push_back(ah::makeBiquad(ah::FilterType::Peaking, fs, s.f, -s.depth, s.q));
    for (int b = 0; b < ah::kBands; ++b)
        cf.push_back(ah::makeBiquad(ah::kEqType[b], fs, P.get(ah::eqF(b)), P.get(ah::eqG(b)), P.get(ah::eqQ(b))));
    const int W = static_cast<int>(plot.getWidth());
    curve.assign(static_cast<size_t>(W / 3 + 2), 0.f);
    for (size_t i = 0; i < curve.size(); ++i) {
        const double f = xf(plot.getX() + static_cast<float>(i) * 3.0f);
        double d = 0; for (const auto& c : cf) d += ah::biquadMagDb(c, fs, f);
        curve[i] = static_cast<float>(d);
    }
}

std::pair<float, float> SpectrumEq::handleX(int b) const
{
    const double fc = getStrip(ah::eqF(b)), bw = ah::qToOctaves(getStrip(ah::eqQ(b)));
    float xl = fx(fc * std::pow(2.0, -bw / 2.0)), xr = fx(fc * std::pow(2.0, bw / 2.0));
    const float cx = fx(fc);
    if (cx - xl < 16.0f) xl = cx - 16.0f;       // keep handles visible/grabbable on very narrow bands
    if (xr - cx < 16.0f) xr = cx + 16.0f;
    return { xl, xr };
}

SpectrumEq::Hit SpectrumEq::hitTest(Point<float> p) const
{
    Hit h;
    const int hb = (selBand >= 0 && selBand < ah::kBands) ? selBand : -1;
    if (hb >= 0 && ah::kEqType[hb] == ah::FilterType::Peaking) {
        const auto hx = handleX(hb); const float hy = yEq(getStrip(ah::eqG(hb)) * 0.5f);
        if (p.getDistanceFrom({ hx.first, hy }) < 10.0f) { h.kind = Hit::Handle; h.idx = hb; h.side = -1; return h; }
        if (p.getDistanceFrom({ hx.second, hy }) < 10.0f) { h.kind = Hit::Handle; h.idx = hb; h.side = 1; return h; }
    }
    float best = 13.0f;
    {
        const float d = p.getDistanceFrom({ fx(getStrip(ah::P_HPF_F)), plot.getBottom() - 14.0f });
        if (d < best) { best = d; h.kind = Hit::Node; h.idx = kHpf; }
    }
    for (int b = 0; b < ah::kBands; ++b) {
        const float d = p.getDistanceFrom({ fx(getStrip(ah::eqF(b))), yEq(getStrip(ah::eqG(b))) });
        if (d < best) { best = d; h.kind = Hit::Node; h.idx = b; }
    }
    if (h.kind != Hit::None) return h;
    for (int j = 0; j < ah::kNotches; ++j) {
        const auto& s = slots[static_cast<size_t>(j)];
        if (!s.on) continue;
        const float x = fx(s.f);
        if (p.getDistanceFrom({ x, yEq(-s.depth) }) < 9.0f) { h.kind = Hit::NotchDepth; h.idx = j; return h; }
        if (std::fabs(p.x - x) < 6.0f && plot.contains(p)) { h.kind = Hit::NotchLine; h.idx = j; return h; }
    }
    return h;
}

// ------------------------------------------------------------------------------------------------
void SpectrumEq::paint(Graphics& g)
{
    if (plot.isEmpty()) return;
    const float X0 = plot.getX(), Y0 = plot.getY(), X1 = plot.getRight(), Y1 = plot.getBottom();
    const int W = static_cast<int>(plot.getWidth());
    const double fs = eng.sampleRate.load();
    const auto& P = eng.sp[static_cast<size_t>(ch)];
    const bool afsOn = eng.gp.get(ah::G_AFS) > 0.5f && P.get(ah::P_AFS) > 0.5f;
    const double now = ah::nowSec();

    g.setColour(Colour(0xff09101a)); g.fillRoundedRectangle(plot, 8.0f);
    g.saveState();
    g.reduceClipRegion(plot.toNearestInt());

    // ---- grid
    static const double fl[] = { 20, 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000,
                                 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 20000 };
    for (double f : fl) {
        const bool major = f == 100 || f == 1000 || f == 10000;
        g.setColour(major ? Colour(0xff243449) : Colour(0xff16212f));
        g.drawVerticalLine(static_cast<int>(fx(f)), Y0, Y1);
    }
    for (int d : { -20, -40, -60, -80 }) { g.setColour(Colour(0xff16212f)); g.drawHorizontalLine(static_cast<int>(ySp(static_cast<float>(d))), X0, X1); }
    {
        const float dash[] = { 4.0f, 4.0f };
        g.setColour(Colour(0xff2c4263));
        g.drawDashedLine(Line<float>(X0, yEq(0), X1, yEq(0)), dash, 2, 1.0f);
    }

    // ---- spectrum
    if (!disp.empty()) {
        Path fill, line, holdP;
        fill.startNewSubPath(X0, Y1);
        for (int px = 0; px <= W; ++px) {
            const float x = X0 + static_cast<float>(px), y = ySp(disp[static_cast<size_t>(px)]);
            fill.lineTo(x, y);
            if (px == 0) { line.startNewSubPath(x, y); holdP.startNewSubPath(x, ySp(hold[0])); }
            else { line.lineTo(x, y); holdP.lineTo(x, ySp(hold[static_cast<size_t>(px)])); }
        }
        fill.lineTo(X0 + static_cast<float>(W), Y1); fill.closeSubPath();
        g.setGradientFill(ColourGradient(ui::kSig.withAlpha(0.55f), 0.0f, Y0, ui::kSig.withAlpha(0.0f), 0.0f, Y1, false));
        g.fillPath(fill);
        g.setColour(ui::kSig); g.strokePath(line, PathStrokeType(1.4f));
        g.setColour(Colours::white.withAlpha(0.2f)); g.strokePath(holdP, PathStrokeType(1.0f));
    }

    // ---- bell footprints (shows each band's width)
    for (int b = 0; b < ah::kBands; ++b) {
        const float gn = getStrip(ah::eqG(b));
        if (std::fabs(gn) < 0.1f) continue;
        const auto c = ah::makeBiquad(ah::kEqType[b], fs, getStrip(ah::eqF(b)), gn, getStrip(ah::eqQ(b)));
        Path p; p.startNewSubPath(X0, yEq(0));
        for (float x = X0; x <= X1; x += 3.0f)
            p.lineTo(x, jlimit(Y0, Y1, yEq(static_cast<float>(ah::biquadMagDb(c, fs, xf(x))))));
        p.lineTo(X1, yEq(0)); p.closeSubPath();
        g.setColour(ui::kBand[b].withAlpha(b == selBand ? 0.22f : 0.09f)); g.fillPath(p);
    }

    // ---- howl / notch lines
    auto tag = [&](float x, float y, const String& text, Colour bg, Colour fg) {
        const Font f = ui::font(11.5f, true);
        const float w = static_cast<float>(GlyphArrangement::getStringWidthInt(f, text)) + 14.0f;
        const float lx = jlimit(X0 + 2.0f, X1 - w - 2.0f, x - w * 0.5f);
        g.setColour(bg); g.fillRoundedRectangle(lx, y, w, 17.0f, 5.0f);
        g.setColour(fg); g.setFont(f); g.drawText(text, Rectangle<float>(lx, y, w, 17.0f), Justification::centred, false);
    };
    int row = 0;
    const float dash2[] = { 5.0f, 4.0f };
    for (int j = 0; j < ah::kNotches; ++j) {
        const auto& s = slots[static_cast<size_t>(j)];
        if (!s.on) continue;
        const float x = fx(s.f);
        const double age = now - s.lastEvent;
        if (age < 6.0 && !s.manual) {                                    // freshly detected howl: bright line + glow + label
            const float a = static_cast<float>(1.0 - age / 6.0);
            const float hw = jmax(5.0f, fx(s.f * std::pow(2.0, ah::qToOctaves(s.q) * 0.5)) - x);
            g.setColour(ui::kCut.withAlpha(0.10f + 0.20f * a)); g.fillRect(x - hw, Y0, hw * 2.0f, Y1 - Y0);
            g.setColour(ui::kCut.withAlpha(0.55f + 0.45f * a)); g.drawLine(x, Y0, x, Y1, 2.4f);
            tag(x, Y0 + 4.0f + static_cast<float>(row++ % 3) * 19.0f, TH("หอน ") + ui::fmtHz(s.f) + "  -" + String(roundToInt(s.depth)) + " dB",
                ui::kCut, Colour(0xff1a0509));
        } else {                                                          // notch that is holding
            const Colour c = s.manual ? ui::kEq : ui::kCut;
            g.setColour(c.withAlpha(afsOn ? 0.75f : 0.3f));
            g.drawDashedLine(Line<float>(x, Y0, x, Y1), dash2, 2, 1.3f);
        }
        const float dy = yEq(-s.depth);                                   // depth handle (drag vertically)
        Path dia; dia.addQuadrilateral(x, dy - 6.0f, x + 6.0f, dy, x, dy + 6.0f, x - 6.0f, dy);
        g.setColour((s.manual ? ui::kEq : ui::kCut).withAlpha(afsOn ? 1.0f : 0.4f)); g.fillPath(dia);
        if (j == selNotch) { g.setColour(Colours::white); g.strokePath(dia, PathStrokeType(1.4f)); }
    }
    // candidates the AI is still evaluating (not yet cut)
    for (const auto& t : tracks) {
        bool covered = false;
        for (const auto& s : slots) if (s.on && std::fabs(std::log2(t.f / s.f)) < 0.045) covered = true;
        if (covered) continue;
        const float x = fx(t.f);
        g.setColour(ui::kDyn.withAlpha(0.35f + 0.5f * t.p));
        g.drawDashedLine(Line<float>(x, Y0, x, Y1), dash2, 2, 1.2f);
        tag(x, Y0 + 4.0f + static_cast<float>(row++ % 3) * 19.0f,
            TH("หอน? ") + ui::fmtHz(t.f) + "  " + String(roundToInt(t.p * 100.0f)) + "%",
            ui::kDyn.withAlpha(0.9f), Colour(0xff1d1000));
    }

    // ---- combined EQ curve (HPF + notches + EQ bands)
    if (!curve.empty()) {
        Path p;
        for (size_t i = 0; i < curve.size(); ++i) {
            const float x = X0 + static_cast<float>(i) * 3.0f, y = jlimit(Y0, Y1, yEq(curve[i]));
            if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
        g.setColour(Colour(0xffa9bdff)); g.strokePath(p, PathStrokeType(2.4f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // ---- bandwidth handles for the selected peaking band
    if (selBand >= 0 && selBand < ah::kBands && ah::kEqType[selBand] == ah::FilterType::Peaking) {
        const auto hx = handleX(selBand); const float hy = yEq(getStrip(ah::eqG(selBand)) * 0.5f);
        g.setColour(ui::kBand[selBand].withAlpha(0.75f)); g.drawLine(hx.first, hy, hx.second, hy, 1.5f);
        for (float x : { hx.first, hx.second }) {
            g.setColour(Colour(0xff0b1017)); g.fillRect(x - 6.0f, hy - 6.0f, 12.0f, 12.0f);
            g.setColour(ui::kBand[selBand]); g.fillRect(x - 4.5f, hy - 4.5f, 9.0f, 9.0f);
        }
    }

    // ---- nodes
    auto node = [&](float x, float y, Colour c, const String& label, bool sel, bool hot, bool dimmed) {
        const float r = hot ? 9.0f : 7.5f;
        g.setColour(Colour(0xff0b1017)); g.fillEllipse(x - r - 2.0f, y - r - 2.0f, 2.0f * r + 4.0f, 2.0f * r + 4.0f);
        g.setColour(c.withAlpha(dimmed ? 0.45f : 1.0f)); g.fillEllipse(x - r, y - r, 2.0f * r, 2.0f * r);
        if (sel) { g.setColour(Colours::white); g.drawEllipse(x - r - 2.0f, y - r - 2.0f, 2.0f * r + 4.0f, 2.0f * r + 4.0f, 1.6f); }
        g.setColour(Colour(0xff0b1017)); g.setFont(ui::mono(10.0f)); g.drawText(label, Rectangle<float>(x - 8.0f, y - 7.0f, 16.0f, 14.0f), Justification::centred, false);
    };
    node(fx(P.get(ah::P_HPF_F)), Y1 - 14.0f, Colour(0xff9fb0c6), "H", selBand == kHpf, hovBand == kHpf, P.get(ah::P_HPF_ON) < 0.5f);
    for (int b = 0; b < ah::kBands; ++b)
        node(fx(P.get(ah::eqF(b))), yEq(P.get(ah::eqG(b))), ui::kBand[b], String(b + 1), selBand == b, hovBand == b, false);

    if (mouse.x >= X0 && mouse.x <= X1 && mouse.y >= Y0 && mouse.y <= Y1 && drag.kind == Hit::None) {
        g.setColour(Colours::white.withAlpha(0.18f)); g.drawVerticalLine(static_cast<int>(mouse.x), Y0, Y1);
    }
    g.restoreState();

    // ---- axes
    g.setFont(ui::mono(10.5f));
    g.setColour(Colour(0xff6a7d94));
    static const std::pair<double, const char*> fl2[] = { { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" }, { 1000, "1k" },
                                                         { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" }, { 20000, "20k" } };
    for (const auto& q : fl2) g.drawText(q.second, Rectangle<float>(fx(q.first) - 18.0f, Y1 + 4.0f, 36.0f, 14.0f), Justification::centred, false);
    for (int d : { -20, -40, -60, -80 }) g.drawText(String(d), Rectangle<float>(0.0f, ySp(static_cast<float>(d)) - 7.0f, X0 - 6.0f, 14.0f), Justification::centredRight, false);
    g.setColour(Colour(0xff5b86c9));
    for (int d : { 12, 0, -12, -24 }) g.drawText((d > 0 ? "+" : "") + String(d), Rectangle<float>(X1 + 6.0f, yEq(static_cast<float>(d)) - 7.0f, 40.0f, 14.0f), Justification::centredLeft, false);
    g.setColour(Colour(0xff6a7d94)); g.drawText("dBFS", Rectangle<float>(0.0f, Y0 - 9.0f, X0 - 6.0f, 12.0f), Justification::centredRight, false);
    g.setColour(Colour(0xff5b86c9)); g.drawText("EQ dB", Rectangle<float>(X1 + 6.0f, Y0 - 9.0f, 42.0f, 12.0f), Justification::centredLeft, false);

    if (!live) {
        g.setColour(ui::kMut); g.setFont(ui::font(14.0f));
        g.drawText(TH("กดปุ่ม “ตั้งค่าอินเตอร์เฟซ” เพื่อเลือกอุปกรณ์ แล้วสเปกตรัมจะเริ่มทำงาน"),
                   plot.withHeight(plot.getHeight() * 0.5f), Justification::centred, false);
    }

    // hover read-out
    if (mouse.x >= X0 && mouse.x <= X1 && mouse.y >= Y0 && mouse.y <= Y1 && !disp.empty()) {
        const int px = jlimit(0, W, static_cast<int>(mouse.x - X0));
        String t = ui::fmtHz(xf(mouse.x));
        if (live && disp[static_cast<size_t>(px)] > -110.f) t << "   " << String(roundToInt(disp[static_cast<size_t>(px)])) << " dB";
        g.setFont(ui::mono(11.5f)); g.setColour(Colour(0xcc0b121b));
        const float w = static_cast<float>(GlyphArrangement::getStringWidthInt(g.getCurrentFont(), t)) + 14.0f;
        g.fillRoundedRectangle(X1 - w - 6.0f, Y1 - 24.0f, w, 18.0f, 5.0f);
        g.setColour(ui::kTxt); g.drawText(t, Rectangle<float>(X1 - w - 6.0f, Y1 - 24.0f, w, 18.0f), Justification::centred, false);
    }

    // big alert while a howl is being fought
    if (afsOn && risk > 0.6f) {
        const float pulse = 0.65f + 0.35f * std::sin(static_cast<float>(Time::getMillisecondCounterHiRes()) * 0.012f);
        const String msg = TH("● ตรวจพบเสียงหอน");   // ● ตรวจพบเสียงหอน
        const Font f = ui::font(13.0f, true);
        const float w = static_cast<float>(GlyphArrangement::getStringWidthInt(f, msg)) + 22.0f;
        g.setColour(ui::kCut.withAlpha(0.20f * pulse + 0.1f)); g.fillRoundedRectangle(X0 + 8.0f, Y1 - 30.0f, w, 24.0f, 8.0f);
        g.setColour(ui::kCut.withAlpha(pulse)); g.drawRoundedRectangle(X0 + 8.0f, Y1 - 30.0f, w, 24.0f, 8.0f, 1.4f);
        g.setFont(f); g.setColour(ui::kTxt);
        g.drawText(msg, Rectangle<float>(X0 + 8.0f, Y1 - 30.0f, w, 24.0f), Justification::centred, false);
    }
}

// ------------------------------------------------------------------------------------------------
void SpectrumEq::mouseMove(const MouseEvent& e)
{
    mouse = e.position;
    const auto h = hitTest(e.position);
    hovBand = (h.kind == Hit::Node || h.kind == Hit::Handle) ? h.idx : -1;
    setMouseCursor(h.kind == Hit::None ? MouseCursor::CrosshairCursor
                   : (h.kind == Hit::Handle ? MouseCursor::LeftRightResizeCursor : MouseCursor::DraggingHandCursor));
}
void SpectrumEq::mouseExit(const MouseEvent&) { mouse = { -1.f, -1.f }; hovBand = -1; }

void SpectrumEq::mouseDown(const MouseEvent& e)
{
    drag = hitTest(e.position);
    if (drag.kind == Hit::Node) {
        selBand = drag.idx;
        if (drag.idx != kHpf) { dragStartQ = getStrip(ah::eqQ(drag.idx)); dragStartY = e.position.y; }
        if (onBandSelected) onBandSelected(drag.idx);
    } else if (drag.kind == Hit::Handle) {
        selBand = drag.idx;
    } else if (drag.kind == Hit::NotchLine || drag.kind == Hit::NotchDepth) {
        selNotch = drag.idx;
        if (onNotchSelected) onNotchSelected(drag.idx);
    }
    repaint();
}

void SpectrumEq::mouseDrag(const MouseEvent& e)
{
    if (drag.kind == Hit::None) return;
    const auto p = e.position;
    const float fHz = static_cast<float>(jlimit(20.0, 20000.0, xf(jlimit(plot.getX(), plot.getRight(), p.x))));
    if (drag.kind == Hit::Node) {
        if (drag.idx == kHpf) { setStrip(ah::P_HPF_F, fHz); setStrip(ah::P_HPF_ON, 1.0f); }
        else if (e.mods.isShiftDown()) setStrip(ah::eqQ(drag.idx), dragStartQ * std::pow(1.015f, dragStartY - p.y));
        else { setStrip(ah::eqF(drag.idx), fHz); setStrip(ah::eqG(drag.idx), std::round(gEq(p.y) * 10.0f) / 10.0f); }
        if (onEqEdited) onEqEdited();
    } else if (drag.kind == Hit::Handle) {
        const double fc = getStrip(ah::eqF(drag.idx));
        const double bw = jmax(0.02, 2.0 * std::fabs(std::log2(xf(jlimit(plot.getX(), plot.getRight(), p.x)) / fc)));
        setStrip(ah::eqQ(drag.idx), static_cast<float>(ah::octavesToQ(bw)));
        if (onEqEdited) onEqEdited();
    } else if (drag.kind == Hit::NotchLine || drag.kind == Hit::NotchDepth) {
        auto& s = slots[static_cast<size_t>(drag.idx)];
        if (!s.on) return;
        if (drag.kind == Hit::NotchLine) s.f = jlimit(90.0, 15000.0, static_cast<double>(fHz));
        else s.depth = jlimit(2.0f, 24.0f, -gEq(p.y));
        eng.afs[static_cast<size_t>(ch)].edit(drag.idx, s.f, s.depth, s.q);
        s.manual = true;
        if (onNotchesChanged) onNotchesChanged();
    }
    repaint();
}

void SpectrumEq::mouseUp(const MouseEvent&) { drag = Hit(); }

void SpectrumEq::mouseDoubleClick(const MouseEvent& e)
{
    const auto h = hitTest(e.position);
    if (h.kind == Hit::Node) {
        if (h.idx == kHpf) setStrip(ah::P_HPF_ON, getStrip(ah::P_HPF_ON) > 0.5f ? 0.0f : 1.0f);
        else { setStrip(ah::eqG(h.idx), 0.0f); setStrip(ah::eqQ(h.idx), ah::stripInfo()[static_cast<size_t>(ah::eqQ(h.idx))].def); }
        if (onEqEdited) onEqEdited();
    } else if (h.kind == Hit::NotchLine || h.kind == Hit::NotchDepth) {
        eng.afs[static_cast<size_t>(ch)].remove(h.idx);
        slots[static_cast<size_t>(h.idx)].on = false;
        if (onNotchesChanged) onNotchesChanged();
    } else if (plot.contains(e.position)) {
        eng.afs[static_cast<size_t>(ch)].addManual(xf(e.position.x), 12.0f, eng.gp.get(ah::G_Q));
        if (onNotchesChanged) onNotchesChanged();
    }
}

void SpectrumEq::mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& w)
{
    const auto h = hitTest(e.position);
    if ((h.kind == Hit::Node || h.kind == Hit::Handle) && h.idx >= 0 && h.idx < ah::kBands) {
        setStrip(ah::eqQ(h.idx), getStrip(ah::eqQ(h.idx)) * std::exp(w.deltaY * 1.6f));
        if (onEqEdited) onEqEdited();
        repaint();
    } else Component::mouseWheelMove(e, w);
}
