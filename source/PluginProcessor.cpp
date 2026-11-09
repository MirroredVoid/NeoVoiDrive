#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
PluginProcessor::PluginProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
       apvts (*this, nullptr, "Parameters", params::ParameterLayout::create())
{
}

PluginProcessor::~PluginProcessor() = default;

//==============================================================================
const juce::String PluginProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PluginProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool PluginProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool PluginProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double PluginProcessor::getTailLengthSeconds() const
{
    // The formant STFT is causal (2048-sample OLA) and the sideband/dry
    // rings hold up to Lmax; 50 ms covers the output tail after the host
    // stops feeding input (offline render tail handling).
    return 0.05;
}

int PluginProcessor::getNumPrograms()
{
    return 1; // NB: some hosts don't cope very well if you tell them there are 0 programs,
              // so this should be at least 1, even if you're not really implementing programs.
}

int PluginProcessor::getCurrentProgram()
{
    return 0;
}

void PluginProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String PluginProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return "Default";
}

void PluginProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // T48/SPEC §6: the plugin trusts the host rate and does NOT resample or
    // silently fall back. Warn for rates outside the supported range (the
    // quality/aliasing guarantees do not apply there); offline_render rejects
    // them at the CLI boundary.
    if (sampleRate < 32000.0 || sampleRate > 384000.0)
        juce::Logger::writeToLog ("NeoVoiDrive: unsupported sample rate "
                                  + juce::String (sampleRate)
                                  + " Hz (supported 32k..384k; processing at host rate)");
    else if (sampleRate > 192000.0)
        // Audit P2-6: above the documented quality range the fixed delay
        // lines / F0 window are not guaranteed (the worst-case pitch depth
        // exceeds the 8192-sample line); processing continues at the host
        // rate, the offline CLI rejects this range outright.
        juce::Logger::writeToLog ("NeoVoiDrive: sample rate "
                                  + juce::String (sampleRate)
                                  + " Hz is above the supported quality range (<=192k)");

    // T38: publish the rate/size to the AudioProcessor base — getSampleRate()
    // otherwise stays 0 (the latency readout and the sideband-latency report
    // both depend on it; BottomBar's "0 ms" was this bug).
    setRateAndBufferSizeDetails (sampleRate, samplesPerBlock);

    // Pre-allocate / configure everything here so processBlock stays
    // allocation-free (T13: the full DSP chain is prepared now).
    latencyManager.prepare (sampleRate);
    bypassRamp.prepare (sampleRate, 5.0); // 5 ms equal-gain ramp (DECISIONS §3.3)
    bypassRamp.reset (1.0f);              // default: not bypassed -> processed chain

    engine.prepare (sampleRate, samplesPerBlock);

    // T14: UI data bridges.
    spectrumSource.prepare (sampleRate);
    spectrumSourceOut.prepare (sampleRate);   // T37: output tap
    engine.setMeterTap (&PluginProcessor::meterTapBridge, this);

    // T24/P02: warm the APVTS parameter-pointer cache NOW (getRawParameterValue
    // emplaces on first call — that allocation must not happen on the audio
    // thread). The same snapshot publishes the initial latency so a host
    // querying it before the first processBlock sees the correct value (T45).
    {
        params::ParamSnapshot warmup;
        warmup.fillFrom (apvts);

        const int sidebandLatency = (warmup.engineVersion == 1)
            ? dsp::SidebandEngine::maxLatencySamplesForRate (sampleRate)
            : 0;
        latencyManager.setExtraLatencySamples (sidebandLatency);
        latencyManager.prepareBlock (warmup.formantEnabled, *this);
    }

    // Per-sample gain arrays for processBlock (reserve once, never reallocate).
    bypassGains.resize (static_cast<size_t> (samplesPerBlock));
    formantGains.resize (static_cast<size_t> (samplesPerBlock));
}

