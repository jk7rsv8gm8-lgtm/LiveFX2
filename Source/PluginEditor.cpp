#include "PluginEditor.h"

using namespace juce;

static const Colour kBg     { 0xff111318 };
static const Colour kPanel  { 0xff1d2129 };
static const Colour kAccent { 0xff2dd4bf };
static const Colour kRed    { 0xffef4444 };
static const Colour kOrange { 0xfff59e0b };
static const Colour kGreen  { 0xff22c55e };

static bool isBlackKey (int n)
{
    const int k = n % 12;
    return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
}

static void alertUnreadable()
{
    AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Could not load file",
                                      "This file could not be read (supported: WAV, AIFF, FLAC, OGG, MP3).");
}

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

// ------------------------------- ZoneKeyboard ------------------------------
ZoneKeyboard::ZoneKeyboard (LiveFXProcessor& p) : proc (p)
{
    for (int n = kNoteLo; n <= kNoteHi; ++n)
    {
        whiteIdx[n] = numWhite;                    // number of white keys below this note
        if (! isBlackKey (n)) whiteNotes[numWhite++] = n;
    }
}

float ZoneKeyboard::whiteW() const { return (float) getWidth() / (float) jmax (1, numWhite); }

float ZoneKeyboard::keyL (int n) const
{
    n = jlimit (kNoteLo, kNoteHi, n);
    const float w = whiteW();
    return isBlackKey (n) ? (float) whiteIdx[n] * w - w * 0.3f : (float) whiteIdx[n] * w;
}

float ZoneKeyboard::keyR (int n) const
{
    n = jlimit (kNoteLo, kNoteHi, n);
    return keyL (n) + whiteW() * (isBlackKey (n) ? 0.6f : 1.f);
}

// y is in component coordinates; the zone strip above the keys counts as the top of the keys (black keys win there)
int ZoneKeyboard::noteAt (float x, float y) const
{
    const float blackH = ((float) getHeight() - stripH) * 0.62f;
    if (y - stripH < blackH)
        for (int n = kNoteLo; n <= kNoteHi; ++n)
            if (isBlackKey (n) && x >= keyL (n) && x < keyR (n)) return n;
    return whiteNotes[jlimit (0, numWhite - 1, (int) (x / whiteW()))];
}

int ZoneKeyboard::zoneAt (int note) const
{
    for (int s = kSlots - 1; s >= 0; --s)
        if (proc.isSlotLoaded (s) && note >= proc.getSlotLow (s) && note <= proc.getSlotHigh (s)) return s;
    return -1;
}

int ZoneKeyboard::freeSlot() const
{
    for (int s = 0; s < kSlots; ++s)
        if (! proc.isSlotLoaded (s)) return s;
    return -1;
}

