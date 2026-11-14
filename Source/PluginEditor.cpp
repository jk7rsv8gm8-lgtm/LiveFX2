#include "PluginEditor.h"

using namespace juce;

static const Colour kBg     { 0xff111318 };
static const Colour kPanel  { 0xff1d2129 };
static const Colour kAccent { 0xff2dd4bf };
static const Colour kRed    { 0xffef4444 };
static const Colour kOrange { 0xfff59e0b };
static const Colour kGreen  { 0xff22c55e };

// ------------------------------- LearnKnob ---------------------------------
LearnKnob::LearnKnob (LiveFXProcessor& p, int paramIndex, const String& label)
    : proc (p), idx (paramIndex), att (p.apvts, kParams[paramIndex].id, slider)
{
    slider.setTextBoxStyle (Slider::TextBoxBelow, false, 70, 16);
    slider.setColour (Slider::rotarySliderFillColourId, kAccent);
    slider.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    slider.addMouseListener (this, false);
    addAndMakeVisible (slider);

    name.setText (label, dontSendNotification);
    name.setJustificationType (Justification::centred);
    name.setFont (Font (FontOptions (13.f)));
    addAndMakeVisible (name);

    cc.setJustificationType (Justification::centred);
    cc.setFont (Font (FontOptions (11.f)));
    cc.setColour (Label::textColourId, Colours::grey);
    addAndMakeVisible (cc);
    refresh();
}

void LearnKnob::resized()
{
    auto r = getLocalBounds();
    name.setBounds (r.removeFromTop (16));
    cc.setBounds (r.removeFromBottom (14));
    slider.setBounds (r);
}

void LearnKnob::refresh()
{
    const int c = proc.ccMap[(size_t) idx].load();
    String t = proc.learnIdx.load() == idx ? "move a knob..." : (c >= 0 ? "CC " + String (c) : "no CC");
    if (cc.getText() != t) cc.setText (t, dontSendNotification);
    cc.setColour (Label::textColourId, proc.learnIdx.load() == idx ? kOrange : Colours::grey);
}

void LearnKnob::mouseDown (const MouseEvent& e)
{
    if (! e.mods.isPopupMenu()) return;
    PopupMenu m;
    m.addItem (1, "MIDI learn (then move a knob on the MiniLab)");
    m.addItem (2, "Clear MIDI mapping");
    Component::SafePointer<LearnKnob> safe (this);
    m.showMenuAsync (PopupMenu::Options(), [safe] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1) safe->proc.learnIdx = safe->idx;
        if (r == 2) safe->proc.ccMap[(size_t) safe->idx] = -1;
    });
}