void PluginProcessor::releaseResources()
{
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // SPEC §6: stereo main bus; mono hosts processed as mono.
    // Input and output layouts must match.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);

    juce::ScopedNoDenormals noDenormals;

    // One snapshot per block: the single parameter source on the audio thread
    // (ARCHITECTURE §7.3). POD on the stack — zero allocation.
    params::ParamSnapshot snapshot;
    snapshot.fillFrom (apvts);

    const auto totalNumInputChannels  = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, clear any output channels that
    // did not contain input data (they are not guaranteed to be empty).
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // ---- T13: the full DSP chain ----
    // One snapshot per block fans out to the engine; internal smoothing
    // (crossover ramp, per-unit gains, dry/wet + trim 10 ms) absorbs steps.
    engine.setParameters (snapshot);

    // Per-sample ramps: bypass (5 ms, DECISIONS §3.3) and the formant
    // crossfade (5 ms, SPEC §5.1 / A2).
    bypassRamp.setTarget (snapshot.bypass ? 0.0f : 1.0f);
    // T45: the sideband engine uses one FIXED baseline Lmax for every depth
    // and every band (A at depth 100 % + fog + 4). Reporting it whenever the
    // Sideband engine is selected keeps the host PDC stable across depth
    // automation, Band On/Sideband On toggles, hidden bands and band-count
    // changes; the dry/Bypass paths in MultibandEngine use the same value.
    // Legacy reports 0 / 2048 (formant only).
    const int sidebandLatency = (snapshot.engineVersion == 1)
        ? dsp::SidebandEngine::maxLatencySamplesForRate (getSampleRate())
        : 0;
    latencyManager.setExtraLatencySamples (sidebandLatency);
    latencyManager.prepareBlock (snapshot.formantEnabled, *this);

    // M3 (T13 review): guard against hosts that deliver more samples than
    // prepareToPlay declared — process in chunks no larger than the
    // preallocated gain arrays (no allocation, no out-of-bounds).
    const auto numSamples = buffer.getNumSamples();
    const auto maxChunk = juce::jmax (1, static_cast<int> (bypassGains.size()));

    // Mono is processed on the left chain only; the right chain stays frozen.
    const auto numChannels = juce::jmin (totalNumInputChannels, 2);
    const auto* inL = buffer.getReadPointer (0);
    const auto* inR = (totalNumInputChannels > 1) ? buffer.getReadPointer (1) : inL;
    auto* outL = buffer.getWritePointer (0);
    auto* outR = (totalNumOutputChannels > 1) ? buffer.getWritePointer (1) : nullptr;

    // T14/O4: input spectrum tap (decimated, SPSC FIFO — UI thread drains
    // it). Stereo input is mono-downmixed so wide material isn't biased.
    spectrumSource.pushSamples (inL, inR, numSamples);

    int pos = 0;
    while (pos < numSamples)
    {
        const auto n = juce::jmin (numSamples - pos, maxChunk);
        for (int i = 0; i < n; ++i)
        {
            bypassGains[static_cast<size_t> (i)] = bypassRamp.getNext();
            formantGains[static_cast<size_t> (i)] = latencyManager.getNextGain();
        }
        engine.process (inL + pos,
                        (numChannels > 1) ? inR + pos : inR,
                        outL + pos,
                        (numChannels > 1) ? outR + pos : outR,
                        n, numChannels,
                        bypassGains.data(), formantGains.data(),
                        latencyManager.isFormantAligned());
        pos += n;
    }

    // T37: output spectrum tap (same decimation contract as the input tap).
    spectrumSourceOut.pushSamples (outL, numSamples > 0 && totalNumOutputChannels > 1 ? outR : outL,
                                   numSamples);
}

//==============================================================================
bool PluginProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

//==============================================================================
void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Serialise the APVTS state as XML with a state version attribute
    // (ARCHITECTURE §7.5). Parameter values live entirely in the APVTS, so the
    // copyState() round-trip preserves all parameters.
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml == nullptr)
    {
        // Allocation failure: hand the host an empty blob rather than
        // dereferencing a null pointer.
        destData.reset();
        return;
    }
    xml->setAttribute ("version", "2");
    copyXmlToBinary (*xml, destData);
}

void PluginProcessor::applyStateXml (const juce::XmlElement& stateXml)
{
    // Never replace the APVTS with an unrelated tree: a wrong root would land
    // every parameter on its default (audit P3). Session restore and PresetIO
    // both pass the APVTS root (<PARAMETERS>).
    if (! stateXml.getTagName().equalsIgnoreCase ("PARAMETERS"))
        return;

    // Version handling: a missing attribute means a pre-versioning state
    // (treat as "1"). Unknown/future versions are loaded tolerantly —
    // APVTS::replaceState keeps current values for any parameter missing from
    // the incoming state, so parameters added in later versions simply fall
    // back to their defaults (ARCHITECTURE §7.5).
    const auto version = stateXml.getStringAttribute ("version", "1");

    apvts.replaceState (juce::ValueTree::fromXml (stateXml));

    // v1 -> v2 migration (REFACTOR_PLAN §4.2 / DECISIONS R20): a v1 state
    // carries the 37 legacy parameters only, so replaceState lands the new
    // sideband parameters on their layout defaults. Force engine_version back
    // to Legacy — the version attribute is the only reliable discriminator
    // once the Phase 6 default flip (engine_version default = Sideband) is in
    // place; without this, legacy harmonic_drive/tone values would be silently
    // ignored. Message thread only (setValueNotifyingHost).
    if (version == "1")
    {
        if (auto* engineParam = dynamic_cast<juce::AudioParameterChoice*> (
                apvts.getParameter (params::IDs::engineVersion)))
            engineParam->setValueNotifyingHost (0.0f); // Legacy (Harmonic Exciter)
    }

    // Audit P1-2: a state/preset load while the host is stopped must update
    // the reported latency immediately (Settings readout + host PDC), not on
    // the next processBlock.
    refreshLatencyReport();
}

void PluginProcessor::refreshLatencyReport() noexcept
{
    params::ParamSnapshot snapshot;
    snapshot.fillFrom (apvts);

    const auto sampleRate = getSampleRate();
    const int sidebandLatency = (snapshot.engineVersion == 1 && sampleRate > 0.0)
        ? dsp::SidebandEngine::maxLatencySamplesForRate (sampleRate)
        : 0;
    latencyManager.setExtraLatencySamples (sidebandLatency);
    latencyManager.publishTargetLatency (snapshot.formantEnabled, *this);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml == nullptr)
        return;

    // Shared with the T42 preset loader (one migration implementation).
    applyStateXml (*xml);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
