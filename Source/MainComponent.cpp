#include "MainComponent.h"

using namespace juce;

// ================================================================================================
// Content
// ================================================================================================
Content::Content(AudioEngine& e)
    : eng(e), graph(e), eq(e, graph), notch(e, graph), dyn(e), afs(e, graph), fx(e),
      test(e, [this] { return sel; }),
      mixer(e, [this](int c) { selectChannel(c); }, [this] { channelCountChanged(); })
{
    for (auto* c : std::initializer_list<Component*>{ &graph, &eq, &notch, &dyn, &afs, &fx, &test, &mixer }) addAndMakeVisible(c);
    for (int i = 0; i < ah::kMaxStrips; ++i) {
        auto& t = tabs[static_cast<size_t>(i)];
        t.setButtonText("CH" + String(i + 1)); t.setClickingTogglesState(true); t.setRadioGroupId(3001);
        t.setColour(TextButton::textColourOnId, ui::kEq); t.setColour(TextButton::buttonOnColourId, ui::kEq.withAlpha(0.16f));
        t.onClick = [this, i] { selectChannel(i); };
        addAndMakeVisible(t);
    }
    ui::styleLabel(domL, "", 13.0f, ui::kMut, Justification::centredRight);
    addAndMakeVisible(domL);

    graph.onEqEdited = [this] { eq.sync(); };
    graph.onBandSelected = [this](int) { eq.sync(); };
    graph.onNotchSelected = [this](int j) { notch.select(j); };
    graph.onNotchesChanged = [this] { notch.refresh(); };
    selectChannel(0);
}

void Content::syncFromEngine() { eq.sync(); dyn.sync(); afs.sync(); fx.sync(); }

void Content::selectChannel(int c)
{
    const int n = jlimit(1, ah::kMaxStrips, static_cast<int>(eng.gp.get(ah::G_NCH)));
    sel = jlimit(0, n - 1, c);
    graph.setChannel(sel); eq.setChannel(sel); notch.setChannel(sel); dyn.setChannel(sel); mixer.setSelected(sel);
    for (int i = 0; i < ah::kMaxStrips; ++i) {
        tabs[static_cast<size_t>(i)].setVisible(i < n);
        tabs[static_cast<size_t>(i)].setToggleState(i == sel, dontSendNotification);
    }
    resized();
}

void Content::channelCountChanged()
{
    mixer.rebuild();
    mixer.setInputNames(inNames); mixer.setOutputNames(outNames);
    selectChannel(sel);
    if (onLayoutChanged) onLayoutChanged();
}

void Content::refreshDeviceNames(const StringArray& in, const StringArray& out)
{
    inNames = in; outNames = out;
    mixer.setInputNames(in); mixer.setOutputNames(out);
}

int Content::preferredHeight() const { return 14 * 6 + 470 + 168 + 160 + 190 + mixer.neededHeight(); }

void Content::tick(int n)
{
    mixer.updateMeters();
    dyn.updateMeters();
    afs.update();
    if (n % 4 == 0) notch.refresh();
    if (n % 3 == 0) {
        if (graph.dominantHz > 0)
            domL.setText(TH("ย่านที่เด่นสุด ") + ui::fmtHz(graph.dominantHz) + "  " + String(roundToInt(graph.dominantDb)) + " dB", dontSendNotification);
        else domL.setText("", dontSendNotification);
    }
}

void Content::paint(Graphics& g) { ui::drawPanel(g, specBounds, TH("สเปกตรัมเรียลไทม์ · EQ · เส้นแสดงตำแหน่งเสียงหอนของช่อง ") + String(sel + 1)); }

void Content::resized()
{
    const int pad = 14, sideW = 372;
    auto r = getLocalBounds().reduced(pad, pad);
    auto bottom = r.removeFromBottom(mixer.neededHeight());
    r.removeFromBottom(pad);
    auto left = r.removeFromLeft(r.getWidth() - sideW - pad);
    r.removeFromLeft(pad);
    auto right = r;

    specBounds = left.removeFromTop(470); left.removeFromTop(pad);
    auto head = specBounds.withTrimmedLeft(420).withHeight(38).reduced(8, 5);
    domL.setBounds(head.removeFromRight(260));
    const int n = jlimit(1, ah::kMaxStrips, static_cast<int>(eng.gp.get(ah::G_NCH)));
    const int tw = jmin(52, head.getWidth() / jmax(1, n));
    for (int i = 0; i < ah::kMaxStrips; ++i) tabs[static_cast<size_t>(i)].setBounds(head.getX() + i * tw, head.getY(), tw - 4, head.getHeight());
    graph.setBounds(specBounds.withTrimmedTop(42).reduced(8, 4));

    eq.setBounds(left.removeFromTop(168)); left.removeFromTop(pad);
    notch.setBounds(left.removeFromTop(160)); left.removeFromTop(pad);
    dyn.setBounds(left.removeFromTop(190));

    afs.setBounds(right.removeFromTop(560)); right.removeFromTop(pad);
    fx.setBounds(right.removeFromTop(190)); right.removeFromTop(pad);
    test.setBounds(right.removeFromTop(150));
    mixer.setBounds(bottom);
}

