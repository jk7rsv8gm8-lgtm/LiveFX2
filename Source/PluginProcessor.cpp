#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

static const float kDlBeats[4]   = { 1.f, 0.5f, 0.75f, 0.25f };   // 1/4, 1/8, 1/8., 1/16
static const float kSeqBeats[4]  = { 1.f, 0.5f, 0.25f, 0.125f };  // 1/4, 1/8, 1/16, 1/32
static const float kStutBeats[4] = { 1.f, 0.5f, 0.25f, 0.125f };

static AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout l;

    for (auto& p : kParams)
    {
        NormalisableRange<float> r (p.lo, p.hi);
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
    for (int s = 0; s < kSteps; ++s)
    {
        stepOn[s]  = apvts.getRawParameterValue ("s" + String (s + 1));
        stepLvl[s] = apvts.getRawParameterValue ("l" + String (s + 1));
    }
    for (auto& p : peaks) p = 0.f;
    formats.registerBasicFormats();
}

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
        case LStopped: loopPos = 0; loopState = LPlay; break;
    }
}

void LiveFXProcessor::loopStopPlay()
{
    switch (loopState.load())
    {
        case LRec:     loopPress(); break;                       // finish recording, start playing
        case LPlay:    loopState = LStopped; break;
        case LDub:     loopState = LStopped; computePeaks(); break;
        case LStopped: loopPos = 0; loopState = LPlay; break;
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

void LiveFXProcessor::sendPad (int idx, bool down)
{
    int s1, n1, s2, n2;
    fifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0) { fifoData[s1] = idx * 2 + (down ? 1 : 0); fifo.finishedWrite (1); }
}

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
            for (int i = 0; i < NumP; ++i)
                if (ccMap[(size_t) i].load() == cc)
                    if (auto* p = apvts.getParameter (kParams[i].id)) p->setValueNotifyingHost (v);
        }
        else if (m.isNoteOn())
        {
            int note = m.getNoteNumber();
            if (m.getChannel() == kPadChannel)
            {
                if (note >= kPadBaseNote && note < kPadBaseNote + 16) padEvent ((note - kPadBaseNote) & 7, true);
            }
            else keyOffset = jlimit (-24, 24, note - 60);   // keys -> grain pitch (middle C = 0)
        }
        else if (m.isNoteOff() && m.getChannel() == kPadChannel)
        {
            int note = m.getNoteNumber();
            if (note >= kPadBaseNote && note < kPadBaseNote + 16) padEvent ((note - kPadBaseNote) & 7, false);
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

    handleMidi (midi);
    while (fifo.getNumReady() > 0)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (1, s1, n1, s2, n2);
        if (n1 <= 0) break;
        int v = fifoData[s1];
        fifo.finishedRead (1);
        padEvent (v >> 1, (v & 1) != 0);
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
    const float outLvl = P (OutLevel);
    const float gateCoef = 1.f - std::exp (-1.f / (0.002f * (float) sr));
    bool sOn[kSteps]; float sLv[kSteps];
    for (int s = 0; s < kSteps; ++s) { sOn[s] = stepOn[s]->load() > 0.5f; sLv[s] = stepLvl[s]->load(); }

    if (seqOn && ! lastSeqOn) { step = 0; accum = 0.0; }
    lastSeqOn = seqOn;

    const float grainGain = 1.f / std::sqrt (jmax (1.f, grDens * grSizeMs * 0.001f));
    const float bits = 16.f - crushAmt * 12.f;
    const float levels = std::pow (2.f, bits - 1.f);
    const int ds = 1 + (int) (crushAmt * crushAmt * 24.f);

    auto* L = buf.getWritePointer (0);
    auto* R = buf.getWritePointer (1);

    for (int i = 0; i < n; ++i)
    {
        const float inL = L[i], inR = R[i];

        // ---- looper (records dry input, plays back into the FX chain) ----
        float lpL = 0.f, lpR = 0.f;
        const int st = loopState.load (std::memory_order_relaxed);
        if (st == LRec)
        {
            loopBuf[0][(size_t) loopPos] = inL;
            loopBuf[1][(size_t) loopPos] = inR;
            if (++loopPos >= loopMax) { loopLen = loopMax; loopPos = 0; loopState = LPlay; computePeaks(); }
        }
        else if ((st == LPlay || st == LDub) && loopLen > 0)
        {
            const size_t idx = (size_t) (rev ? loopLen - 1 - loopPos : loopPos);
            lpL = loopBuf[0][idx]; lpR = loopBuf[1][idx];
            if (st == LDub)
            {
                loopBuf[0][idx] = std::tanh (lpL + inL);
                loopBuf[1][idx] = std::tanh (lpR + inR);
            }
            if (++loopPos >= loopLen) loopPos = 0;
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
            if (accum >= stepSamples) { accum -= stepSamples; step = (step + 1) % kSteps; }
            const float t = sOn[step] ? sLv[step] : 0.f;
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
                spawnGrain (grSizeMs, pitch, grPos, grSpray);
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
        apvts.replaceState (vt);
    }
}

AudioProcessorEditor* LiveFXProcessor::createEditor() { return new LiveFXEditor (*this); }
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LiveFXProcessor(); }
