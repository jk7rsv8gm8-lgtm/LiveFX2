#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

static const float kDlBeats[4]   = { 1.f, 0.5f, 0.75f, 0.25f };   // 1/4, 1/8, 1/8., 1/16
static const float kSeqBeats[4]  = { 1.f, 0.5f, 0.25f, 0.125f };  // 1/4, 1/8, 1/16, 1/32
static const float kStutBeats[4] = { 1.f, 0.5f, 0.25f, 0.125f };

enum { SmOneShot, SmGate, SmLoop, SmPitched };
static const int kDefKeys[kSlots] = { 48, 50, 52, 53, 55, 57, 59, 60 };   // default key per slot (white keys)

static AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout l;

    for (auto& p : kParams)
    {
        NormalisableRange<float> r (p.lo, p.hi, String (p.id) == "seqLen" ? 1.f : 0.f);
        if (p.mid > 0.f) r.setSkewForCentre (p.mid);
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { p.id, 1 }, p.name, r, p.def));
    }

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "fltType", 1 }, "Filter Type", StringArray { "LP", "HP", "BP" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "dlDiv", 1 },   "Delay Division", StringArray { "1/4", "1/8", "1/8.", "1/16" }, 1));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "seqRate", 1 }, "Seq Rate", StringArray { "1/4", "1/8", "1/16", "1/32" }, 2));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "stutDiv", 1 }, "Stutter Division", StringArray { "1/4", "1/8", "1/16", "1/32" }, 2));

    l.add (std::make_unique<AudioParameterBool> (ParameterID { "dlSync", 1 },    "Delay Sync", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "grFreeze", 1 },  "Grain Freeze", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "rvFreeze", 1 },  "Reverb Freeze", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "seqOn", 1 },     "Sequencer On", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "keysPitch", 1 }, "Keys To Grain Pitch", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "loopRev", 1 },   "Loop Reverse", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "psSize", 1 }, "PaulStretch Window", StringArray { "2048", "4096", "8192", "16384" }, 2));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "psFreeze", 1 }, "PaulStretch Freeze", false));

    for (int s = 0; s < kSlots; ++s)
    {
        l.add (std::make_unique<AudioParameterInt> (ParameterID { "smLow" + String (s + 1), 1 }, "Zone Low " + String (s + 1), 0, 127, kDefKeys[s]));
        l.add (std::make_unique<AudioParameterInt> (ParameterID { "smHigh" + String (s + 1), 1 }, "Zone High " + String (s + 1), 0, 127, kDefKeys[s]));
        l.add (std::make_unique<AudioParameterInt> (ParameterID { "smRoot" + String (s + 1), 1 }, "Zone Root " + String (s + 1), 0, 127, kDefKeys[s]));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "smMode" + String (s + 1), 1 }, "Sample Mode " + String (s + 1),
                                                       StringArray { "One-shot", "Gate", "Loop", "Pitched" }, 0));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "smLvl" + String (s + 1), 1 }, "Sample Level " + String (s + 1), 0.f, 1.5f, 1.f));
    }

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "seqDir", 1 }, "Seq Direction", StringArray { "Forward", "Backward", "Ping-pong", "Random" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "seqRatchet", 1 }, "Seq Ratchet", StringArray { "1x", "2x", "3x", "4x" }, 0));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "seqPitch", 1 }, "Step Pitch To Grains", false));
    for (int s = 0; s < kSteps; ++s)
        l.add (std::make_unique<AudioParameterInt> (ParameterID { "p" + String (s + 1), 1 }, "Step Pitch " + String (s + 1), -12, 12, 0));

    for (int s = 0; s < kSteps; ++s)
    {
        l.add (std::make_unique<AudioParameterBool> (ParameterID { "s" + String (s + 1), 1 }, "Step " + String (s + 1), s % 4 == 0));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "l" + String (s + 1), 1 }, "Step Level " + String (s + 1), 0.f, 1.f, 1.f));
    }
    return l;
}

LiveFXProcessor::LiveFXProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (int i = 0; i < NumP; ++i)
    {
        ccMap[(size_t) i] = kParams[i].cc;
        pv[i] = apvts.getRawParameterValue (kParams[i].id);
    }
    pFltType  = apvts.getRawParameterValue ("fltType");
    pDlDiv    = apvts.getRawParameterValue ("dlDiv");
    pSeqRate  = apvts.getRawParameterValue ("seqRate");
    pStutDiv  = apvts.getRawParameterValue ("stutDiv");
    pDlSync   = apvts.getRawParameterValue ("dlSync");
    pGrFreeze = apvts.getRawParameterValue ("grFreeze");
    pRvFreeze = apvts.getRawParameterValue ("rvFreeze");
    pSeqOn    = apvts.getRawParameterValue ("seqOn");
    pKeys     = apvts.getRawParameterValue ("keysPitch");
    pLoopRev  = apvts.getRawParameterValue ("loopRev");
    pPsSize     = apvts.getRawParameterValue ("psSize");
    pPsFreeze   = apvts.getRawParameterValue ("psFreeze");
    pSeqDir     = apvts.getRawParameterValue ("seqDir");
    pSeqRatchet = apvts.getRawParameterValue ("seqRatchet");
    pSeqPitch   = apvts.getRawParameterValue ("seqPitch");
    for (int s = 0; s < kSteps; ++s) stepPit[s] = apvts.getRawParameterValue ("p" + String (s + 1));
    for (auto& a : actionMap) a = -1;
    for (int s = 0; s < kSteps; ++s)
    {
        stepOn[s]  = apvts.getRawParameterValue ("s" + String (s + 1));
        stepLvl[s] = apvts.getRawParameterValue ("l" + String (s + 1));
    }
    for (int s = 0; s < kSlots; ++s)
    {
        pSlotLow[s]  = apvts.getRawParameterValue ("smLow" + String (s + 1));
        pSlotHigh[s] = apvts.getRawParameterValue ("smHigh" + String (s + 1));
        pSlotRoot[s] = apvts.getRawParameterValue ("smRoot" + String (s + 1));
        pSlotMode[s] = apvts.getRawParameterValue ("smMode" + String (s + 1));
        pSlotLvl[s]  = apvts.getRawParameterValue ("smLvl" + String (s + 1));
    }
    for (auto& p : peaks) p = 0.f;
    formats.registerBasicFormats();
    startTimerHz (20);   // message-thread jobs: finishing GRAB requests and running sequencer actions
}

