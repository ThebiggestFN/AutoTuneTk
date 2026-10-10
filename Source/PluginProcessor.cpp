#include "PluginProcessor.h"

AutoTuneTKAudioProcessor::AutoTuneTKAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout AutoTuneTKAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "key", 1 }, "Key",
        juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "scale", 1 }, "Scale",
        juce::StringArray { "Chromatic", "Major", "Minor" }, 1));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "speed", 1 }, "Retune Speed",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 80.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mix", 1 }, "Dry/Wet",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f));

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

    const int   key   = (int) apvts.getRawParameterValue ("key")->load();
    const int   scale = (int) apvts.getRawParameterValue ("scale")->load();
    const float speed = apvts.getRawParameterValue ("speed")->load() * 0.01f;
    const float mix   = apvts.getRawParameterValue ("mix")->load() * 0.01f;

    // which semitones (relative to the key root) are allowed
    static const bool majorMask[12] = { true, false, true, false, true, true, false, true, false, true, false, true };
    static const bool minorMask[12] = { true, false, true, true, false, true, false, true, true, false, true, false };
    auto allowed = [&] (int midiNote)
    {
        const int pc = ((midiNote - key) % 12 + 12) % 12;
        if (scale == 1) return majorMask[pc];
        if (scale == 2) return minorMask[pc];
        return true;
    };

    // retune speed: 100% = ~1 ms (hard snap), 0% = ~400 ms (gentle)
    const float tauMs = 400.0f * std::pow (1.0f / 400.0f, speed);
    const float coef  = 1.0f - std::exp (-1.0f / (tauMs * 0.001f * (float) currentSampleRate));

    for (int i = 0; i < numSamples; ++i)
    {
        // mono mix into the analysis ring buffer
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
                const float midi = 69.0f + 12.0f * std::log2 (f0 / 440.0f);

                float best = midi, bestDist = 1000.0f;
                const int base = (int) std::floor (midi);
                for (int n = base - 2; n <= base + 3; ++n)
                {
                    if (! allowed (n)) continue;
                    const float dist = std::abs ((float) n - midi);
                    if (dist < bestDist) { bestDist = dist; best = (float) n; }
                }
                targetSemis = best - midi;
            }
            else
            {
                targetSemis = 0.0f; // no pitch: pass through untouched
            }
        }

        corrSemis += (targetSemis - corrSemis) * coef;
        const float ratio = std::exp2 (corrSemis / 12.0f);

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* data = buffer.getWritePointer (ch);
            float dry = 0.0f;
            const float wet = shifter[ch].process (data[i], ratio, dry);
            data[i] = dry * (1.0f - mix) + wet * mix;
        }
    }
}

juce::AudioProcessorEditor* AutoTuneTKAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
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