void ZoneKeyboard::paint (Graphics& g)
{
    const float w = whiteW(), h = (float) getHeight(), kh = h - stripH, bh = kh * 0.62f;
    g.fillAll (Colour (0xff0b0d11));

    // white keys
    for (int i = 0; i < numWhite; ++i)
    {
        const int n = whiteNotes[i];
        const bool down = proc.noteHeld[n].load() || n == heldNote;
        g.setColour (down ? kAccent : Colour (0xffe8e8ea));
        g.fillRect ((float) i * w + 0.5f, stripH, w - 1.f, kh);
        if (n % 12 == 0)
        {
            g.setColour (Colours::black.withAlpha (0.55f));
            g.setFont (11.f);
            g.drawText (MidiMessage::getMidiNoteName (n, true, true, 3),
                        Rectangle<float> ((float) i * w, h - 16.f, w, 14.f).toNearestInt(), Justification::centred);
        }
    }

    // zones tint the keys they cover
    for (int s = 0; s < kSlots; ++s)
    {
        if (! proc.isSlotLoaded (s)) continue;
        const Colour c = Colour::fromHSV ((float) s / (float) kSlots, 0.6f, 0.9f, 1.f);
        const float x0 = keyL (proc.getSlotLow (s)), x1 = keyR (proc.getSlotHigh (s));
        g.setColour (c.withAlpha (0.30f));
        g.fillRect (x0, stripH, x1 - x0, kh);
    }

    // black keys
    for (int n = kNoteLo; n <= kNoteHi; ++n)
    {
        if (! isBlackKey (n)) continue;
        const bool down = proc.noteHeld[n].load() || n == heldNote;
        g.setColour (down ? kAccent.darker (0.2f) : Colour (0xff23262e));
        g.fillRect (keyL (n), stripH, keyR (n) - keyL (n), bh);
    }

    // root key markers
    for (int s = 0; s < kSlots; ++s)
    {
        if (! proc.isSlotLoaded (s)) continue;
        const Colour c = Colour::fromHSV ((float) s / (float) kSlots, 0.6f, 0.9f, 1.f);
        const int root = proc.getSlotRoot (s);
        const float cx = (keyL (root) + keyR (root)) * 0.5f;
        g.setColour (c);
        g.fillEllipse (cx - 4.f, isBlackKey (root) ? stripH + bh - 12.f : stripH + kh - 34.f, 8.f, 8.f);
    }

    // zone strip
    g.setColour (Colour (0xff161920));
    g.fillRect (0.f, 0.f, (float) getWidth(), stripH);
    for (int s = 0; s < kSlots; ++s)
    {
        if (! proc.isSlotLoaded (s)) continue;
        const Colour c = Colour::fromHSV ((float) s / (float) kSlots, 0.6f, 0.9f, 1.f);
        const float x0 = keyL (proc.getSlotLow (s)), x1 = keyR (proc.getSlotHigh (s));
        const Rectangle<float> r (x0 + 1.f, 3.f, jmax (4.f, x1 - x0 - 2.f), stripH - 6.f);
        g.setColour (c.withAlpha (0.85f));
        g.fillRoundedRectangle (r, 4.f);
        g.setColour (Colours::black.withAlpha (0.8f));
        g.setFont (12.f);
        g.drawText (String (s + 1) + " " + proc.getSlotName (s), r.reduced (5.f, 0.f).toNearestInt(), Justification::centredLeft, true);
        g.setColour (Colours::white.withAlpha (0.7f));
        g.fillRect (r.getX(), r.getY() + 5.f, 2.f, r.getHeight() - 10.f);                 // resize handles
        g.fillRect (r.getRight() - 2.f, r.getY() + 5.f, 2.f, r.getHeight() - 10.f);
    }

    // selection and drop target
    if (selLow >= 0)
    {
        const float x0 = keyL (selLow), x1 = keyR (selHigh);
        g.setColour (kOrange.withAlpha (0.16f));
        g.fillRect (x0, 0.f, x1 - x0, h);
        g.setColour (kOrange);
        g.drawRect (Rectangle<float> (x0, 1.f, x1 - x0, stripH - 2.f), 1.5f);
    }
    if (dragNote >= 0)
    {
        g.setColour (kOrange);
        g.drawRect (Rectangle<float> (keyL (dragNote), stripH, keyR (dragNote) - keyL (dragNote), kh), 2.f);
    }
    bool anyZone = false;
    for (int s = 0; s < kSlots; ++s) anyZone = anyZone || proc.isSlotLoaded (s);
    if (! anyZone && selLow < 0)
    {
        g.setColour (Colours::grey);
        g.setFont (13.f);
        g.drawText ("Drop a sound on a key (or select a key range on this strip first)", Rectangle<int> (8, 0, getWidth() - 16, (int) stripH), Justification::centredLeft);
    }
}