LiveFXProcessor::~LiveFXProcessor() { stopTimer(); }

// Decodes + resamples on the message thread, then hands the result to the audio thread.
bool LiveFXProcessor::loadAudioFile (const File& f)
{
    std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples <= 0 || sr <= 0.0) return false;

    const int srcLen = (int) jmin (reader->lengthInSamples, (int64) (60.0 * reader->sampleRate));
    AudioBuffer<float> src (2, srcLen);
    src.clear();
    reader->read (&src, 0, srcLen, 0, true, true);
    if (reader->numChannels < 2) src.copyFrom (1, 0, src, 0, 0, srcLen);

    const double ratio = reader->sampleRate / sr;           // source samples per output sample
    const int outLen = jmin ((int) (60.0 * sr), (int) ((double) srcLen / ratio));
    if (outLen < 1024) return false;

    AudioBuffer<float> out (2, outLen);
    for (int c = 0; c < 2; ++c)
    {
        LagrangeInterpolator li;
        li.process (ratio, src.getReadPointer (c), out.getWritePointer (c), outLen);
    }
    {
        SpinLock::ScopedLockType lock (pendingLock);
        pendingBuf = std::move (out);
    }
    hasPending = true;
    return true;
}

// ------------------------------- key sampler --------------------------------
// Samples are kept at their native rate and resampled on playback, so they are always in tune.
bool LiveFXProcessor::loadSlotFile (int slot, const File& f)
{
    if (slot < 0 || slot >= kSlots) return false;
    std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples <= 0) return false;

    const int len = (int) jmin (reader->lengthInSamples, (int64) (30.0 * reader->sampleRate));
    AudioBuffer<float> b (2, len);
    b.clear();
    reader->read (&b, 0, len, 0, true, true);
    if (reader->numChannels < 2) b.copyFrom (1, 0, b, 0, 0, len);
    {
        SpinLock::ScopedLockType lock (slotLock);
        pendingSlotBuf[slot] = std::move (b);
        pendingSlotRate[slot] = reader->sampleRate;
    }
    slotPending[slot] = true;
    slotHas[slot] = true;
    {
        ScopedLock l (pathLock);
        slotPath[slot] = f.getFullPathName();
        const String n = f.getFileNameWithoutExtension();
        slotLabel[slot] = n.startsWith ("loop-slot") ? String ("from looper") : n;
    }
    return true;
}

void LiveFXProcessor::clearSlot (int slot)
{
    if (slot < 0 || slot >= kSlots) return;
    {
        SpinLock::ScopedLockType lock (slotLock);
        pendingSlotBuf[slot] = AudioBuffer<float>();
        pendingSlotRate[slot] = 44100.0;
    }
    slotPending[slot] = true;
    slotHas[slot] = false;
    { ScopedLock l (pathLock); slotPath[slot] = String(); slotLabel[slot] = String(); }
}

String LiveFXProcessor::getSlotName (int slot) const
{
    ScopedLock l (pathLock);
    return slotLabel[slot];
}

// Audio thread: copy the loop region (loop start .. loop end, as heard - incl. reverse) into the staging buffer.
void LiveFXProcessor::captureLoop (int slot)
{
    const int st = loopState.load();
    if (capReady.load() || loopLen <= 0 || st == LEmpty || st == LRec) return;
    const int minRegion = jmin (loopLen, 1024);
    const int e = jlimit (minRegion, loopLen, (int) (pv[LoopEnd]->load() * (float) loopLen));
    const int b = jlimit (0, e - minRegion, (int) (pv[LoopStart]->load() * (float) loopLen));
    const int len = e - b;
    if (len < 64 || (size_t) len > capBuf[0].size()) return;
    const bool rev = pLoopRev->load() > 0.5f;
    for (int c = 0; c < 2; ++c)
        for (int k = 0; k < len; ++k)
            capBuf[c][(size_t) k] = loopBuf[c][(size_t) (rev ? e - 1 - k : b + k)];
    capLen = len;
    capSlot = slot;
    capReady = true;
}

// Message thread (timer): store the captured loop as a WAV (so it survives saving the project) and hand it to the sampler.
void LiveFXProcessor::pollCapture()
{
    if (! capReady.load()) return;
    const int slot = capSlot, len = capLen;
    AudioBuffer<float> b (2, len);
    for (int c = 0; c < 2; ++c) FloatVectorOperations::copy (b.getWritePointer (c), capBuf[c].data(), len);
    capReady = false;

    File out = File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("LiveFX Performer")
                   .getChildFile ("captures")
                   .getChildFile ("loop-slot" + String (slot + 1) + "-" + String (Time::currentTimeMillis()) + ".wav");
    String path;
    if (out.getParentDirectory().createDirectory().wasOk())
    {
        WavAudioFormat wav;
        if (auto os = std::unique_ptr<FileOutputStream> (out.createOutputStream()))
            if (auto w = std::unique_ptr<AudioFormatWriter> (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0)))
            {
                os.release();   // the writer owns the stream now
                w->writeFromAudioSampleBuffer (b, 0, len);
                path = out.getFullPathName();
            }
    }
    {
        SpinLock::ScopedLockType lock (slotLock);
        pendingSlotBuf[slot] = std::move (b);
        pendingSlotRate[slot] = sr;
    }
    slotPending[slot] = true;
    slotHas[slot] = true;
    { ScopedLock l (pathLock); slotPath[slot] = path; slotLabel[slot] = "from looper"; }
}

void LiveFXProcessor::timerCallback()
{
    pollCapture();
    const unsigned m = pendingActions.exchange (0);
    for (int a = 0; a < kActions; ++a)
        if (m & (1u << a)) runSeqAction (a);
}

