#include "PluginEditor.h"

using namespace juce;

//==============================================================================
TKLookAndFeel::TKLookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, Colour (0xff0b0b12));
    setColour (Label::textColourId, Colour (0xffb8b8cc));
    setColour (Slider::textBoxTextColourId, Colours::white);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (ComboBox::backgroundColourId, Colour (0xff1d1d2a));
    setColour (ComboBox::outlineColourId, Colour (0xff343448));
    setColour (ComboBox::textColourId, Colours::white);
    setColour (ComboBox::arrowColourId, tkAccent);
    setColour (PopupMenu::backgroundColourId, Colour (0xff15151f));
    setColour (PopupMenu::textColourId, Colours::white);
    setColour (PopupMenu::highlightedBackgroundColourId, Colour (0xff00a8bd));
    setColour (PopupMenu::highlightedTextColourId, Colours::black);
}

void TKLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height,
                                      float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                      Slider&)
{
    auto bounds = Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (8.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const float trackW = 5.0f;
    const float arcR = radius - trackW;

    // track
    Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (Colour (0xff262634));
    g.strokePath (track, PathStrokeType (trackW, PathStrokeType::curved, PathStrokeType::rounded));

    // value arc with gradient
    Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, rotaryStartAngle, angle, true);
    g.setGradientFill (ColourGradient (tkAccent2, centre.x - radius, centre.y,
                                       tkAccent,  centre.x + radius, centre.y, false));
    g.strokePath (value, PathStrokeType (trackW, PathStrokeType::curved, PathStrokeType::rounded));

    // knob body
    const float bodyR = radius - trackW * 2.4f;
    g.setGradientFill (ColourGradient (Colour (0xff343446), centre.x, centre.y - bodyR,
                                       Colour (0xff13131b), centre.x, centre.y + bodyR, false));
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour (Colour (0xff3d3d52));
    g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

    // pointer
    Path pointer;
    pointer.addRoundedRectangle (-1.8f, -bodyR + 3.0f, 3.6f, bodyR * 0.42f, 1.8f);
    g.setColour (Colours::white);
    g.fillPath (pointer, AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

void TKLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b,
                                      bool shouldDrawButtonAsHighlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (3.0f);
    const bool on = b.getToggleState();
    const bool isSharp = (bool) b.getProperties()["black"];

    Colour base = on ? (isSharp ? tkAccent2 : tkAccent)
                     : Colour (0xff23232f);
    if (shouldDrawButtonAsHighlighted)
        base = base.brighter (0.15f);

    g.setColour (base);
    g.fillRoundedRectangle (r, 7.0f);

    g.setColour (on ? Colours::black : Colour (0xff8d8da6));
    g.setFont (14.0f);
    g.drawText (b.getButtonText(), b.getLocalBounds(), Justification::centred);
}

//==============================================================================
static String noteName (int midi)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int m = jmax (0, midi);
    return String (names[m % 12]) + String (m / 12 - 1);
}

void PitchMeter::setValues (float detected, float snapped, int transposeSemis)
{
    detectedMidi = detected;
    snappedMidi = snapped;
    transpose = transposeSemis;
    repaint();
}

void PitchMeter::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colour (0xff13131d));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (Colour (0xff262634));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);

    auto area = r.reduced (18.0f, 10.0f);
    auto textArea = area.removeFromTop (area.getHeight() * 0.62f);
    auto barArea = area.reduced (0.0f, 2.0f);

    if (detectedMidi <= 0.0f)
    {
        g.setColour (Colour (0xff6a6a80));
        g.setFont (22.0f);
        g.drawText ("NO PITCH", textArea, Justification::centred);
    }
    else
    {
        const int singing = (int) std::round (detectedMidi);
        const int target  = (int) std::round (snappedMidi) + transpose;

        auto left = textArea.removeFromLeft (textArea.getWidth() * 0.5f);
        auto right = textArea;

        g.setColour (Colour (0xff6a6a80));
        g.setFont (11.0f);
        g.drawText ("SINGING", left.removeFromTop (14.0f), Justification::centred);
        g.drawText ("TUNED TO", right.removeFromTop (14.0f), Justification::centred);

        g.setFont (44.0f);
        g.setColour (Colours::white);
        g.drawText (noteName (singing), left, Justification::centred);
        g.setColour (tkAccent);
        g.drawText (noteName (target), right, Justification::centred);
    }

    // cents bar
    const float cy = barArea.getCentreY();
    const float halfW = barArea.getWidth() * 0.5f;
    const float cx = barArea.getCentreX();

    g.setColour (Colour (0xff2a2a3a));
    g.fillRoundedRectangle (barArea.getX(), cy - 2.0f, barArea.getWidth(), 4.0f, 2.0f);

    for (int c = -50; c <= 50; c += 10)
    {
        const float x = cx + ((float) c / 50.0f) * halfW;
        const bool centre = (c == 0);
        g.setColour (centre ? Colours::white.withAlpha (0.85f) : Colour (0xff3a3a4e));
        g.fillRect (x - 1.0f, cy - (centre ? 10.0f : 6.0f), 2.0f, centre ? 20.0f : 12.0f);
    }

    if (detectedMidi > 0.0f)
    {
        const float cents = jlimit (-50.0f, 50.0f, (detectedMidi - snappedMidi) * 100.0f);
        const float x = cx + (cents / 50.0f) * halfW;
        g.setColour (std::abs (cents) < 12.0f ? Colour (0xff3dff8a) : tkAccent2);
        g.fillEllipse (x - 7.0f, cy - 7.0f, 14.0f, 14.0f);
    }
}