void ZoneKeyboard::mouseDown (const MouseEvent& e)
{
    const float x = e.position.x, y = e.position.y;
    if (y >= stripH)   // the keys: play
    {
        heldNote = noteAt (x, y);
        proc.previewNote (heldNote, true);
        mode = PlayKey;
        repaint();
        return;
    }
    if (e.mods.isPopupMenu()) { zoneMenu (x); return; }

    mode = None;
    for (int s = kSlots - 1; s >= 0; --s)   // grab a zone edge to resize it
    {
        if (! proc.isSlotLoaded (s)) continue;
        const float x0 = keyL (proc.getSlotLow (s)), x1 = keyR (proc.getSlotHigh (s));
        if (std::abs (x - x0) < 6.f) { mode = ResizeLow; dragSlot = s; return; }
        if (std::abs (x - x1) < 6.f) { mode = ResizeHigh; dragSlot = s; return; }
    }
    const int zs = zoneAt (noteAt (x, 0.f));
    if (zs >= 0)   // clicking a zone selects its range
    {
        selLow = proc.getSlotLow (zs);
        selHigh = proc.getSlotHigh (zs);
        repaint();
        return;
    }
    anchor = noteAt (x, 0.f);   // empty strip: start selecting a range
    selLow = selHigh = anchor;
    mode = Select;
    repaint();
}

void ZoneKeyboard::mouseDrag (const MouseEvent& e)
{
    const float x = e.position.x;
    if (mode == PlayKey)
    {
        const int n = noteAt (x, jmax (stripH, e.position.y));
        if (n != heldNote)
        {
            proc.previewNote (heldNote, false);
            heldNote = n;
            proc.previewNote (n, true);
        }
    }
    else if (mode == Select)
    {
        const int n = noteAt (x, 0.f);
        selLow = jmin (anchor, n);
        selHigh = jmax (anchor, n);
    }
    else if ((mode == ResizeLow || mode == ResizeHigh) && dragSlot >= 0)
    {
        int lo = proc.getSlotLow (dragSlot), hi = proc.getSlotHigh (dragSlot);
        const int n = noteAt (x, 0.f);
        if (mode == ResizeLow) lo = jmin (n, hi); else hi = jmax (n, lo);
        const int root = jlimit (lo, hi, proc.getSlotRoot (dragSlot));
        // a range of several keys only makes sense pitched: switch from One-shot to Pitched automatically
        proc.setSlotZone (dragSlot, lo, hi, root, (hi > lo && proc.getSlotMode (dragSlot) == 0) ? 3 : -1);
        selLow = lo;
        selHigh = hi;
    }
    repaint();
}

void ZoneKeyboard::mouseUp (const MouseEvent&)
{
    if (mode == PlayKey && heldNote >= 0)
    {
        proc.previewNote (heldNote, false);
        heldNote = -1;
    }
    mode = None;
    dragSlot = -1;
    repaint();
}

void ZoneKeyboard::zoneMenu (float x)
{
    const int note = noteAt (x, 0.f);
    const int slot = zoneAt (note);
    if (slot < 0) return;
    PopupMenu m;
    m.addSectionHeader ("Zone " + String (slot + 1) + ": " + proc.getSlotName (slot));
    m.addItem (1, "Root key = " + MidiMessage::getMidiNoteName (note, true, true, 3) + "  (sound plays at its original pitch here)");
    m.addItem (2, "Mode: One-shot");
    m.addItem (3, "Mode: Gate");
    m.addItem (4, "Mode: Loop");
    m.addItem (5, "Mode: Pitched (transposed by key)");
    m.addSeparator();
    m.addItem (6, "Remove sound from this zone");
    Component::SafePointer<ZoneKeyboard> safe (this);
    m.showMenuAsync (PopupMenu::Options(), [safe, slot, note] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& p = safe->proc;
        if (r == 1) p.setSlotZone (slot, p.getSlotLow (slot), p.getSlotHigh (slot), note);
        else if (r >= 2 && r <= 5) p.setSlotZone (slot, p.getSlotLow (slot), p.getSlotHigh (slot), p.getSlotRoot (slot), r - 2);
        else if (r == 6) p.clearSlot (slot);
        safe->repaint();
    });
}

void ZoneKeyboard::fileDragMove (const StringArray&, int x, int y)
{
    dragNote = noteAt ((float) x, (float) y);
    repaint();
}

void ZoneKeyboard::fileDragExit (const StringArray&)
{
    dragNote = -1;
    repaint();
}