// Sequencer pattern actions (message thread). They only edit the step parameters, so everything stays automatable/saved.
void LiveFXProcessor::runSeqAction (int a)
{
    Random r;
    auto get = [this] (const String& id) { return apvts.getRawParameterValue (id)->load(); };
    auto set = [this] (const String& id, float plain)
    {
        if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (plain));
    };
    const int len = jlimit (1, kSteps, (int) (get ("seqLen") + 0.5f));
    static const int scale[9] = { -12, -7, -5, 0, 0, 3, 5, 7, 12 };   // note choices for random step pitches

    if (a == 0)          // RANDOMIZE: new random pattern (steps, levels and pitches)
    {
        for (int s = 1; s <= kSteps; ++s)
        {
            set ("s" + String (s), r.nextFloat() < 0.5f ? 1.f : 0.f);
            set ("l" + String (s), 0.4f + 0.6f * r.nextFloat());
            set ("p" + String (s), (float) scale[r.nextInt (9)]);
        }
    }
    else if (a == 1)     // MUTATE: small random changes to the current pattern
    {
        for (int k = 0; k < 3; ++k) { const String id = "s" + String (1 + r.nextInt (len)); set (id, get (id) > 0.5f ? 0.f : 1.f); }
        for (int k = 0; k < 3; ++k) set ("l" + String (1 + r.nextInt (len)), 0.4f + 0.6f * r.nextFloat());
        for (int k = 0; k < 2; ++k) set ("p" + String (1 + r.nextInt (len)), (float) scale[r.nextInt (9)]);
    }
    else if (a == 2 || a == 3)   // SHIFT < / SHIFT >: rotate the pattern by one step
    {
        float on[kSteps], lv[kSteps], pt[kSteps];
        for (int i = 0; i < len; ++i)
        {
            on[i] = get ("s" + String (i + 1)); lv[i] = get ("l" + String (i + 1)); pt[i] = get ("p" + String (i + 1));
        }
        for (int i = 0; i < len; ++i)
        {
            const int src = (a == 2) ? (i + 1) % len : (i + len - 1) % len;
            set ("s" + String (i + 1), on[src]); set ("l" + String (i + 1), lv[src]); set ("p" + String (i + 1), pt[src]);
        }
    }
    else if (a == 4) { for (int s = 1; s <= kSteps; ++s) set ("s" + String (s), 0.f); }   // CLEAR
    else if (a == 5) { for (int s = 1; s <= kSteps; ++s) set ("s" + String (s), 1.f); }   // FILL
}

// Sets a slot's key zone (parameters only, so it is saved and automatable). mode < 0 keeps the current mode.
void LiveFXProcessor::setSlotZone (int slot, int low, int high, int root, int mode)
{
    if (slot < 0 || slot >= kSlots) return;
    low = jlimit (0, 127, low);
    high = jlimit (low, 127, high);
    root = jlimit (0, 127, root);
    auto set = [this] (const String& id, float v)
    {
        if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    const String n (slot + 1);
    set ("smLow" + n, (float) low);
    set ("smHigh" + n, (float) high);
    set ("smRoot" + n, (float) root);
    if (mode >= 0) set ("smMode" + n, (float) mode);
}

void LiveFXProcessor::previewNote (int note, bool down)
{
    if (note >= 0 && note < 128) pushCmd (2000 + note * 2 + (down ? 1 : 0));
}

// A key plays every slot whose zone (low..high) contains it. In "Pitched" mode the sound is transposed relative to the root key.
bool LiveFXProcessor::sampleNoteOn (int note, float vel)
{
    bool any = false;
    for (int slot = 0; slot < kSlots; ++slot)
    {
        if (slotLen[slot] <= 0) continue;
        if (note < (int) pSlotLow[slot]->load() || note > (int) pSlotHigh[slot]->load()) continue;

        Voice* v = nullptr;
        for (auto& c : voices) if (! c.active) { v = &c; break; }
        if (v == nullptr) v = &voices[(size_t) (stealIdx++ % kVoices)];

        const int mode = (int) pSlotMode[slot]->load();
        const int root = (int) pSlotRoot[slot]->load();
        v->active = true; v->held = true; v->releasing = false;
        v->slot = slot; v->note = note; v->mode = mode;
        v->pos = 0.0; v->env = 0.f;
        v->inc = slotRate[slot] / sr * (mode == SmPitched ? std::pow (2.0, (note - root) / 12.0) : 1.0);
        v->gain = jmax (0.1f, vel) * pSlotLvl[slot]->load();
        any = true;
    }
    return any;
}

void LiveFXProcessor::releaseNote (int note)
{
    for (auto& v : voices)
        if (v.active && v.held && v.note == note)
        {
            v.held = false;
            if (v.mode != SmOneShot) v.releasing = true;   // one-shots always play to the end
        }
}

// ------------------------------- PaulStretch ---------------------------------
// Takes a window of the recent audio, keeps the FFT magnitudes, randomises the phases, and overlap-adds the result.
// The read position advances 1/stretch as fast as real time, so sound is stretched into a slowly evolving drone.
void LiveFXProcessor::psReset (int idx)
{
    psSizeIdxLast = idx;
    const int N = 2048 << idx;
    for (int c = 0; c < 2; ++c) std::fill (psAcc[c].begin(), psAcc[c].end(), 0.f);
    for (int i = 0; i < N; ++i)
        psWin[(size_t) i] = std::sin (MathConstants<float>::pi * ((float) i + 0.5f) / (float) N);   // sine window: 50 % overlap sums to 1
    psPos = N / 2;                                  // makes the next sample compute a fresh block
    psRead = (double) psW - (double) N;
    if (psRead < 0.0) psRead += (double) psH;
}

void LiveFXProcessor::psNextBlock (int N, float stretch, bool frozen)
{
    const int hop = N / 2;
    for (int c = 0; c < 2; ++c)                      // slide the overlap-add accumulators by one hop
    {
        float* a = psAcc[c].data();
        std::copy (a + hop, a + N, a);
        std::fill (a + hop, a + N, 0.f);
    }
    psPos = 0;

    double lag = (double) psW - psRead;              // jump to "now" when the read position fell too far behind
    if (lag < 0.0) lag += (double) psH;
    if (! frozen && (lag > (double) (psH - N - 8) || lag < (double) N))
    {
        psRead = (double) psW - (double) N;
        if (psRead < 0.0) psRead += (double) psH;
    }

    auto& fft = *psFft[(size_t) psSizeIdxLast];
    const int start = (int) psRead;
    for (int c = 0; c < 2; ++c)
    {
        const float* h = psHist[c].data();
        double inEnergy = 0.0;
        for (int i = 0; i < N; ++i)
        {
            int idx = start + i;
            if (idx >= psH) idx -= psH;
            const float x = h[idx] * psWin[(size_t) i];
            psSpecIn[(size_t) i] = std::complex<float> (x, 0.f);
            inEnergy += (double) x * (double) x;
        }
        fft.perform (psSpecIn.data(), psSpecOut.data(), false);

        psSpecIn[0] = std::complex<float> (std::abs (psSpecOut[0]), 0.f);
        psSpecIn[(size_t) hop] = std::complex<float> (std::abs (psSpecOut[(size_t) hop]), 0.f);
        for (int k = 1; k < hop; ++k)                // keep magnitude, random phase (mirrored so the result is real)
        {
            const float mag = std::abs (psSpecOut[(size_t) k]);
            const float ph = rng.nextFloat() * MathConstants<float>::twoPi;
            const std::complex<float> v (mag * std::cos (ph), mag * std::sin (ph));
            psSpecIn[(size_t) k] = v;
            psSpecIn[(size_t) (N - k)] = std::conj (v);
        }
        fft.perform (psSpecIn.data(), psSpecOut.data(), true);

        double outEnergy = 0.0;
        for (int i = 0; i < N; ++i)
        {
            const float x = psSpecOut[(size_t) i].real() * psWin[(size_t) i];
            psSpecIn[(size_t) i] = std::complex<float> (x, 0.f);      // scratch
            outEnergy += (double) x * (double) x;
        }
        const float g = outEnergy > 1.0e-12 ? (float) std::sqrt (inEnergy / outEnergy) : 0.f;   // match the level of the input
        float* a = psAcc[c].data();
        for (int i = 0; i < N; ++i) a[i] += psSpecIn[(size_t) i].real() * g;
    }
    if (! frozen)
    {
        psRead += (double) hop / (double) jmax (1.f, stretch);
        if (psRead >= (double) psH) psRead -= (double) psH;
    }
}

bool LiveFXProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainInputChannelSet() == AudioChannelSet::stereo()
        && l.getMainOutputChannelSet() == AudioChannelSet::stereo();
}

void LiveFXProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    sampleRateUI = sr;
    loopMax = (int) (60.0 * sr);
    gSize   = (int) (4.0 * sr);
    hSize   = (int) (4.0 * sr);
    dSize   = (int) (2.3 * sr) + 4;
    for (int c = 0; c < 2; ++c)
    {
        loopBuf[c].assign ((size_t) loopMax, 0.f);
        gBuf[c].assign ((size_t) gSize, 0.f);
        hBuf[c].assign ((size_t) hSize, 0.f);
        dBuf[c].assign ((size_t) dSize, 0.f);
    }
    loopState = LEmpty; loopPos = loopLen = 0; loopPosUI = loopLenUI = 0;
    gW = hW = dW = 0;
    for (auto& g : grains) g.on = false;
    stutActive = false;
    fl[0] = fl[1] = {};
    cutS = 20000.f; dTime = (float) (0.375 * sr); dlpL = dlpR = 0.f;
    gateSm = 1.f; step = 0; accum = 0.0;
    for (auto& p : peaks) p = 0.f;
    atkStep = 1.f / (0.002f * (float) sr);
    relStep = 1.f / (0.015f * (float) sr);
    for (auto& v : voices) v.active = false;
    for (int c = 0; c < 2; ++c) capBuf[c].assign ((size_t) loopMax, 0.f);
    capReady = false;
    psH = (int) (15.0 * sr);
    for (int c = 0; c < 2; ++c) { psHist[c].assign ((size_t) psH, 0.f); psAcc[c].assign (16384, 0.f); }
    psWin.assign (16384, 0.f);
    psSpecIn.assign (16384, std::complex<float> (0.f, 0.f));
    psSpecOut.assign (16384, std::complex<float> (0.f, 0.f));
    psFft.clear();
    for (int i = 0; i < 4; ++i) psFft.push_back (std::make_unique<juce::dsp::FFT> (11 + i));
    psW = 0; psPos = 0; psSizeIdxLast = -1; psRead = 0.0; psWasActive = false;
    reverb.setSampleRate (sr);
    reverb.reset();
}

// ------------------------------- looper ------------------------------------
void LiveFXProcessor::computePeaks()
{
    if (loopLen <= 0) return;
    for (int b = 0; b < kPeaks; ++b)
    {
        int s = (int) ((int64) b * loopLen / kPeaks), e = (int) ((int64) (b + 1) * loopLen / kPeaks);
        e = jmax (e, s + 1);
        float m = 0.f;
        for (int i = s; i < e && i < loopLen; ++i)
            m = jmax (m, std::abs (loopBuf[0][(size_t) i]), std::abs (loopBuf[1][(size_t) i]));
        peaks[(size_t) b] = m;
    }
}

void LiveFXProcessor::loopPress()
{
    switch (loopState.load())
    {
        case LEmpty:   loopPos = 0; loopState = LRec; break;
        case LRec:
            if (loopPos < 1024) { loopClear(); break; }
            loopLen = loopPos; loopPos = 0; loopState = LPlay; computePeaks(); break;
        case LPlay:    loopState = LDub; break;
        case LDub:     loopState = LPlay; computePeaks(); break;
        case LStopped: loopPos = loopStartIdx; loopState = LPlay; break;
    }
}

void LiveFXProcessor::loopStopPlay()
{
    switch (loopState.load())
    {
        case LRec:     loopPress(); break;                       // finish recording, start playing
        case LPlay:    loopState = LStopped; break;
        case LDub:     loopState = LStopped; computePeaks(); break;
        case LStopped: loopPos = loopStartIdx; loopState = LPlay; break;
        default: break;
    }
}

void LiveFXProcessor::loopClear()
{
    for (int c = 0; c < 2; ++c) std::fill (loopBuf[c].begin(), loopBuf[c].end(), 0.f);
    loopLen = loopPos = 0;
    loopState = LEmpty;
    for (auto& p : peaks) p = 0.f;
}

