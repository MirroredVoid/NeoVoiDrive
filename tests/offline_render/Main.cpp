/*
    T21 offline renderer: wav in -> full engine -> wav out (console tool).

    Usage:
      offline_render --in in.wav --out out.wav
                     [--sr 48000] [--block 512] [--seconds 60]
                     [--param id=value]...        (physical values, repeatable)
                     [--block-scan 1,64,512,8192] (writes out.b{N}.wav each)
                     [--help]

    The render loop itself lives in RenderCore (unit-tested); this file only
    parses CLI, reads/writes WAV via juce_audio_formats and reports results.
    Test tool: file IO / allocation allowed (ARCHITECTURE §5.3).
*/

#include <juce_audio_formats/juce_audio_formats.h>

#include <iostream>

#include "RenderCore.h"

#include "PluginProcessor.h"

namespace
{

struct Options
{
    juce::File inFile;
    juce::File outFile;
    double sampleRate = 48000.0;
    int blockSize = 512;
    int bitsPerSample = 16;
    std::int64_t randomSeed = -1;
    double maxSeconds = 0.0;
    std::vector<std::pair<juce::String, float>> paramValues;
    std::vector<int> blockScan; // empty = render once with blockSize
};

void printUsage (const char* argv0)
{
    std::cout << argv0
              << " --in in.wav --out out.wav [--sr 48000] [--block 512]\n"
                 "     [--bits 16|24|32] [--seed N] [--seconds 60] [--param id=value]...\n"
                 "     [--block-scan 1,64,512,8192] [--help]\n"
                 "  --param accepts any of the 68 plugin parameter IDs with a\n"
                 "  PHYSICAL value (e.g. b1_jitter_depth=8, formant_enabled=1,\n"
                 "  formant_shift=-4.5, xover1_freq=300, engine_version=1,\n"
                 "  b1_sideband_depth=60). Repeatable.\n"
                 "  CHOICE params use the INDEX as the value: band_count 0/1/2 =\n"
                 "  1/2/3 bands; engine_version 0=Legacy, 1=Sideband.\n"
                 "  Per-band params affect only that band: use band_count=0 for a\n"
                 "  whole-range demo, or set all b1/b2/b3 values.\n";
}

bool parseArgs (int argc, char* argv[], Options& o)
{
    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);

        if (arg == "--help" || arg == "-h")
        {
            printUsage (argv[0]);
            std::exit (0);
        }
        else if (arg == "--in" && i + 1 < argc)        o.inFile = argv[++i];
        else if (arg.startsWith ("--in="))             o.inFile = arg.substring (5);
        else if (arg == "--out" && i + 1 < argc)       o.outFile = argv[++i];
        else if (arg.startsWith ("--out="))            o.outFile = arg.substring (6);
        else if (arg == "--sr" && i + 1 < argc)        o.sampleRate = juce::String (argv[++i]).getDoubleValue();
        else if (arg.startsWith ("--sr="))             o.sampleRate = arg.substring (5).getDoubleValue();
        else if (arg == "--block" && i + 1 < argc)     o.blockSize = juce::String (argv[++i]).getIntValue();
        else if (arg.startsWith ("--block="))          o.blockSize = arg.substring (8).getIntValue();
        else if (arg == "--bits" && i + 1 < argc)      o.bitsPerSample = juce::String (argv[++i]).getIntValue();
        else if (arg.startsWith ("--bits="))           o.bitsPerSample = arg.substring (7).getIntValue();
        else if (arg == "--seed" && i + 1 < argc)      o.randomSeed = juce::String (argv[++i]).getLargeIntValue();
        else if (arg.startsWith ("--seed="))           o.randomSeed = arg.substring (7).getLargeIntValue();
        else if (arg == "--seconds" && i + 1 < argc)   o.maxSeconds = juce::String (argv[++i]).getDoubleValue();
        else if (arg.startsWith ("--seconds="))        o.maxSeconds = arg.substring (10).getDoubleValue();
        else if (arg == "--param" || arg.startsWith ("--param="))
        {
            const auto body = arg == "--param"
                            ? (i + 1 < argc ? juce::String (argv[++i]) : juce::String())
                            : arg.substring (8);
            const auto eq = body.indexOfChar ('=');
            if (eq <= 0)
            {
                std::cerr << "bad --param (expected id=value): " << body << std::endl;
                return false;
            }
            o.paramValues.emplace_back (body.substring (0, eq),
                                        body.substring (eq + 1).getFloatValue());
        }
        else if (arg == "--block-scan" || arg.startsWith ("--block-scan="))
        {
            const auto body = arg == "--block-scan"
                            ? (i + 1 < argc ? juce::String (argv[++i]) : juce::String())
                            : arg.substring (13);
            juce::StringArray parts;
            parts.addTokens (body, ",", "");
            for (auto& p : parts)
                o.blockScan.push_back (p.trim().getIntValue());
        }
        else
        {
            std::cerr << "unknown option: " << arg << std::endl;
            return false;
        }
    }

    if (! o.inFile.existsAsFile())
    {
        std::cerr << "input file not found: " << o.inFile.getFullPathName() << std::endl;
        return false;
    }
    if (o.outFile == juce::File())
    {
        std::cerr << "--out is required" << std::endl;
        return false;
    }
    // The plugin itself trusts the host rate (no resampler); the CLI is the
    // tool boundary and restricts the supported range (T48 / SPEC §6).
    // Audit P2-6: the delay-line capacity / F0 window guarantee only covers
    // up to 192 kHz (worst-case pitch depth at 384 kHz exceeds 8192 samples),
    // so the tool rejects anything above the supported range.
    if (o.sampleRate < 32000.0 || o.sampleRate > 192000.0)
    {
        std::cerr << "sample rate out of supported range (32k..192k): " << o.sampleRate
                  << std::endl;
        return false;
    }
    if (o.blockSize < 1 || o.blockSize > 8192)
    {
        std::cerr << "block size out of range (1..8192): " << o.blockSize << std::endl;
        return false;
    }
    if (o.maxSeconds < 0.0)
    {
        std::cerr << "negative --seconds: " << o.maxSeconds << std::endl;
        return false;
    }
    return true;
}

