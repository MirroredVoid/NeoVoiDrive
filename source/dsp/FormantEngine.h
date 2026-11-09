#pragma once

/*
    FormantEngine: dedicated spectral-envelope formant shifter (T12/T68).

    The engine is self-contained.  It keeps only what a formant-only
    effect needs:

      - a causal 2048-point STFT (hop 512, periodic Hann analysis/synthesis),
        OLA with a constant 4x overlap sum (latency = 2048 samples);
      - cepstral envelope extraction (quefrency lifter), a 1.5x envelope
        contrast, frequency-axis warping in the log-magnitude domain and
        re-application with the ORIGINAL phase (no vocoder, no time stretch);
      - the warp is band-limited to 150..5500 Hz with 1 kHz cosine crossfades,
        so the fundamental/low end and the air above 5.5 kHz are left intact;
      - per-frame energy matching keeps the output level stable across shift
        values;
      - arbitrary host block sizes are adapted by a per-sample ring: frames run
        on a fixed hop grid and one output sample is read per input sample.

    At shift = 0 the magnitude gain is exactly 1 and the OLA reconstructs the
    delayed input (float rounding), so enabling Formant with zero shift is
    transparent.

    Public interface, latency (2048 samples) and parameter semantics are
    unchanged from T12.  RT contract (steady state): zero heap allocation, no
    locks, no exceptions, no IO.  All FFT/ring buffers are allocated in
    prepare(); process() only touches preallocated storage.
*/

#include <memory>

namespace dsp
{

class FormantEngine
{
public:
    static constexpr int fftSize = 2048;        // analysis = synthesis
    static constexpr int hopSize = 512;         // fftSize / 4
    static constexpr int latencySamples = 2048; // one full STFT window of delay

    /** Configures sample rate / block size, builds the FFT and ring buffers and
        warms the FFT.  All allocations happen here, off the RT path. */
    void prepare (double sampleRate, int maxBlockSize) noexcept;

    /** Destructor (PIMPL: out-of-line so the incomplete Impl is fine). */
    ~FormantEngine();

    /** Resets the streaming state (rings, sliding window, frame counter)
        without re-allocating.  The stale buffered audio must not be replayed
        when the engine is re-enabled (T13). */
    void clear() noexcept;

    FormantEngine() = default;

    FormantEngine (FormantEngine&&) noexcept = default;
    FormantEngine& operator= (FormantEngine&&) noexcept = default;
    FormantEngine (const FormantEngine&) = delete;
    FormantEngine& operator= (const FormantEngine&) = delete;

    /** Formant shift in semitones, clamped to [-12, +12]. Frame-boundary
        smoothed. */
    void setShiftSemitones (float semitones) noexcept;

    /** Envelope lifter quefrency in milliseconds, clamped to [0, 10].
        0 disables the envelope (flat gain = transparent); the APVTS parameter
        uses 0.5..5.0 ms.  Frame-boundary smoothed. */
    void setQuefrencyMs (float milliseconds) noexcept;

    /** Processes one block of audio.  RT safe (steady state). */
    void process (const float* input, float* output, int numSamples) noexcept;

    /** Fixed latency in samples (2048: one STFT window of causal OLA). */
    int getLatencySamples() const noexcept { return latencySamples; }

private:
    struct Impl;
    std::shared_ptr<Impl> impl;
};

} // namespace dsp