void ZoneKeyboard::filesDropped (const StringArray& files, int x, int y)
{
    const int note = noteAt ((float) x, (float) y);
    dragNote = -1;
    const bool inSelection = selLow >= 0 && note >= selLow && note <= selHigh;

    for (int i = 0; i < files.size(); ++i)
    {
        const File f (files[i]);
        int slot = -1, low = jlimit (kNoteLo, kNoteHi, note + i), high = low, root = low, newMode = -1;

        if (i == 0 && inSelection)            // sound covers the selected range, its own pitch is at the dropped key
        {
            slot = freeSlot();
            low = selLow; high = selHigh; root = note;
            newMode = (high > low) ? 3 : -1;
        }
        else if (i == 0 && zoneAt (note) >= 0)   // drop on an existing zone: replace the sound, keep the zone
        {
            slot = zoneAt (note);
            low = proc.getSlotLow (slot); high = proc.getSlotHigh (slot); root = proc.getSlotRoot (slot);
        }
        else slot = freeSlot();

        if (slot < 0)
        {
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "No free slot",
                                              "All 8 sampler slots are in use. Remove a sound first (right-click its zone > Remove).");
            break;
        }
        if (! proc.loadSlotFile (slot, f)) { alertUnreadable(); continue; }
        proc.setSlotZone (slot, low, high, root, newMode);
    }
    repaint();
}

