#include "PluginProcessor.h"
#include "PluginEditor.h"

AutoTuneTKAudioProcessor::AutoTuneTKAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pKey       = apvts.getRawParameterValue ("key");
    pScale     = apvts.getRawParameterValue ("scale");
    pSpeed     = apvts.getRawParameterValue ("speed");
    pAmount    = apvts.getRawParameterValue ("amount");
    pHumanize  = apvts.getRawParameterValue ("humanize");
    pTranspose = apvts.getRawParameterValue ("transpose");
    pMix       = apvts.getRawParameterValue ("mix");
    pGain      = apvts.getRawParameterValue ("gain");
    for (int i = 0; i < 12; ++i)
        pNote[i] = apvts.getRawParameterValue ("note" + juce::String (i));
}

juce::AudioProcessorValueTreeState::ParameterLayout AutoTuneTKAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; };
    auto db  = [] (float v, int) { return juce::String (v, 1) + " dB"; };

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "key", 1 }, "Key", keyNames(), 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "scale", 1 }, "Scale", scaleNames(), 1));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "speed", 1 }, "Retune Speed",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 80.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "humanize", 1 }, "Humanize",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "amount", 1 }, "Amount",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "transpose", 1 }, "Transpose", -12, 12, 0));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mix", 1 }, "Dry/Wet",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gain", 1 }, "Output",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (db)));

    const auto names = keyNames();
    for (int i = 0; i < 12; ++i)
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "note" + juce::String (i), 1 }, "Note " + names[i], true));

    return layout;
}

void AutoTuneTKAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    for (auto& s : shifter)
        s.prepare (sampleRate);

    ring.assign ((size_t) windowSize, 0.0f);
    window.assign ((size_t) windowSize, 0.0f);
    cmnd.assign ((size_t) windowSize / 2 + 4, 1.0f);
    ringPos = 0;
    sinceDetect = 0;
    corrSemis = 0.0f;
    targetSemis = 0.0f;
    grainTarget = (float) (0.02 * sampleRate);
    humFactor = 1.0f;
    heldNote = -1;
    heldHops = 0;
    unvoicedHops = 99;

    setLatencySamples (shifter[0].dryDelay);
}

bool AutoTuneTKAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

// YIN pitch detector. Returns frequency in Hz, or 0 if no clear pitch.
float AutoTuneTKAudioProcessor::detectPitch (const float* x, int n)
{
    const int half   = n / 2;
    const int minLag = juce::jmax (2, (int) (currentSampleRate / 1000.0));
    const int maxLag = juce::jmin (half - 2, (int) (currentSampleRate / 70.0));

    float running = 0.0f;
    cmnd[0] = 1.0f;
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        float d = 0.0f;
        for (int j = 0; j < half; ++j)
        {
            const float diff = x[j] - x[j + tau];
            d += diff * diff;
        }
        running += d;
        cmnd[(size_t) tau] = running > 0.0f ? d * (float) tau / running : 1.0f;
    }

    for (int tau = minLag; tau < maxLag; ++tau)
    {
        if (cmnd[(size_t) tau] < 0.15f)
        {
            while (tau + 1 < maxLag && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau])
                ++tau;

            const float a = cmnd[(size_t) tau - 1];
            const float b = cmnd[(size_t) tau];
            const float c = cmnd[(size_t) tau + 1];
            const float denom = a - 2.0f * b + c;
            const float shift = std::abs (denom) > 1.0e-9f ? 0.5f * (a - c) / denom : 0.0f;
            return (float) currentSampleRate / ((float) tau + shift);
        }
    }
    return 0.0f;
}

void AutoTuneTKAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh      = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();
    if (numCh == 0) return;

    const int   key       = (int) pKey->load();
    const int   scale     = juce::jlimit (0, 7, (int) pScale->load());
    const float speed     = pSpeed->load() * 0.01f;
    const float amount    = pAmount->load() * 0.01f;
    const float humanize  = pHumanize->load() * 0.01f;
    const int   transpose = (int) pTranspose->load();
    const float mix       = pMix->load() * 0.01f;
    const float outGain   = juce::Decibels::decibelsToGain (pGain->load());
    const float sr        = (float) currentSampleRate;

    // scales as semitone steps from the key root
    static const int scaleLen[8] = { 12, 7, 7, 7, 7, 5, 5, 6 };
    static const int scaleNotes[8][12] = {
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },  // chromatic
        { 0, 2, 4, 5, 7, 9, 11 },                   // major
        { 0, 2, 3, 5, 7, 8, 10 },                   // minor
        { 0, 2, 3, 5, 7, 8, 11 },                   // harmonic minor
        { 0, 2, 3, 5, 7, 9, 10 },                   // dorian
        { 0, 2, 4, 7, 9 },                          // pentatonic major
        { 0, 3, 5, 7, 10 },                         // pentatonic minor
        { 0, 3, 5, 6, 7, 10 }                       // blues
    };

    // allowed notes (by absolute pitch class, C = 0): scale AND the note buttons
    bool mask[12] = {};
    for (int k = 0; k < scaleLen[scale]; ++k)
        mask[(key + scaleNotes[scale][k]) % 12] = true;

    bool any = false;
    for (int pc = 0; pc < 12; ++pc)
    {
        if (pNote[pc]->load() < 0.5f) mask[pc] = false;
        any = any || mask[pc];
    }
    if (! any)
        for (int pc = 0; pc < 12; ++pc) mask[pc] = true;

    // retune speed: 100% = ~0.3 ms (hard snap), 0% = ~400 ms (gentle)
    const float tauBase = 400.0f * std::pow (0.3f / 400.0f, speed);
    auto makeCoef = [&]
    {
        return 1.0f - std::exp (-1.0f / (tauBase * humFactor * 0.001f * sr));
    };
    float coef = makeCoef();

    for (int i = 0; i < numSamples; ++i)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            mono += buffer.getReadPointer (ch)[i];
        mono /= (float) numCh;

        ring[(size_t) ringPos] = mono;
        ringPos = (ringPos + 1) % windowSize;

        if (++sinceDetect >= hopSize)
        {
            sinceDetect = 0;

            float sumSq = 0.0f;
            for (int k = 0; k < windowSize; ++k)
            {
                const float s = ring[(size_t) ((ringPos + k) % windowSize)];
                window[(size_t) k] = s;
                sumSq += s * s;
            }

            float f0 = 0.0f;
            if (sumSq / (float) windowSize > 2.5e-5f) // simple level gate
                f0 = detectPitch (window.data(), windowSize);

            if (f0 > 0.0f)
            {
                unvoicedHops = 0;

                const float midi = 69.0f + 12.0f * std::log2 (f0 / 440.0f);

                float best = midi, bestDist = 1000.0f;
                const int base = (int) std::floor (midi);
                for (int n = base - 3; n <= base + 4; ++n)
                {
                    if (! mask[((n % 12) + 12) % 12]) continue;
                    const float dist = std::abs ((float) n - midi);
                    if (dist < bestDist) { bestDist = dist; best = (float) n; }
                }

                // humanize: the longer a note is held, the slower the correction
                const int bestInt = (int) best;
                if (bestInt == heldNote) ++heldHops;
                else { heldNote = bestInt; heldHops = 0; }
                const float heldSec = (float) heldHops * (float) hopSize / sr;
                humFactor = 1.0f + humanize * 8.0f * juce::jlimit (0.0f, 1.0f, heldSec / 0.25f);
                coef = makeCoef();

                targetSemis = (best - midi) * amount + (float) transpose;
                detectedMidi.store (midi);
                snappedMidi.store (best);

                // grain = an even number of pitch periods (keeps both taps in phase)
                const float period = sr / f0;
                const int   kPer   = juce::jmax (1, (int) std::round (0.008f / period));
                grainTarget = juce::jlimit (0.008f * sr, 0.04f * sr, 2.0f * (float) kPer * period);
            }
            else if (++unvoicedHops > 4)
            {
                // breath / silence: let go of the correction
                targetSemis = (float) transpose;
                heldNote = -1;
                heldHops = 0;
                humFactor = 1.0f;
                coef = makeCoef();
                detectedMidi.store (0.0f);
                snappedMidi.store (0.0f);
            }
        }

        corrSemis += (targetSemis - corrSemis) * coef;
        const float ratio = std::exp2 (corrSemis / 12.0f);

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* data = buffer.getWritePointer (ch);
            float dry = 0.0f;
            const float wet = shifter[ch].process (data[i], ratio, grainTarget, dry);
            data[i] = (dry * (1.0f - mix) + wet * mix) * outGain;
        }
    }
}

juce::AudioProcessorEditor* AutoTuneTKAudioProcessor::createEditor()
{
    return new AutoTuneTKAudioProcessorEditor (*this);
}

void AutoTuneTKAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AutoTuneTKAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AutoTuneTKAudioProcessor();
}
