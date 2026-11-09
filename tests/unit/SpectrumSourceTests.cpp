/*
    NeoVoiDrive T14 tests — SpectrumSource + MeterSource data bridges.

    Acceptance (TASKS.md T14):
      1. writer side: zero allocation, never blocks (drops when full);
      2. decimation: 192 kHz input -> ratio 4, FIFO holds every 4th sample;
      3. FIFO order preserved (push -> readBlock returns the same sequence);
      4. a full 2048-point FFT block is available and coherent under a
         30 Hz refresh cadence;
      5. reader stalls (UI closed) do not degrade the writer (no alloc, no
         block, no crash);
      6. MeterSource: peaks round-trip, readLatest returns the newest,
         overflow drops without blocking;
      7. integration: MultibandEngine meter tap -> MeterSource yields the
         actual output peaks.
*/

#include <juce_core/juce_core.h>

#include "dsp/MultibandEngine.h"
#include "ui/MeterSource.h"
#include "ui/SpectrumSource.h"

#include <cmath>
#include <vector>

//==============================================================================
//  Allocation audit: FormantEngineTests.cpp installs the global operator new
//  counter for the whole test binary; we read the same counter here (it is a
//  plain global, link-visible). No duplicate operator new definitions.
//==============================================================================
extern std::int64_t g_allocCount;

namespace
{

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;

//==============================================================================
class SpectrumSourceTest final : public juce::UnitTest
{
public:
    SpectrumSourceTest() : juce::UnitTest ("SpectrumSource") {}

