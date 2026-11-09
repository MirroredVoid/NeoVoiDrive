#pragma once

/*
    MultibandEngine (T13): the complete plugin DSP chain.

      IN -> CrossoverBank (1..3 bands, bypassed at bandCount==1)
          -> 3 x BandProcessor (per band; disabled bands stay frozen, R12)
          -> sum
          -> FormantEngine (when formantEnabled; block-level, crossfaded in)
          -> Dry/Wet (dry/Bypass/Band off delayed to the reported latency, T45)
          -> Output Trim
          -> bypass (plugin-wide, applied by PluginProcessor's 5 ms ramp)
          -> OUT + Meter tap (peak callback, T14 wires a SPSC FIFO)

    Band layout (ARCHITECTURE §3, DECISIONS §4.4):
      bandCount 1: full-range straight into bands[0], crossover fully bypassed
      bandCount 2: single crossover at xover1; bands[1] receives mid+hi
                   (setCrossover f2 = maxFreq: LP(f2) ~ allpass, HP(f2) ~ 0,
                   so mid+hi ~ HP(f1) — the correct high band)
      bandCount 3: lo/mid/hi into bands[0..2]

    RT contract: process() is allocation-free and lock-free; all buffers are
    prepared in prepare().  Per-sample gains (bypass ramp, formant crossfade)
    arrive as arrays from PluginProcessor (its bypassRamp / LatencyManager).
*/

