#pragma once
#include "PluginProcessor.h"

// The window is laid out at this size and scaled to fit the real window (which can be resized with the corner handle).
constexpr int kDesignW = 1280, kDesignH = 1005;

// Rotary knob with a label, a MIDI-CC readout and right-click MIDI learn.
class LearnKnob : public juce::Component
{
public:
    LearnKnob (LiveFXProcessor& p, int paramIndex, const juce::String& label);
    void resized() override;
    void refresh();
    void mouseDown (const juce::MouseEvent&) override;

private:
    LiveFXProcessor& proc;
    int idx;
    juce::Slider slider { juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label name, cc;
    juce::AudioProcessorValueTreeState::SliderAttachment att;
};

// Piano keyboard with a zone strip on top. Drop a sound file on a key (or on a selected key range) to map it.
//  - drag on the empty strip: select a key range; drop a file on the selection = the sound covers that range
//  - drop on a key: single-key zone (several files: consecutive keys); drop on an existing zone: replace its sound
//  - drag the edge of a zone to resize it; right-click a zone: root key, mode, clear
//  - click the keys to play them
class ZoneKeyboard : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit ZoneKeyboard (LiveFXProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    enum DragMode { None, Select, ResizeLow, ResizeHigh, PlayKey };
    static constexpr float stripH = 38.f;

    float whiteW() const;
    float keyL (int n) const;
    float keyR (int n) const;
    int noteAt (float x, float y) const;
    int zoneAt (int note) const;
    int freeSlot() const;
    void zoneMenu (float x);

    LiveFXProcessor& proc;
    int whiteIdx[128] {};
    int whiteNotes[128] {};
    int numWhite = 0;
    DragMode mode = None;
    int selLow = -1, selHigh = -1, anchor = 0, dragSlot = -1, heldNote = -1, dragNote = -1;
};

class LiveFXEditor : public juce::AudioProcessorEditor,
                     public juce::FileDragAndDropTarget,
                     private juce::Timer
{
public:
    explicit LiveFXEditor (LiveFXProcessor&);
    ~LiveFXEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;
    void mouseDown (const juce::MouseEvent&) override;   // right-click a sequencer action = MIDI learn

private:
    // everything lives in this component, which is drawn at the design size and scaled to the window
    struct Content : juce::Component
    {
        std::function<void (juce::Graphics&)> onPaint;
        void paint (juce::Graphics& g) override { if (onPaint) onPaint (g); }
    };

    void timerCallback() override;
    void layoutContent();
    void paintContent (juce::Graphics&);
    void loadFile (const juce::File&);
    void loadSlot (int slot, const juce::File&);
    void updateEditMode();

    LiveFXProcessor& proc;
    Content content;
    std::unique_ptr<juce::FileChooser> chooser;

    std::vector<std::unique_ptr<LearnKnob>> owned;
    std::array<LearnKnob*, NumP> knobs {};
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
    std::vector<std::unique_ptr<juce::ComboBox>> combos;
    std::vector<std::unique_ptr<juce::Slider>> sliders;
    std::unique_ptr<ZoneKeyboard> keyboard;

    juce::TextButton *recBtn {}, *stopBtn {}, *clearBtn {}, *revBtn {}, *loadBtn {}, *grFreezeBtn {}, *keysBtn {},
                     *dlSyncBtn {}, *rvFreezeBtn {}, *stutBtn {}, *seqOnBtn {}, *psFreezeBtn {};
    juce::ComboBox *dlDivCb {}, *fltTypeCb {}, *stutDivCb {}, *seqRateCb {}, *psSizeCb {};
    juce::TextButton* stepBtn[kSteps] {};
    juce::Slider* stepSld[kSteps] {};
    juce::Slider* stepPit[kSteps] {};
    juce::TextButton* seqActBtn[kActions] {};
    juce::TextButton *editModeBtn {}, *seqPitchBtn {};
    juce::ComboBox *seqDirCb {}, *seqRatchetCb {};
    bool stutterDown = false;
    std::array<juce::TextButton*, kSlots> slotLoad {}, slotKey {}, slotClear {}, slotGrab {};
    std::array<juce::ComboBox*, kSlots> slotMode {};
    std::array<juce::Slider*, kSlots> slotLvl {};

    juce::Rectangle<int> looperR { 10, 10, 620, 250 }, granR { 640, 10, 630, 250 },
                         delayR { 10, 270, 330, 170 }, reverbR { 350, 270, 250, 170 },
                         fxR { 610, 270, 330, 170 }, paulR { 950, 270, 320, 170 },
                         seqR { 10, 450, 1260, 250 }, sampR { 10, 710, 1260, 285 }, waveR;

    // attachments are declared last so they are destroyed before the components they point to
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> bAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> cAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveFXEditor)
};