// ------------------------------- Editor ------------------------------------
LiveFXEditor::LiveFXEditor (LiveFXProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    auto knob = [&] (int idx, const char* label)
    {
        auto k = std::make_unique<LearnKnob> (proc, idx, label);
        addAndMakeVisible (*k);
        knobs[(size_t) idx] = k.get();
        owned.push_back (std::move (k));
    };
    auto toggle = [&] (const String& text, const String& id)
    {
        auto b = std::make_unique<TextButton> (text);
        b->setClickingTogglesState (true);
        b->setColour (TextButton::buttonOnColourId, kAccent);
        b->setColour (TextButton::textColourOnId, Colours::black);
        addAndMakeVisible (*b);
        bAtt.push_back (std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, *b));
        auto* r = b.get(); buttons.push_back (std::move (b)); return r;
    };
    auto action = [&] (const String& text)
    {
        auto b = std::make_unique<TextButton> (text);
        addAndMakeVisible (*b);
        auto* r = b.get(); buttons.push_back (std::move (b)); return r;
    };
    auto combo = [&] (const StringArray& items, const String& id)
    {
        auto c = std::make_unique<ComboBox>();
        c->addItemList (items, 1);
        addAndMakeVisible (*c);
        cAtt.push_back (std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, *c));
        auto* r = c.get(); combos.push_back (std::move (c)); return r;
    };

    // looper
    recBtn  = action ("REC/PLAY/DUB (pad 1)");
    stopBtn = action ("STOP/PLAY (pad 2)");
    clearBtn = action ("CLEAR (3)");
    revBtn  = toggle ("REVERSE (4)", "loopRev");
    loadBtn = action ("LOAD FILE...");
    loadBtn->onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Load an audio file into the looper", File(),
                                                 "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        Component::SafePointer<LiveFXEditor> safe (this);
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [safe] (const FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (safe != nullptr && f != File()) safe->loadFile (f);
                              });
    };
    recBtn->onClick   = [this] { proc.sendPad (0, true); };
    stopBtn->onClick  = [this] { proc.sendPad (1, true); };
    clearBtn->onClick = [this] { proc.sendPad (2, true); };
    knob (LoopLevel, "Loop Level");
    knob (LoopStart, "Loop Start");

    // granular
    for (auto [i, l] : { std::pair<int, const char*> { GrMix, "Grain Mix" }, { GrSize, "Size ms" }, { GrPitch, "Pitch st" },
                         { GrDens, "Density" }, { GrPos, "Position" }, { GrSpray, "Spray" } })
        knob (i, l);
    grFreezeBtn = toggle ("FREEZE   (pad 5)", "grFreeze");
    keysBtn = toggle ("KEYS -> GRAIN PITCH", "keysPitch");

    // delay
    knob (DlMix, "Delay Mix"); knob (DlTime, "Time ms"); knob (DlFb, "Feedback"); knob (DlTone, "Tone Hz");
    dlSyncBtn = toggle ("TEMPO SYNC", "dlSync");
    dlDivCb = combo ({ "1/4", "1/8", "1/8.", "1/16" }, "dlDiv");

    // reverb
    knob (RvMix, "Reverb Mix"); knob (RvSize, "Size"); knob (RvDamp, "Damping");
    rvFreezeBtn = toggle ("FREEZE   (pad 8)", "rvFreeze");

    // fx
    knob (FltCut, "Cutoff"); knob (FltRes, "Resonance"); knob (Crush, "Crusher"); knob (Drive, "Drive");
    fltTypeCb = combo ({ "Lowpass", "Highpass", "Bandpass" }, "fltType");
    stutBtn = action ("STUTTER (hold, pad 6)");
    stutBtn->onStateChange = [this]
    {
        const bool d = stutBtn->isDown();
        if (d != stutterDown) { stutterDown = d; proc.sendPad (5, d); }
    };
    stutDivCb = combo ({ "1/4", "1/8", "1/16", "1/32" }, "stutDiv");

    // sequencer
    seqOnBtn = toggle ("SEQUENCER ON   (pad 7)", "seqOn");
    seqRateCb = combo ({ "1/4", "1/8", "1/16", "1/32" }, "seqRate");
    knob (SeqDepth, "Gate Depth"); knob (OutLevel, "OUTPUT");
    for (int s = 0; s < kSteps; ++s)
    {
        stepBtn[s] = toggle (String (s + 1), "s" + String (s + 1));
        auto sl = std::make_unique<Slider> (Slider::LinearVertical, Slider::NoTextBox);
        sl->setColour (Slider::trackColourId, kAccent);
        addAndMakeVisible (*sl);
        sAtt.push_back (std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "l" + String (s + 1), *sl));
        stepSld[s] = sl.get(); sliders.push_back (std::move (sl));
    }

    // key sampler
    for (int s = 0; s < kSlots; ++s)
    {
        slotLoad[(size_t) s] = action ("load sound...");
        slotLoad[(size_t) s]->onClick = [this, s]
        {
            chooser = std::make_unique<FileChooser> ("Choose a sound for slot " + String (s + 1), File(),
                                                     "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            Component::SafePointer<LiveFXEditor> safe (this);
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                                  [safe, s] (const FileChooser& fc)
                                  {
                                      auto f = fc.getResult();
                                      if (safe != nullptr && f != File()) safe->loadSlot (s, f);
                                  });
        };
        slotClear[(size_t) s] = action ("X");
        slotClear[(size_t) s]->onClick = [this, s] { proc.clearSlot (s); };
        slotKey[(size_t) s] = action ("KEY");
        slotKey[(size_t) s]->onClick = [this, s] { proc.keyLearnSlot = (proc.keyLearnSlot.load() == s) ? -1 : s; };
        slotGrab[(size_t) s] = action ("GRAB");
        slotGrab[(size_t) s]->onClick = [this, s] { proc.grabLoopToSlot (s); };
        slotMode[(size_t) s] = combo ({ "One-shot", "Gate", "Loop", "Pitched" }, "smMode" + String (s + 1));

        auto sl = std::make_unique<Slider> (Slider::LinearHorizontal, Slider::NoTextBox);
        sl->setColour (Slider::trackColourId, kAccent);
        addAndMakeVisible (*sl);
        sAtt.push_back (std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "smLvl" + String (s + 1), *sl));
        slotLvl[(size_t) s] = sl.get(); sliders.push_back (std::move (sl));
    }

    setSize (1100, 860);   // must come after all controls exist: it triggers resized()
    startTimerHz (30);
}

LiveFXEditor::~LiveFXEditor() { stopTimer(); }

void LiveFXEditor::loadFile (const File& f)
{
    if (! proc.loadAudioFile (f))
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Could not load file",
                                          "This file could not be read (supported: WAV, AIFF, FLAC, OGG, MP3).");
}

void LiveFXEditor::loadSlot (int slot, const File& f)
{
    if (! proc.loadSlotFile (slot, f))
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Could not load file",
                                          "This file could not be read (supported: WAV, AIFF, FLAC, OGG, MP3).");
}

