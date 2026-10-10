#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <vector>
#include <cmath>

class AutoTuneTKAudioProcessor : public juce::AudioProcessor
{
public:
    AutoTuneTKAudioProcessor();
    ~AutoTuneTKAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "AutoTuneTK"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::StringArray keyNames()
    {
        return juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    }

    static juce::StringArray scaleNames()
    {
        return juce::StringArray { "Chromatic", "Major", "Minor", "Harmonic Minor",
                                   "Dorian", "Pentatonic Major", "Pentatonic Minor", "Blues" };
    }

    juce::AudioProcessorValueTreeState apvts;

    // read by the editor for the live pitch meter (0 = no pitch found)
    std::atomic<float> detectedMidi { 0.0f };
    std::atomic<float> snappedMidi  { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float detectPitch (const float* x, int n);

    // Pitch-synchronous dual-tap shifter. The grain length follows the singer's
    // pitch period, which keeps the two taps in phase and avoids the warbly
    // chorus sound of a fixed-size grain. Also holds a delayed dry path.
    struct PitchShifter
    {
        std::vector<float> buf, dryBuf;
        int w = 0, dryW = 0, dryDelay = 0;
        float phase = 0.0f, grain = 1.0f, maxGrain = 1.0f;

        void prepare (double sr)
        {
            maxGrain = (float) (0.04 * sr);
            grain = (float) (0.02 * sr);
            buf.assign ((size_t) maxGrain + 16, 0.0f);
            dryDelay = (int) (0.009 * sr);
            dryBuf.assign ((size_t) dryDelay + 8, 0.0f);
            w = 0;
            dryW = 0;
            phase = 0.0f;
        }

        float readAt (float delay) const
        {
            const int size = (int) buf.size();
            float pos = (float) w - delay;
            while (pos < 0.0f) pos += (float) size;
            int i0 = (int) pos;
            if (i0 >= size) i0 -= size;
            const int i1 = (i0 + 1) % size;
            const float f = pos - (float) i0;
            return buf[(size_t) i0] * (1.0f - f) + buf[(size_t) i1] * f;
        }

        float process (float in, float ratio, float grainTarget, float& dryOut)
        {
            buf[(size_t) w] = in;

            // glide the grain length so it follows the voice without clicks
            grain += (grainTarget - grain) * 0.0005f;
            if (grain > maxGrain) grain = maxGrain;
            if (grain < 16.0f) grain = 16.0f;

            const float pA = phase;
            const float pB = std::fmod (phase + 0.5f, 1.0f);
            const float gA = std::sin (juce::MathConstants<float>::pi * pA);
            const float gB = std::sin (juce::MathConstants<float>::pi * pB);
            const float wet = readAt (pA * grain) * gA * gA
                            + readAt (pB * grain) * gB * gB;

            phase += (1.0f - ratio) / grain;
            phase -= std::floor (phase);
            w = (w + 1) % (int) buf.size();

            const int dsize = (int) dryBuf.size();
            dryBuf[(size_t) dryW] = in;
            int r = dryW - dryDelay;
            if (r < 0) r += dsize;
            dryOut = dryBuf[(size_t) r];
            dryW = (dryW + 1) % dsize;

            return wet;
        }
    };

    PitchShifter shifter[2];

    static constexpr int windowSize = 2048;
    static constexpr int hopSize    = 512;

    std::vector<float> ring, window, cmnd;
    int ringPos = 0, sinceDetect = 0;
    float corrSemis = 0.0f, targetSemis = 0.0f;
    float grainTarget = 800.0f;
    float humFactor = 1.0f;
    int heldNote = -1, heldHops = 0, unvoicedHops = 99;
    double currentSampleRate = 44100.0;

    std::atomic<float> *pKey = nullptr, *pScale = nullptr, *pSpeed = nullptr, *pAmount = nullptr,
                       *pHumanize = nullptr, *pTranspose = nullptr, *pMix = nullptr, *pGain = nullptr;
    std::atomic<float>* pNote[12] = {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoTuneTKAudioProcessor)
};