// ================================================================================================
// MainComponent
// ================================================================================================
static File appDir()
{
    auto d = File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("AntiHowlAI");
    d.createDirectory();
    return d;
}

static int loadSettings(AudioEngine& e, bool& firstRun, std::unique_ptr<XmlElement>& audio)
{
    const auto sf = appDir().getChildFile("settings.json"), af = appDir().getChildFile("audio.xml");
    firstRun = !sf.existsAsFile();
    if (!firstRun) e.fromVar(JSON::parse(sf.loadFileAsString()));
    if (af.existsAsFile()) audio = XmlDocument::parse(af);
    return 1;
}

MainComponent::MainComponent()
    : settingsLoaded(loadSettings(engine, firstRun, savedAudio)), content(engine)
{
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);
    content.onLayoutChanged = [this] { resized(); };

    devBtn.setButtonText(TH("ตั้งค่าอินเตอร์เฟซ / ASIO"));
    devBtn.setColour(TextButton::buttonColourId, ui::kSig.withAlpha(0.18f));
    devBtn.setColour(TextButton::textColourOffId, ui::kSig);
    devBtn.onClick = [this] { openDeviceDialog(); };
    addAndMakeVisible(devBtn);

    ui::styleToggle(afsBtn, TH("ระบบกันหอน"), ui::kSig);
    afsBtn.setToggleState(engine.gp.get(ah::G_AFS) > 0.5f, dontSendNotification);
    afsBtn.onClick = [this] { engine.gp.set(ah::G_AFS, afsBtn.getToggleState() ? 1.f : 0.f); afsBtn.setButtonText(afsBtn.getToggleState() ? TH("ระบบกันหอน เปิด") : TH("ระบบกันหอน ปิด")); };
    afsBtn.setButtonText(afsBtn.getToggleState() ? TH("ระบบกันหอน เปิด") : TH("ระบบกันหอน ปิด"));
    addAndMakeVisible(afsBtn);

    ui::styleLabel(statusL, "", 13.0f, ui::kMut, Justification::centredLeft); addAndMakeVisible(statusL);
    ui::styleLabel(cpuL, "", 13.0f, ui::kMut, Justification::centredRight); addAndMakeVisible(cpuL);

    // Open the audio device: restore the previous setup, otherwise prefer ASIO on Windows.
    dm.initialise(8, 2, savedAudio.get(), true);
    if (savedAudio == nullptr)
        for (auto* t : dm.getAvailableDeviceTypes())
            if (t->getTypeName() == "ASIO") { dm.setCurrentAudioDeviceType("ASIO", true); break; }
    dm.addAudioCallback(&engine);

    setSize(1480, 960);
    startTimerHz(30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    saveAll();
    dm.removeAudioCallback(&engine);
}

void MainComponent::saveAll()
{
    appDir().getChildFile("settings.json").replaceWithText(JSON::toString(engine.toVar()));
    if (auto xml = dm.createStateXml()) xml->writeTo(appDir().getChildFile("audio.xml"));
}

