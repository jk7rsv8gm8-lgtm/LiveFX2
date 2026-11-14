#pragma once
#include <JuceHeader.h>

// ---------------------------------------------------------------------------
//  Performance settings - edit these to match your MiniLab 3 setup
// ---------------------------------------------------------------------------
constexpr int kPadChannel  = 10;   // MiniLab 3 pads send on MIDI channel 10 by default
constexpr int kPadBaseNote = 36;   // pads: notes 36-43 (bank A) and 44-51 (bank B) -> pads 1-8
constexpr int kSteps       = 16;
constexpr int kPeaks       = 256;

// Continuous parameters. 'cc' = default MIDI CC (-1 = none). Every one of them can be re-mapped
// with MIDI learn (right-click a knob in the plugin window).
struct PDef { const char* id; const char* name; float lo, hi, def, mid; int cc; };

enum PIdx { LoopLevel, GrMix, GrSize, GrPitch, GrDens, GrPos, GrSpray,
            DlMix, DlTime, DlFb, DlTone, RvMix, RvSize, RvDamp,
            FltCut, FltRes, Crush, Drive, SeqDepth, OutLevel, NumP };

inline constexpr PDef kParams[NumP] = {
    { "loopLevel", "Loop Level",     0.f,   1.f,    0.8f,   -1.f,   -1 },
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
    { "seqDepth",  "Seq Depth",      0.f,   1.f,    1.f,    -1.f,   -1 },
    { "outLevel",  "Output",         0.f,   1.5f,   1.f,    -1.f,   -1 },
};

enum LoopState { LEmpty, LRec, LPlay, LDub, LStopped };

class LiveFXProcessor : public juce::AudioProcessor
{
public:
    LiveFXProcessor();

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

private:
    juce::AudioFormatManager formats;
    juce::SpinLock pendingLock;
    juce::AudioBuffer<float> pendingBuf;
    std::atomic<bool> hasPending { false };

    void handleMidi (juce::MidiBuffer&);
    void padEvent (int idx, bool down);
    void toggleParam (const char* id);
    void loopPress();
    void loopStopPlay();
    void loopClear();
    void computePeaks();
    void startStutter();
    void spawnGrain (float sizeMs, float pitch, float posKnob, float spray);

    double sr = 44100.0, curBpm = 120.0;

    std::atomic<float>* pv[NumP] {};
    std::atomic<float> *pFltType {}, *pDlDiv {}, *pSeqRate {}, *pStutDiv {}, *pDlSync {},
                       *pGrFreeze {}, *pRvFreeze {}, *pSeqOn {}, *pKeys {}, *pLoopRev {};
    std::atomic<float> *stepOn[kSteps] {}, *stepLvl[kSteps] {};
    std::atomic<int> keyOffset { 0 };

    juce::AbstractFifo fifo { 64 };
    int fifoData[64] {};

    // looper
    std::vector<float> loopBuf[2];
    int loopMax = 0, loopLen = 0, loopPos = 0;

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
    int step = 0;
    double accum = 0.0;
    bool lastSeqOn = false;
    float gateSm = 1.f;

    juce::Reverb reverb;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveFXProcessor)
};
