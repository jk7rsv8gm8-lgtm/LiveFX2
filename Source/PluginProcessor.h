#pragma once
#include <JuceHeader.h>
#include <complex>

// ---------------------------------------------------------------------------
//  Performance settings - edit these to match your MiniLab 3 setup
// ---------------------------------------------------------------------------
constexpr int kPadChannel  = 10;   // MiniLab 3 pads send on MIDI channel 10 by default
constexpr int kPadBaseNote = 36;   // pad bank A: notes 36-43 = looper/FX actions; pad bank B: notes 44-51 = GRAB loop into key slot 1-8
constexpr int kSteps       = 16;
constexpr int kPeaks       = 256;
constexpr int kSlots       = 8;    // key-sampler slots
constexpr int kVoices      = 16;   // key-sampler polyphony
constexpr int kNoteLo      = 24;   // lowest / highest key shown on the on-screen piano
constexpr int kNoteHi      = 96;
constexpr int kActions     = 6;    // sequencer one-shot actions (randomize, mutate, shift, clear, fill)
inline constexpr const char* kActionNames[kActions] = { "RANDOMIZE", "MUTATE", "SHIFT <", "SHIFT >", "CLEAR", "FILL" };

// Continuous parameters. 'cc' = default MIDI CC (-1 = none). Every one of them can be re-mapped
// with MIDI learn (right-click a knob in the plugin window).
struct PDef { const char* id; const char* name; float lo, hi, def, mid; int cc; };

enum PIdx { LoopLevel, LoopStart, LoopEnd, GrMix, GrSize, GrPitch, GrDens, GrPos, GrSpray,
            DlMix, DlTime, DlFb, DlTone, RvMix, RvSize, RvDamp,
            FltCut, FltRes, Crush, Drive, PsMix, PsStretch, SeqDepth, SeqLen, SeqSwing, SeqGate, SeqProb, OutLevel, NumP };

inline constexpr PDef kParams[NumP] = {
    { "loopLevel", "Loop Level",     0.f,   1.f,    0.8f,   -1.f,   -1 },
    { "loopStart", "Loop Start",     0.f,   1.f,    0.f,    -1.f,   -1 },
    { "loopEnd",   "Loop End",       0.f,   1.f,    1.f,    -1.f,   -1 },
    { "grMix",     "Grain Mix",      0.f,   1.f,    0.f,    -1.f,   74 },
    { "grSize",    "Grain Size",     10.f,  500.f,  120.f,  100.f,  71 },
    { "grPitch",   "Grain Pitch",    -24.f, 24.f,   0.f,    -1.f,   76 },
    { "grDens",    "Grain Density",  1.f,   80.f,   20.f,   15.f,   -1 },
    { "grPos",     "Grain Position", 0.f,   1.f,    0.3f,   -1.f,   -1 },
    { "grSpray",   "Grain Spray",    0.f,   1.f,    0.2f,   -1.f,   -1 },
    { "dlMix",     "Delay Mix",      0.f,   1.f,    0.f,    -1.f,   77 },
    { "dlTime",    "Delay Time",     20.f,  2000.f, 375.f,  400.f,  -1 },
    { "dlFb",      "Delay Feedback", 0.f,   0.95f,  0.4f,   -1.f,   93 },
    { "dlTone",    "Delay Tone",     500.f, 16000.f,6000.f, 3000.f, -1 },
    { "rvMix",     "Reverb Mix",     0.f,   1.f,    0.f,    -1.f,   18 },
    { "rvSize",    "Reverb Size",    0.f,   1.f,    0.6f,   -1.f,   -1 },
    { "rvDamp",    "Reverb Damp",    0.f,   1.f,    0.5f,   -1.f,   -1 },
    { "fltCut",    "Filter Cutoff",  20.f,  20000.f,20000.f,1000.f, 19 },
    { "fltRes",    "Filter Reso",    0.5f,  10.f,   0.7f,   -1.f,   -1 },
    { "crush",     "Crusher",        0.f,   1.f,    0.f,    -1.f,   16 },
    { "drive",     "Drive",          0.f,   1.f,    0.f,    -1.f,   -1 },
    { "psMix",     "PaulStretch Mix",0.f,   1.f,    0.f,    -1.f,   -1 },
    { "psStretch", "Stretch",        1.f,   100.f,  8.f,    10.f,   -1 },
    { "seqDepth",  "Seq Depth",      0.f,   1.f,    1.f,    -1.f,   -1 },
    { "seqLen",    "Seq Length",     1.f,   16.f,   16.f,   -1.f,   -1 },
    { "seqSwing",  "Swing",          0.f,   1.f,    0.f,    -1.f,   -1 },
    { "seqGate",   "Gate Length",    0.05f, 1.f,    1.f,    -1.f,   -1 },
    { "seqProb",   "Probability",    0.f,   1.f,    1.f,    -1.f,   -1 },
    { "outLevel",  "Output",         0.f,   1.5f,   1.f,    -1.f,   -1 },
};

