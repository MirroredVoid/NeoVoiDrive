#pragma once

/*
    Dynamic latency reporting + switch crossfade (T03, SPEC §5.1 / DECISIONS A2).

    The formant path (T12) introduces a fixed latency of 2048 samples when
    enabled (analysis frame 1024 + one frame of input accumulation; the
    per-sample adapter needs a full block before the STFT can run, T13).
    Enabling/disabling it forces the host to re-compensate (PDC), which can
    cause an audible glitch; a short output crossfade absorbs it.

    Contract:
      - prepareBlock() must be called once per audio block (block boundary);
      - the reported latency flips only at block boundaries;
      - getNextGain() yields the formant-path CONTENT gain:
          enable  : 0 for 2048 samples (engine latency — T13 review S2: the
                    engine's first valid frame only arrives after one full
                    latency, so the gain must stay at 0 until then),
                    then a ~5 ms linear ramp 0 -> 1;
          disable : a ~5 ms linear ramp 1 -> 0 (engine keeps processing
                    during the ramp — MultibandEngine drives it off fg[0]);
    - isFormantAligned() tells the chain which dry tap matches the REPORTED
      latency: true from the enable block through the disable block.  During
      the enable hold the output therefore uses the 2048-sample delayed dry
      instead of the undelayed path, so the transition window stays
      time-aligned with the host's PDC (audit P1-1).  On disable the report
      drops immediately and the dry returns to tap 0; the 5 ms wet fade-out is
      the only accepted sub-block misalignment.

    Latency publishing (audit P1-2):
      - prepare() marks a forced publish so the first prepareBlock() always
        reaches the host, even when the new total equals the cached value
        (a reused instance may still carry 53/2048/2101 from a previous
        session);
      - publishTargetLatency() publishes from the message thread (state /
        preset loads while the host is stopped) without touching the fade
        state machine.

    Zero allocation, no locks, no exceptions — RT safe.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

#include "../util/SmoothedValue.h"

namespace dsp
{

class LatencyManager
{
public:
    // SPEC §5.1: formant on = 2048 samples (analysis frame + accumulation), off = 0.
    static constexpr int formantLatencySamples = 2048;

    // SPEC §4.2 "formant_enabled" smoothing column / DECISIONS A2.
    static constexpr float crossfadeMilliseconds = 5.0f;

    /** Configures the crossfade ramp. Call from prepareToPlay. */
    void prepare (double sampleRate) noexcept
    {
        gain.prepare (sampleRate, crossfadeMilliseconds);
        gain.reset (0.0f); // formant defaults to off -> dry path at unity
        currentFormantEnabled = false;
        currentLatencySamples.store (0, std::memory_order_relaxed);
        extraLatencySamples.store (0, std::memory_order_relaxed);
        forcePublish = true; // reused instances may still carry an old report
        state = FadeState::off;
        holdRemaining = 0;
        rampingUp = false;
    }

    /** T38/T45: sideband wet-path baseline latency (samples) — reported ON
        TOP of the formant latency so the host's PDC covers the wet path
        (fixed Lmax = A(100 %) + F + 4; 53 samples @48 kHz). Control-rate:
        call once per block from the audio thread (or from a state load). */
    void setExtraLatencySamples (int samples) noexcept
    {
        extraLatencySamples.store (juce::jmax (0, samples), std::memory_order_relaxed);
    }

    /** Detects formant_enabled changes at a block boundary, updates the host
        latency report and starts the crossfade. Call once per processBlock.
        T38: the report also follows setExtraLatencySamples() changes (the
        sideband wet-path baseline), even when the formant state is stable. */
    void prepareBlock (bool formantEnabled, juce::AudioProcessor& processor) noexcept
    {
        const bool formantChanged = (formantEnabled != currentFormantEnabled);
        const auto totalLatency = (formantEnabled ? formantLatencySamples : 0)
                                + extraLatencySamples.load (std::memory_order_relaxed);

        if (formantChanged)
            currentFormantEnabled = formantEnabled;

        if (forcePublish || formantChanged || totalLatency != getCurrentLatencySamples())
        {
            forcePublish = false;
            currentLatencySamples.store (totalLatency, std::memory_order_relaxed);
            processor.setLatencySamples (getCurrentLatencySamples());
        }

        if (! formantChanged)
            return;

        if (formantEnabled)
        {
            // S2 (T13 review): hold the content gain at 0 until the engine's
            // first valid frame arrives (one full latency), then fade in.
            // The report flips NOW and the chain switches its dry/wet source
            // to the delayed tap (isFormantAligned), so the whole hold window
            // stays aligned with the host's new PDC value (audit P1-1).
            state = FadeState::holding;
            holdRemaining = formantLatencySamples;
            rampingUp = true;
            gain.reset (0.0f);
        }
        else if (state == FadeState::holding)
        {
            // Never actually engaged: the content gain never left 0 — go
            // straight off. The report has already dropped to the dry value.
            state = FadeState::off;
        }
        else
        {
            // Engaged: fade the formant path out. setTarget() ramps from the
            // CURRENT gain, so a disable mid fade-in reverses smoothly instead
            // of resetting to 1.0 (review).
            state = FadeState::ramping;
            rampingUp = false;
            gain.setTarget (0.0f);
        }
    }

    /** Publishes the latency for the given formant state without touching the
        fade state machine. Message thread (state / preset load while the host
        is stopped); the next processBlock re-publishes from its snapshot. */
    void publishTargetLatency (bool formantEnabled, juce::AudioProcessor& processor) noexcept
    {
        const auto totalLatency = (formantEnabled ? formantLatencySamples : 0)
                                + extraLatencySamples.load (std::memory_order_relaxed);
        currentLatencySamples.store (totalLatency, std::memory_order_relaxed);
        processor.setLatencySamples (totalLatency);
    }

    /** Advances the crossfade by one sample and returns the current
        formant-path content gain. Call once per sample. */
    float getNextGain() noexcept
    {
        switch (state)
        {
            case FadeState::holding:
                if (--holdRemaining <= 0)
                {
                    gain.setTarget (1.0f);
                    state = FadeState::ramping;
                }
                return 0.0f;

            case FadeState::ramping:
            {
                const auto g = gain.getNext();
                if (! gain.isSmoothing())
                    state = rampingUp ? FadeState::on : FadeState::off;
                return g;
            }

            case FadeState::on:
            case FadeState::off:
                break;
        }
        return (state == FadeState::on) ? 1.0f : 0.0f;
    }

    /** True while the crossfade ramp is still running. */
    bool isCrossfading() const noexcept
    {
        return state != FadeState::on && state != FadeState::off;
    }

    /** Current formant_enabled state as seen by this manager. */
    bool isFormantEnabled() const noexcept
    {
        return currentFormantEnabled;
    }

    /** True while the reported latency includes the formant path. The chain
        must use the delayed dry tap (and the delayed base for the formant
        crossfade) whenever this is set, including the enable hold. */
    bool isFormantAligned() const noexcept
    {
        return currentFormantEnabled;
    }

    /** Latency currently reported to the host. Atomic: written on the audio
        thread (prepareBlock) or the message thread (state loads), read from
        the UI thread. */
    int getCurrentLatencySamples() const noexcept
    {
        return currentLatencySamples.load (std::memory_order_relaxed);
    }

private:
    enum class FadeState { off, holding, ramping, on };

    util::SmoothedValue gain;
    bool currentFormantEnabled = false;
    std::atomic<int> currentLatencySamples { 0 };
    std::atomic<int> extraLatencySamples { 0 }; // T38: sideband wet-path baseline
    bool forcePublish = true;                    // first prepareBlock after prepare
    FadeState state = FadeState::off;
    int holdRemaining = 0;
    bool rampingUp = false;
};

} // namespace dsp
