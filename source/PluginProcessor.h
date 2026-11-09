#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/LatencyManager.h"
#include "dsp/MultibandEngine.h"
#include "params/ParameterLayout.h"
#include "params/ParamSnapshot.h"
#include "ui/MeterSource.h"
#include "ui/SpectrumSource.h"
#include "util/SmoothedValue.h"

#include <atomic>
#include <vector>

//==============================================================================
/**
    NeoVoiDrive plugin processor: APVTS + the full DSP chain + dynamic
    latency/PDC.

    processBlock snapshots every parameter once (params::ParamSnapshot), feeds
    dsp::MultibandEngine (crossover -> 3 band chains -> formant -> Growl ->
    dry/wet -> trim), and reports the latency implied by the engine/formant
    state through dsp::LatencyManager. Real-time discipline: no heap
    allocation, no locking, no exceptions, no blocking, no file IO and no
    logging on the audio thread.
*/
class PluginProcessor final : public juce::AudioProcessor
{
public:
    //==============================================================================
    PluginProcessor();
    ~PluginProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    /** Latency from the atomic LatencyManager source — the UI reads the same
        value the audio thread wrote (T19 review; getLatencySamples() itself
        is non-virtual in JUCE 9, so the UI uses this accessor). */
    int getReportedLatencySamples() const noexcept
    {
        return latencyManager.getCurrentLatencySamples();
    }

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Applies a state XML tree (DAW session restore or a T42 *.nvpreset file).

        Contains the state-version migration shared by both paths: a state with
        `version="1"` (or no version) is forced to `engine_version=Legacy`
        (DECISIONS R20). Message thread only — APVTS::replaceState uses locks
        and is not realtime-safe. */
    void applyStateXml (const juce::XmlElement& stateXml);

    /** Publishes the latency implied by the current APVTS values (message
        thread). State/preset loads use this while the host is stopped — the
        audio thread re-publishes from its snapshot on the next block. */
    void refreshLatencyReport() noexcept;

    /** Offline A/B renders: overrides the per-instance random seed so two
        builds produce sample-identical random layers. Call after prepare. */
    void setRandomSeedForOfflineRender (std::int64_t seed) noexcept { engine.setSeed (seed); }

    //==============================================================================
    /** All 68 parameters (SPEC §4). Public so the editor can bind attachments (T16+). */
    juce::AudioProcessorValueTreeState apvts;

    /** UI data bridge for the spectrum display (T14 / T17). */
    ui::SpectrumSource& getSpectrumSource() noexcept { return spectrumSource; }
    ui::SpectrumSource& getSpectrumSourceOut() noexcept { return spectrumSourceOut; }

    /** UI data bridge for the peak meters (T14 / T19). */
    ui::MeterSource& getMeterSource() noexcept { return meterSource; }

private:
    //==============================================================================
    // 5 ms equal-gain bypass ramp (DECISIONS §3.3): 1 = processed chain,
    // 0 = dry passthrough. Advanced every block; applied by the main chain.
    util::SmoothedValue bypassRamp;

    // Dynamic formant latency report + switch crossfade (SPEC §5.1 / A2).
    dsp::LatencyManager latencyManager;

    // T13: the full DSP chain (crossover -> bands -> formant -> dry/wet -> trim).
    dsp::MultibandEngine engine;

    // T14: UI data bridges (audio thread writes, UI thread reads).
    ui::SpectrumSource spectrumSource; // input decimation tap (IN)
    ui::SpectrumSource spectrumSourceOut; // output decimation tap (OUT, T37)
    ui::MeterSource meterSource;       // block-end peaks (OUT, via engine tap)

    // Per-sample gains for processBlock (preallocated in prepareToPlay).
    std::vector<float> bypassGains, formantGains;

    // T14/M1: the engine's meter tap carries the owning instance as its
    // context, so every processor instance feeds its own meterSource (no
    // static bridge, no cross-instance stealing).
    static void meterTapBridge (void* context, float leftPeak, float rightPeak) noexcept
    {
        if (auto* owner = static_cast<PluginProcessor*> (context))
            owner->meterSource.pushPeaks (leftPeak, rightPeak);
    }

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
