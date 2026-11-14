#pragma once
#include "PluginProcessor.h"

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
    void timerCallback() override;
    void loadFile (const juce::File&);
    void loadSlot (int slot, const juce::File&);
    void updateEditMode();
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TextButton* loadBtn {};

    LiveFXProcessor& proc;

    std::vector<std::unique_ptr<LearnKnob>> owned;
    std::array<LearnKnob*, NumP> knobs {};
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
    std::vector<std::unique_ptr<juce::ComboBox>> combos;
    std::vector<std::unique_ptr<juce::Slider>> sliders;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> bAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> cAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sAtt;

    juce::TextButton *recBtn {}, *stopBtn {}, *clearBtn {}, *revBtn {}, *grFreezeBtn {}, *keysBtn {},
                     *dlSyncBtn {}, *rvFreezeBtn {}, *stutBtn {}, *seqOnBtn {};
    juce::ComboBox *dlDivCb {}, *fltTypeCb {}, *stutDivCb {}, *seqRateCb {};
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

    juce::Rectangle<int> looperR { 10, 10, 520, 250 }, granR { 540, 10, 550, 250 },
                         delayR { 10, 270, 360, 170 }, reverbR { 380, 270, 270, 170 },
                         fxR { 660, 270, 430, 170 }, seqR { 10, 450, 1080, 250 },
                         sampR { 10, 710, 1080, 160 }, waveR;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveFXEditor)
};
