/*
    NeoVoiDrive T66 integration tests — Growl in the full chain.

    GR-I01  Growl amount=0 leaves Legacy F0 detection asleep.
    GR-I02  Growl>0 wakes the F0 tracker and affects the voiced wet path.
    GR-I03  Dry/Wet=0 and Bypass=0 remain Growl-independent.
    GR-I04  Stereo identity and finite output.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "dsp/MultibandEngine.h"
#include "dsp/SidebandEngine.h"

#include <cmath>
#include <vector>

extern std::int64_t g_allocCount;

namespace
{

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;

std::vector<float> makeSine (float freq, int total, float amp = 0.4f)
{
    std::vector<float> x (static_cast<size_t> (total));
    for (int n = 0; n < total; ++n)
        x[static_cast<size_t> (n)] = amp * std::sin (
            juce::MathConstants<float>::twoPi * freq * static_cast<float> (n)
            / static_cast<float> (kSampleRate));
    return x;
}

struct Runner
{
    dsp::MultibandEngine engine;
    std::vector<float> bypass, formant;

    Runner()
    {
        engine.prepare (kSampleRate, kBlock);
        engine.setSeed (424242);
        bypass.assign (static_cast<size_t> (kBlock), 1.0f);
        formant.assign (static_cast<size_t> (kBlock), 0.0f);
    }

    void processMono (const std::vector<float>& in, std::vector<float>& out,
                      params::ParamSnapshot p)
    {
        out.assign (in.size(), 0.0f);
        for (int pos = 0; pos < static_cast<int> (in.size()); pos += kBlock)
        {
            const int n = juce::jmin (kBlock, static_cast<int> (in.size()) - pos);
            engine.setParameters (p);
            engine.process (in.data() + pos, nullptr, out.data() + pos, nullptr,
                            n, 1, bypass.data(), formant.data());
        }
    }

    void processStereo (const std::vector<float>& in, std::vector<float>& l,
                        std::vector<float>& r, params::ParamSnapshot p)
    {
        l.assign (in.size(), 0.0f);
        r.assign (in.size(), 0.0f);
        for (int pos = 0; pos < static_cast<int> (in.size()); pos += kBlock)
        {
            const int n = juce::jmin (kBlock, static_cast<int> (in.size()) - pos);
            engine.setParameters (p);
            engine.process (in.data() + pos, in.data() + pos,
                            l.data() + pos, r.data() + pos,
                            n, 2, bypass.data(), formant.data());
        }
    }
};

bool allFinite (const std::vector<float>& v)
{
    for (auto x : v)
        if (! std::isfinite (x))
            return false;
    return true;
}

class GrowlIntegrationTests final : public juce::UnitTest
{
public:
    GrowlIntegrationTests() : juce::UnitTest ("GrowlIntegration") {}

    void runTest() override
    {
        const auto input = makeSine (440.0f, static_cast<int> (1.2 * kSampleRate));

        beginTest ("amount=0 keeps Legacy F0 tracking asleep");
        {
            Runner chain;
            params::ParamSnapshot p;
            p.bandCount = 1;
            p.engineVersion = 0;
            p.dryWet = 100.0f;
            p.growlAmount = 0.0f;

            std::vector<float> out;
            chain.processMono (input, out, p);

            expectEquals (chain.engine.getF0NsdfRuns(), 0,
                          "Legacy + Growl 0 must not run the NSDF");
            expect (! chain.engine.isGrowlActive(), "Growl must be idle at amount 0");
        }

        beginTest ("amount=100 wakes the tracker and modulates the wet path");
        {
            Runner chain;
            params::ParamSnapshot p;
            p.bandCount = 1;
            p.engineVersion = 0;
            p.dryWet = 100.0f;
            p.growlAmount = 100.0f;
            p.growlRate = 0.20f;
            p.growlTone = 0.0f;

            std::vector<float> out;
            chain.processMono (input, out, p);

            expectGreaterThan (chain.engine.getF0NsdfRuns(), 0,
                               "Growl must wake the F0 tracker");
            expect (chain.engine.isGrowlActive(), "Growl must be active");
            expectGreaterThan (chain.engine.getGrowlEffectiveAmount(), 0.0f,
                               "gate/amount must open on a voiced sine");
            expect (allFinite (out), "Growl output must stay finite");

            // Compare against the same chain with Growl off: the wet path must
            // actually change once the tracker locks and the gate opens.
            Runner baseline;
            params::ParamSnapshot pb = p;
            pb.growlAmount = 0.0f;
            std::vector<float> baseOut;
            baseline.processMono (input, baseOut, pb);

            double diffSq = 0.0, sigSq = 0.0;
            const auto start = static_cast<size_t> (0.6 * kSampleRate);
            for (size_t i = start; i < out.size(); ++i)
            {
                const auto d = static_cast<double> (out[i]) - baseOut[i];
                diffSq += d * d;
                sigSq += static_cast<double> (baseOut[i]) * baseOut[i];
            }
            const auto diffRatio = std::sqrt (diffSq / juce::jmax (sigSq, 1.0e-30));
            expectGreaterThan (diffRatio, 0.01,
                               "voiced Growl should change the wet path measurably");
        }

        beginTest ("dry/wet=0 and bypass=0 stay Growl-independent");
        {
            params::ParamSnapshot p;
            p.bandCount = 1;
            p.engineVersion = 0;
            p.dryWet = 0.0f;
            p.growlAmount = 100.0f;
            p.growlRate = 0.20f;
            p.growlTone = 0.0f;

            Runner a, b;
            std::vector<float> outA, outB;
            a.processMono (input, outA, p);

            p.growlAmount = 0.0f;
            b.processMono (input, outB, p);

            bool exact = true;
            for (size_t i = 0; i < outA.size(); ++i)
                exact = exact && (outA[i] == outB[i]);
            expect (exact, "Dry/Wet=0 must bypass Growl exactly");

            // Bypass gain 0 = dry passthrough; growl must not leak into it.
            Runner c, d;
            c.bypass.assign (static_cast<size_t> (kBlock), 0.0f);
            d.bypass.assign (static_cast<size_t> (kBlock), 0.0f);
            params::ParamSnapshot pc = p;
            pc.growlAmount = 100.0f;
            params::ParamSnapshot pd = p;
            pd.growlAmount = 0.0f;
            std::vector<float> outC, outD;
            c.processMono (input, outC, pc);
            d.processMono (input, outD, pd);

            bool bypassExact = true;
            for (size_t i = 0; i < outC.size(); ++i)
                bypassExact = bypassExact && (outC[i] == outD[i]);
            expect (bypassExact, "Bypass must remain Growl-independent");
        }

        beginTest ("Growl does not change reported latency/PDC");
        {
            PluginProcessor processor;
            processor.prepareToPlay (kSampleRate, kBlock);

            const auto setParam = [&processor] (const char* id, float value)
            {
                if (auto* param = processor.apvts.getParameter (id))
                    param->setValueNotifyingHost (value);
            };

            const auto processOneBlock = [&processor]
            {
                juce::AudioBuffer<float> buffer (2, kBlock);
                buffer.clear();
                juce::MidiBuffer midi;
                processor.processBlock (buffer, midi);
            };

            setParam ("growl_amount", 1.0f);
            processOneBlock();
            expectEquals (processor.getReportedLatencySamples(), 0,
                          "Legacy + Growl must keep zero reported latency");

            setParam ("engine_version", 1.0f);
            processOneBlock();
            expectEquals (processor.getReportedLatencySamples(),
                          dsp::SidebandEngine::maxLatencySamplesForRate (kSampleRate),
                          "Sideband latency must not change with Growl");

            setParam ("formant_enabled", 1.0f);
            processOneBlock();
            expectEquals (processor.getReportedLatencySamples(),
                          dsp::FormantEngine::latencySamples
                              + dsp::SidebandEngine::maxLatencySamplesForRate (kSampleRate),
                          "Formant + Sideband latency must not change with Growl");
        }

        beginTest ("Growl-on processing is allocation-free");
        {
            Runner chain;
            params::ParamSnapshot p;
            p.bandCount = 1;
            p.engineVersion = 0;
            p.dryWet = 100.0f;
            p.growlAmount = 100.0f;
            p.growlRate = 0.20f;
            p.growlTone = 0.0f;

            std::vector<float> in (static_cast<size_t> (kBlock), 0.1f);
            std::vector<float> out (static_cast<size_t> (kBlock), 0.0f);

            const auto measure = [&] (float amount)
            {
                p.growlAmount = amount;
                for (int b = 0; b < 64; ++b)
                {
                    chain.engine.setParameters (p);
                    chain.engine.process (in.data(), nullptr, out.data(), nullptr,
                                          kBlock, 1, chain.bypass.data(), chain.formant.data());
                }
                const auto before = g_allocCount;
                for (int b = 0; b < 32; ++b)
                {
                    chain.engine.setParameters (p);
                    chain.engine.process (in.data(), nullptr, out.data(), nullptr,
                                          kBlock, 1, chain.bypass.data(), chain.formant.data());
                }
                return g_allocCount - before;
            };

            const auto off = measure (0.0f);
            const auto on  = measure (100.0f);
            logMessage ("  alloc delta: growl off=" + juce::String (off)
                        + " on=" + juce::String (on));
            expectEquals (static_cast<int> (off), 0, "Growl off must not allocate");
            expectEquals (static_cast<int> (on), 0, "Growl on must not allocate");
        }

        beginTest ("stereo identity with the same input");
        {
            Runner chain;
            params::ParamSnapshot p;
            p.bandCount = 1;
            p.engineVersion = 0;
            p.dryWet = 100.0f;
            p.growlAmount = 100.0f;
            p.growlRate = 0.20f;
            p.growlTone = 0.0f;

            std::vector<float> l, r;
            chain.processStereo (input, l, r, p);

            bool identical = true;
            for (size_t i = 0; i < l.size(); ++i)
                identical = identical && (l[i] == r[i]);

            expect (identical, "identical L/R input must stay identical with Growl");
            expect (allFinite (l) && allFinite (r), "stereo Growl output must stay finite");
        }
    }
};

GrowlIntegrationTests growlIntegrationTests;

} // namespace
