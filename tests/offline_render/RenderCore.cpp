#include "RenderCore.h"

#include <algorithm>

#include "PluginProcessor.h"

namespace offline
{

int renderBuffer (juce::AudioBuffer<float>& output,
                  const juce::AudioBuffer<float>& input,
                  const RenderOptions& options)
{
    const auto numIn = input.getNumSamples();
    const auto numOut = options.maxSeconds > 0.0
                      ? juce::jmin (numIn, static_cast<int> (options.maxSeconds * options.sampleRate))
                      : numIn;

    output.setSize (2, numOut, false, false, true);

    // Full production chain (T13: crossover -> bands -> formant -> dry/wet ->
    // trim), including the bypass/formant crossfade ramps and the dynamic
    // latency report.
    PluginProcessor processor;
    processor.prepareToPlay (options.sampleRate, options.blockSize);

    // A/B renders must not depend on the per-instance system seed.
    if (options.randomSeed >= 0)
        processor.setRandomSeedForOfflineRender (options.randomSeed);

    // Apply CLI parameter overrides (physical values, like the UI sliders).
    // T33: choice parameters take their INDEX as the physical value
    // (band_count=0 → "1", engine_version=1 → Sideband) — the generic
    // normalised-range conversion would map band_count=1 to index 1 ("2").
    for (const auto& [id, value] : options.paramValues)
    {
        if (auto* param = processor.apvts.getParameter (id))
        {
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
                *choice = juce::jlimit (0, choice->choices.size() - 1,
                                        static_cast<int> (std::lround (value)));
            else
                param->setValueNotifyingHost (param->getNormalisableRange().convertTo0to1 (value));
        }
    }

    // Per-block workspace: fixed-size storage + a view of the actual size,
    // so the processor always sees a buffer of exactly `n` samples.
    juce::AudioBuffer<float> storage (2, options.blockSize);
    juce::MidiBuffer midi;

    const auto inChannels = juce::jmax (1, input.getNumChannels());
    int pos = 0;
    while (pos < numOut)
    {
        const auto n = juce::jmin (options.blockSize, numOut - pos);

        float* ptrs[2] = { storage.getWritePointer (0), storage.getWritePointer (1) };
        juce::AudioBuffer<float> view (ptrs, 2, n);
        view.clear();

        if (inChannels == 1)
        {
            view.copyFrom (0, 0, input, 0, pos, n);
            view.copyFrom (1, 0, input, 0, pos, n);
        }
        else
        {
            view.copyFrom (0, 0, input, 0, pos, n);
            view.copyFrom (1, 0, input, 1, pos, n);
        }

        processor.processBlock (view, midi);

        output.copyFrom (0, pos, view, 0, 0, n);
        output.copyFrom (1, pos, view, 1, 0, n);
        pos += n;
    }

    // Formant path latency compensation (review B): the engine reports its
    // latency (0 or 2048 samples) via getLatencySamples(). A host would
    // compensate with PDC; offline we shift the rendered output forward so
    // it lines up with the input timeline. The tail `latency` samples of the
    // input are not yet processed — they become silence (physical limit).
    const auto latency = processor.getLatencySamples();
    if (latency > 0 && latency < numOut)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = output.getWritePointer (ch);
            std::copy (data + latency, data + numOut, data);
            std::fill (data + (numOut - latency), data + numOut, 0.0f);
        }
    }

    return numOut;
}

} // namespace offline