void MainComponent::openDeviceDialog()
{
    auto* sel = new AudioDeviceSelectorComponent(dm, 0, 32, 0, 32, false, false, false, false);
    sel->setSize(580, 560);
    DialogWindow::LaunchOptions o;
    o.content.setOwned(sel);
    o.dialogTitle = TH("ตั้งค่าอินเตอร์เฟซเสียง (เลือก ASIO เพื่อดีเลย์ต่ำสุด)");
    o.dialogBackgroundColour = ui::kPanel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void MainComponent::refreshNames()
{
    StringArray in, out;
    if (auto* d = dm.getCurrentAudioDevice()) {
        const auto inN = d->getInputChannelNames(), outN = d->getOutputChannelNames();
        const auto ai = d->getActiveInputChannels(), ao = d->getActiveOutputChannels();
        for (int i = ai.findNextSetBit(0); i >= 0; i = ai.findNextSetBit(i + 1)) in.add(inN[i]);
        for (int i = ao.findNextSetBit(0); i >= 0; i = ao.findNextSetBit(i + 1)) out.add(outN[i]);
    }
    content.refreshDeviceNames(in, out);
    // first launch: one strip per input channel the interface provides
    if (firstRun && autoChannels && in.size() > 0) {
        engine.gp.set(ah::G_NCH, static_cast<float>(jlimit(1, ah::kMaxStrips, in.size())));
        autoChannels = false;
        content.channelCountChanged();
    }
}

void MainComponent::updateStatus()
{
    auto* d = dm.getCurrentAudioDevice();
    if (d == nullptr) {
        statusL.setText(TH("ยังไม่ได้เลือกอุปกรณ์เสียง · กด “ตั้งค่าอินเตอร์เฟซ / ASIO”"), dontSendNotification);
        statusL.setColour(Label::textColourId, ui::kDyn); cpuL.setText("", dontSendNotification);
        return;
    }
    const double sr = d->getCurrentSampleRate();
    const int buf = d->getCurrentBufferSizeSamples();
    const double rt = (d->getInputLatencyInSamples() + d->getOutputLatencyInSamples()) / sr * 1000.0;
    const bool asio = d->getTypeName() == "ASIO";
    const String sep = TH("  ·  ");
    String s = d->getTypeName() + sep + d->getName() + sep + String(sr / 1000.0, 1) + " kHz" + sep + TH("บัฟเฟอร์ ") + String(buf)
               + " (" + String(buf / sr * 1000.0, 1) + " ms)" + sep + TH("ดีเลย์ไป-กลับโดยประมาณ ") + String(rt, 1) + " ms";
    if (!asio) s += TH("  (ไม่ได้ใช้ ASIO · ดีเลย์จะสูงกว่า)");
    statusL.setText(s, dontSendNotification);
    statusL.setColour(Label::textColourId, asio ? ui::kSig : ui::kDyn);
    cpuL.setText(TH("CPU เสียง ") + String(engine.cpuAudioPct.load(), 1) + TH("%  ·  CPU ของ AI ") + String(engine.cpuAfsPct.load(), 1) + "%", dontSendNotification);
}

void MainComponent::timerCallback()
{
    ++tickCount;
    content.tick(tickCount);
    if (tickCount % 10 == 0) {
        updateStatus();
        const int ni = engine.numIn.load(), no = engine.numOut.load(), bs = engine.bufferSize.load();
        if (ni != lastNumIn || no != lastNumOut || bs != lastBuf) { lastNumIn = ni; lastNumOut = no; lastBuf = bs; refreshNames(); }
    }
    if (tickCount % 600 == 0) saveAll();
}

void MainComponent::paint(Graphics& g)
{
    g.fillAll(ui::kBg);
    // logo
    auto logo = Rectangle<float>(18.0f, 12.0f, 44.0f, 44.0f);
    g.setColour(Colour(0xff0d1520)); g.fillRoundedRectangle(logo, 13.0f);
    g.setColour(ui::kSig); g.drawRoundedRectangle(logo.reduced(1.0f), 13.0f, 2.0f);
    g.setColour(ui::kSig); g.fillRoundedRectangle(logo.getCentreX() - 5.0f, logo.getY() + 9.0f, 10.0f, 18.0f, 5.0f);
    g.setColour(ui::kEq);
    Path cradle; cradle.addCentredArc(logo.getCentreX(), logo.getY() + 22.0f, 10.0f, 10.0f, 0.0f, MathConstants<float>::halfPi, MathConstants<float>::pi * 1.5f, true);
    g.strokePath(cradle, PathStrokeType(2.2f, PathStrokeType::curved, PathStrokeType::rounded));
    g.drawLine(logo.getCentreX(), logo.getY() + 32.0f, logo.getCentreX(), logo.getY() + 37.0f, 2.2f);
    g.setColour(ui::kCut); g.drawLine(logo.getX() + 8.0f, logo.getBottom() - 8.0f, logo.getRight() - 8.0f, logo.getY() + 8.0f, 2.6f);

    g.setColour(ui::kTxt); g.setFont(ui::font(22.0f, true));
    g.drawText("ANTIHOWL", 74, 10, 150, 28, Justification::centredLeft, false);
    g.setColour(ui::kSig); g.drawText("AI", 74 + 118, 10, 40, 28, Justification::centredLeft, false);
    g.setColour(ui::kMut); g.setFont(ui::font(12.5f));
    g.drawText(TH("ระบบกันไมค์หอนอัจฉริยะ · เวอร์ชันเนทีฟ (ASIO)"), 74, 36, 330, 20, Justification::centredLeft, false);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop(66).reduced(14, 10);
    head.removeFromLeft(400);
    afsBtn.setBounds(head.removeFromRight(190).withSizeKeepingCentre(190, 40)); head.removeFromRight(10);
    devBtn.setBounds(head.removeFromRight(220).withSizeKeepingCentre(220, 40)); head.removeFromRight(14);
    statusL.setBounds(head.removeFromTop(22)); cpuL.setBounds(head);
    viewport.setBounds(r);
    content.setSize(jmax(1180, r.getWidth() - viewport.getScrollBarThickness()), content.preferredHeight());
}
