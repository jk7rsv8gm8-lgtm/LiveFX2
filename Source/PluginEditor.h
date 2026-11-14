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

private:
    void timerCallback() override;
    void loadFile (const juce::File&);
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
    bool stutterDown = false;

    juce::Rectangle<int> looperR { 10, 10, 520, 250 }, granR { 540, 10, 550, 250 },
                         delayR { 10, 270, 360, 190 }, reverbR { 380, 270, 270, 190 },
                         fxR { 660, 270, 430, 190 }, seqR { 10, 470, 1080, 240 }, waveR;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveFXEditor)
};