// ------------------------------- Editor ------------------------------------
LiveFXEditor::LiveFXEditor (LiveFXProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    addAndMakeVisible (content);
    content.onPaint = [this] (Graphics& g) { paintContent (g); };

    auto knob = [&] (int idx, const char* label)
    {
        auto k = std::make_unique<LearnKnob> (proc, idx, label);
        content.addAndMakeVisible (*k);
        knobs[(size_t) idx] = k.get();
        owned.push_back (std::move (k));
    };
    auto toggle = [&] (const String& text, const String& id)
    {
        auto b = std::make_unique<TextButton> (text);
        b->setClickingTogglesState (true);
        b->setColour (TextButton::buttonOnColourId, kAccent);
        b->setColour (TextButton::textColourOnId, Colours::black);
        content.addAndMakeVisible (*b);
        bAtt.push_back (std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, *b));
        auto* r = b.get(); buttons.push_back (std::move (b)); return r;
    };
    auto action = [&] (const String& text)
    {
        auto b = std::make_unique<TextButton> (text);
        content.addAndMakeVisible (*b);
        auto* r = b.get(); buttons.push_back (std::move (b)); return r;
    };
    auto combo = [&] (const StringArray& items, const String& id)
    {
        auto c = std::make_unique<ComboBox>();
        c->addItemList (items, 1);
        content.addAndMakeVisible (*c);
        cAtt.push_back (std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, *c));
        auto* r = c.get(); combos.push_back (std::move (c)); return r;
    };

    // looper
    recBtn  = action ("REC / PLAY / DUB   (pad 1)");
    stopBtn = action ("STOP / PLAY   (pad 2)");
    clearBtn = action ("CLEAR   (pad 3)");
    revBtn  = toggle ("REVERSE   (pad 4)", "loopRev");
    loadBtn = action ("LOAD AUDIO FILE...");
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
    knob (LoopEnd, "Loop End");

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

    // filter / crush / drive / stutter
    knob (FltCut, "Cutoff"); knob (FltRes, "Resonance"); knob (Crush, "Crusher"); knob (Drive, "Drive");
    fltTypeCb = combo ({ "Lowpass", "Highpass", "Bandpass" }, "fltType");
    stutBtn = action ("STUTTER (hold, pad 6)");
    stutBtn->onStateChange = [this]
    {
        const bool d = stutBtn->isDown();
        if (d != stutterDown) { stutterDown = d; proc.sendPad (5, d); }
    };
    stutDivCb = combo ({ "1/4", "1/8", "1/16", "1/32" }, "stutDiv");

    // PaulStretch
    knob (PsMix, "Stretch Mix"); knob (PsStretch, "Stretch x");
    psSizeCb = combo ({ "Window 2048", "Window 4096", "Window 8192", "Window 16384" }, "psSize");
    psFreezeBtn = toggle ("FREEZE", "psFreeze");

    // sequencer
    seqOnBtn = toggle ("SEQUENCER ON   (pad 7)", "seqOn");
    seqRateCb = combo ({ "1/4", "1/8", "1/16", "1/32" }, "seqRate");
    knob (SeqLen, "Length"); knob (SeqSwing, "Swing"); knob (SeqGate, "Gate Len"); knob (SeqProb, "Probability");
    knob (SeqDepth, "Gate Depth"); knob (OutLevel, "OUTPUT");
    seqDirCb = combo ({ "Forward", "Backward", "Ping-pong", "Random" }, "seqDir");
    seqRatchetCb = combo ({ "Ratchet 1x", "Ratchet 2x", "Ratchet 3x", "Ratchet 4x" }, "seqRatchet");
    for (int a = 0; a < kActions; ++a)   // right-click = MIDI learn (a key or a CC on the MiniLab)
    {
        seqActBtn[a] = action (kActionNames[a]);
        seqActBtn[a]->setTriggeredOnMouseDown (true);
        seqActBtn[a]->addMouseListener (this, false);
        seqActBtn[a]->onClick = [this, a] { if (! ModifierKeys::getCurrentModifiers().isPopupMenu()) proc.runSeqAction (a); };
    }
    editModeBtn = action ("EDIT: LEVEL");
    editModeBtn->setClickingTogglesState (true);
    editModeBtn->setColour (TextButton::buttonOnColourId, kOrange);
    editModeBtn->onClick = [this] { updateEditMode(); };
    seqPitchBtn = toggle ("STEP PITCH > GRAINS", "seqPitch");
    for (int s = 0; s < kSteps; ++s)
    {
        stepBtn[s] = toggle (String (s + 1), "s" + String (s + 1));
        auto sl = std::make_unique<Slider> (Slider::LinearVertical, Slider::NoTextBox);
        sl->setColour (Slider::trackColourId, kAccent);
        content.addAndMakeVisible (*sl);
        sAtt.push_back (std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "l" + String (s + 1), *sl));
        stepSld[s] = sl.get(); sliders.push_back (std::move (sl));

        auto ps = std::make_unique<Slider> (Slider::LinearVertical, Slider::TextBoxBelow);
        ps->setTextBoxStyle (Slider::TextBoxBelow, false, 40, 14);
        ps->setColour (Slider::trackColourId, kOrange);
        content.addChildComponent (*ps);   // shown instead of the level slider in "EDIT: PITCH" mode
        sAtt.push_back (std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "p" + String (s + 1), *ps));
        stepPit[s] = ps.get(); sliders.push_back (std::move (ps));
    }

    // key sampler: one column per slot + the piano with key zones underneath
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
        slotKey[(size_t) s] = action ("KEY");   // click, then press a key on the MiniLab: the zone becomes that single key
        slotKey[(size_t) s]->onClick = [this, s] { proc.keyLearnSlot = (proc.keyLearnSlot.load() == s) ? -1 : s; };
        slotGrab[(size_t) s] = action ("GRAB");
        slotGrab[(size_t) s]->onClick = [this, s] { proc.grabLoopToSlot (s); };
        slotMode[(size_t) s] = combo ({ "One-shot", "Gate", "Loop", "Pitched" }, "smMode" + String (s + 1));

        auto sl = std::make_unique<Slider> (Slider::LinearHorizontal, Slider::NoTextBox);
        sl->setColour (Slider::trackColourId, kAccent);
        content.addAndMakeVisible (*sl);
        sAtt.push_back (std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "smLvl" + String (s + 1), *sl));
        slotLvl[(size_t) s] = sl.get(); sliders.push_back (std::move (sl));
    }
    keyboard = std::make_unique<ZoneKeyboard> (proc);
    content.addAndMakeVisible (*keyboard);

    layoutContent();
    updateEditMode();

    // the window can be resized (corner handle); the content scales to fit. Start at a size that fits this screen.
    setResizable (true, true);
    setResizeLimits (kDesignW / 2, kDesignH / 2, kDesignW * 2, kDesignH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kDesignW / (double) kDesignH);
    float fit = 1.f;
    if (auto* d = Desktop::getInstance().getDisplays().getPrimaryDisplay())
        fit = jmin (1.f, (float) (d->userArea.getHeight() - 90) / (float) kDesignH, (float) (d->userArea.getWidth() - 20) / (float) kDesignW);
    fit = jmax (0.5f, fit);
    setSize ((int) ((float) kDesignW * fit), (int) ((float) kDesignH * fit));
    startTimerHz (30);
}

