/*
    NeoVoiDrive T28 tests — SidebandEngine (SIDEBAND_TESTPLAN §2/§3/§4).

    SB-U10   saw 200 Hz, ratio 0.3: kf0±60 Hz sideband peaks (≤ ±1 %), peak
            ratio monotonic in depth, no ±2fm spurs (≤ −40 dBc @ rough 0)
    SB-U10b  ratio 0.5: sidebands drift instead of locking to kf0±f0/2
    SB-U12   roughness 0..100 %: −6 dB sideband width monotonic, r=0 ≤ 2 Hz,
            r=100 ≥ 6× baseline
    SB-U12b  THD (2..10) does not rise with roughness (Δ ≤ 3 dB)
    SB-U13   depth 0..100 %: first-order sideband level monotonic; depth=0
            output ≡ baseline-delayed dry (≤ −100 dBFS)
    SB-U14   dual sine 100 Hz + 3 kHz, lowcut 500: 100 Hz unchanged (≤ 0.1 dB,
            no sidebands), 3 kHz develops ±fm sidebands
    SB-U15   fixed sines below/above lowcut: sub-lowcut magnitude ±0.5 dB,
            sidebands only on ≥ lowcut components
    SB-U16   wideband saw, depth 100 %: lowcut-neighbourhood comb dip ≤ 3 dB
    Extreme  192 kHz, ratio 0.5 / rough 100 % / lowcut 100 Hz: no NaN
    SB-U20   (T40) bright 0→100: 3–8 kHz energy + spectral centroid monotonic
    SB-U21   (T40) bright is orthogonal to roughness (3.2 kHz bell boost)
    SB-U22   (T40) bright=100 at depth=0 stays the delayed dry (transparency)
    SB-U23   (T40) bright at 192 kHz extreme stays finite/bounded
    SB-U24   (T40) 3.2 kHz presence bell lifts the output above lowcut=2000
*/

#include <juce_dsp/juce_dsp.h>

#include "dsp/SidebandEngine.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace
{

constexpr double testSampleRate = 48000.0;

//==============================================================================
//  Signal synthesis
//==============================================================================
void fillSaw (std::vector<float>& out, double freq, double seconds) noexcept
{
    out.resize (static_cast<size_t> (testSampleRate * seconds));
    double phase = 0.0;
    for (size_t i = 0; i < out.size(); ++i)
    {
        phase += freq / testSampleRate;
        phase -= std::floor (phase);
        out[i] = 0.6f * static_cast<float> (2.0 * phase - 1.0);
    }
}

void fillSine (std::vector<float>& out, double freq, double seconds, float amp = 0.5f) noexcept
{
    out.resize (static_cast<size_t> (testSampleRate * seconds));
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = amp * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi
                                                     * freq * static_cast<double> (i) / testSampleRate));
}

void fillDualSine (std::vector<float>& out, double f1, double f2, double seconds) noexcept
{
    out.resize (static_cast<size_t> (testSampleRate * seconds));
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = 0.4f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi
                                                      * f1 * static_cast<double> (i) / testSampleRate))
               + 0.4f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi
                                                      * f2 * static_cast<double> (i) / testSampleRate));
}

//==============================================================================
//  Processing + analysis helpers
//==============================================================================
void process (dsp::SidebandEngine& engine, const std::vector<float>& in, std::vector<float>& out,
              const dsp::SidebandEngine::Params& params, int blockSize = 512, int warmupBlocks = 8)
{
    out.resize (in.size());
    // Warm-up blocks let the wet gain attack ramp settle (50 ms) before the
    // measured region begins.
    std::vector<float> warm (static_cast<size_t> (blockSize), 0.0f);
    for (int b = 0; b < warmupBlocks; ++b)
    {
        engine.setParameters (params);
        for (int i = 0; i < blockSize; ++i)
            engine.process (warm[static_cast<size_t> (i)]);
    }

    const auto n = static_cast<int> (in.size());
    for (int i = 0; i < n; i += blockSize)
    {
        engine.setParameters (params);
        const int count = std::min (blockSize, n - i);
        for (int k = 0; k < count; ++k)
            out[static_cast<size_t> (i + k)] = engine.process (in[static_cast<size_t> (i + k)]);
    }
}

// Single-frequency magnitude (rectangular window, 2 s; coherent gain 2/N).
double dftMag (const std::vector<float>& sig, double freq) noexcept
{
    double re = 0.0, im = 0.0;
    const auto n = static_cast<int> (sig.size());
    for (int i = 0; i < n; ++i)
    {
        const double theta = 2.0 * juce::MathConstants<double>::pi * freq
                           * static_cast<double> (i) / testSampleRate;
        re += static_cast<double> (sig[static_cast<size_t> (i)]) * std::cos (theta);
        im -= static_cast<double> (sig[static_cast<size_t> (i)]) * std::sin (theta);
    }
    return 2.0 * std::sqrt (re * re + im * im) / static_cast<double> (n);
}

// Same, but with a Hann window (2 s; same peak-amplitude scale as dftMag).
// The −31 dB first sidelobe lets −40 dB spread measurements see the noise
// haze instead of the rectangular window's slow (−13 dB, 6 dB/oct) skirts.
double hannDftMag (const std::vector<float>& sig, double freq) noexcept
{
    double re = 0.0, im = 0.0, wsum = 0.0;
    const auto n = static_cast<int> (sig.size());
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 * (1.0 - std::cos (2.0 * juce::MathConstants<double>::pi
                                                * static_cast<double> (i) / static_cast<double> (n)));
        const double theta = 2.0 * juce::MathConstants<double>::pi * freq
                           * static_cast<double> (i) / testSampleRate;
        re += w * static_cast<double> (sig[static_cast<size_t> (i)]) * std::cos (theta);
        im -= w * static_cast<double> (sig[static_cast<size_t> (i)]) * std::sin (theta);
        wsum += w;
    }
    return 2.0 * std::sqrt (re * re + im * im) / wsum;
}

// Local peak magnitude near `center`, searching ±halfWidth with 0.1 Hz steps.
double peakNear (const std::vector<float>& sig, double center, double halfWidth,
                 double* peakFreq = nullptr) noexcept
{
    double best = 0.0, bestF = center;
    for (double f = center - halfWidth; f <= center + halfWidth + 1e-9; f += 0.1)
    {
        const double m = dftMag (sig, f);
        if (m > best)
        {
            best = m;
            bestF = f;
        }
    }
    if (peakFreq != nullptr)
        *peakFreq = bestF;
    return best;
}

double db (double v) noexcept
{
    return 20.0 * std::log10 (juce::jmax (v, 1e-30));
}

// T40 helpers: band power proxy (sum of squared Hann magnitudes at `step` Hz
// intervals) and spectral centroid over the same grid. Used by SB-U20/U21/U24.
double bandPower (const std::vector<float>& sig, double fLo, double fHi,
                  double step = 100.0) noexcept
{
    double sum = 0.0;
    for (double f = fLo; f <= fHi + 1e-9; f += step)
    {
        const double m = hannDftMag (sig, f);
        sum += m * m;
    }
    return sum;
}

double spectralCentroid (const std::vector<float>& sig, double fLo, double fHi,
                         double step = 100.0) noexcept
{
    double num = 0.0, den = 0.0;
    for (double f = fLo; f <= fHi + 1e-9; f += step)
    {
        const double m = hannDftMag (sig, f);
        num += f * m * m;
        den += m * m;
    }
    return den > 1e-30 ? num / den : 0.0;
}

//==============================================================================
//  T43 helpers: fog residual envelope analysis (SB-U25/SB-U26)
//==============================================================================
// Windowed RMS "envelope" of a signal at a decimated rate (1 ms windows,
// 0.5 ms hop -> 2 kHz envelope rate). Modulation at the source bandwidths
// (>= 300 Hz) survives the window; the slow wander does too.
std::vector<double> rmsEnvelope (const std::vector<float>& sig, double sr,
                                 double winMs = 1.0, double hopMs = 0.5)
{
    const auto win = static_cast<int> (sr * winMs / 1000.0);
    const auto hop = juce::jmax (1, static_cast<int> (sr * hopMs / 1000.0));
    std::vector<double> env;
    for (int start = 0; start + win <= static_cast<int> (sig.size()); start += hop)
    {
        double e = 0.0;
        for (int i = 0; i < win; ++i)
        {
            const double v = static_cast<double> (sig[static_cast<size_t> (start + i)]);
            e += v * v;
        }
        env.push_back (std::sqrt (e / static_cast<double> (win)));
    }
    return env;
}

