#pragma once
#include "PluginProcessor.h"

inline const juce::Colour tkAccent  (0xff00e5ff);   // cyan
inline const juce::Colour tkAccent2 (0xffff2d95);   // pink

// Custom look: neon rotary knobs, rounded note buttons, dark theme
class TKLookAndFeel : public juce::LookAndFeel_V4
{
public:
    TKLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
};

// Live pitch display: the note being sung, the note it is tuned to, and a cents needle
class PitchMeter : public juce::Component
{
public:
    void setValues (float detected, float snapped, int transposeSemis);
    void paint (juce::Graphics&) override;

private:
    float detectedMidi = 0.0f, snappedMidi = 0.0f;
    int transpose = 0;
};

class AutoTuneTKAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer
{
public:
    explicit AutoTuneTKAudioProcessorEditor (AutoTuneTKAudioProcessor&);
    ~AutoTuneTKAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    AutoTuneTKAudioProcessor& proc;
    TKLookAndFeel lnf;

    PitchMeter meter;
    juce::ComboBox keyBox, scaleBox;
    juce::Label keyLabel, scaleLabel, notesLabel;
    juce::Slider knobs[6];
    juce::Label knobLabels[6];
    juce::ToggleButton noteButtons[12];

    juce::Rectangle<int> notePanel, knobPanel;
    float shownDetected = 0.0f, shownSnapped = 0.0f;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<ComboAtt>  keyAtt, scaleAtt;
    std::unique_ptr<SliderAtt> knobAtt[6];
    std::unique_ptr<ButtonAtt> noteAtt[12];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoTuneTKAudioProcessorEditor)
};