LiveFXEditor::~LiveFXEditor() { stopTimer(); }

void LiveFXEditor::paint (Graphics& g) { g.fillAll (kBg); }

void LiveFXEditor::resized()
{
    const float s = jmin ((float) getWidth() / (float) kDesignW, (float) getHeight() / (float) kDesignH);
    content.setBounds (0, 0, kDesignW, kDesignH);
    content.setTransform (AffineTransform::scale (s));
}

void LiveFXEditor::loadFile (const File& f)
{
    if (! proc.loadAudioFile (f)) alertUnreadable();
}

void LiveFXEditor::loadSlot (int slot, const File& f)
{
    if (! proc.loadSlotFile (slot, f)) alertUnreadable();
}

// Files dropped on the sampler columns fill slots (keeping their zones); on the piano the ZoneKeyboard handles them; elsewhere -> looper.
void LiveFXEditor::filesDropped (const StringArray& files, int x, int y)
{
    if (files.isEmpty()) return;
    const auto p = content.getLocalPoint (this, Point<int> (x, y));
    if (sampR.contains (p))
    {
        const int first = jlimit (0, kSlots - 1, (p.x - sampR.getX() - 8) * kSlots / jmax (1, sampR.getWidth() - 16));
        for (int k = 0; k < files.size() && first + k < kSlots; ++k)
            loadSlot (first + k, File (files[k]));
    }
    else loadFile (File (files[0]));
}

void LiveFXEditor::updateEditMode()
{
    const bool pitch = editModeBtn->getToggleState();
    for (int s = 0; s < kSteps; ++s) { stepSld[s]->setVisible (! pitch); stepPit[s]->setVisible (pitch); }
    editModeBtn->setButtonText (pitch ? "EDIT: PITCH" : "EDIT: LEVEL");
}

void LiveFXEditor::mouseDown (const MouseEvent& e)
{
    if (! e.mods.isPopupMenu()) return;
    for (int a = 0; a < kActions; ++a)
        if (e.eventComponent == seqActBtn[a])
        {
            PopupMenu m;
            m.addItem (1, "MIDI learn (then press a key or move a control on the MiniLab)");
            m.addItem (2, "Clear MIDI mapping");
            Component::SafePointer<LiveFXEditor> safe (this);
            m.showMenuAsync (PopupMenu::Options(), [safe, a] (int r)
            {
                if (safe == nullptr) return;
                if (r == 1) safe->proc.actionLearn = a;
                if (r == 2) safe->proc.actionMap[(size_t) a] = -1;
            });
            return;
        }
}

void LiveFXEditor::timerCallback()
{
    for (auto* k : knobs) if (k != nullptr) k->refresh();
    for (int s = 0; s < kSlots; ++s)
    {
        const auto i = (size_t) s;
        const String name = proc.getSlotName (s);
        slotLoad[i]->setButtonText (name.isEmpty() ? String ("load sound...") : name);
        const bool learning = proc.keyLearnSlot.load() == s;
        const int lo = proc.getSlotLow (s), hi = proc.getSlotHigh (s);
        String range = MidiMessage::getMidiNoteName (lo, true, true, 3);
        if (hi != lo) range += "-" + MidiMessage::getMidiNoteName (hi, true, true, 3);
        slotKey[i]->setButtonText (learning ? String ("press key...") : "KEY: " + range);
        slotKey[i]->setColour (TextButton::buttonColourId, learning ? kOrange : getLookAndFeel().findColour (TextButton::buttonColourId));
    }
    for (int a = 0; a < kActions; ++a)
    {
        String t = kActionNames[a];
        const int code = proc.actionMap[(size_t) a].load();
        if (proc.actionLearn.load() == a) t += "  [press...]";
        else if (code >= 0) t += "  [" + (code < 128 ? "CC" + String (code) : MidiMessage::getMidiNoteName (code - 128, true, true, 3)) + "]";
        seqActBtn[a]->setButtonText (t);
    }
    const int seqLenV = jlimit (1, kSteps, (int) (proc.apvts.getRawParameterValue ("seqLen")->load() + 0.5f));
    for (int s = 0; s < kSteps; ++s)   // steps beyond the pattern length are greyed out
    {
        const bool en = s < seqLenV;
        if (stepBtn[s]->isEnabled() != en) { stepBtn[s]->setEnabled (en); stepSld[s]->setEnabled (en); stepPit[s]->setEnabled (en); }
    }
    keyboard->repaint();
    content.repaint();
}