#include "dsp/BandProcessor.h"
#include "dsp/CrossoverBank.h"
#include "dsp/F0Tracker.h"
#include "dsp/FormantEngine.h"
#include "dsp/GrowlEngine.h"
#include <juce_core/juce_core.h> // juce::Random (construction-time seed)
#include "params/ParamSnapshot.h"
#include "util/SmoothedValue.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dsp
{

class MultibandEngine
{
public:
    static constexpr int maxBands = 3;
    static constexpr float maxCrossoverFreq = 18000.0f; // SPEC §4.2 upper bound

    // S1 (T13 review): the dry path is delayed by the total reported latency
    // (formant latency + the fixed sideband baseline Lmax, T45), so the
    // dry/wet mix and Bypass stay time-aligned in every engine/formant state.
    static constexpr int formantLatencySamples = dsp::FormantEngine::latencySamples;

    // Tap write point (T14): block-end peak callback. Function pointer plus
    // an opaque context (the owning processor) so the RT path stays
    // allocation-free and multi-instance safe (M1 review: a static bridge
    // singleton let the last-prepared instance steal every meter).
    using MeterTap = void (*) (void* context, float leftPeak, float rightPeak) noexcept;

    // Per-instance seed drawn at construction; L/R share it (mono stays mono)
    // while different plugin instances still get different streams.
    MultibandEngine() { randomSeed = juce::Random::getSystemRandom().nextInt64(); }

    //==========================================================================
    void prepare (double sampleRate, int maxBlockSize) noexcept
    {
        left.crossover.prepare (sampleRate, maxBlockSize);
        right.crossover.prepare (sampleRate, maxBlockSize);

        for (auto& band : left.bands)
            band.prepare (sampleRate, maxBlockSize);
        for (auto& band : right.bands)
            band.prepare (sampleRate, maxBlockSize);

        formantL.prepare (sampleRate, maxBlockSize);
        formantR.prepare (sampleRate, maxBlockSize);

        // SPEC §4.2: dry_wet and output_trim are smoothed over 10 ms.
        dryWetSmoother.prepare (sampleRate, 10.0);
        trimSmoother.prepare (sampleRate, 10.0);
        dryWetSmoother.reset (1.0f);

        // T29: global F0 tracker (SIDEBAND_SPEC §5/§6.5) — one instance for
        // both channels, fed the mono downmix, broadcasting to all bands.
        f0Tracker.prepare (sampleRate, maxBlockSize);
        f0Tracker.setActive (false);   // §6.6: asleep until sideband/growl needs it
        f0Broadcast = { 0.0f, 0.0f, false };

        // T66: global Growl (post-sum/post-formant, wet path only).
        growl.prepare (sampleRate);

        // L/R share one per-instance seed (mono stays mono; user requirement).
        setSeed (randomSeed);


        trimSmoother.reset (1.0f);

        chainSumL.assign (static_cast<size_t> (std::max (1, maxBlockSize)), 0.0f);
        chainSumR.assign (static_cast<size_t> (std::max (1, maxBlockSize)), 0.0f);
        formantOutL.assign (static_cast<size_t> (std::max (1, maxBlockSize)), 0.0f);
        formantOutR.assign (static_cast<size_t> (std::max (1, maxBlockSize)), 0.0f);

        // S1/T45: one ring covers every dry tap we need: 0, Lmax, 2048 and
        // 2048 + Lmax. The ring has one extra slot so the longest delay is
        // still unambiguous (no overlap with the just-written sample).
        dryLatencySideband = dsp::SidebandEngine::maxLatencySamplesForRate (sampleRate);
        dryDelayLength = dsp::FormantEngine::latencySamples + dryLatencySideband + 1;
        dryDelayL.assign (static_cast<size_t> (dryDelayLength), 0.0f);
        dryDelayR.assign (static_cast<size_t> (dryDelayLength), 0.0f);
        dryPos = 0;

        sidebandLatencyBlend.prepare (sampleRate, dsp::BandProcessor::engineCrossfadeMs);
        sidebandLatencyBlend.reset (0.0f);
        parametersInitialised = false;

        // Audit P2-3: a band-count change is a structural routing change
        // (band inputs, crossover state). It fades the processed output out
        // and back in over 5 ms and switches the routing at the zero point.
        routingFadeSteps = std::max (1, static_cast<int> (std::lround (0.005 * sampleRate)));
        routingFadeRemaining = 0;
        routingState = RoutingState::steady;
        routingSwitchPending = false;
        routingInitialised = false;
        pendingBandCount = bandCount;

        lPeak = 0.0f;
        rPeak = 0.0f;
    }

    //==========================================================================
    /** Fans out the snapshot once per block: crossover + per-band params +
        formant + smoother targets. All internal smoothing absorbs the rest. */
    void setParameters (const params::ParamSnapshot& p) noexcept
    {
        const int requestedBandCount = juce::jlimit (1, maxBands, p.bandCount);

        if (! routingInitialised)
        {
            bandCount = requestedBandCount;
            pendingBandCount = requestedBandCount;
            routingState = RoutingState::steady;
            routingFadeRemaining = 0;
            routingInitialised = true;
        }
        else if (requestedBandCount != pendingBandCount)
        {
            pendingBandCount = requestedBandCount;

            if (pendingBandCount != bandCount)
            {
                // Fade out from the CURRENT gain (a reversal during fade-in
                // continues from where it is), then switch at zero. An
                // already-running fade-out keeps its progress — only the
                // pending destination changes.
                if (routingState != RoutingState::fadingOut)
                    routingFadeRemaining = (routingState == RoutingState::fadingIn)
                        ? routingFadeSteps - routingFadeRemaining
                        : routingFadeSteps;

                if (routingFadeRemaining <= 0)
                    routingFadeRemaining = routingFadeSteps;
                routingState = RoutingState::fadingOut;
                routingSwitchPending = false;
            }
            else if (routingState == RoutingState::fadingOut)
            {
                // Reverted to the active count: fade back up from the
                // current gain instead of switching.
                routingFadeRemaining = routingFadeSteps - routingFadeRemaining;
                routingState = RoutingState::fadingIn;
            }
        }

        formantEnabled = p.formantEnabled;

        // T29: engine selection + F0 tracker activation (SIDEBAND_SPEC §6.6).
        // The tracker runs only when the sideband engine is selected AND at
        // least one band has enable=on with depth>0; otherwise it sleeps
        // (ring keeps advancing, NSDF skipped — zero detection CPU).
        const bool useSideband = (p.engineVersion == 1);
        if (! parametersInitialised)
        {
            // Snap the first block: the startup engine is already known, so a
            // 5 ms latency blend would only create a startup mismatch.
            sidebandLatencyBlend.reset (useSideband ? 1.0f : 0.0f);
            parametersInitialised = true;
        }
        else
        {
            sidebandLatencyBlend.setTarget (useSideband ? 1.0f : 0.0f);
        }

        bool anySidebandActive = false;
        const int activeBands = juce::jlimit (0, maxBands, p.bandCount);
        for (int b = 0; b < activeBands; ++b)
        {
            const auto& band = p.bands[static_cast<size_t> (b)];
            anySidebandActive = anySidebandActive
                || (band.enable && band.sidebandEnable && band.sidebandDepth > 0.0f);
        }

        // T66: Growl shares the tracker with Sideband. Keep it awake through
        // the amount fade-out as well.
        growl.setParameters ({ p.growlAmount, p.growlRate, p.growlTone });
        f0Tracker.setActive ((useSideband && anySidebandActive)
                             || p.growlAmount > 0.0f
                             || growl.isActive());

        if (pendingBandCount > 1)
        {
            // 2 bands: single crossover point; f2 = maxFreq collapses the
            // mid+hi sum into the high band (see file header). Updated from
            // the PENDING count so a 1->2/3 switch finds the new crossover
            // already configured (audit P2-3).
            const auto f2 = (pendingBandCount == 2) ? maxCrossoverFreq : p.xover2Freq;
            left.crossover.setCrossover (p.xover1Freq, f2, slopeFromInt (p.xoverSlope));
            right.crossover.setCrossover (p.xover1Freq, f2, slopeFromInt (p.xoverSlope));
        }

        for (int b = 0; b < maxBands; ++b)
        {
            left.bands[static_cast<size_t> (b)].setParameters (p.bands[b]);
            right.bands[static_cast<size_t> (b)].setParameters (p.bands[b]);

            // T29: engine selector + F0 broadcast (same for L/R — mono stays mono).
            left.bands[static_cast<size_t> (b)].setEngineVersion (useSideband);
            right.bands[static_cast<size_t> (b)].setEngineVersion (useSideband);
            left.bands[static_cast<size_t> (b)].setF0Broadcast (f0Broadcast.f0, f0Broadcast.clarity, f0Broadcast.voiced);
            right.bands[static_cast<size_t> (b)].setF0Broadcast (f0Broadcast.f0, f0Broadcast.clarity, f0Broadcast.voiced);
        }

        if (formantEnabled)
        {
            // Re-enabling after an idle period must not replay stale audio
            // from before the disable (the engine was frozen): flush it.
            if (! lastFormantEnabled)
            {
                formantL.clear();
                formantR.clear();
            }
            formantL.setShiftSemitones (p.formantShift);
            formantL.setQuefrencyMs (p.formantQuefrency);
            formantR.setShiftSemitones (p.formantShift);
            formantR.setQuefrencyMs (p.formantQuefrency);
        }
        lastFormantEnabled = formantEnabled;

        dryWetSmoother.setTarget (p.dryWet * 0.01f);
        trimSmoother.setTarget (std::pow (10.0f, p.outputTrim / 20.0f));
    }

    /** Seeds all random sources (tests / reproducibility). Both channels
        share the seed (mono stays mono); bands derive distinct streams. */
    void setSeed (std::int64_t seed) noexcept
    {
        // The SAME seed for both channels (mono must stay mono — user
        // requirement; independent L/R seeds turned a mono source stereo).
        // Bands still get DERIVED seeds so per-band jitter/shimmer stay
        // uncorrelated (R6: per-band independence), while L/R stay identical.
        for (int b = 0; b < maxBands; ++b)
        {
            // Band 0 keeps the raw seed so band_count=1 stays bit-identical
            // to a directly-wired BandProcessor (regression test); bands 1+
            // derive distinct seeds so per-band jitter/shimmer stay
            // uncorrelated (R6). L/R always share the seed.
            const auto bandSeed = (b == 0)
                ? seed
                : static_cast<std::int64_t> (
                      static_cast<std::uint64_t> (seed)
                      ^ (0x9E3779B97F4A7C15ull * static_cast<std::uint64_t> (b)));
            left.bands[static_cast<size_t> (b)].setSeed (bandSeed);
            right.bands[static_cast<size_t> (b)].setSeed (bandSeed);
        }

        // T66: one Growl controller shared by L/R (mono stays mono).
        growl.setSeed (seed ^ 0x47524F574C5F4E33LL);
    }

    void setMeterTap (MeterTap tap, void* context) noexcept
    {
        meterTap = tap;
        meterTapContext = context;
    }

    //==========================================================================
    /** Processes one block.  `bypassGain[i]` / `formantGain[i]` are the
        per-sample ramps produced by PluginProcessor (bypassRamp / LatencyManager).
        `numChannels` is 1 (mono) or 2; the right chain only runs for 2.
        `formantLatencyAligned` mirrors LatencyManager::isFormantAligned():
        while set, the dry and the formant-crossfade base use the delayed tap
        that matches the host's reported latency (audit P1-1: the enable hold
        must not output the undelayed path). */
    void process (const float* inL, const float* inR,
                  float* outL, float* outR,
                  int numSamples, int numChannels,
                  const float* bypassGain, const float* formantGain,
                  bool formantLatencyAligned = false) noexcept
    {
        // --- 0. F0 tracker input: mono downmix of the PRE-crossover signal
        // (SIDEBAND_SPEC §6.5). pushSample always advances the ring buffer
        // (stream alignment); the NSDF only runs while active (§6.6).
        for (int i = 0; i < numSamples; ++i)
            f0Tracker.pushSample (numChannels > 1 ? (inL[i] + inR[i]) * 0.5f : inL[i]);

        // --- 1. band chains -> sum (block-level intermediate buffer) ---
        // Audit P2-3: a band-count switch happens at the output fade's zero
        // point. Split the block there so the new routing starts exactly at
        // that sample (no block-size-dependent silence).
        int switchAt = numSamples;
        if (routingSwitchPending)
            switchAt = 0;
        else if (routingState == RoutingState::fadingOut)
            switchAt = juce::jmin (numSamples, juce::jmax (0, routingFadeRemaining));

        if (switchAt <= 0)
        {
            finishRoutingSwitch();
            processChain (inL, chainSumL.data(), numSamples, left);
            if (numChannels > 1)
                processChain (inR, chainSumR.data(), numSamples, right);
        }
        else if (switchAt < numSamples)
        {
            processChain (inL, chainSumL.data(), switchAt, left);
            if (numChannels > 1)
                processChain (inR, chainSumR.data(), switchAt, right);

            finishRoutingSwitch();

            processChain (inL + switchAt, chainSumL.data() + switchAt,
                          numSamples - switchAt, left);
            if (numChannels > 1)
                processChain (inR + switchAt, chainSumR.data() + switchAt,
                              numSamples - switchAt, right);
        }
        else
        {
            processChain (inL, chainSumL.data(), numSamples, left);
            if (numChannels > 1)
                processChain (inR, chainSumR.data(), numSamples, right);
        }

        // --- 2. formant (block-level, STFT frame scheduling inside) ---
        // Keep processing while the 5 ms disable crossfade is running
        // (fg[0] > 0): the engine must stay stream-aligned or the frozen
        // formantOut would be a whole block out of stream (up to maxChunk -
        // 1 samples), clicking on the switch.
        // Once fg has reached 0 the engine freezes and saves the CPU.
        const bool formantActive = formantEnabled
            || (numSamples > 0 && formantGain[0] > 0.0f);
        if (formantActive)
        {
            formantL.process (chainSumL.data(), formantOutL.data(), numSamples);
            if (numChannels > 1)
                formantR.process (chainSumR.data(), formantOutR.data(), numSamples);
        }

        // --- 3. mix: formant crossfade -> dry/wet -> trim -> bypass + meter ---
        lPeak = 0.0f;
        rPeak = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto bg = bypassGain[i];
            const auto fg = formantGain[i];
            const auto dw = dryWetSmoother.getNext();
            const auto tg = trimSmoother.getNext();
            const auto eng = sidebandLatencyBlend.getNext();
            const auto routingGain = nextRoutingGain();

            // T66: one shared Growl control sample for both channels. It only
            // touches the wet path below; amount=0 returns wet unchanged.
            const auto growlGain = growl.nextGain (f0Broadcast.f0,
                                                    f0Broadcast.clarity,
                                                    f0Broadcast.voiced);

            const auto processChannel = [&] (float x, std::vector<float>& ring,
                                             float sum, float formantOut,
                                             float& out, float& peak, int channel)
            {
                ring[dryPos] = x;

                // T45: two taps bridge the formant on/off states, and the
                // engine blend bridges Legacy (0) and Sideband (Lmax). The
                // dry follows the SAME ramps as the wet path, so dry, wet and
                // Bypass are time-aligned at every instant.
                const auto base = readDry (ring, dryPos, 0) * (1.0f - eng)
                                + readDry (ring, dryPos, dryLatencySideband) * eng;
                const auto form = readDry (ring, dryPos, formantLatencySamples) * (1.0f - eng)
                                + readDry (ring, dryPos, formantLatencySamples + dryLatencySideband) * eng;

                // Audit P1-1: as soon as the host is told about the formant
                // latency the dry (and the formant-crossfade base) must use
                // the delayed tap — including the 2048-sample enable hold,
                // whose content gain is 0. Otherwise that window would run
                // 42.7 ms ahead of the reported PDC value.
                const auto dry = formantLatencyAligned
                                   ? form
                                   : base * (1.0f - fg) + form * fg;

                // The fg crossfade always applies: when formant is disabled
                // the engines freeze and formantOut holds the last frame, so
                // the 5 ms ramp glides from the old formant path to the dry
                // sum instead of hard-cutting (T13 review).
                const auto wetBase = formantLatencyAligned ? form : sum;
                const auto wetPre = (fg > 0.0f) ? (wetBase * (1.0f - fg) + formantOut * fg)
                                                : wetBase;
                const auto wet = growl.process (wetPre, channel, growlGain);
                const auto mixed = (dry * (1.0f - dw) + wet * dw) * routingGain;
                const auto y = mixed * tg;
                out = dry * (1.0f - bg) + y * bg;
                peak = std::max (peak, std::abs (out));
            };

            processChannel (inL[i], dryDelayL, chainSumL[static_cast<size_t> (i)],
                            formantOutL[static_cast<size_t> (i)], outL[i], lPeak, 0);
            if (numChannels > 1)
                processChannel (inR[i], dryDelayR, chainSumR[static_cast<size_t> (i)],
                                formantOutR[static_cast<size_t> (i)], outR[i], rPeak, 1);

            // Advance the delay-line position once per sample (shared by both
            // channels; they are always written/read in lockstep).
            dryPos = (dryPos + 1) % static_cast<size_t> (dryDelayLength);
        }

        if (meterTap != nullptr)
            meterTap (meterTapContext, lPeak, rPeak);

        // --- 4. refresh the F0 broadcast for the NEXT block (control rate
        // ≈ block rate; SB-U11's 200 ms re-lock budget is unaffected) ---
        f0Broadcast.f0      = f0Tracker.getF0();
        f0Broadcast.clarity = f0Tracker.getClarity();
        f0Broadcast.voiced  = f0Tracker.isVoiced();
    }

    /** T29 diagnostic: NSDF executions (sleep assertions, SIDEBAND_SPEC §6.6). */
    int getF0NsdfRuns() const noexcept { return f0Tracker.getNsdfRuns(); }

    // T66 diagnostics (tests): Growl activity and gain.
    bool isGrowlActive() const noexcept { return growl.isActive(); }
    float getLastGrowlGain() const noexcept { return growl.getLastGain(); }
    float getGrowlEffectiveAmount() const noexcept { return growl.getEffectiveAmount(); }

private:
    //==========================================================================
    /** Reads an integer tap from the dry ring (delay < ring.size()). */
    static float readDry (const std::vector<float>& ring, size_t pos, int delay) noexcept
    {
        const auto n = ring.size();
        return ring[(pos + n - static_cast<size_t> (delay)) % n];
    }

    struct ChannelChain
    {
        dsp::CrossoverBank crossover;
        std::array<dsp::BandProcessor, maxBands> bands;
    };

    //==========================================================================
    static CrossoverBank::Slope slopeFromInt (int slope) noexcept
    {
        switch (slope)
        {
            case 12: return CrossoverBank::Slope::slope12;
            case 36: return CrossoverBank::Slope::slope36;
            case 48: return CrossoverBank::Slope::slope48;
            case 60: return CrossoverBank::Slope::slope60;
            case 72: return CrossoverBank::Slope::slope72;
            default: return CrossoverBank::Slope::slope24;
        }
    }

    //==========================================================================
    // Audit P2-3: band-count routing fade.
    float nextRoutingGain() noexcept
    {
        switch (routingState)
        {
            case RoutingState::steady:
                return 1.0f;

            case RoutingState::fadingOut:
            {
                if (routingFadeRemaining > 0)
                    --routingFadeRemaining;

                const auto g = static_cast<float> (routingFadeRemaining)
                             / static_cast<float> (routingFadeSteps);

                if (routingFadeRemaining == 0)
                {
                    // The switch may land on a block boundary; the next
                    // process() performs it before touching the chain.
                    if (bandCount != pendingBandCount)
                        routingSwitchPending = true;
                    routingState = RoutingState::fadingIn;
                    routingFadeRemaining = routingFadeSteps;
                }
                return g;
            }

            case RoutingState::fadingIn:
            {
                if (routingFadeRemaining > 0)
                    --routingFadeRemaining;

                const auto g = 1.0f - static_cast<float> (routingFadeRemaining)
                                     / static_cast<float> (routingFadeSteps);
                if (routingFadeRemaining == 0)
                    routingState = RoutingState::steady;
                return g;
            }
        }
        return 1.0f;
    }

    void finishRoutingSwitch() noexcept
    {
        routingSwitchPending = false;
        if (bandCount == pendingBandCount)
            return;

        bandCount = pendingBandCount;

        // The output is silent here, so the stale delay histories of the
        // previously hidden bands can be cleared without a click.
        left.crossover.resetState();
        right.crossover.resetState();
        for (auto& band : left.bands)  band.resetState();
        for (auto& band : right.bands) band.resetState();
    }

    //==========================================================================
    void processChain (const float* in, float* sumOut, int n, ChannelChain& chain) noexcept
    {
        if (bandCount == 1)
        {
            for (int i = 0; i < n; ++i)
                sumOut[i] = chain.bands[0].process (in[i]);
            return;
        }

        if (bandCount == 2)
        {
            for (int i = 0; i < n; ++i)
            {
                float lo, mid, hi;
                chain.crossover.process (in[i], lo, mid, hi);
                sumOut[i] = chain.bands[0].process (lo) + chain.bands[1].process (mid + hi);
            }
            return;
        }

        for (int i = 0; i < n; ++i)
        {
            float lo, mid, hi;
            chain.crossover.process (in[i], lo, mid, hi);
            sumOut[i] = chain.bands[0].process (lo) + chain.bands[1].process (mid) + chain.bands[2].process (hi);
        }
    }

    //==========================================================================
    ChannelChain left, right;
    dsp::FormantEngine formantL, formantR;
    dsp::F0Tracker f0Tracker;   // T29: global single instance (SIDEBAND_SPEC §5)
    dsp::GrowlEngine growl;     // T66: global wet-path Growl (SPEC §4.2)

    // T29: f0/clarity/voiced broadcast, refreshed per block (control rate).
    struct F0Broadcast { float f0 = 0.0f; float clarity = 0.0f; bool voiced = false; };
    F0Broadcast f0Broadcast = { 0.0f, 0.0f, false };

    util::SmoothedValue dryWetSmoother; // 10 ms (SPEC §4.2)
    util::SmoothedValue trimSmoother;   // 10 ms
    util::SmoothedValue sidebandLatencyBlend; // Legacy 0 <-> Sideband Lmax, 5 ms
    bool parametersInitialised = false;

    std::vector<float> chainSumL, chainSumR;    // intermediate (preallocated)
    std::vector<float> formantOutL, formantOutR;
    std::vector<float> dryDelayL, dryDelayR;    // S1/T45: latency-aligned dry
    int dryLatencySideband = 0;                 // fixed Lmax in samples
    int dryDelayLength = formantLatencySamples + 1;
    size_t dryPos = 0;

    std::int64_t randomSeed = 0; // per-instance, shared by L/R (mono stays mono)

    int bandCount = 3;
    bool formantEnabled = false;
    bool lastFormantEnabled = false;

    // Audit P2-3: routing fade state (5 ms out, switch, 5 ms in).
    enum class RoutingState { steady, fadingOut, fadingIn };
    RoutingState routingState = RoutingState::steady;
    int routingFadeSteps = 240;
    int routingFadeRemaining = 0;
    bool routingSwitchPending = false;
    bool routingInitialised = false;
    int pendingBandCount = 3;

    MeterTap meterTap = nullptr;


    void* meterTapContext = nullptr;
    float lPeak = 0.0f;
    float rPeak = 0.0f;
};

} // namespace dsp