// Files dropped on the sampler panel fill slots (starting at the column under the mouse); elsewhere they go to the looper.
void LiveFXEditor::filesDropped (const StringArray& files, int x, int y)
{
    if (files.isEmpty()) return;
    if (sampR.contains (x, y))
    {
        const int first = jlimit (0, kSlots - 1, (x - sampR.getX() - 8) * kSlots / jmax (1, sampR.getWidth() - 16));
        for (int k = 0; k < files.size() && first + k < kSlots; ++k)
            loadSlot (first + k, File (files[k]));
    }
    else loadFile (File (files[0]));
}

void LiveFXEditor::timerCallback()
{
    for (auto* k : knobs) if (k != nullptr) k->refresh();
    for (int s = 0; s < kSlots; ++s)
    {
        const auto i = (size_t) s;
        const String name = proc.getSlotName (s);
        slotLoad[i]->setButtonText (name.isEmpty() ? "load sound..." : name);
        const bool learning = proc.keyLearnSlot.load() == s;
        const int key = proc.getSlotKey (s);
        slotKey[i]->setButtonText (learning ? String ("press key...")
                                            : "KEY: " + MidiMessage::getMidiNoteName (key, true, true, 3));
        slotKey[i]->setColour (TextButton::buttonColourId, learning ? kOrange : getLookAndFeel().findColour (TextButton::buttonColourId));
    }
    repaint();
}

static void row (Rectangle<int> r, std::initializer_list<Component*> cs)
{
    const int w = r.getWidth() / (int) cs.size();
    for (auto* c : cs) c->setBounds (r.removeFromLeft (w).reduced (2));
}

void LiveFXEditor::resized()
{
    auto content = [] (Rectangle<int> r) { return r.reduced (8).withTrimmedTop (22); };

    // looper
    {
        auto c = content (looperR);
        waveR = c.removeFromTop (100);
        c.removeFromTop (6);
        auto kn = c.removeFromRight (220);
        knobs[LoopLevel]->setBounds (kn.removeFromLeft (110));
        knobs[LoopStart]->setBounds (kn);
        auto top = c.removeFromTop (c.getHeight() / 2);
        row (top, { recBtn, stopBtn });
        row (c, { clearBtn, revBtn, loadBtn });
    }
    // granular
    {
        auto c = content (granR);
        auto bot = c.removeFromBottom (36);
        row (c, { knobs[GrMix], knobs[GrSize], knobs[GrPitch], knobs[GrDens], knobs[GrPos], knobs[GrSpray] });
        row (bot, { grFreezeBtn, keysBtn });
    }
    // delay
    {
        auto c = content (delayR);
        auto bot = c.removeFromBottom (32);
        row (c, { knobs[DlMix], knobs[DlTime], knobs[DlFb], knobs[DlTone] });
        row (bot, { dlSyncBtn, dlDivCb });
    }
    // reverb
    {
        auto c = content (reverbR);
        auto bot = c.removeFromBottom (32);
        row (c, { knobs[RvMix], knobs[RvSize], knobs[RvDamp] });
        row (bot, { rvFreezeBtn });
    }
    // fx
    {
        auto c = content (fxR);
        auto bot = c.removeFromBottom (32);
        row (c, { knobs[FltCut], knobs[FltRes], knobs[Crush], knobs[Drive] });
        row (bot, { fltTypeCb, stutBtn, stutDivCb });
    }
    // sequencer
    {
        auto c = content (seqR);
        auto top = c.removeFromTop (90);
        auto left = top.removeFromLeft (260);
        seqOnBtn->setBounds (left.removeFromTop (38).reduced (2));
        seqRateCb->setBounds (left.removeFromTop (38).reduced (2));
        knobs[SeqDepth]->setBounds (top.removeFromLeft (110));
        knobs[OutLevel]->setBounds (top.removeFromRight (110));
        const int w = c.getWidth() / kSteps;
        for (int s = 0; s < kSteps; ++s)
        {
            auto col = c.removeFromLeft (w).reduced (2);
            stepBtn[s]->setBounds (col.removeFromTop (28));
            stepSld[s]->setBounds (col.reduced (0, 4));
        }
    }
    // key sampler
    {
        auto c = content (sampR);
        const int w = c.getWidth() / kSlots;
        for (size_t s = 0; s < (size_t) kSlots; ++s)
        {
            auto col = c.removeFromLeft (w).reduced (3, 0);
            auto top = col.removeFromTop (30);
            slotClear[s]->setBounds (top.removeFromRight (28));
            slotLoad[s]->setBounds (top.withTrimmedRight (2));
            col.removeFromTop (4);
            auto r2 = col.removeFromTop (28);
            slotGrab[s]->setBounds (r2.removeFromRight (50));
            slotKey[s]->setBounds (r2.withTrimmedRight (2));
            col.removeFromTop (4);
            slotMode[s]->setBounds (col.removeFromTop (28));
            col.removeFromTop (4);
            slotLvl[s]->setBounds (col.removeFromTop (26));
        }
    }
}