    void runTest() override
    {
        beginTest ("push/read preserves order (full rate at 48 kHz)");
        {
            ui::SpectrumSource source;
            source.prepare (kSampleRate); // ratio 1

            std::vector<float> in (8192);
            for (int i = 0; i < 8192; ++i)
                in[static_cast<size_t> (i)] = static_cast<float> (i) * 0.001f;

            source.pushSamples (in.data(), 8192);
            expectEquals (source.available(), 8192);

            std::vector<float> out (8192, -1.0f);
            const auto n = source.readBlock (out.data(), 8192);
            expectEquals (n, 8192);

            bool same = true;
            for (int i = 0; i < 8192; ++i)
                if (out[static_cast<size_t> (i)] != in[static_cast<size_t> (i)]) { same = false; break; }
            expect (same, "read back order differs from push order");
            expectEquals (source.available(), 0);
        }

        beginTest ("decimation ratio follows the sample rate (192 kHz -> 4)");
        {
            ui::SpectrumSource source;
            source.prepare (192000.0); // ratio 4

            std::vector<float> in (16000);
            for (int i = 0; i < 16000; ++i)
                in[static_cast<size_t> (i)] = static_cast<float> (i);

            source.pushSamples (in.data(), 16000);
            expectEquals (source.available(), 4000); // every 4th sample

            std::vector<float> out (4000, -1.0f);
            const auto n = source.readBlock (out.data(), 4000);
            expectEquals (n, 4000);

            bool decimated = true;
            for (int i = 0; i < 4000; ++i)
                if (out[static_cast<size_t> (i)] != static_cast<float> (4 * i)) { decimated = false; break; }
            expect (decimated, "decimation did not keep every 4th sample");
        }

        beginTest ("full FFT block is available and coherent (30 Hz cadence)");
        {
            // Simulate 30 Hz UI refresh on 48 kHz: ~1600 input samples per
            // tick; a 2048-point window must become available and read back
            // in order (coherent window for the FFT).
            ui::SpectrumSource source;
            source.prepare (kSampleRate);

            std::vector<float> in (1600);
            for (int tick = 0; tick < 4; ++tick)
            {
                for (int i = 0; i < 1600; ++i)
                    in[static_cast<size_t> (i)] = static_cast<float> (tick * 1600 + i);
                source.pushSamples (in.data(), 1600);

                if (tick >= 1) // 3200 > 2048 accumulated
                {
                    expect (source.hasFullBlock(), "no full FFT block after two ticks");

                    // UI pattern: skip the stale head, then read the newest
                    // 2048-sample window (contiguous recent history).
                    source.skip (source.available() - ui::SpectrumSource::fftBlockSize);
                    float window[ui::SpectrumSource::fftBlockSize];
                    const auto n = source.readBlock (window, ui::SpectrumSource::fftBlockSize);
                    expectEquals (n, ui::SpectrumSource::fftBlockSize);

                    // After tick 1 the FIFO holds samples 0..3199; the newest
                    // 2048 are 1152..3199 (tick0 tail + tick1 head).
                    bool coherent = true;
                    for (int i = 0; i < 2048; ++i)
                    {
                        const auto expected = static_cast<float> (1152 + i);
                        if (window[static_cast<size_t> (i)] != expected) { coherent = false; break; }
                    }
                    expect (coherent, "FFT window is not the contiguous recent history");
                    break;
                }
            }
        }

        beginTest ("P2-1 readNewestBlock keeps the displayed window one block behind the writer");
        {
            ui::SpectrumSource source;
            source.prepare (kSampleRate);

            // Emulate the 60 Hz UI tick: 1600 new samples per refresh. The
            // production reader must discard the stale head and return the
            // newest 2048 samples — never a window that drifts behind.
            int written = 0;
            int checked = 0;
            std::vector<float> tick (1600);
            std::vector<float> window (ui::SpectrumSource::fftBlockSize);

            for (int t = 0; t < 10; ++t)
            {
                for (int i = 0; i < 1600; ++i)
                    tick[static_cast<size_t> (i)] = static_cast<float> (written + i);
                source.pushSamples (tick.data(), 1600);
                written += 1600;

                if (source.available() < ui::SpectrumSource::fftBlockSize)
                    continue;

                const auto n = source.readNewestBlock (window.data(),
                                                       ui::SpectrumSource::fftBlockSize);
                expectEquals (n, ui::SpectrumSource::fftBlockSize,
                              "full block not returned");
                expectEquals (window.back(), static_cast<float> (written - 1),
                              "window does not end at the newest sample");
                expectEquals (window.front(),
                              static_cast<float> (written - ui::SpectrumSource::fftBlockSize),
                              "window is not exactly one block behind the writer");
                ++checked;
            }

            expectGreaterThan (checked, 0, "no full blocks were checked");
            expectLessThan (source.available(), ui::SpectrumSource::fftBlockSize,
                            "reader left more than one block behind");

            source.discardAll();
            expectEquals (source.available(), 0, "discardAll left data behind");

            // A hidden source (drained every tick) cannot build a backlog:
            // after the switch the next full window is fresh.
            ui::SpectrumSource hidden;
            hidden.prepare (kSampleRate);
            for (int t = 0; t < 20; ++t)
            {
                hidden.pushSamples (tick.data(), 1600);
                if (t < 18) // two ticks of fresh data after the switch
                    hidden.discardAll();
            }
            const auto n = hidden.readNewestBlock (window.data(),
                                                   ui::SpectrumSource::fftBlockSize);
            expectEquals (n, ui::SpectrumSource::fftBlockSize);
            expect (hidden.available() < ui::SpectrumSource::fftBlockSize,
                    "drained source accumulated a backlog");
        }

        beginTest ("writer never blocks or allocates when the reader stalls");
        {
            ui::SpectrumSource source;
            source.prepare (kSampleRate);

            std::vector<float> in (8192, 0.25f);
            std::vector<float> out (8192, -1.0f);

            // No reader: push several times the capacity — must not block
            // and must not allocate (drop-new semantics: the FIFO keeps the
            // oldest 16384, the rest is dropped).
            for (int round = 0; round < 4; ++round)
                source.pushSamples (in.data(), 8192); // warm-up rounds
            const auto mid = g_allocCount;
            for (int round = 0; round < 4; ++round)
                source.pushSamples (in.data(), 8192);

            const auto allocated = g_allocCount - mid;
            expectEquals (static_cast<int> (allocated), 0, "writer allocated while stalled");
            expect (source.available() > 0, "FIFO emptied while stalled");
            expectLessThan (source.available(), 8192 * 8, "overflow did not drop");

            // Written data is intact and in order (all samples identical here).
            const auto n = source.readBlock (out.data(), 8192);
            expectEquals (n, 8192);
            bool intact = true;
            for (int i = 0; i < 8192; ++i)
                if (out[static_cast<size_t> (i)] != 0.25f) { intact = false; break; }
            expect (intact, "stalled FIFO corrupted the buffered data");
        }

        beginTest ("writer stays zero-allocation with an active reader");
        {
            ui::SpectrumSource source;
            source.prepare (kSampleRate);

            std::vector<float> in (kBlock, 0.5f);
            std::vector<float> out (kBlock, 0.0f);

            // Warm up one round (one-time initialization, if any), then
            // measure the steady state.
            source.pushSamples (in.data(), kBlock);
            source.readBlock (out.data(), kBlock);
            const auto before = g_allocCount;

            for (int block = 0; block < 500; ++block)
            {
                source.pushSamples (in.data(), kBlock);
                source.readBlock (out.data(), kBlock);
            }
            const auto allocated = g_allocCount - before;
            expectEquals (static_cast<int> (allocated), 0, "push/read allocated");
        }

        beginTest ("stereo push mono-downmixes (O4)");
        {
            ui::SpectrumSource src;
            src.prepare (48000.0);

            // Left-only signal must appear at half amplitude after the
            // 0.5 * (L + R) downmix; full-scale on both stays full-scale.
            std::array<float, 16> l {}, r {};
            l.fill (0.8f);
            r.fill (0.0f);
            src.pushSamples (l.data(), r.data(), 16);

            std::array<float, 64> buf {};
            const auto n = src.readBlock (buf.data(), static_cast<int> (buf.size()));
            expect (n == 16, "decimation ratio 1 at 48k -> 16 samples");
            bool ok = true;
            for (int i = 0; i < 16; ++i)
            {
                const auto expected = 0.5f * (0.8f + 0.0f);
                if (std::abs (buf[static_cast<size_t> (i)] - expected) > 1e-5f)
                    ok = false;
            }
            expect (ok, "stereo push did not downmix 0.5 * (L + R)");
        }

        beginTest ("reset empties the FIFO and restarts decimation");
        {
            ui::SpectrumSource source;
            source.prepare (192000.0);

            std::vector<float> in (400, 1.0f);
            source.pushSamples (in.data(), 400);
            expect (source.available() > 0);

            source.reset();
            expectEquals (source.available(), 0);

            // Decimation counter restarted: sample 0 is captured again.
            in.assign (400, 2.0f);
            source.pushSamples (in.data(), 400);
            expectEquals (source.available(), 100);
        }

        beginTest ("MeterSource: peaks round-trip, readLatest returns newest");
        {
            ui::MeterSource meter;

            float l = 0.0f, r = 0.0f;
            expect (! meter.readLatest (l, r), "empty meter reported peaks");

            meter.pushPeaks (0.1f, 0.2f);
            meter.pushPeaks (0.3f, 0.4f);
            meter.pushPeaks (0.5f, 0.6f);

            expectEquals (meter.available(), 3);
            expect (meter.readLatest (l, r), "readLatest empty");
            expectWithinAbsoluteError (l, 0.5f, 1e-6f);
            expectWithinAbsoluteError (r, 0.6f, 1e-6f);
            expectEquals (meter.available(), 0, "readLatest must drain");

            // readOldest preserves order.
            meter.pushPeaks (0.7f, 0.8f);
            meter.pushPeaks (0.9f, 1.0f);
            expect (meter.readOldest (l, r), "readOldest empty");
            expectWithinAbsoluteError (l, 0.7f, 1e-6f);
        }

        beginTest ("MeterSource: overflow drops without blocking or allocating");
        {
            ui::MeterSource meter;
            float l = 0.0f, r = 0.0f;

            // Warm up one round, then measure the steady state.
            meter.pushPeaks (0.0f, 0.0f);
            meter.readLatest (l, r);
            const auto before = g_allocCount;

            for (int i = 0; i < ui::MeterSource::meterCapacity * 4; ++i)
                meter.pushPeaks (static_cast<float> (i) * 0.01f, 0.0f);

            const auto allocated = g_allocCount - before;
            expectEquals (static_cast<int> (allocated), 0, "meter writer allocated");
            // Drop-new semantics: the FIFO keeps the earliest `capacity`
            // items, so readLatest returns the last surviving write
            // (index capacity-1), not the final write.
            expectEquals (meter.available(), ui::MeterSource::meterCapacity);
            expect (meter.readLatest (l, r));
            const auto lastSurviving = static_cast<float> (ui::MeterSource::meterCapacity - 1) * 0.01f;
            expectWithinAbsoluteError (l, lastSurviving, 1e-4f);
        }

        beginTest ("integration: engine meter tap feeds MeterSource the output peaks");
        {
            ui::MeterSource meter;
            dsp::MultibandEngine engine;
            engine.prepare (kSampleRate, kBlock);
            engine.setMeterTap ([] (void*, float l, float r) noexcept
                                {
                                    meterPushed = true;
                                    meterLastL = l;
                                    meterLastR = r;
                                }, nullptr);

            params::ParamSnapshot snap; // defaults: transparent chain
            engine.setParameters (snap);

            std::vector<float> in (static_cast<size_t> (kBlock), 0.0f);
            std::vector<float> out (static_cast<size_t> (kBlock), 0.0f);
            std::vector<float> bypass (static_cast<size_t> (kBlock), 1.0f);
            std::vector<float> formant (static_cast<size_t> (kBlock), 0.0f);

            for (int i = 0; i < kBlock; ++i)
                in[static_cast<size_t> (i)] = 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * i / static_cast<float> (kSampleRate));

            engine.process (in.data(), in.data(), out.data(), out.data(), kBlock, 2,
                            bypass.data(), formant.data());

            expect (meterPushed, "meter tap not invoked");
            // Transparent chain (with the exciter's fixed latency): the
            // output peaks equal the input peaks (0.5 amplitude sine).
            float peak = 0.0f;
            for (int i = 0; i < kBlock; ++i)
                peak = juce::jmax (peak, std::abs (out[static_cast<size_t> (i)]));
            expectWithinAbsoluteError (meterLastL, peak, 1e-3f);
            expectWithinAbsoluteError (meterLastR, peak, 1e-3f);
        }