static void row (Rectangle<int> r, std::initializer_list<Component*> cs)
{
    const int w = r.getWidth() / (int) cs.size();
    for (auto* c : cs) c->setBounds (r.removeFromLeft (w).reduced (2));
}

void LiveFXEditor::layoutContent()
{
    auto content_ = [] (Rectangle<int> r) { return r.reduced (8).withTrimmedTop (22); };

    // looper
    {
        auto c = content_ (looperR);
        waveR = c.removeFromTop (100);
        c.removeFromTop (6);
        auto kn = c.removeFromRight (300);
        row (kn, { knobs[LoopLevel], knobs[LoopStart], knobs[LoopEnd] });
        auto top = c.removeFromTop (c.getHeight() / 2);
        row (top, { recBtn, stopBtn });
        row (c, { clearBtn, revBtn, loadBtn });
    }
    // granular
    {
        auto c = content_ (granR);
        auto bot = c.removeFromBottom (36);
        row (c, { knobs[GrMix], knobs[GrSize], knobs[GrPitch], knobs[GrDens], knobs[GrPos], knobs[GrSpray] });
        row (bot, { grFreezeBtn, keysBtn });
    }
    // delay
    {
        auto c = content_ (delayR);
        auto bot = c.removeFromBottom (32);
        row (c, { knobs[DlMix], knobs[DlTime], knobs[DlFb], knobs[DlTone] });
        row (bot, { dlSyncBtn, dlDivCb });
    }
    // reverb
    {
        auto c = content_ (reverbR);
        auto bot = c.removeFromBottom (32);
        row (c, { knobs[RvMix], knobs[RvSize], knobs[RvDamp] });
        row (bot, { rvFreezeBtn });
    }
    // filter / crush / drive / stutter
    {
        auto c = content_ (fxR);
        auto bot = c.removeFromBottom (32);
        row (c, { knobs[FltCut], knobs[FltRes], knobs[Crush], knobs[Drive] });
        row (bot, { fltTypeCb, stutBtn, stutDivCb });
    }
    // PaulStretch
    {
        auto c = content_ (paulR);
        auto bot = c.removeFromBottom (32);
        row (c, { knobs[PsMix], knobs[PsStretch] });
        row (bot, { psSizeCb, psFreezeBtn });
    }
    // sequencer
    {
        auto c = content_ (seqR);
        auto top = c.removeFromTop (90);
        auto colA = top.removeFromLeft (200);
        seqOnBtn->setBounds (colA.removeFromTop (40).reduced (2));
        seqRateCb->setBounds (colA.removeFromTop (40).reduced (2));
        auto colB = top.removeFromLeft (170);
        seqDirCb->setBounds (colB.removeFromTop (40).reduced (2));
        seqRatchetCb->setBounds (colB.removeFromTop (40).reduced (2));
        knobs[OutLevel]->setBounds (top.removeFromRight (100));
        row (top, { knobs[SeqLen], knobs[SeqSwing], knobs[SeqGate], knobs[SeqProb], knobs[SeqDepth] });
        c.removeFromTop (2);
        row (c.removeFromTop (28), { seqActBtn[0], seqActBtn[1], seqActBtn[2], seqActBtn[3], seqActBtn[4], seqActBtn[5], editModeBtn, seqPitchBtn });
        c.removeFromTop (4);
        const int w = c.getWidth() / kSteps;
        for (int s = 0; s < kSteps; ++s)
        {
            auto col = c.removeFromLeft (w).reduced (2);
            stepBtn[s]->setBounds (col.removeFromTop (28));
            const auto sb = col.reduced (0, 3);
            stepSld[s]->setBounds (sb);
            stepPit[s]->setBounds (sb);
        }
    }
    // key sampler: slot columns, then the piano
    {
        auto c = content_ (sampR);
        auto cols = c.removeFromTop (121);
        c.removeFromTop (6);
        keyboard->setBounds (c);
        const int w = cols.getWidth() / kSlots;
        for (size_t s = 0; s < (size_t) kSlots; ++s)
        {
            auto col = cols.removeFromLeft (w).reduced (3, 0);
            auto top = col.removeFromTop (30);
            slotClear[s]->setBounds (top.removeFromRight (28));
            slotLoad[s]->setBounds (top.withTrimmedRight (2));
            col.removeFromTop (3);
            auto r2 = col.removeFromTop (28);
            slotGrab[s]->setBounds (r2.removeFromRight (50));
            slotKey[s]->setBounds (r2.withTrimmedRight (2));
            col.removeFromTop (3);
            slotMode[s]->setBounds (col.removeFromTop (28));
            col.removeFromTop (3);
            slotLvl[s]->setBounds (col.removeFromTop (26));
        }
    }
}