void LiveFXEditor::paint (Graphics& g)
{
    g.fillAll (kBg);
    auto panel = [&] (Rectangle<int> r, const char* t)
    {
        g.setColour (kPanel);
        g.fillRoundedRectangle (r.toFloat(), 10.f);
        g.setColour (kAccent);
        g.setFont (15.f);
        g.drawText (t, r.getX() + 12, r.getY() + 4, r.getWidth() - 24, 20, Justification::left);
    };
    panel (looperR, "LIVE LOOPER");
    panel (granR,   "GRANULAR");
    panel (delayR,  "PING-PONG DELAY");
    panel (reverbR, "REVERB");
    panel (fxR,     "FILTER / CRUSH / DRIVE / STUTTER");
    panel (seqR,    "STEP GATE SEQUENCER  (16 steps - toggle = on, slider = level)");
    panel (sampR,   "KEY SAMPLER  -  LOAD a file or GRAB the loop (UI button or pad bank B, pads 1-8) into a slot, click KEY, press a key");

    // ---- looper window ----
    const int st = proc.loopState.load();
    const double sr = proc.sampleRateUI.load();
    const double pos = proc.loopPosUI.load();
    const double len = proc.loopLenUI.load();

    Colour sc = st == LRec ? kRed : st == LDub ? kOrange : st == LPlay ? kGreen : Colours::grey;
    g.setColour (Colour (0xff0b0d11));
    g.fillRoundedRectangle (waveR.toFloat(), 6.f);
    if (st != LEmpty && st != LRec && waveR.getWidth() > 0)
    {
        g.setColour (sc.withAlpha (0.8f));
        for (int x = 0; x < waveR.getWidth(); ++x)
        {
            const int b = jlimit (0, kPeaks - 1, x * kPeaks / waveR.getWidth());
            const float h = jlimit (0.f, 1.f, proc.peaks[(size_t) b].load()) * (float) waveR.getHeight() * 0.45f;
            g.drawVerticalLine (waveR.getX() + x, (float) waveR.getCentreY() - h, (float) waveR.getCentreY() + h + 1.f);
        }
    }
    double frac = 0.0, startFrac = 0.0;
    String label;
    if (st == LRec)      { frac = pos / (60.0 * sr); label = "RECORDING  " + String (pos / sr, 1) + " s"; }
    else if (st == LEmpty) label = "EMPTY - pad 1 = record, or load / drop an audio file";
    else
    {
        const bool rev = proc.apvts.getRawParameterValue ("loopRev")->load() > 0.5f;
        const double sIdx = len > 0 ? jlimit (0.0, len - jmin (len, 1024.0), (double) proc.apvts.getRawParameterValue ("loopStart")->load() * len) : 0.0;
        frac = len > 0 ? (rev ? len - 1.0 + sIdx - pos : pos) / len : 0.0;
        startFrac = len > 0 ? sIdx / len : 0.0;
        label = String (st == LPlay ? "PLAYING" : st == LDub ? "OVERDUBBING" : "STOPPED")
                + "   " + String (len / sr, 2) + " s loop";
    }
    if (st != LEmpty && st != LRec)
    {
        const int sx = waveR.getX() + (int) (startFrac * waveR.getWidth());
        g.setColour (Colour (0xaa0b0d11));
        g.fillRect (waveR.getX(), waveR.getY(), sx - waveR.getX(), waveR.getHeight());
        g.setColour (kOrange);
        g.drawVerticalLine (sx, (float) waveR.getY(), (float) waveR.getBottom());
        g.fillRect (sx, waveR.getY(), 8, 5);
    }
    if (st != LEmpty)
    {
        const float x = (float) waveR.getX() + (float) frac * (float) waveR.getWidth();
        g.setColour (st == LRec ? kRed : Colours::white);
        g.drawVerticalLine ((int) x, (float) waveR.getY(), (float) waveR.getBottom());
    }
    g.setColour (sc);
    g.setFont (15.f);
    g.drawText (label, waveR.reduced (8, 4), Justification::topLeft);
    g.setColour (st == LRec ? kRed : Colours::darkgrey);
    g.drawRoundedRectangle (waveR.toFloat(), 6.f, st == LRec ? 2.f : 1.f);

    // ---- current sequencer step ----
    if (proc.apvts.getRawParameterValue ("seqOn")->load() > 0.5f)
    {
        const int s = jlimit (0, kSteps - 1, proc.curStep.load());
        g.setColour (kOrange);
        g.drawRoundedRectangle (stepBtn[s]->getBounds().expanded (2).toFloat(), 4.f, 2.f);
    }
}