        beginTest ("integration: engine tap -> MeterSource bridge end to end");
        {
            ui::MeterSource meter;
            dsp::MultibandEngine engine;
            engine.prepare (kSampleRate, kBlock);
            engine.setMeterTap ([] (void*, float l, float r) noexcept
                                {
                                    bridgeL = l;
                                    bridgeR = r;
                                }, nullptr);

            params::ParamSnapshot snap;
            snap.bands[1].shimmerDepth = 60.0f; // make the chain audible
            engine.setParameters (snap);

            std::vector<float> in (static_cast<size_t> (kBlock), 0.0f);
            std::vector<float> out (static_cast<size_t> (kBlock), 0.0f);
            std::vector<float> bypass (static_cast<size_t> (kBlock), 1.0f);
            std::vector<float> formant (static_cast<size_t> (kBlock), 0.0f);

            for (int i = 0; i < kBlock; ++i)
                in[static_cast<size_t> (i)] = 0.8f * std::sin (2.0f * juce::MathConstants<float>::pi * 200.0f * i / static_cast<float> (kSampleRate));

            engine.process (in.data(), in.data(), out.data(), out.data(), kBlock, 2,
                            bypass.data(), formant.data());

            float peak = 0.0f;
            for (int i = 0; i < kBlock; ++i)
                peak = juce::jmax (peak, std::abs (out[static_cast<size_t> (i)]));
            expectWithinAbsoluteError (bridgeL, peak, 1e-3f);
            expectWithinAbsoluteError (bridgeR, peak, 1e-3f);
        }
    }

private:
    static inline bool meterPushed = false;
    static inline float meterLastL = 0.0f;
    static inline float meterLastR = 0.0f;
    static inline float bridgeL = 0.0f;
    static inline float bridgeR = 0.0f;
};

SpectrumSourceTest spectrumSourceTest;

} // namespace