// ------------------------------- stutter -----------------------------------
void LiveFXProcessor::startStutter()
{
    int len = (int) (60.0 / curBpm * kStutBeats[jlimit (0, 3, (int) pStutDiv->load())] * sr);
    stutLen = jlimit (64, hSize - 2, len);
    stutStart = (hW - stutLen + hSize) % hSize;
    stutPhase = 0;
    stutActive = true;
}

// ------------------------------- MIDI --------------------------------------
void LiveFXProcessor::toggleParam (const char* id)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.f : 1.f);
}

// Pad map (MiniLab 3 pads, channel 10):
//  1 loop rec/play/overdub   2 loop stop/play   3 loop clear   4 loop reverse
//  5 grain freeze            6 stutter (hold)   7 sequencer on/off   8 reverb freeze
void LiveFXProcessor::padEvent (int idx, bool down)
{
    switch (idx)
    {
        case 0: if (down) loopPress(); break;
        case 1: if (down) loopStopPlay(); break;
        case 2: if (down) loopClear(); break;
        case 3: if (down) toggleParam ("loopRev"); break;
        case 4: if (down) toggleParam ("grFreeze"); break;
        case 5: if (down) startStutter(); else stutActive = false; break;
        case 6: if (down) toggleParam ("seqOn"); break;
        case 7: if (down) toggleParam ("rvFreeze"); break;
        default: break;
    }
}

void LiveFXProcessor::pushCmd (int v)
{
    int s1, n1, s2, n2;
    fifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0) { fifoData[s1] = v; fifo.finishedWrite (1); }
}

void LiveFXProcessor::sendPad (int idx, bool down) { pushCmd (idx * 2 + (down ? 1 : 0)); }
void LiveFXProcessor::grabLoopToSlot (int slot)    { if (slot >= 0 && slot < kSlots) pushCmd (1000 + slot); }

void LiveFXProcessor::handleMidi (MidiBuffer& midi)
{
    for (const auto meta : midi)
    {
        auto m = meta.getMessage();
        if (m.isController())
        {
            int cc = m.getControllerNumber();
            float v = (float) m.getControllerValue() / 127.f;
            int li = learnIdx.load();
            if (li >= 0) { ccMap[(size_t) li] = cc; learnIdx = -1; }

            // sequencer action buttons can be mapped to a CC: the action fires when the value rises above half
            const int al = actionLearn.load();
            if (al >= 0 && v >= 0.5f) { actionMap[(size_t) al] = cc; actionLearn = -1; ccHigh[cc] = true; }
            else if (v >= 0.5f && ! ccHigh[cc])
            {
                ccHigh[cc] = true;
                for (int a = 0; a < kActions; ++a)
                    if (actionMap[(size_t) a].load() == cc) pendingActions.fetch_or (1u << a);
            }
            else if (v < 0.5f) ccHigh[cc] = false;
            for (int i = 0; i < NumP; ++i)
                if (ccMap[(size_t) i].load() == cc)
                    if (auto* p = apvts.getParameter (kParams[i].id)) p->setValueNotifyingHost (v);
        }
        else if (m.isNoteOn())
        {
            int note = m.getNoteNumber();
            if (m.getChannel() != kPadChannel) noteHeld[note] = true;
            if (m.getChannel() == kPadChannel)
            {
                if (note >= kPadBaseNote && note < kPadBaseNote + 8) padEvent (note - kPadBaseNote, true);           // pad bank A: looper / FX
                else if (note >= kPadBaseNote + 8 && note < kPadBaseNote + 16) captureLoop (note - kPadBaseNote - 8);   // pad bank B: GRAB loop into key slot 1-8
            }
            else if (actionLearn.load() >= 0)   // a key was pressed while learning a sequencer action
            {
                actionMap[(size_t) actionLearn.load()] = 128 + note;
                actionLearn = -1;
            }
            else if (actionForNote (note) >= 0)   // this key is mapped to a sequencer action
                pendingActions.fetch_or (1u << actionForNote (note));
            else
            {
                const int ls = keyLearnSlot.load();
                if (ls >= 0)   // "KEY" button was pressed in the UI: this key is assigned to that slot
                {
                    setSlotZone (ls, note, note, note);   // the zone becomes this single key
                    keyLearnSlot = -1;
                }
                else if (! sampleNoteOn (note, m.getFloatVelocity()))
                    keyOffset = jlimit (-24, 24, note - 60);   // keys -> grain pitch (middle C = 0)
            }
        }
        else if (m.isNoteOff())
        {
            const int note = m.getNoteNumber();
            if (m.getChannel() != kPadChannel) noteHeld[note] = false;
            if (m.getChannel() == kPadChannel)
            {
                if (note >= kPadBaseNote && note < kPadBaseNote + 8) padEvent (note - kPadBaseNote, false);
            }
            else releaseNote (note);
        }
    }
    midi.clear();
}

void LiveFXProcessor::spawnGrain (float sizeMs, float pitch, float posKnob, float spray)
{
    for (auto& g : grains)
    {
        if (g.on) continue;
        g.len = jmax (32, (int) (sizeMs * 0.001 * sr));
        g.inc = std::pow (2.0, pitch / 12.0);
        double off = posKnob * 3.4 * sr + spray * rng.nextFloat() * 0.5 * sr;
        off = jmax (off, g.len * jmax (1.0, g.inc) + 4.0);
        off = jmin (off, (double) gSize - 4.0);
        g.pos = (double) gW - off;
        if (g.pos < 0) g.pos += gSize;
        float p = (rng.nextFloat() * 2.f - 1.f) * spray;
        float a = (p * 0.5f + 0.5f) * MathConstants<float>::halfPi;
        g.pl = std::cos (a) * 1.414f;
        g.pr = std::sin (a) * 1.414f;
        g.age = 0;
        g.on = true;
        return;
    }
}