//==============================================================================
AutoTuneTKAudioProcessorEditor::AutoTuneTKAudioProcessorEditor (AutoTuneTKAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    setSize (760, 500);

    addAndMakeVisible (meter);

    // key + scale
    keyBox.addItemList (AutoTuneTKAudioProcessor::keyNames(), 1);
    scaleBox.addItemList (AutoTuneTKAudioProcessor::scaleNames(), 1);
    addAndMakeVisible (keyBox);
    addAndMakeVisible (scaleBox);
    keyAtt   = std::make_unique<ComboAtt> (proc.apvts, "key", keyBox);
    scaleAtt = std::make_unique<ComboAtt> (proc.apvts, "scale", scaleBox);

    keyLabel.setText ("KEY", dontSendNotification);
    scaleLabel.setText ("SCALE", dontSendNotification);
    notesLabel.setText ("ALLOWED NOTES  (click a note to turn it off)", dontSendNotification);
    addAndMakeVisible (keyLabel);
    addAndMakeVisible (scaleLabel);
    addAndMakeVisible (notesLabel);

    // note buttons
    static const char* noteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const bool sharp[12] = { false, true, false, true, false, false, true, false, true, false, true, false };
    for (int i = 0; i < 12; ++i)
    {
        noteButtons[i].setButtonText (noteNames[i]);
        noteButtons[i].getProperties().set ("black", sharp[i]);
        addAndMakeVisible (noteButtons[i]);
        noteAtt[i] = std::make_unique<ButtonAtt> (proc.apvts, "note" + String (i), noteButtons[i]);
    }

    // knobs
    static const char* ids[6]   = { "speed", "humanize", "amount", "transpose", "mix", "gain" };
    static const char* names[6] = { "RETUNE SPEED", "HUMANIZE", "AMOUNT", "TRANSPOSE", "MIX", "OUTPUT" };
    for (int i = 0; i < 6; ++i)
    {
        auto& s = knobs[i];
        s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (Slider::TextBoxBelow, false, 80, 20);
        s.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
        addAndMakeVisible (s);
        knobAtt[i] = std::make_unique<SliderAtt> (proc.apvts, ids[i], s);

        knobLabels[i].setText (names[i], dontSendNotification);
        knobLabels[i].setJustificationType (Justification::centred);
        addAndMakeVisible (knobLabels[i]);
    }

    startTimerHz (30);
}

AutoTuneTKAudioProcessorEditor::~AutoTuneTKAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AutoTuneTKAudioProcessorEditor::timerCallback()
{
    const float d = proc.detectedMidi.load();
    const float s = proc.snappedMidi.load();

    if (d <= 0.0f)
        shownDetected = 0.0f;
    else
        shownDetected = (shownDetected <= 0.0f) ? d : shownDetected + (d - shownDetected) * 0.5f;

    shownSnapped = s;
    const int tr = (int) proc.apvts.getRawParameterValue ("transpose")->load();
    meter.setValues (shownDetected, shownSnapped, tr);
}

void AutoTuneTKAudioProcessorEditor::paint (Graphics& g)
{
    g.setGradientFill (ColourGradient (Colour (0xff141420), 0.0f, 0.0f,
                                       Colour (0xff07070c), 0.0f, (float) getHeight(), false));
    g.fillAll();

    // header
    g.setColour (Colours::white);
    g.setFont (30.0f);
    g.drawText ("AUTOTUNE TK", 20, 12, 300, 38, Justification::centredLeft);

    g.setGradientFill (ColourGradient (tkAccent2, 20.0f, 0.0f, tkAccent, 200.0f, 0.0f, false));
    g.fillRoundedRectangle (22.0f, 52.0f, 180.0f, 3.0f, 1.5f);

    g.setColour (Colour (0xff6a6a80));
    g.setFont (13.0f);
    g.drawText ("PITCH CORRECTION", getWidth() - 220, 18, 200, 24, Justification::centredRight);

    // panels
    g.setColour (Colour (0xff10101a));
    g.fillRoundedRectangle (notePanel.toFloat(), 12.0f);
    g.fillRoundedRectangle (knobPanel.toFloat(), 12.0f);
    g.setColour (Colour (0xff22222f));
    g.drawRoundedRectangle (notePanel.toFloat().reduced (0.5f), 12.0f, 1.0f);
    g.drawRoundedRectangle (knobPanel.toFloat().reduced (0.5f), 12.0f, 1.0f);
}

void AutoTuneTKAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (20);
    r.removeFromTop (46); // header

    // key / scale on the left, meter on the right
    auto top = r.removeFromTop (130);
    auto left = top.removeFromLeft (210);
    keyLabel.setBounds (left.removeFromTop (18));
    keyBox.setBounds (left.removeFromTop (32));
    left.removeFromTop (8);
    scaleLabel.setBounds (left.removeFromTop (18));
    scaleBox.setBounds (left.removeFromTop (32));
    top.removeFromLeft (16);
    meter.setBounds (top);

    r.removeFromTop (12);

    // note buttons
    notesLabel.setBounds (r.removeFromTop (20));
    auto notes = r.removeFromTop (52);
    notePanel = notes;
    auto noteRow = notes.reduced (6, 4);
    const int nw = noteRow.getWidth() / 12;
    for (int i = 0; i < 12; ++i)
        noteButtons[i].setBounds (noteRow.removeFromLeft (nw));

    r.removeFromTop (12);

    // knobs
    knobPanel = r;
    auto knobRow = r.reduced (6, 8);
    const int kw = knobRow.getWidth() / 6;
    for (int i = 0; i < 6; ++i)
    {
        auto cell = knobRow.removeFromLeft (kw);
        knobLabels[i].setBounds (cell.removeFromTop (20));
        knobs[i].setBounds (cell);
    }
}