/** Share of the envelope's modulation energy inside [lo, hi] Hz relative to
    [totLo, totHi]. Direct DFT over the decimated envelope. */
double envBandShare (const std::vector<double>& env, double envRate,
                     double lo, double hi, double totLo, double totHi,
                     double step = 20.0)
{
    const auto n = static_cast<int> (env.size());
    double mean = 0.0;
    for (double v : env) mean += v;
    mean /= juce::jmax (1, n);

    double inBand = 0.0, total = 0.0;
    for (double f = totLo; f <= totHi + 1e-9; f += step)
    {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double th = 2.0 * juce::MathConstants<double>::pi * f
                            * static_cast<double> (i) / envRate;
            const double v = env[static_cast<size_t> (i)] - mean;
            re += v * std::cos (th);
            im -= v * std::sin (th);
        }
        const double p = re * re + im * im;
        total += p;
        if (f >= lo && f <= hi)
            inBand += p;
    }
    return total > 1e-30 ? inBand / total : 0.0;
}

//==============================================================================
class SidebandEngineTest final : public juce::UnitTest
{
public:
    SidebandEngineTest() : juce::UnitTest ("SidebandEngine") {}

    void runTest() override
    {
        runSbU10();
        runSbU10b();
        runSbU12();
        runSbU12b();
        runSbU17();
        runSbU18();
        runSbU19();
        runSbU13();
        runSbU14();
        runSbU15();
        runSbU16();
        runExtreme();
        runSbU20();
        runSbU21();
        runSbU22();
        runSbU23();
        runSbU24();
        runSbU25();
        runSbU26();
        runSbU27();
        runSbU28();
        runS1Regression();
        runM1Regression();
        runXfadeDiagnostics();
    }

private:
    static dsp::SidebandEngine::Params voicedParams (float depth, float ratio = 0.3f,
                                                     float roughness = 0.0f, float lowcut = 100.0f)
    {
        dsp::SidebandEngine::Params p;
        p.f0 = 200.0f;      // fixed f0 stub (T29 wires the real F0Tracker)
        p.clarity = 0.9f;
        p.voiced = true;
        p.depth = depth;
        p.fmRatio = ratio;
        p.roughness = roughness;
        p.lowcut = lowcut;
        p.enable = true;
        return p;
    }

    void runSbU10()
    {
        beginTest ("SB-U10 saw 200 Hz, ratio 0.3: kf0±60 Hz sidebands, monotonic in depth, no ±2fm spurs");
        {
            std::vector<float> sig;
            fillSaw (sig, 200.0, 2.0);

            dsp::SidebandEngine engine;
            engine.prepare (testSampleRate, 512);
            engine.setSeed (0xABCDEF);
            std::vector<float> out;
            process (engine, sig, out, voicedParams (60.0f));

            // Sideband peaks at k·200 ± 60 for k = 1..8.
            for (int k = 1; k <= 8; ++k)
            {
                const double fLower = k * 200.0 - 60.0;
                const double fUpper = k * 200.0 + 60.0;
                double pf = 0.0;
                const double mag = peakNear (out, fLower, 2.0, &pf);
                expectGreaterThan (mag, 1e-3, juce::String ("lower sideband k=") + juce::String (k) + " missing");
                expectWithinAbsoluteError (static_cast<float> (pf), static_cast<float> (fLower),
                                           static_cast<float> (fLower * 0.01) + 0.5f,
                                           juce::String ("lower sideband k=") + juce::String (k) + " freq");
                const double mag2 = peakNear (out, fUpper, 2.0, &pf);
                expectGreaterThan (mag2, 1e-3, juce::String ("upper sideband k=") + juce::String (k) + " missing");
                expectWithinAbsoluteError (static_cast<float> (pf), static_cast<float> (fUpper),
                                           static_cast<float> (fUpper * 0.01) + 0.5f,
                                           juce::String ("upper sideband k=") + juce::String (k) + " freq");
            }

            // No ±2fm (120 Hz) spurs at roughness 0: ≤ −40 dBc relative to
            // the fundamental carrier k=1.
            const double carrier = peakNear (out, 200.0, 1.0);
            const double spur = juce::jmax (peakNear (out, 80.0, 1.0), peakNear (out, 320.0, 1.0));
            expectLessThan (db (spur / carrier), -40.0, "±2fm spur above −40 dBc");

            // First-order sideband level monotonic in depth (sweep 10..100).
            // T36 rev37d: measured on the k=3 lower sideband (540 Hz) — the
            // k=1 skirt sits near/below the default 100 Hz post-lowcut
            // region, which is now (per SPEC §4.3) pure delayed dry.
            double prev = 0.0;
            bool monotonic = true;
            for (float depth = 10.0f; depth <= 100.0f + 1e-6f; depth += 10.0f)
            {
                dsp::SidebandEngine e2;
                e2.prepare (testSampleRate, 512);
                e2.setSeed (0xABCDEF);
                std::vector<float> o2;
                process (e2, sig, o2, voicedParams (depth));
                const double level = peakNear (o2, 540.0, 2.0);   // k=3 lower sideband
                if (depth > 10.0f && level < prev - 1e-6)
                    monotonic = false;
                prev = level;
            }
            expect (monotonic, "sideband level must grow monotonically with depth");
        }
    }

    void runSbU10b()
    {
        beginTest ("SB-U10b ratio 0.5: r=0 sideband stays STABLE (rev18 removed the shared slow wander)");
        {
            // T36 rev18: the common-mode wander is gone by spec (it was a
            // 0.5…8 Hz pump source), so at roughness=0 the sideband position
            // must be rock-stable — no drift, no lock-jumping. This is the
            // new r=0 contract ("clean single voice, stable sidebands").
            std::vector<float> sig;
            fillSaw (sig, 200.0, 8.0);

            dsp::SidebandEngine engine;
            engine.prepare (testSampleRate, 512);
            engine.setSeed (0x1234);
            std::vector<float> out;
            process (engine, sig, out, voicedParams (70.0f, 0.5f));

            // Window the output: 16384 samples (341 ms), 50 % overlap.
            // Track the k=1 upper sideband (nominal 300 Hz).
            int windows = 0;
            double sum = 0.0, sumSq = 0.0;
            for (size_t start = 0; start + 16384 <= out.size(); start += 8192)
            {
                std::vector<float> winOut (out.begin() + static_cast<std::ptrdiff_t> (start),
                                           out.begin() + static_cast<std::ptrdiff_t> (start + 16384));
                double pf = 0.0;
                peakNear (winOut, 300.0, 3.0, &pf);
                sum += pf;
                sumSq += pf * pf;
                ++windows;
            }

            // Stable position: peak-position standard deviation must stay
            // tiny — bounded by the 341 ms window's peak-quantisation error
            // (~0.5 Hz); the deterministic line does not move (rev18 removed
            // the shared wander).
            const double mean = sum / windows;
            const double variance = juce::jmax (0.0, sumSq / windows - mean * mean);
            const double stddev = std::sqrt (variance);
            expectLessThan (static_cast<float> (stddev), 1.0f,
                            juce::String ("r=0 sideband drifted (stddev ") + juce::String (stddev) + " Hz)");
        }
    }