// ------------------------------- audio -------------------------------------
void LiveFXProcessor::processBlock (AudioBuffer<float>& buf, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buf.getNumSamples();
    if (buf.getNumChannels() < 2) return;

    curBpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                if (*b > 20.0) curBpm = *b;

    if (hasPending.load())
    {
        SpinLock::ScopedTryLockType lock (pendingLock);
        if (lock.isLocked())
        {
            const int len = jmin (pendingBuf.getNumSamples(), loopMax);
            if (len > 0)
            {
                for (int c = 0; c < 2; ++c)
                    FloatVectorOperations::copy (loopBuf[c].data(), pendingBuf.getReadPointer (c), len);
                loopLen = len; loopPos = 0;
                loopState = LPlay;
                computePeaks();
            }
            hasPending = false;
        }
    }

    for (int i = 0; i < kSlots; ++i)   // pick up newly loaded key samples
    {
        if (! slotPending[i].load()) continue;
        SpinLock::ScopedTryLockType lock (slotLock);
        if (! lock.isLocked()) continue;
        for (auto& v : voices) if (v.active && v.slot == i) v.active = false;
        const int newLen = pendingSlotBuf[i].getNumSamples();
        std::swap (slotBuf[i], pendingSlotBuf[i]);       // swap (not move): nothing is freed on the audio thread
        std::swap (slotRate[i], pendingSlotRate[i]);
        slotLen[i] = newLen;
        slotPending[i] = false;
    }

    handleMidi (midi);
    while (fifo.getNumReady() > 0)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (1, s1, n1, s2, n2);
        if (n1 <= 0) break;
        int v = fifoData[s1];
        fifo.finishedRead (1);
        if (v >= 2000)
        {
            const int nt = (v - 2000) >> 1;
            if ((v & 1) != 0) sampleNoteOn (nt, 0.8f); else releaseNote (nt);
        }
        else if (v >= 1000) captureLoop (v - 1000);
        else padEvent (v >> 1, (v & 1) != 0);
    }

    // ---- fetch parameters ----
    auto P = [&] (int i) { return pv[i]->load(); };
    const float loopLvl = P (LoopLevel);
    const float grMix = P (GrMix), grSizeMs = P (GrSize), grDens = P (GrDens), grPos = P (GrPos), grSpray = P (GrSpray);
    const bool frozen = pGrFreeze->load() > 0.5f;
    const float pitch = P (GrPitch) + (pKeys->load() > 0.5f ? (float) keyOffset.load() : 0.f);
    const float dlMix = P (DlMix), dlFb = P (DlFb), dlTone = P (DlTone);
    const bool sync = pDlSync->load() > 0.5f;
    const double dlMs = sync ? 60000.0 / curBpm * kDlBeats[jlimit (0, 3, (int) pDlDiv->load())] : (double) P (DlTime);
    const float dTarget = (float) jlimit (16.0, (double) dSize - 4.0, dlMs * 0.001 * sr);
    const float toneA = 1.f - std::exp (-MathConstants<float>::twoPi * dlTone / (float) sr);
    const float cutT = P (FltCut), q = P (FltRes);
    const int ftype = jlimit (0, 2, (int) pFltType->load());
    const bool fltActive = ! (ftype == 0 && cutT >= 19000.f);
    const float crushAmt = P (Crush), drive = P (Drive);
    const bool seqOn = pSeqOn->load() > 0.5f;
    const float depth = P (SeqDepth);
    const double stepSamples = 60.0 / curBpm * kSeqBeats[jlimit (0, 3, (int) pSeqRate->load())] * sr;
    const bool rev = pLoopRev->load() > 0.5f;
    {   // loop region: playback and overdub run from Loop Start to Loop End, then wrap back to the start
        const int minRegion = jmin (loopLen, 1024);
        if (loopLen > 0)
        {
            const int e = jlimit (minRegion, loopLen, (int) (P (LoopEnd) * (float) loopLen));
            loopEndIdx = e;
            loopStartIdx = jlimit (0, e - minRegion, (int) (P (LoopStart) * (float) loopLen));
        }
        else { loopStartIdx = 0; loopEndIdx = 0; }
    }
    const float outLvl = P (OutLevel);
    const float gateCoef = 1.f - std::exp (-1.f / (0.002f * (float) sr));
    bool sOn[kSteps]; float sLv[kSteps]; int sPit[kSteps];
    for (int s = 0; s < kSteps; ++s) { sOn[s] = stepOn[s]->load() > 0.5f; sLv[s] = stepLvl[s]->load(); sPit[s] = (int) stepPit[s]->load(); }

    // sequencer settings
    const int seqLen = jlimit (1, kSteps, (int) (P (SeqLen) + 0.5f));
    const int seqDirV = jlimit (0, 3, (int) pSeqDir->load());
    const int ratchet = 1 + jlimit (0, 3, (int) pSeqRatchet->load());
    const float swing = P (SeqSwing), gateLen = P (SeqGate), prob = P (SeqProb);
    const bool seqPitchOn = pSeqPitch->load() > 0.5f;
    if (step >= seqLen) step = 0;
    if (! seqOn) seqPitchSt = 0.f;

    // decide what the current step does (probability, level, pitch) and how long it lasts (swing)
    auto beginStep = [&]
    {
        stepActive = sOn[step] && (prob >= 0.999f || rng.nextFloat() < prob);
        stepLevel = sLv[step];
        seqPitchSt = seqPitchOn ? (float) sPit[step] : 0.f;
        curLen = jmax (16.0, stepSamples * (((tickCount & 1) == 0) ? 1.0 + 0.5 * swing : 1.0 - 0.5 * swing));
    };
    // move to the next step according to the direction setting
    auto nextStep = [&]
    {
        switch (seqDirV)
        {
            case 0:  step = (step + 1) % seqLen; break;
            case 1:  step = (step + seqLen - 1) % seqLen; break;
            case 2:
                if (seqLen < 2) { step = 0; break; }
                step += pingDir;
                if (step >= seqLen) { pingDir = -1; step = seqLen - 2; }
                else if (step < 0)  { pingDir = 1;  step = 1; }
                break;
            default: step = rng.nextInt (seqLen); break;
        }
    };

    if (seqOn && ! lastSeqOn)
    {
        step = (seqDirV == 1) ? seqLen - 1 : 0;
        tickCount = 0; pingDir = 1; accum = 0.0;
        beginStep();
    }
    lastSeqOn = seqOn;

    // PaulStretch settings
    const int psIdx = jlimit (0, 3, (int) pPsSize->load());
    const int psSizeN = 2048 << psIdx, psHopN = psSizeN / 2;
    const float psMix = P (PsMix), psStretch = P (PsStretch);
    const bool psFrozen = pPsFreeze->load() > 0.5f;
    const bool psActive = psMix > 0.001f;
    if (psIdx != psSizeIdxLast || (! psActive && psWasActive)) psReset (psIdx);   // new window size, or switched off
    psWasActive = psActive;

    const float grainGain = 1.f / std::sqrt (jmax (1.f, grDens * grSizeMs * 0.001f));
    const float bits = 16.f - crushAmt * 12.f;
    const float levels = std::pow (2.f, bits - 1.f);
    const int ds = 1 + (int) (crushAmt * crushAmt * 24.f);

    auto* L = buf.getWritePointer (0);
    auto* R = buf.getWritePointer (1);

    for (int i = 0; i < n; ++i)
    {
        // ---- key sampler voices (mixed in before the looper, so the loop can record them) ----
        float sL = 0.f, sR = 0.f;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const int len = slotLen[v.slot];
            if (len < 2) { v.active = false; continue; }
            int i0 = (int) v.pos;
            if (i0 >= len)
            {
                if (v.mode == SmLoop) { v.pos -= (double) len; i0 = (int) v.pos; }
                else { v.active = false; continue; }
            }
            int i1 = i0 + 1;
            if (i1 >= len) i1 = (v.mode == SmLoop) ? 0 : len - 1;
            const float* dl = slotBuf[v.slot].getReadPointer (0);
            const float* dr = slotBuf[v.slot].getReadPointer (1);
            const float fr = (float) (v.pos - i0);
            if (v.releasing) { v.env -= relStep; if (v.env <= 0.f) { v.active = false; continue; } }
            else if (v.env < 1.f) v.env = jmin (1.f, v.env + atkStep);
            sL += (dl[i0] + (dl[i1] - dl[i0]) * fr) * v.env * v.gain;
            sR += (dr[i0] + (dr[i1] - dr[i0]) * fr) * v.env * v.gain;
            v.pos += v.inc;
        }

        const float inL = L[i] + sL, inR = R[i] + sR;

        // ---- looper (records dry input, plays back into the FX chain) ----
        float lpL = 0.f, lpR = 0.f;
        const int st = loopState.load (std::memory_order_relaxed);
        if (st == LRec)
        {
            loopBuf[0][(size_t) loopPos] = inL;
            loopBuf[1][(size_t) loopPos] = inR;
            if (++loopPos >= loopMax) { loopLen = loopMax; loopEndIdx = loopLen; loopStartIdx = 0; loopPos = 0; loopState = LPlay; computePeaks(); }
        }
        else if ((st == LPlay || st == LDub) && loopLen > 0)
        {
            if (loopPos < loopStartIdx || loopPos >= loopEndIdx) loopPos = loopStartIdx;
            const size_t idx = (size_t) (rev ? loopEndIdx - 1 + loopStartIdx - loopPos : loopPos);
            lpL = loopBuf[0][idx]; lpR = loopBuf[1][idx];
            if (st == LDub)
            {
                loopBuf[0][idx] = std::tanh (lpL + inL);
                loopBuf[1][idx] = std::tanh (lpR + inR);
            }
            if (++loopPos >= loopEndIdx) loopPos = loopStartIdx;
        }
        float xL = inL + lpL * loopLvl, xR = inR + lpR * loopLvl;

        // ---- stutter (captures last 1/4..1/32 note, loops it while pad held) ----
        if (stutActive)
        {
            const size_t idx = (size_t) ((stutStart + stutPhase) % hSize);
            const float g = jmin (1.f, (float) jmin (stutPhase, stutLen - stutPhase) / 16.f);
            hBuf[0][(size_t) hW] = xL; hBuf[1][(size_t) hW] = xR;
            xL = hBuf[0][idx] * g; xR = hBuf[1][idx] * g;
            if (++stutPhase >= stutLen) stutPhase = 0;
        }
        else { hBuf[0][(size_t) hW] = xL; hBuf[1][(size_t) hW] = xR; }
        if (++hW >= hSize) hW = 0;

        // ---- step-gate sequencer ----
        float gTarget = 1.f;
        if (seqOn)
        {
            accum += 1.0;
            if (accum >= curLen) { accum -= curLen; ++tickCount; nextStep(); beginStep(); }
            const double sub = accum / curLen * ratchet;                 // ratchet: repeat the gate inside the step
            const bool open = stepActive && (sub - std::floor (sub)) < (double) gateLen;
            const float t = open ? stepLevel : 0.f;
            gTarget = 1.f - depth * (1.f - t);
        }
        gateSm += (gTarget - gateSm) * gateCoef;
        xL *= gateSm; xR *= gateSm;

        // ---- drive ----
        if (drive > 0.001f)
        {
            const float k = 1.f + drive * 9.f, nk = 1.f / std::tanh (k);
            xL = std::tanh (xL * k) * nk; xR = std::tanh (xR * k) * nk;
        }

        // ---- bit / sample-rate crusher ----
        if (crushAmt > 0.001f)
        {
            if (++crushCnt >= ds)
            {
                crushCnt = 0;
                heldL = std::round (xL * levels) / levels;
                heldR = std::round (xR * levels) / levels;
            }
            xL = heldL; xR = heldR;
        }

        // ---- state-variable filter ----
        if ((i & 15) == 0)
        {
            cutS += (cutT - cutS) * 0.2f;
            const float fc = jlimit (20.f, 0.45f * (float) sr, cutS);
            const float g = std::tan (MathConstants<float>::pi * fc / (float) sr);
            fk = 1.f / q;
            fa1 = 1.f / (1.f + g * (g + fk)); fa2 = g * fa1; fa3 = g * fa2;
        }
        if (fltActive)
        {
            auto svf = [&] (SVF& f, float x)
            {
                const float v3 = x - f.s2;
                const float v1 = fa1 * f.s1 + fa2 * v3;
                const float v2 = f.s2 + fa2 * f.s1 + fa3 * v3;
                f.s1 = 2.f * v1 - f.s1; f.s2 = 2.f * v2 - f.s2;
                return ftype == 0 ? v2 : (ftype == 1 ? x - fk * v1 - v2 : fk * v1);
            };
            xL = svf (fl[0], xL); xR = svf (fl[1], xR);
        }

        // ---- PaulStretch (history is not written while frozen, so the frozen moment stays intact) ----
        if (! psFrozen)
        {
            psHist[0][(size_t) psW] = xL; psHist[1][(size_t) psW] = xR;
            if (++psW >= psH) psW = 0;
        }
        if (psActive)
        {
            if (psPos >= psHopN) psNextBlock (psSizeN, psStretch, psFrozen);
            xL = xL * (1.f - psMix) + psAcc[0][(size_t) psPos] * psMix;
            xR = xR * (1.f - psMix) + psAcc[1][(size_t) psPos] * psMix;
            ++psPos;
        }

        // ---- granular ----
        if (! frozen)
        {
            gBuf[0][(size_t) gW] = xL; gBuf[1][(size_t) gW] = xR;
            if (++gW >= gSize) gW = 0;
        }
        if (grMix > 0.0005f)
        {
            grainCountdown -= 1.f;
            if (grainCountdown <= 0.f)
            {
                spawnGrain (grSizeMs, pitch + seqPitchSt, grPos, grSpray);   // + pitch of the current sequencer step (if enabled)
                grainCountdown = (float) sr / grDens * (0.7f + 0.6f * rng.nextFloat());
            }
            float gl = 0.f, gr = 0.f;
            for (auto& g : grains)
            {
                if (! g.on) continue;
                const float w = 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * (float) g.age / (float) g.len);
                const int i0 = (int) g.pos;
                const float fr = (float) (g.pos - i0);
                const int i1 = (i0 + 1 >= gSize) ? 0 : i0 + 1;
                const float a = gBuf[0][(size_t) i0], b = gBuf[0][(size_t) i1];
                const float c = gBuf[1][(size_t) i0], d = gBuf[1][(size_t) i1];
                gl += (a + (b - a) * fr) * w * g.pl;
                gr += (c + (d - c) * fr) * w * g.pr;
                g.pos += g.inc;
                if (g.pos >= gSize) g.pos -= gSize;
                if (++g.age >= g.len) g.on = false;
            }
            xL = xL * (1.f - grMix) + gl * grainGain * grMix;
            xR = xR * (1.f - grMix) + gr * grainGain * grMix;
        }

        // ---- ping-pong delay ----
        dTime += (dTarget - dTime) * 0.0005f;
        float rp = (float) dW - dTime;
        if (rp < 0.f) rp += (float) dSize;
        const int d0 = (int) rp, d1 = (d0 + 1) % dSize;
        const float df = rp - (float) d0;
        const float dL = dBuf[0][(size_t) d0] + (dBuf[0][(size_t) d1] - dBuf[0][(size_t) d0]) * df;
        const float dR = dBuf[1][(size_t) d0] + (dBuf[1][(size_t) d1] - dBuf[1][(size_t) d0]) * df;
        dlpL += toneA * (std::tanh (dL * dlFb) - dlpL);
        dlpR += toneA * (std::tanh (dR * dlFb) - dlpR);
        dBuf[0][(size_t) dW] = 0.5f * (xL + xR) + dlpR;
        dBuf[1][(size_t) dW] = dlpL;
        if (++dW >= dSize) dW = 0;
        xL += dL * dlMix; xR += dR * dlMix;

        L[i] = xL; R[i] = xR;
    }

    // ---- reverb (with freeze) ----
    Reverb::Parameters rp;
    const float rvMix = P (RvMix);
    rp.roomSize = P (RvSize); rp.damping = P (RvDamp); rp.width = 1.f;
    rp.wetLevel = rvMix * 0.5f; rp.dryLevel = 0.5f * (1.f - 0.4f * rvMix);
    rp.freezeMode = pRvFreeze->load() > 0.5f ? 1.f : 0.f;
    reverb.setParameters (rp);
    reverb.processStereo (L, R, n);

    // ---- output level + soft safety limiter (protects PA / ears during live use) ----
    for (int i = 0; i < n; ++i)
    {
        for (auto* ch : { L, R })
        {
            float y = ch[i] * outLvl;
            const float a = std::abs (y);
            if (a > 0.9f) y = std::copysign (0.9f + 0.1f * std::tanh ((a - 0.9f) / 0.1f), y);
            ch[i] = y;
        }
    }

    loopPosUI = loopPos;
    loopLenUI = loopLen;
    curStep = step;
}

