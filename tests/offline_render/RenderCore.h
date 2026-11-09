#pragma once

/*
    T21 offline-render core: wav in -> full engine -> wav out, parameterised
    by CLI (see Main.cpp). The render loop lives here (pure function over
    buffers) so the unit tests can smoke it without file IO.

    This is a TEST tool: allocation / file IO are fine (ARCHITECTURE §5.3 —
    the real-time discipline does not apply to the renderer). It exercises
    the production PluginProcessor + full DSP chain end to end.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdint>

namespace offline
{

struct RenderOptions
{
    double sampleRate = 48000.0;
    int blockSize = 512;             // 1..8192, arbitrary (block-size scan)
    std::int64_t randomSeed = -1;    // >= 0 = deterministic A/B renders
    double maxSeconds = 0.0;         // 0 = render the whole input
    std::vector<std::pair<juce::String, float>> paramValues; // physical values
};

/**
    Renders `input` through the complete plugin chain.

    @param output  receives the stereo result (2 channels, numSamples = min
                   of input length and maxSeconds).
    @param input   any channel count; mono is duplicated to stereo.
    @return        number of output samples written.
*/
int renderBuffer (juce::AudioBuffer<float>& output,
                  const juce::AudioBuffer<float>& input,
                  const RenderOptions& options);

} // namespace offline