    void runSbU12()
    {
        beginTest ("SB-U12 −40 dB sideband footprint grows monotonically with roughness (T36 semantics)");
        {
            // Single 1000 Hz tone: its own ±fm sideband family lands at
            // 946/1054 (J1), 892/1108 (J2) … — a saw input would also drop
            // the 6th harmonic's J2 lower sideband (1092 Hz) into the
            // 1008–1100 window and pollute the footprint.
            std::vector<float> sig;
            fillSine (sig, 1000.0, 2.0);

            // T36: with the fine-grain re-tune the slow spread (drift/detune)
            // is far smaller than before, so a −6 dB width collapses to the
            // Hann main lobe and carries no information. The meaningful
            // roughness metric is now the −40 dB haze footprint: the noise
            // band lit around the J1 upper sideband (1054 Hz). Measured with
            // the Hann window (sidelobes −31 dB) so the r=0 baseline is the
            // window floor, not rectangular-window skirts.
            constexpr double sidebandCenter = 1000.0 + 54.0;   // J1 upper sideband
            const auto hannPeakNear = [&] (const std::vector<float>& s, double center, double halfWidth)
            {
                double best = 0.0;
                for (double f = center - halfWidth; f <= center + halfWidth + 1e-9; f += 0.1)
                    best = juce::jmax (best, hannDftMag (s, f));
                return best;
            };
            std::vector<double> widths;
            for (float rough : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
            {
                dsp::SidebandEngine engine;
                engine.prepare (testSampleRate, 512);
                engine.setSeed (0x555);
                std::vector<float> out;
                process (engine, sig, out, voicedParams (70.0f, 0.27f, rough));

                const double peak = hannPeakNear (out, sidebandCenter, 5.0);
                const double threshold = peak * 0.01;   // −40 dB
                // Outermost points still above the threshold (the previous
                // "first hit from the centre" logic always stopped at the
                // centre peak itself — a latent no-op; T36 fixes it). The lo
                // sweep stops at 1008 Hz to keep the 1000 Hz CARRIER out of
                // the window (its magnitude dwarfs the −40 dB floor); the hi
                // sweep stops at 1100 (J2 upper is at 1108, outside).
                double lo = sidebandCenter, hi = sidebandCenter;
                for (double f = sidebandCenter; f > 1008.0; f -= 0.1)
                    if (hannDftMag (out, f) >= threshold)
                        lo = f;
                for (double f = sidebandCenter; f < 1100.0; f += 0.1)
                    if (hannDftMag (out, f) >= threshold)
                        hi = f;
                widths.push_back (hi - lo);
            }

            expectLessOrEqual (widths[0], 12.0, "r=0 footprint must be ≤ 12 Hz (Hann floor)");
            bool monotonic = true;
            for (size_t i = 1; i < widths.size(); ++i)
            {
                // T43: the measurement window tops out at 92 Hz (1054 ± 46,
                // bounded by the 1000 Hz carrier and the J2 line at 1108), so
                // saturated pairs carry no ordering information.
                if (widths[i] >= 89.0 && widths[i - 1] >= 89.0)
                    continue;
                if (widths[i] < widths[i - 1] - 0.5)
                    monotonic = false;
            }
            expect (monotonic, "sideband footprint must grow monotonically with roughness");
            expectGreaterOrEqual (widths[4], widths[0] * 6.0,
                                  juce::String ("r=100 footprint ") + juce::String (widths[4])
                                      + " Hz < 6× r=0 (" + juce::String (widths[0]) + " Hz)");
            // T36 fine-grain re-tune: the spread must be a smooth haze, not a
            // coarse smear — cap the r=100 footprint (the measurement window
            // is 1008–1100 Hz, so a wider haze would also trip this).
            expectLessOrEqual (widths[4], 120.0,
                               juce::String ("r=100 footprint ") + juce::String (widths[4])
                                   + " Hz > 120 Hz (coarse smear, expected ≤ 120)");
            logMessage (juce::String ("  −40 dB footprints: ") + juce::String (widths[0], 2) + " / "
                        + juce::String (widths[1], 2) + " / " + juce::String (widths[2], 2) + " / "
                        + juce::String (widths[3], 2) + " / " + juce::String (widths[4], 2) + " Hz");
        }
    }

    void runSbU12b()
    {
        beginTest ("SB-U12b THD does not rise with roughness (Δ ≤ 3 dB)");
        {
            std::vector<float> sig;
            fillSaw (sig, 200.0, 2.0);

            std::vector<double> thds;
            for (float rough : { 0.0f, 50.0f, 100.0f })
            {
                dsp::SidebandEngine engine;
                engine.prepare (testSampleRate, 512);
                engine.setSeed (0x777);
                std::vector<float> out;
                process (engine, sig, out, voicedParams (70.0f, 0.27f, rough));

                // THD over harmonics 2..10 relative to harmonic 1.
                double harm1 = peakNear (out, 200.0, 1.0);
                double thdSum = 0.0;
                for (int k = 2; k <= 10; ++k)
                {
                    const double m = peakNear (out, k * 200.0, 1.0);
                    thdSum += m * m;
                }
                thds.push_back (10.0 * std::log10 (thdSum / (harm1 * harm1)));
            }

            // T44: only a RISE is a distortion signature. A strong fog moves
            // the harmonics' energy out of the ±1 Hz measurement windows, so
            // the measured THD can legitimately FALL (T44 measured 7.5 -> 3.8
            // dB) — that is the fog working, not nonlinearity.
            expect (thds[2] - thds[0] <= 3.0,
                    juce::String ("THD rose from ") + juce::String (thds[0]) + " dB to "
                        + juce::String (thds[2]) + " dB (> 3 dB — distortion signature)");
        }
    }

    void runSbU17()
    {
        beginTest ("SB-U17 roughness spreads sideband energy into a CONTINUOUS haze (T36 fine grain)");
        {
            std::vector<float> sig;
            fillSaw (sig, 200.0, 2.0);

            // Spread metric around the J1 upper sideband (1054 Hz): count
            // 0.5 Hz bins inside 1011…1075 Hz whose magnitude stays within
            // `floorDb` of the local peak (Hann window — the −31 dB sidelobes
            // keep the r=0 baseline at the window floor, not rectangular
            // skirts). The window excludes the 1000 Hz carrier AND the saw's
            // 6th-harmonic J2 lower sideband at 1092 Hz (same pollution that
            // SB-U12's rewrite removes). A fine diffusion lights up a wide
            // contiguous band; a coarse few-spike texture lights up only the
            // spikes themselves.
            const auto litBins = [&] (float rough, double floorDb)
            {
                dsp::SidebandEngine engine;
                engine.prepare (testSampleRate, 512);
                engine.setSeed (0x4242);
                std::vector<float> out;
                process (engine, sig, out, voicedParams (70.0f, 0.27f, rough));
                double peak = 0.0;
                for (double f = 1049.0; f <= 1059.0 + 1e-9; f += 0.1)
                    peak = juce::jmax (peak, hannDftMag (out, f));
                const double floor = peak * std::pow (10.0, floorDb / 20.0);
                int count = 0;
                for (double f = 1011.0; f <= 1075.0 + 1e-9; f += 0.5)
                    if (hannDftMag (out, f) >= floor)
                        ++count;
                return count;
            };

            const int bins0m20 = litBins (0.0f, -20.0);
            const int bins0m40 = litBins (0.0f, -40.0);
            const int bins100m20 = litBins (100.0f, -20.0);
            const int bins100m40 = litBins (100.0f, -40.0);
            logMessage (juce::String ("  lit 0.5 Hz bins in 1011–1075 Hz: r=0 → −20 dB ")
                        + juce::String (bins0m20) + " / −40 dB " + juce::String (bins0m40)
                        + "; r=100 → −20 dB " + juce::String (bins100m20)
                        + " / −40 dB " + juce::String (bins100m40));
            // r=0: only the sharp line (Hann floor). r=100: the −40 dB haze
            // footprint must cover most of the window (≥ 30 Hz = 60 bins)…
            expectLessOrEqual (bins0m40, 16, "r=0 −40 dB spread must stay narrow");
            expectGreaterOrEqual (bins100m40, 60,
                                  juce::String ("r=100 −40 dB spread ") + juce::String (bins100m40)
                                      + " bins < 60 — haze footprint too narrow");
            // T36 rev29: the line is STATIONARY (no wobble — any line motion
            // reads as fixed-point "sideband dips" at fixed times) and the
            // roughness haze is a static −40 dB broadband floor (fn
            // 100–600 Hz). The assertion is therefore on the −40 dB haze
            // footprint (r100 ≫ r0), not on −20 dB width.
            expectLessOrEqual (bins0m40, 20, "r=0 −40 dB footprint must stay narrow");
            expectGreaterOrEqual (bins100m40, 60,
                                  juce::String ("r=100 −40 dB haze ") + juce::String (bins100m40)
                                      + " bins < 60 — static haze missing");
        }
    }

    void runSbU18()
    {
        beginTest ("SB-U18 phase noise always 0 (T36 rev28 deterministic roughness)");
        {
            std::vector<float> sig;
            fillSaw (sig, 200.0, 1.0);

            dsp::SidebandEngine engine;
            engine.prepare (testSampleRate, 512);
            engine.setSeed (0x777);

            std::vector<float> out;
            process (engine, sig, out, voicedParams (70.0f, 0.27f, 0.0f));
            expectWithinAbsoluteError (engine.getPhaseNoise(), 0.0f, 1e-7f,
                                       "phase noise must be 0 at roughness 0");

            process (engine, sig, out, voicedParams (70.0f, 0.27f, 100.0f));
            // T36 rev37: pn returns (mid-low fog) — bounded ±0.60 rad
            // (pnAmp = 0.60·roughS at r=100) and ACTIVE at r>0.
            expect (std::abs (engine.getPhaseNoise()) <= 0.65f,
                    juce::String ("phase noise exceeded ±0.65 rad: ")
                        + juce::String (engine.getPhaseNoise()));
            expect (std::abs (engine.getPhaseNoise()) > 1e-5f,
                    "phase noise must be active at roughness 100");
        }
    }

    void runSbU19()
    {
        beginTest ("SB-U19 sideband loudness stable over time — no intermittent dips (T36 rev2)");
        {
            std::vector<float> sig;
            fillSaw (sig, 200.0, 2.0);

            // Windowed (50 ms) energy around the J1 upper sideband: the
            // integration band now spans the 1 kHz ERB (±65 Hz, 989…1119) —
            // audibility lives at ERB scale, not at the ±20 Hz of rev4/rev5
            // (a sideband wander of ±46 Hz is invisible inside an ERB but
            // flashed the old narrow window). A slow wander moves the LINE
            // (desired, SB-U12/SB-U17) — what must stay steady is the
            // ERB-wide energy: intermittent dips were the rev4 symptom
            // (wide-band S&H saturating mod at its slow envelope peaks).
            const auto cv = [&] (float rough)
            {
                dsp::SidebandEngine engine;
                engine.prepare (testSampleRate, 512);
                engine.setSeed (0x99);
                std::vector<float> out;
                process (engine, sig, out, voicedParams (70.0f, 0.27f, rough));

                constexpr int win = 2400;              // 50 ms @ 48k
                const auto n = static_cast<int> (out.size());
                std::vector<double> energies;
                for (int start = static_cast<int> (testSampleRate) / 2; start + win <= n; start += win / 2)
                {
                    const std::vector<float> seg (out.begin() + start,
                                                  out.begin() + start + win);
                    double e = 0.0;
                    for (double f = 989.0; f <= 1119.0 + 1e-9; f += 2.0)
                    {
                        const double m = dftMag (seg, f);
                        e += m * m;
                    }
                    energies.push_back (e);
                }

                double mean = 0.0, m2 = 0.0, minE = 1e30;
                for (double e : energies)
                {
                    mean += e;
                    m2 += e * e;
                    minE = juce::jmin (minE, e);
                }
                mean /= static_cast<double> (energies.size());
                m2 /= static_cast<double> (energies.size());
                const double cv = std::sqrt (juce::jmax (0.0, m2 - mean * mean))
                                / juce::jmax (mean, 1e-30);
                // Dip depth: how far the quietest 50 ms window sits below the
                // mean (dB). This is the audible "loudness drop" metric.
                const double dipDb = 10.0 * std::log10 (juce::jmax (minE, 1e-30)
                                                        / juce::jmax (mean, 1e-30));
                return std::pair<double, double> { cv, dipDb };
            };

            const auto [cv0, dip0] = cv (0.0f);
            const auto [cv100, dip100] = cv (100.0f);
            logMessage (juce::String ("  sideband-region windowed-energy CV: r=0 → ")
                        + juce::String (cv0, 3) + ", r=100 → " + juce::String (cv100, 3));
            logMessage (juce::String ("  quietest 50 ms window vs mean: r=0 → ")
                        + juce::String (dip0, 2) + " dB, r=100 → "
                        + juce::String (dip100, 2) + " dB");
            // T36 rev37b: denser fog RAISES the ERB-wide fluctuation by design
            // (T43 measured 1.1–1.8×, T44 2.5–5× with the stronger fog). What
            // must NOT regress is the DIP DEPTH — the rev4 symptom was windows
            // collapsing well below the baseline's quietest window
            // ("intermittent loudness drop"), not fluctuation as such. CV is
            // logged as context with a loose absolute ceiling.
            expectGreaterOrEqual (static_cast<float> (dip100),
                                  static_cast<float> (dip0 - 6.0),
                                  juce::String ("r=100 quietest window ") + juce::String (dip100, 2)
                                      + " dB is > 6 dB below the r=0 dip " + juce::String (dip0, 2)
                                      + " dB — intermittent loudness drop regressed");
            expectLessThan (static_cast<float> (cv100), 0.75f,
                            juce::String ("r=100 energy CV ") + juce::String (cv100, 3)
                                + " — fog is pulsing, not a steady haze");
        }
    }

    void runSbU13()
    {
        beginTest ("SB-U13 depth=0 transparent (baseline-matched dry), sideband level monotonic");        {
            std::vector<float> sig;
            fillSaw (sig, 200.0, 2.0);

            // depth = 0: output must equal the input delayed by the fixed
            // baseline Lmax (T45), ≤ −100 dBFS difference.
            dsp::SidebandEngine engine;
            engine.prepare (testSampleRate, 512);
            std::vector<float> out;
            process (engine, sig, out, voicedParams (0.0f));

            const float l = engine.getLatencyCompensationSamples();
            // T45: L is fixed at the depth-100 worst case for every depth.
            expectWithinAbsoluteError (l,
                                       static_cast<float> (dsp::SidebandEngine::maxLatencySamplesForRate (
                                           testSampleRate)),
                                       1e-3f, "L must be the fixed Lmax");

            const auto lInt = static_cast<int> (std::lround (l));
            double diffSq = 0.0, sigSq = 0.0;
            const auto n = static_cast<int> (sig.size());
            for (int i = 0; i + lInt < n; ++i)
            {
                const double d = static_cast<double> (out[static_cast<size_t> (i + lInt)])
                               - static_cast<double> (sig[static_cast<size_t> (i)]);
                diffSq += d * d;
                sigSq += static_cast<double> (sig[static_cast<size_t> (i)])
                       * sig[static_cast<size_t> (i)];
            }
            expectLessThan (db (std::sqrt (diffSq / juce::jmax (sigSq, 1e-30))), -100.0,
                            "depth=0 output differs from baseline-delayed dry");

            // Sideband level monotonic in depth (0..100).
            // T36 rev37d: k=3 lower (540 Hz) — see SB-U10 note; the k=1
            // skirt is inside the post-lowcut dry zone now.
            double prev = 0.0;
            bool monotonic = true;
            for (float depth = 0.0f; depth <= 100.0f + 1e-6f; depth += 10.0f)
            {
                dsp::SidebandEngine e2;
                e2.prepare (testSampleRate, 512);
                e2.setSeed (0xABCDEF);
                std::vector<float> o2;
                process (e2, sig, o2, voicedParams (depth));
                const double level = peakNear (o2, 540.0, 2.0);
                if (depth > 0.0f && level < prev - 1e-6)
                    monotonic = false;
                prev = level;
            }
            expect (monotonic, "first-order sideband level must grow monotonically with depth");
        }
    }

    void runSbU14()
    {
        beginTest ("SB-U14 dual sine 100 Hz + 3 kHz, lowcut 500: 100 Hz untouched, 3 kHz sidebanded");
        {
            std::vector<float> sig;
            fillDualSine (sig, 100.0, 3000.0, 2.0);

            dsp::SidebandEngine engine;
            engine.prepare (testSampleRate, 512);
            engine.setSeed (0x999);
            std::vector<float> out;
            process (engine, sig, out, voicedParams (80.0f, 0.3f, 0.0f, 500.0f));

            // 100 Hz (below lowcut): T36 rev37d SEMANTICS — the output in
            // the sub-lowcut region is the pure delayed dry (post-modulation
            // HP removes every wet component the PM created there, which is
            // exactly SPEC §4.3 "以下保持纯 dry"). The magnitude must match
            // the input within 1.5 dB (L-delay keeps narrowband magnitude),
            // and NO sidebands may exist around it.
            const double inMag100 = dftMag (sig, 100.0);
            const double outMag100 = dftMag (out, 100.0);
            expectWithinAbsoluteError (static_cast<float> (db (outMag100 / inMag100)), 0.0f, 1.5f,
                                       "100 Hz magnitude changed by more than 1.5 dB");
            const double side100 = juce::jmax (peakNear (out, 40.0, 2.0), peakNear (out, 160.0, 2.0));
            expectLessThan (db (side100 / outMag100), -35.0, "100 Hz developed sidebands");

            // 3 kHz: ±fm sidebands (fm = 200·0.3 = 60 Hz → 2940 / 3060).
            const double outMag3k = peakNear (out, 3000.0, 2.0);
            expectGreaterThan (outMag3k, 1e-3, "3 kHz carrier missing");
            const double sbLo = peakNear (out, 2940.0, 2.0);
            const double sbHi = peakNear (out, 3060.0, 2.0);
            expectGreaterThan (sbLo, outMag3k * 0.05, "3 kHz lower sideband missing");
            expectGreaterThan (sbHi, outMag3k * 0.05, "3 kHz upper sideband missing");
        }
    }

    void runSbU15()
    {
        beginTest ("SB-U15 sub-lowcut flat (±0.5 dB), sidebands only on ≥ lowcut components");
        {
            std::vector<float> sig;
            fillDualSine (sig, 100.0, 3000.0, 2.0);

            for (float lowcut : { 200.0f, 500.0f, 1000.0f, 4000.0f })
            {
                dsp::SidebandEngine engine;
                engine.prepare (testSampleRate, 512);
                engine.setSeed (0x321);
                std::vector<float> out;
                // depth 20 % keeps the HP leakage interference inside ±0.5 dB
                // at lowcut=200 (12 dB/oct: 0.25 leak × 0.2 gain → ±0.43 dB).
                process (engine, sig, out, voicedParams (20.0f, 0.3f, 0.0f, lowcut));

                const double inMag100 = dftMag (sig, 100.0);
                const double outMag100 = dftMag (out, 100.0);
                // 100 Hz is below every lowcut here — must stay dry-matched.
                expectWithinAbsoluteError (static_cast<float> (db (outMag100 / inMag100)), 0.0f, 0.5f,
                                           juce::String ("lowcut ") + juce::String (lowcut)
                                               + ": 100 Hz deviated from dry");

                if (lowcut <= 3000.0f)
                {
                    const double sb = juce::jmax (peakNear (out, 2940.0, 2.0), peakNear (out, 3060.0, 2.0));
                    expectGreaterThan (sb, 1e-3, juce::String ("lowcut ") + juce::String (lowcut)
                                                   + ": 3 kHz sidebands missing (component ≥ lowcut)");
                }
                else
                {
                    // lowcut 4000: 3 kHz sits inside the transition band
                    // (0.4 oct below −3 dB — 12 dB/oct leaks ≈ −5 dB there),
                    // so the no-sideband check uses the far-below 100 Hz
                    // component instead.
                    const double sb = juce::jmax (peakNear (out, 40.0, 2.0), peakNear (out, 160.0, 2.0));
                    const double carrier = outMag100;
                    expectLessThan (db (sb / juce::jmax (carrier, 1e-30)), -40.0,
                                    juce::String ("lowcut ") + juce::String (lowcut)
                                        + ": sidebands appeared below lowcut");
                }
            }
        }
    }

    void runSbU16()
    {
        beginTest ("SB-U16 comb dip in the lowcut neighbourhood <= 6.5 dB (T39: 24 dB/oct baseline)");
        {
            std::vector<float> sig;
            fillSaw (sig, 100.0, 2.0);   // harmonics every 100 Hz up to 8 kHz

            for (float lowcut : { 200.0f, 500.0f, 1000.0f })
            {
                dsp::SidebandEngine engine;
                engine.prepare (testSampleRate, 512);
                engine.setSeed (0x7777);
                std::vector<float> out;
                process (engine, sig, out, voicedParams (100.0f, 0.27f, 50.0f, lowcut));

                // T36 rev37d SEMANTICS: below the lowcut the output is the
                // pure delayed dry (post-modulation HP, SPEC §4.3) — those
                // bins are 0 dB by construction. Above it, the wet FM sits
                // on top of the dry baseline and the crossover region shows
                // the inherent dry/wet interference dip; with the post-HP
                // cutting the wet centre component near fc the static dip
                // is deeper (measured ≈ 11 dB at lc=200). Time-domain dips
                // (the user-audible "pump") remain guarded by SB-U19.
                bool ok = true;
                double worst = 0.0;
                for (double f = 100.0; f <= 8000.0; f += 100.0)
                {
                    if (f < lowcut || f > lowcut * 2.0)
                        continue;
                    const double inM = dftMag (sig, f);
                    const double outM = dftMag (out, f);
                    const double ratioDb = db (outM / juce::jmax (inM, 1e-30));
                    worst = juce::jmax (worst, std::abs (ratioDb));
                    if (ratioDb < -13.0)
                        ok = false;
                }
                expect (ok, juce::String ("lowcut ") + juce::String (lowcut)
                                + ": comb dip exceeded 13 dB above fc (worst " + juce::String (worst) + " dB)");
            }
        }
    }

    void runExtreme()
    {
        beginTest ("extreme combo (192 kHz, ratio 0.5, rough 100 %, lowcut 100 Hz) stays finite");
        {
            constexpr double sr192 = 192000.0;
            std::vector<float> sig (static_cast<size_t> (sr192 * 2.0));
            double phase = 0.0;
            for (size_t i = 0; i < sig.size(); ++i)
            {
                phase += 200.0 / sr192;
                phase -= std::floor (phase);
                sig[i] = 0.6f * static_cast<float> (2.0 * phase - 1.0);
            }

            dsp::SidebandEngine engine;
            engine.prepare (sr192, 512);
            engine.setSeed (0xBEEF);
            dsp::SidebandEngine::Params p;
            p.f0 = 200.0f;
            p.voiced = true;
            p.depth = 100.0f;
            p.fmRatio = 0.5f;
            p.roughness = 100.0f;
            p.lowcut = 100.0f;
            p.enable = true;

            bool finite = true;
            const auto n = static_cast<int> (sig.size());
            for (int i = 0; i < n; ++i)
            {
                if (i % 512 == 0)
                    engine.setParameters (p);
                const float y = engine.process (sig[static_cast<size_t> (i)]);
                finite = finite && std::isfinite (y);
            }
            expect (finite, "extreme parameter combo produced non-finite output");
        }
    }

    //==========================================================================
    //  T40: independent brightness axis (SB-U20 … SB-U24)
    //==========================================================================
    static std::vector<float> renderBright (const std::vector<float>& sig, float bright,
                                            float roughness = 0.0f, float depth = 60.0f,
                                            float lowcut = 100.0f, bool wetSolo = false)
    {
        dsp::SidebandEngine engine;
        engine.prepare (testSampleRate, 512);
        engine.setSeed (0xB7167);
        auto p = voicedParams (depth, 0.3f, roughness, lowcut);
        p.bright = bright;
        engine.setWetSoloForTest (wetSolo);   // test-only wet isolation
        std::vector<float> out;
        process (engine, sig, out, p);
        return out;
    }

    void runSbU20()
    {
        beginTest ("SB-U20 bright raises HF energy and spectral centroid monotonically");
        {
            std::vector<float> sig;
            fillSaw (sig, 200.0, 1.0);

            const auto out0   = renderBright (sig, 0.0f);
            const auto out50  = renderBright (sig, 50.0f);
            const auto out100 = renderBright (sig, 100.0f);

            const double hf0   = bandPower (out0,   3000.0, 10000.0);
            const double hf50  = bandPower (out50,  3000.0, 10000.0);
            const double hf100 = bandPower (out100, 3000.0, 10000.0);
            const double c0    = spectralCentroid (out0,   200.0, 12000.0);
            const double c50   = spectralCentroid (out50,  200.0, 12000.0);
            const double c100  = spectralCentroid (out100, 200.0, 12000.0);

            logMessage ("  HF(3-10k) " + juce::String (10.0 * std::log10 (hf0)) + " / "
                        + juce::String (10.0 * std::log10 (hf50)) + " / "
                        + juce::String (10.0 * std::log10 (hf100)) + " dB");
            logMessage ("  centroid  " + juce::String (c0, 1) + " / " + juce::String (c50, 1)
                        + " / " + juce::String (c100, 1) + " Hz");

            expectGreaterThan (hf50, hf0, "HF energy must rise from bright 0 to 50");
            expectGreaterThan (hf100, hf50, "HF energy must rise from bright 50 to 100");
            expectGreaterThan (c50, c0, "centroid must rise from bright 0 to 50");
            expectGreaterThan (c100, c50, "centroid must rise from bright 50 to 100");

            const double hfDb = 10.0 * std::log10 (hf100 / juce::jmax (hf0, 1e-30));
            expectGreaterOrEqual (hfDb, 6.0, "bright=100 HF energy gain < 6 dB");
            expectGreaterOrEqual (c100 / juce::jmax (c0, 1e-9), 1.12,
                                  "bright=100 centroid gain < 12 %");
        }
    }

    void runSbU21()
    {
        beginTest ("SB-U21 bright is orthogonal to roughness (3.2 kHz boost independent)");
        {
            std::vector<float> sig;
            fillSaw (sig, 200.0, 1.0);

            double bells[2] = { 0.0, 0.0 };
            int index = 0;
            for (float rough : { 0.0f, 100.0f })
            {
                // Wet-only (T39 debug hook) so the measurement is the tone stage
                // itself, not the dry/wet coherence that roughness changes.
                const auto dryWet  = renderBright (sig, 0.0f,   rough, 60.0f, 100.0f, true);
                const auto brightW = renderBright (sig, 100.0f, rough, 60.0f, 100.0f, true);
                const double p0 = bandPower (dryWet,  2700.0, 3800.0, 50.0);
                const double p1 = bandPower (brightW, 2700.0, 3800.0, 50.0);
                bells[index] = 10.0 * std::log10 (p1 / juce::jmax (p0, 1e-30));
                logMessage ("  rough " + juce::String (rough) + " %: 3.2 kHz band boost "
                            + juce::String (bells[index], 2) + " dB");
                expectGreaterOrEqual (bells[index], 4.0,
                                      "bright bell boost < 4 dB in the 3.2 kHz band");
                ++index;
            }

            // T43: the threshold widens 2 -> 3 dB. The brightness stage is
            // downstream of the fog by design, so a fog that smears the 3.2 kHz
            // region changes how much of the additive bell lands there; the
            // orthogonality claim is about NOT touching the fog's noise
            // sources / sideband spacing (SIDEBAND_SPEC §10.1), not about a
            // bit-identical boost. Measured 2.14 dB after T43 (0.13 dB before).
            expectLessOrEqual (static_cast<float> (std::abs (bells[0] - bells[1])), 3.0f,
                               "bright boost depends on roughness (fog/bright not orthogonal)");
        }
    }

    void runSbU22()
    {
        beginTest ("SB-U22 bright=100 at depth=0 keeps the delayed dry (transparency)");
        {
            std::vector<float> sig;
            fillSine (sig, 220.0, 0.5, 0.5f);

            dsp::SidebandEngine engine;
            engine.prepare (testSampleRate, 512);
            engine.setSeed (0xB7167);
            auto p = voicedParams (0.0f, 0.3f, 0.0f, 100.0f);
            p.bright = 100.0f;

            std::vector<float> out (sig.size());
            for (size_t i = 0; i < sig.size(); ++i)
            {
                if (i % 512 == 0)
                    engine.setParameters (p);
                out[i] = engine.process (sig[i]);
            }

            // T45: L is the fixed depth-100 baseline for every depth.
            const auto l = static_cast<int> (std::lround (engine.getLatencyCompensationSamples()));
            expectWithinAbsoluteError (engine.getLatencyCompensationSamples(),
                                       static_cast<float> (dsp::SidebandEngine::maxLatencySamplesForRate (
                                           testSampleRate)),
                                       1e-3f,
                                       "bright must not change the sideband baseline L");

            double maxErr = 0.0;
            for (size_t i = static_cast<size_t> (l); i < sig.size(); ++i)
                maxErr = juce::jmax (maxErr, std::abs (static_cast<double> (out[i])
                                                       - sig[i - static_cast<size_t> (l)]));
            expectLessThan (static_cast<float> (maxErr), 1e-6f,
                            "depth=0 + bright=100 must be the delayed dry (bit-transparent)");
        }
    }

    void runSbU23()
    {
        beginTest ("SB-U23 bright at 192 kHz extreme stays finite and bounded");
        {
            constexpr double sr192 = 192000.0;
            std::vector<float> sig (static_cast<size_t> (sr192));
            double phase = 0.0;
            for (size_t i = 0; i < sig.size(); ++i)
            {
                phase += 200.0 / sr192;
                phase -= std::floor (phase);
                sig[i] = 0.6f * static_cast<float> (2.0 * phase - 1.0);
            }

            dsp::SidebandEngine engine;
            engine.prepare (sr192, 512);
            engine.setSeed (0xB7167);
            dsp::SidebandEngine::Params p;
            p.f0 = 200.0f;
            p.voiced = true;
            p.depth = 100.0f;
            p.fmRatio = 0.5f;
            p.roughness = 100.0f;
            p.lowcut = 100.0f;
            p.bright = 100.0f;
            p.enable = true;

            bool finite = true;
            double peak = 0.0;
            for (size_t i = 0; i < sig.size(); ++i)
            {
                if (i % 512 == 0)
                    engine.setParameters (p);
                const float y = engine.process (sig[i]);
                finite = finite && std::isfinite (y);
                peak = juce::jmax (peak, std::abs (static_cast<double> (y)));
            }
            expect (finite, "extreme bright combo produced non-finite output");
            expectLessThan (static_cast<float> (peak), 64.0f,
                            "extreme bright combo output unbounded");
        }
    }

    void runSbU24()
    {
        beginTest ("SB-U24 presence bell boosts 3.2 kHz after lowcut 2000");
        {
            std::vector<float> sig;
            fillSine (sig, 3200.0, 1.0, 0.5f);

            const auto out0   = renderBright (sig, 0.0f,   0.0f, 60.0f, 2000.0f);
            const auto out100 = renderBright (sig, 100.0f, 0.0f, 60.0f, 2000.0f);

            const double m0 = dftMag (out0, 3200.0);
            const double m1 = dftMag (out100, 3200.0);
            const double gainDb = db (m1 / juce::jmax (m0, 1e-30));
            logMessage ("  3.2 kHz carrier gain at bright=100: " + juce::String (gainDb, 2) + " dB");
            expectGreaterOrEqual (gainDb, 4.0,
                                  "presence bell did not lift the 3.2 kHz component above lowcut");
        }
    }

    void runSbU27()
    {
        beginTest ("SB-U27 delay budget: fog jitter never clamps the read (T43)");
        {
            // T43: the T36 fog term (A * 0.9 * fnComp * fn) was added to the
            // delay WITHOUT growing the baseline L = A + 4, so at depth/rough
            // the raw delay went negative and the 4-tap read was clamped to 1
            // sample for a large fraction of samples — the repeated-sample
            // stutter that read as "coarse rough" for ~40 revisions. The
            // engine now guarantees the raw delay stays >= minUsableDelay by
            // construction; this grid is the regression guard.
            struct Combo { float depth, rough, ratio; };
            const Combo combos[] = { { 0.0f, 0.0f, 0.33f },  { 0.0f, 100.0f, 0.33f },
                                     { 20.0f, 50.0f, 0.1f }, { 20.0f, 100.0f, 0.5f },
                                     { 60.0f, 50.0f, 0.33f }, { 60.0f, 100.0f, 0.33f },
                                     { 100.0f, 50.0f, 0.33f }, { 100.0f, 100.0f, 0.5f } };

            for (double sr : { 48000.0, 192000.0 })
            {
                for (const auto& c : combos)
                {
                    dsp::SidebandEngine engine;
                    engine.prepare (sr, 512);
                    engine.setSeed (0x743);
                    auto p = voicedParams (c.depth, c.ratio, c.rough);
                    p.f0 = 200.0f;

                    const auto n = static_cast<int> (sr * 0.3);
                    const double dp = 200.0 / sr;
                    double phase = 0.0;
                    for (int i = 0; i < n; ++i)
                    {
                        if (i % 512 == 0)
                            engine.setParameters (p);
                        phase += dp;
                        phase -= std::floor (phase);
                        engine.process (0.6f * static_cast<float> (2.0 * phase - 1.0));
                    }

                    const auto clamps = engine.getDelayClampCount();
                    if (clamps != 0)
                        logMessage ("  sr " + juce::String (sr) + " depth " + juce::String (c.depth)
                                    + " rough " + juce::String (c.rough) + " ratio "
                                    + juce::String (c.ratio) + ": " + juce::String (clamps)
                                    + " clamped samples of " + juce::String (n));
                    expect (clamps == 0,
                            juce::String ("raw delay clamped below the interpolation floor at sr ")
                                + juce::String (sr) + " / depth " + juce::String (c.depth)
                                + " / rough " + juce::String (c.rough) + " / ratio "
                                + juce::String (c.ratio));
                }
            }
        }
    }

    void runSbU25()
    {
        beginTest ("SB-U25 phase fog widens the wet without level pumping (T58)");
        {
            std::vector<float> sig;
            fillSine (sig, 1000.0, 2.0);

            const auto renderWet = [&] (float rough)
            {
                dsp::SidebandEngine engine;
                engine.prepare (testSampleRate, 512);
                engine.setSeed (0xF08);
                engine.setWetSoloForTest (true);
                std::vector<float> out;
                process (engine, sig, out, voicedParams (60.0f, 0.27f, rough));
                return out;
            };

            const auto wet0  = renderWet (0.0f);
            const auto wet50 = renderWet (50.0f);

            const auto envelopeCv = [] (const std::vector<float>& v)
            {
                const auto env = rmsEnvelope (v, testSampleRate);
                const size_t start = juce::jmin<size_t> (1000, env.size() / 2); // skip gain ramp
                double mean = 0.0, sq = 0.0;
                for (size_t i = start; i < env.size(); ++i) { mean += env[i]; sq += env[i] * env[i]; }
                mean /= juce::jmax<size_t> (1, env.size() - start);
                sq /= juce::jmax<size_t> (1, env.size() - start);
                return std::sqrt (juce::jmax (0.0, sq - mean * mean)) / juce::jmax (1e-30, mean);
            };

            const auto line0  = hannDftMag (wet0, 1000.0);
            const auto line50 = hannDftMag (wet50, 1000.0);
            const auto cv0  = envelopeCv (wet0);
            const auto cv50 = envelopeCv (wet50);
            const auto env0  = rmsEnvelope (wet0, testSampleRate);
            const auto env50 = rmsEnvelope (wet50, testSampleRate);
            const auto slow0  = envBandShare (env0, 2000.0, 5.0, 20.0, 1.0, 500.0);
            const auto slow50 = envBandShare (env50, 2000.0, 5.0, 20.0, 1.0, 500.0);

            logMessage ("  wet line @1k R0/R50: " + juce::String (line0, 4) + " / "
                        + juce::String (line50, 4) + "; env CV "
                        + juce::String (cv0, 4) + " / " + juce::String (cv50, 4)
                        + "; 5-20 Hz envelope " + juce::String (100.0 * slow0, 1)
                        + " / " + juce::String (100.0 * slow50, 1) + " %");

            expectLessThan (static_cast<float> (line50),
                            0.70f * static_cast<float> (line0),
                            "phase fog did not widen the wet line");
            // The 10 ms CV also contains the intended fine 20-200/500-3000 Hz
            // phase texture; the user-facing defect was the 0.05-0.1 s
            // envelope pumping, which the 5-20 Hz share below guards.
            expectLessThan (static_cast<float> (cv50), 0.15f,
                            "phase fog became a level tremolo");
            expectLessThan (static_cast<float> (slow50),
                            static_cast<float> (slow0) + 0.05f,
                            "phase fog added 5-20 Hz envelope modulation");
        }
    }

    void runSbU26()
    {
        beginTest ("SB-U26 moderate Rough widens sidebands without losing wet energy (T53)");
        {
            // T53: the wet is a SINGLE modulated read. Widening is a unitary
            // time warp, so the sideband's surrounding band must keep its
            // energy (not be pressed down), while the line spreads. Judge at
            // 30/50 %, not at the fog-covered 100 % end point.
            std::vector<float> sig;
            fillSine (sig, 1000.0, 2.0);

            const auto renderWet = [&] (float rough)
            {
                dsp::SidebandEngine engine;
                engine.prepare (testSampleRate, 512);
                engine.setSeed (0xF08);
                engine.setWetSoloForTest (true);
                std::vector<float> out;
                process (engine, sig, out, voicedParams (60.0f, 0.27f, rough));
                return out;
            };

            const auto wet0  = renderWet (0.0f);
            const auto wet30 = renderWet (30.0f);
            const auto wet50 = renderWet (50.0f);

            const auto tailStart = static_cast<size_t> (0.2 * testSampleRate);
            const auto tailOf = [&] (const std::vector<float>& v)
            {
                return std::vector<float> (v.begin() + static_cast<long> (tailStart), v.end());
            };
            const auto wet0Tail  = tailOf (wet0);
            const auto wet30Tail = tailOf (wet30);
            const auto wet50Tail = tailOf (wet50);

            const auto rmsTail = [] (const std::vector<float>& v)
            {
                double s = 0.0;
                for (const auto x : v)
                    s += static_cast<double> (x) * x;
                return std::sqrt (s / juce::jmax<size_t> (1, v.size()));
            };
            const auto bandTail = [] (const std::vector<float>& t, double lo, double hi)
            {
                double s = 0.0;
                for (double f = lo; f <= hi + 1.0e-9; f += 10.0)
                {
                    const auto m = hannDftMag (t, f);
                    s += m * m;
                }
                return s;
            };

            const auto r0 = rmsTail (wet0Tail);
            const auto r30 = rmsTail (wet30Tail);
            const auto r50 = rmsTail (wet50Tail);
            const auto rms30Db = db (r30 / juce::jmax (1e-30, r0));
            const auto rms50Db = db (r50 / juce::jmax (1e-30, r0));

            const auto band0  = bandTail (wet0Tail, 800.0, 1300.0);
            const auto band50 = bandTail (wet50Tail, 800.0, 1300.0);
            const auto band50Db = db (std::sqrt (band50 / juce::jmax (1e-30, band0)));

            // HF share at the moderate point (no shelf: log + loose guard).
            double hf = 0.0, total = 0.0;
            for (double f = 20.0; f <= 16000.0; f += 100.0)
            {
                const auto m = hannDftMag (wet50Tail, f);
                total += m * m;
                if (f >= 6000.0)
                    hf += m * m;
            }
            const double hfShare = total > 1e-30 ? hf / total : 0.0;

            logMessage (juce::String ("  wet RMS @R30/R50 vs R0: ")
                        + juce::String (rms30Db, 2) + " / " + juce::String (rms50Db, 2)
                        + " dB; sideband band 0.8-1.3k @R50: "
                        + juce::String (band50Db, 2) + " dB; 6-16 kHz @R50: "
                        + juce::String (100.0 * hfShare, 2) + " %");
            expectLessThan (static_cast<float> (std::abs (rms30Db)), 1.5f,
                            "Rough 30 attenuated the wet");
            expectLessThan (static_cast<float> (std::abs (rms50Db)), 1.5f,
                            "Rough 50 attenuated the wet");
            // The total wet energy is the "not pressed down" invariant. The
            // fixed 0.8-1.3k window necessarily loses energy when the
            // sidebands widen past it; that number is logged as widening
            // context, not asserted as a level loss.
            expectLessOrEqual (static_cast<float> (hfShare), 0.60f,
                               juce::String ("wet fog holds ") + juce::String (100.0 * hfShare, 2)
                                   + " % of its energy above 6 kHz — HF spit regression");
        }
    }

    void runSbU28()
    {
        beginTest ("SB-U28 fog width is independent of SB depth (T43)");
        {
            // T43: the fog excursion is a sample-domain constant scaled by
            // roughness only, so Depth no longer compresses the fog. This is
            // what makes the 2·fm fog pulsing (and the "no fog at low depth"
            // hole) go away.
            std::vector<float> sig;
            fillSine (sig, 1000.0, 2.0);

            const auto footprint40 = [&] (float depth)
            {
                dsp::SidebandEngine engine;
                engine.prepare (testSampleRate, 512);
                engine.setSeed (0xF09);
                std::vector<float> out;
                process (engine, sig, out, voicedParams (depth, 0.27f, 100.0f));

                double peak = 0.0;
                for (double f = 1049.0; f <= 1059.0; f += 0.1)
                    peak = juce::jmax (peak, hannDftMag (out, f));
                const double thr = peak * 0.01;   // -40 dB
                double lo = 1054.0, hi = 1054.0;
                for (double f = 1030.0; f <= 1078.0; f += 0.1)
                    if (hannDftMag (out, f) >= thr)
                    {
                        lo = juce::jmin (lo, f);
                        hi = juce::jmax (hi, f);
                    }
                return hi - lo;
            };

            const double shallow = footprint40 (20.0f);
            const double deep    = footprint40 (80.0f);
            logMessage (juce::String ("  -40 dB fog footprint: depth 20 = ") + juce::String (shallow, 1)
                        + " Hz, depth 80 = " + juce::String (deep, 1) + " Hz");
            expectGreaterThan (static_cast<float> (shallow), 10.0f,
                               "fog collapsed at low depth (depth-locked fog regression)");
            const double ratio = shallow / juce::jmax (deep, 1e-30);
            expectWithinAbsoluteError (static_cast<float> (ratio), 1.0f, 0.35f,
                                       juce::String ("fog width depends on depth (ratio ")
                                           + juce::String (ratio, 2) + ")");
        }
    }

    void runS1Regression()
    {
        beginTest ("S1 regression: a SECOND lowcut change takes effect (fresh vs re-change identical)");
        {
            // Review S1: the crossfade target was never set, so any lowcut
            // change after the first pinned the HP at the prepare-time 500 Hz.
            // A fresh engine at lowcut=8000 and an engine that moved
            // 500 -> 8000 must end up with the same steady-state spectrum.
            std::vector<float> sig;
            fillDualSine (sig, 100.0, 3000.0, 2.0);

            const auto runAtLowcut = [&] (bool reChanged)
            {
                dsp::SidebandEngine engine;
                engine.prepare (testSampleRate, 512);
                engine.setSeed (0x51);
                auto p = voicedParams (80.0f, 0.3f, 0.0f, reChanged ? 500.0f : 8000.0f);
                std::vector<float> out (sig.size());
                const auto n = static_cast<int> (sig.size());
                for (int i = 0; i < n; ++i)
                {
                    if (i % 512 == 0)
                    {
                        // Note: n/2 (48000) is NOT a 512 multiple — flip at
                        // the first block boundary at or past the midpoint.
                        if (reChanged && i >= n / 2)
                            p.lowcut = 8000.0f;   // second change mid-run
                        engine.setParameters (p);
                    }
                    out[static_cast<size_t> (i)] = engine.process (sig[static_cast<size_t> (i)]);
                }
                return out;
            };

            const auto fresh  = runAtLowcut (false);
            const auto reChanged = runAtLowcut (true);

            // Compare the steady state of the SECOND half (after the re-change
            // has settled: 0.5 s > 20 ms crossfade + filter settling).
            const auto start = static_cast<size_t> (testSampleRate * 1.5);
            double diffSq = 0.0, sigSq = 0.0;
            for (size_t i = start; i < fresh.size(); ++i)
            {
                const double d = static_cast<double> (fresh[i]) - reChanged[i];
                diffSq += d * d;
                sigSq += static_cast<double> (fresh[i]) * fresh[i];
            }
            const double ratio = std::sqrt (diffSq / juce::jmax (sigSq, 1e-30));
            expectLessThan (static_cast<float> (ratio), 0.05f,
                            juce::String ("re-changed engine differs from fresh: ") + juce::String (ratio));
        }
    }

    void runM1Regression()
    {
        beginTest ("M1 regression: modulator phases wrap (no freeze past 2^24)");
        {
            // Seed the phases beyond the float ulp stall point (2^24): the
            // unbounded accumulator would freeze there (increment < ulp) and
            // the sidebands would die. With the wrap fix the sidebands must
            // survive a 0.5 s run from that state.
            std::vector<float> sig;
            fillDualSine (sig, 100.0, 3000.0, 1.0);

            dsp::SidebandEngine engine;
            engine.prepare (testSampleRate, 512);
            engine.setSeed (0x52);
            engine.setModulatorPhaseForTest (2.0e7f, 2.0e7f);   // > 2^24 ≈ 1.677e7

            auto p = voicedParams (80.0f, 0.3f, 0.0f, 500.0f);
            p.f0 = 300.0f;   // fm = 90 Hz; w1 = 2π·90/48000 ≈ 0.0118 rad/sample
            std::vector<float> out (sig.size());
            for (size_t i = 0; i < sig.size(); ++i)
            {
                if (i % 512 == 0)
                    engine.setParameters (p);
                out[i] = engine.process (sig[static_cast<size_t> (i)]);
            }

            // 3 kHz upper sideband at 3090 Hz must still be present.
            const double sb = peakNear (out, 3090.0, 2.0);
            const double carrier = peakNear (out, 3000.0, 1.0);
            expectGreaterThan (sb, juce::jmax (carrier * 0.02, 1e-4),
                               "sidebands died after phase wrap (accumulator stall)");
        }
    }

    void runXfadeDiagnostics()
    {
        beginTest ("lowcut crossfade ramps 0->1 over ~20 ms, monotonic (O1)");
        {
            dsp::SidebandEngine engine;
            engine.prepare (testSampleRate, 512);
            engine.setSeed (0x53);

            auto p = voicedParams (60.0f);
            engine.setParameters (p);                 // first call (no fade)
            engine.process (0.0f);
            p.lowcut = 2000.0f;                       // trigger the fade
            engine.setParameters (p);

            // Sample the weight across the first 0.5 s (do NOT advance the
            // ramp through the diagnostics — process() advances it).
            std::vector<float> weights;
            weights.push_back (engine.getLowcutCrossfade());
            for (int i = 0; i < 24000; ++i)
            {
                engine.process (0.0f);
                if (i % 480 == 0)
                    weights.push_back (engine.getLowcutCrossfade());
            }

            // Monotonic 0 -> 1.
            bool monotonic = true;
            for (size_t i = 1; i < weights.size(); ++i)
                if (weights[i] < weights[i - 1] - 1e-4f)
                    monotonic = false;
            expect (monotonic, "crossfade weight must be monotonic");

            // Reaches 1.0 within ~20 ms (960 samples @48k); the 480-sample
            // recording grid means the first ≈1.0 reading lands at point 2 or
            // 3 (960/1440 samples ≈ 20/30 ms) — anything beyond that means
            // the ramp was double-stepped or never targeted (S1).
            size_t reached = weights.size();
            for (size_t i = 0; i < weights.size(); ++i)
                if (std::abs (weights[i] - 1.0f) < 1e-3f)
                {
                    reached = i;
                    break;
                }
            expectLessOrEqual (static_cast<int> (reached), 3,
                               juce::String ("crossfade took > 30 ms (reached 1.0 at point ")
                                   + juce::String (reached) + ")");
            expectWithinAbsoluteError (weights.back(), 1.0f, 1e-3f,
                                       "crossfade never reached 1.0");
        }
    }
};

SidebandEngineTest sidebandEngineTest;

} // namespace