// ------------------------------- state -------------------------------------
void LiveFXProcessor::getStateInformation (MemoryBlock& dest)
{
    auto state = apvts.copyState();
    StringArray sa;
    for (auto& c : ccMap) sa.add (String (c.load()));
    state.setProperty ("ccMap", sa.joinIntoString (","), nullptr);
    { ScopedLock l (pathLock); for (int s = 0; s < kSlots; ++s) state.setProperty ("slotPath" + String (s), slotPath[s], nullptr); }
    { StringArray act; for (auto& a : actionMap) act.add (String (a.load())); state.setProperty ("actionMap", act.joinIntoString (","), nullptr); }
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void LiveFXProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto vt = ValueTree::fromXml (*xml);
        if (! vt.isValid()) return;
        auto sa = StringArray::fromTokens (vt.getProperty ("ccMap").toString(), ",", "");
        for (int i = 0; i < NumP && i < sa.size(); ++i) ccMap[(size_t) i] = sa[i].getIntValue();
        auto act = StringArray::fromTokens (vt.getProperty ("actionMap").toString(), ",", "");
        for (int i = 0; i < kActions && i < act.size(); ++i) actionMap[(size_t) i] = act[i].getIntValue();
        apvts.replaceState (vt);
        for (int s = 0; s < kSlots; ++s)   // reload the sounds assigned to keys
        {
            const String path = vt.getProperty ("slotPath" + String (s)).toString();
            if (path.isNotEmpty() && File (path).existsAsFile()) loadSlotFile (s, File (path));
        }
    }
}

AudioProcessorEditor* LiveFXProcessor::createEditor() { return new LiveFXEditor (*this); }
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LiveFXProcessor(); }