void LiveFXEditor::paintContent (Graphics& g)
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
    panel (paulR,   "PAULSTRETCH");
    panel (seqR,    "STEP SEQUENCER  -  right-click an action button to MIDI-learn it (press a key or a control on the MiniLab)");
    panel (sampR,   "KEY SAMPLER  -  drop sounds on the piano (select a key range on the strip first to cover a range), or LOAD / GRAB a sound per slot");

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
    double frac = 0.0, startFrac = 0.0, endFrac = 1.0;
    String label;
    if (st == LRec)      { frac = pos / (60.0 * sr); label = "RECORDING  " + String (pos / sr, 1) + " s"; }
    else if (st == LEmpty) label = "EMPTY - pad 1 = record, or load / drop an audio file";
    else
    {
        const bool rev = proc.apvts.getRawParameterValue ("loopRev")->load() > 0.5f;
        const double minRegion = jmin (len, 1024.0);
        const double eIdx = len > 0 ? jlimit (minRegion, len, (double) proc.apvts.getRawParameterValue ("loopEnd")->load() * len) : 0.0;
        const double sIdx = len > 0 ? jlimit (0.0, eIdx - minRegion, (double) proc.apvts.getRawParameterValue ("loopStart")->load() * len) : 0.0;
        frac = len > 0 ? (rev ? eIdx - 1.0 + sIdx - pos : pos) / len : 0.0;
        startFrac = len > 0 ? sIdx / len : 0.0;
        endFrac = len > 0 ? eIdx / len : 1.0;
        label = String (st == LPlay ? "PLAYING" : st == LDub ? "OVERDUBBING" : "STOPPED")
                + "   " + String ((eIdx - sIdx) / sr, 2) + " s of " + String (len / sr, 2) + " s";
    }
    if (st != LEmpty && st != LRec)
    {
        const int sx = waveR.getX() + (int) (startFrac * waveR.getWidth());
        const int ex = waveR.getX() + (int) (endFrac * waveR.getWidth());
        g.setColour (Colour (0xaa0b0d11));                       // the parts outside the loop region are dimmed
        g.fillRect (waveR.getX(), waveR.getY(), sx - waveR.getX(), waveR.getHeight());
        g.fillRect (ex, waveR.getY(), waveR.getRight() - ex, waveR.getHeight());
        g.setColour (kOrange);                                   // loop start marker
        g.drawVerticalLine (sx, (float) waveR.getY(), (float) waveR.getBottom());
        g.fillRect (sx, waveR.getY(), 8, 5);
        g.setColour (kRed);                                      // loop end marker
        g.drawVerticalLine (ex - 1, (float) waveR.getY(), (float) waveR.getBottom());
        g.fillRect (ex - 8, waveR.getY(), 8, 5);
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