bool readWav (juce::AudioFormatManager& formats, const juce::File& file,
              juce::AudioBuffer<float>& buffer, double& sampleRate)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        std::cerr << "cannot read wav: " << file.getFullPathName() << std::endl;
        return false;
    }

    sampleRate = reader->sampleRate;
    const auto channels = juce::jmax (1, static_cast<int> (reader->numChannels));
    buffer.setSize (channels, static_cast<int> (reader->lengthInSamples), false, false, true);
    reader->read (&buffer, 0, static_cast<int> (reader->lengthInSamples), 0, true, true);
    return true;
}

bool writeWav (juce::AudioFormatManager& formats, const juce::File& file,
               const juce::AudioBuffer<float>& buffer, double sampleRate, int bitsPerSample)
{
    // FileOutputStream does not truncate an existing file on all platforms;
    // without this, a repeated render (same --out path) would keep the old
    // leading audio and leave trailing data behind.
    if (file.existsAsFile() && ! file.deleteFile())
    {
        std::cerr << "cannot replace output: " << file.getFullPathName() << std::endl;
        return false;
    }

    std::unique_ptr<juce::OutputStream> stream (new juce::FileOutputStream (file));
    if (stream == nullptr || ! static_cast<juce::FileOutputStream*> (stream.get())->openedOk())
    {
        std::cerr << "cannot open output: " << file.getFullPathName() << std::endl;
        return false;
    }

    auto* wav = formats.findFormatForFileExtension ("wav");
    if (wav == nullptr)
    {
        std::cerr << "wav format not available (registerBasicFormats failed?)" << std::endl;
        return false;
    }
    const juce::AudioFormatWriterOptions writerOptions =
        juce::AudioFormatWriterOptions()
            .withSampleRate (sampleRate)
            .withNumChannels (buffer.getNumChannels())
            .withBitsPerSample (juce::jlimit (16, 32, bitsPerSample));
    // The writer takes ownership of the stream (JUCE 9 API).
    std::unique_ptr<juce::AudioFormatWriter> writer (wav->createWriterFor (stream, writerOptions));
    if (writer == nullptr)
    {
        std::cerr << "cannot create wav writer" << std::endl;
        return false;
    }

    const bool ok = writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    if (! ok)
        std::cerr << "wav write failed: " << file.getFullPathName() << std::endl;
    return ok;
}

int renderOnce (const Options& o, int blockSize, const juce::AudioBuffer<float>& input,
                double inputRate, juce::AudioBuffer<float>& result)
{
    offline::RenderOptions ro;
    ro.sampleRate = o.sampleRate;
    ro.blockSize = blockSize;
    ro.randomSeed = o.randomSeed;
    ro.maxSeconds = o.maxSeconds;
    ro.paramValues = o.paramValues;

    const auto start = juce::Time::getMillisecondCounterHiRes();
    const auto n = offline::renderBuffer (result, input, ro);
    const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;

    std::cout << "rendered " << n << " samples (" << (n / ro.sampleRate) << " s) "
              << "in " << static_cast<int> (elapsed) << " ms "
              << "| sr=" << static_cast<int> (ro.sampleRate)
              << " block=" << blockSize
              << " in=" << static_cast<int> (inputRate) << " Hz"
              << std::endl;
    return n;
}

} // namespace

int main (int argc, char* argv[])
{
    Options o;
    if (! parseArgs (argc, argv, o))
    {
        printUsage (argv[0]);
        return 1;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    // Validate parameter IDs up front (unknown ids were silently ignored).
    {
        PluginProcessor probe;
        for (const auto& [id, value] : o.paramValues)
        {
            if (probe.apvts.getParameter (id) == nullptr)
            {
                std::cerr << "unknown parameter id: " << id << std::endl;
                return 1;
            }
        }
    }

    juce::AudioBuffer<float> input;
    double inputRate = 0.0;
    if (! readWav (formats, o.inFile, input, inputRate))
        return 1;

    if (o.blockScan.empty())
    {
        juce::AudioBuffer<float> result;
        if (renderOnce (o, o.blockSize, input, inputRate, result) <= 0)
            return 1;
        if (! writeWav (formats, o.outFile, result, o.sampleRate, o.bitsPerSample))
            return 1;
        std::cout << "wrote " << o.outFile.getFullPathName() << std::endl;
    }
    else
    {
        for (int block : o.blockScan)
        {
            if (block < 1 || block > 8192)
            {
                std::cerr << "block-scan value out of range: " << block << std::endl;
                return 1;
            }
            juce::AudioBuffer<float> result;
            if (renderOnce (o, block, input, inputRate, result) <= 0)
                return 1;
            const auto name = o.outFile.getFileNameWithoutExtension()
                            + ".b" + juce::String (block) + ".wav";
            const auto path = o.outFile.getSiblingFile (name);
            if (! writeWav (formats, path, result, o.sampleRate, o.bitsPerSample))
                return 1;
            std::cout << "wrote " << path.getFullPathName() << std::endl;
        }
    }

    return 0;
}