enum LoopState { LEmpty, LRec, LPlay, LDub, LStopped };

class LiveFXProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    LiveFXProcessor();
    ~LiveFXProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "LiveFX Performer"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- shared with the UI ----
    juce::AudioProcessorValueTreeState apvts;
    std::array<std::atomic<int>, NumP> ccMap;
    std::atomic<int> learnIdx { -1 };
    std::atomic<int> loopState { LEmpty }, loopPosUI { 0 }, loopLenUI { 0 }, curStep { 0 };
    std::atomic<double> sampleRateUI { 44100.0 };
    std::array<std::atomic<float>, kPeaks> peaks;

    void sendPad (int padIndex, bool down);   // called from the UI thread

    // Loads an audio file (wav/aiff/flac/ogg/mp3) into the looper and starts playing it.
    // Message thread only. Returns false if the file could not be read.
    bool loadAudioFile (const juce::File&);

    // ---- key sampler: assign sounds to keys ----
    bool loadSlotFile (int slot, const juce::File&);     // message thread
    void clearSlot (int slot);                           // message thread
    juce::String getSlotName (int slot) const;
    // key zones: every slot plays its sound on the keys low..high (pitched relative to root in "Pitched" mode)
    int getSlotLow (int s) const  { return (int) pSlotLow[s]->load(); }
    int getSlotHigh (int s) const { return (int) pSlotHigh[s]->load(); }
    int getSlotRoot (int s) const { return (int) pSlotRoot[s]->load(); }
    int getSlotMode (int s) const { return (int) pSlotMode[s]->load(); }
    bool isSlotLoaded (int s) const { return slotHas[s].load(); }
    void setSlotZone (int slot, int low, int high, int root, int mode = -1);   // sets parameters only (any thread)
    void previewNote (int note, bool down);                                    // on-screen piano -> sampler
    std::atomic<bool> noteHeld[128] {};                                        // keys held on the MIDI keyboard
    std::atomic<int> keyLearnSlot { -1 };                // slot waiting for a key press
    void grabLoopToSlot (int slot);                      // copy the current loop into a key slot (from the UI)

    // ---- sequencer actions (randomize, mutate, shift, clear, fill) ----
    void runSeqAction (int action);                      // message thread
    std::array<std::atomic<int>, kActions> actionMap;    // -1 none, 0..127 = CC number, 128+n = key n
    std::atomic<int> actionLearn { -1 };

