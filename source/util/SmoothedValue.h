#pragma once

/*
    Smoothed control-value wrapper (T04).

    Canonical use of juce::SmoothedValue for parameter smoothing on the audio
    thread: prepares the ramp from a time constant in *milliseconds* (SPEC §4
    "平滑" column) and hides the sample-rate / seconds conversion so DSP units
    share one convention.

    Usage (control-signal domain, e.g. once per sample):
        prepare (sampleRate, 20.0);   // 20 ms ramp (per prepareToPlay)
        reset   (currentValue);       // snap to current (per prepareToPlay)
        setTarget (newParamValue);    // once per block, from ParamSnapshot
        auto v = getNext();           // once per sample

    Zero allocation, no locks, no exceptions — RT safe.
*/

#include <juce_audio_basics/juce_audio_basics.h>

namespace util
{

class SmoothedValue
{
public:
    /** Configures the linear ramp length. Call from prepareToPlay. */
    void prepare (double sampleRate, double rampMilliseconds) noexcept
    {
        jassert (sampleRate > 0.0);
        smoothed.reset (sampleRate, rampMilliseconds * 0.001);
    }

    /** Jumps immediately to `value` (no ramp). Call from prepareToPlay. */
    void reset (float value) noexcept
    {
        smoothed.setCurrentAndTargetValue (value);
    }

    /** Starts ramping towards `value` over the ramp length configured by
        prepare(). NOTE: this is a RAMP, NOT a snap — to jump immediately use
        snapTo(). If the configured ramp length is zero the jump is immediate.
        Call once per block (e.g. from the param snapshot). */
    void setTarget (float value) noexcept
    {
        smoothed.setTargetValue (value);
    }

    /** Jumps immediately to `value`, discarding any in-flight ramp. */
    void snapTo (float value) noexcept
    {
        smoothed.setCurrentAndTargetValue (value);
    }

    /** Advances one sample and returns the current smoothed value. */
    float getNext() noexcept
    {
        return smoothed.getNextValue();
    }

    /** True while the value is still moving towards its target. */
    bool isSmoothing() const noexcept
    {
        return smoothed.isSmoothing();
    }

    /** Current smoothed value (does not advance the ramp). */
    float getCurrentValue() const noexcept
    {
        return smoothed.getCurrentValue();
    }

private:
    juce::SmoothedValue<float> smoothed;
};

} // namespace util