private:
    juce::AudioFormatManager formats;
    juce::SpinLock pendingLock;
    juce::AudioBuffer<float> pendingBuf;
    std::atomic<bool> hasPending { false };

    // key sampler data. slotBuf/slotRate/slotLen belong to the audio thread; new samples arrive via pending*.
    std::atomic<float> *pSlotLow[kSlots] {}, *pSlotHigh[kSlots] {}, *pSlotRoot[kSlots] {}, *pSlotMode[kSlots] {}, *pSlotLvl[kSlots] {};
    std::atomic<bool> slotHas[kSlots] {};
    juce::AudioBuffer<float> slotBuf[kSlots], pendingSlotBuf[kSlots];
    double slotRate[kSlots] {}, pendingSlotRate[kSlots] {};
    int slotLen[kSlots] {};
    std::atomic<bool> slotPending[kSlots] {};
    juce::SpinLock slotLock;
    struct Voice { bool active = false, held = false, releasing = false; int slot = 0, note = 0, mode = 0;
                   double pos = 0.0, inc = 1.0; float env = 0.f, gain = 1.f; };
    std::array<Voice, kVoices> voices;
    int stealIdx = 0;
    float atkStep = 0.01f, relStep = 0.002f;
    juce::String slotPath[kSlots];
    juce::String slotLabel[kSlots];
    mutable juce::CriticalSection pathLock;
    std::vector<float> capBuf[2];
    int capLen = 0, capSlot = 0;
    std::atomic<bool> capReady { false };

    void handleMidi (juce::MidiBuffer&);
    void padEvent (int idx, bool down);
    void toggleParam (const char* id);
    void loopPress();
    void loopStopPlay();
    void loopClear();
    void computePeaks();
    void startStutter();
    void spawnGrain (float sizeMs, float pitch, float posKnob, float spray);
    bool sampleNoteOn (int note, float vel);
    void releaseNote (int note);
    void psReset (int sizeIdx);
    void psNextBlock (int N, float stretch, bool frozen);
    void pushCmd (int v);
    void captureLoop (int slot);      // audio thread: copy the loop into the staging buffer
    void pollCapture();               // message thread: turn the staging buffer into a key sound
    void timerCallback() override;
    int actionForNote (int note) const { for (int a = 0; a < kActions; ++a) if (actionMap[(size_t) a].load() == 128 + note) return a; return -1; }

    double sr = 44100.0, curBpm = 120.0;

    std::atomic<float>* pv[NumP] {};
    std::atomic<float> *pFltType {}, *pDlDiv {}, *pSeqRate {}, *pStutDiv {}, *pDlSync {},
                       *pGrFreeze {}, *pRvFreeze {}, *pSeqOn {}, *pKeys {}, *pLoopRev {};
    std::atomic<float> *stepOn[kSteps] {}, *stepLvl[kSteps] {}, *stepPit[kSteps] {};
    std::atomic<float> *pSeqDir {}, *pSeqRatchet {}, *pSeqPitch {};
    std::atomic<unsigned> pendingActions { 0 };
    bool ccHigh[128] {};
    std::atomic<int> keyOffset { 0 };

    juce::AbstractFifo fifo { 64 };
    int fifoData[64] {};

    // looper
    std::vector<float> loopBuf[2];
    int loopMax = 0, loopLen = 0, loopPos = 0, loopStartIdx = 0, loopEndIdx = 0;

    // history for stutter
    std::vector<float> hBuf[2];
    int hSize = 0, hW = 0, stutStart = 0, stutLen = 1, stutPhase = 0;
    bool stutActive = false;

    // granular
    struct Grain { bool on = false; double pos = 0, inc = 1; int age = 0, len = 1; float pl = 1, pr = 1; };
    std::array<Grain, 48> grains;
    std::vector<float> gBuf[2];
    int gSize = 0, gW = 0;
    float grainCountdown = 0.f;
    juce::Random rng;

    // delay
    std::vector<float> dBuf[2];
    int dSize = 0, dW = 0;
    float dTime = 22050.f, dlpL = 0.f, dlpR = 0.f;

    // filter / crusher
    struct SVF { float s1 = 0.f, s2 = 0.f; } fl[2];
    float fa1 = 0.f, fa2 = 0.f, fa3 = 0.f, fk = 1.f, cutS = 20000.f;
    int crushCnt = 0;
    float heldL = 0.f, heldR = 0.f;

    // sequencer
    int step = 0, tickCount = 0, pingDir = 1;
    double accum = 0.0, curLen = 1000.0;
    bool lastSeqOn = false, stepActive = true;
    float gateSm = 1.f, stepLevel = 1.f, seqPitchSt = 0.f;

    // PaulStretch: random-phase FFT time stretch of the last seconds of audio (window sizes 2048..16384)
    std::vector<std::unique_ptr<juce::dsp::FFT>> psFft;
    std::vector<float> psHist[2], psAcc[2], psWin;
    std::vector<std::complex<float>> psSpecIn, psSpecOut;
    int psH = 0, psW = 0, psPos = 0, psSizeIdxLast = -1;
    double psRead = 0.0;
    bool psWasActive = false;
    std::atomic<float> *pPsSize {}, *pPsFreeze {};

    juce::Reverb reverb;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveFXProcessor)
};
