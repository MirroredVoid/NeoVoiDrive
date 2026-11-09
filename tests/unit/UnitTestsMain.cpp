/*
    NeoVoiDrive unit test harness entry point (T20).

    Console runner for every juce_core::UnitTest-derived case in the tree.
    Follows the JUCE 9.0 official pattern (extras/UnitTestRunner/Source/Main.cpp),
    slimmed down for CI use:

      - all log output goes to stdout (on Windows the default Logger target is
        OutputDebugString, which is invisible in a console / CI log);
      - assertions on failure are disabled so a failed expect() never blocks
        the process with a debugger dialog;
      - exit code is 0 when every test passes, 1 otherwise — suitable for
        ctest and CI pipelines.

    Options:
      --name=TestName       run only tests with this name
      --category=Category   run only tests in this category
      --seed=123 / --seed=0x1a  deterministic random seed
      --list-categories     list all registered test categories and exit
      --help                show usage
*/

#include <juce_events/juce_events.h> // DeletedAtShutdown lives in juce_events since JUCE 9

#include <iostream>

class ConsoleLogger final : public juce::Logger
{
    void logMessage (const juce::String& message) override
    {
        std::cout << message << std::endl;

       #if JUCE_WINDOWS
        juce::Logger::outputDebugString (message);
       #endif
    }
};

class ConsoleUnitTestRunner final : public juce::UnitTestRunner
{
    void logMessage (const juce::String& message) override
    {
        juce::Logger::writeToLog (message);
    }
};

int main (int argc, char* argv[])
{
    constexpr auto helpOption       = "--help|-h";
    constexpr auto listOption       = "--list-categories|-l";
    constexpr auto categoryOption   = "--category|-c";
    constexpr auto seedOption       = "--seed|-s";
    constexpr auto nameOption       = "--name|-n";

    juce::ArgumentList args (argc, argv);

    if (args.containsOption (helpOption))
    {
        std::cout << argv[0]
                  << " [" << helpOption << "]"
                  << " [" << listOption << "]"
                  << " [" << categoryOption << "=category]"
                  << " [" << seedOption << "=seed]"
                  << " [" << nameOption << "=name]"
                  << std::endl;
        return 0;
    }

    if (args.containsOption (listOption))
    {
        for (auto& category : juce::UnitTest::getAllCategories())
            std::cout << category << std::endl;

        return 0;
    }

    ConsoleLogger logger;
    juce::Logger::setCurrentLogger (&logger);

    const juce::ScopeGuard onExit { [&]
    {
        juce::Logger::setCurrentLogger (nullptr);
        juce::DeletedAtShutdown::deleteAll();
    }};

    ConsoleUnitTestRunner runner;
    runner.setAssertOnFailure (false); // never block on a debugger dialog in CI

    const auto seed = [&]
    {
        if (args.containsOption (seedOption))
        {
            auto seedValueString = args.getValueForOption (seedOption);

            if (seedValueString.startsWith ("0x"))
                return seedValueString.getHexValue64();

            return seedValueString.getLargeIntValue();
        }

        return juce::Random::getSystemRandom().nextInt64();
    }();

    if (args.containsOption (categoryOption) || args.containsOption (nameOption))
    {
        while (args.containsOption (categoryOption))
            runner.runTestsInCategory (args.removeValueForOption (categoryOption), seed);

        while (args.containsOption (nameOption))
            runner.runTestsWithName (args.removeValueForOption (nameOption), seed);
    }
    else
    {
        runner.runAllTests (seed);
    }

    std::vector<juce::String> failures;

    const auto numResults = runner.getNumResults();

    if (numResults == 0)
    {
        // No tests matched the given --name/--category filters (or nothing is
        // registered). Treat this as a failure so a typo can never produce a
        // green CI run.
        logger.writeToLog ("No tests matched the given filters");
        return 1;
    }

    for (int i = 0; i < numResults; ++i)
    {
        auto* result = runner.getResult (i);

        if (result->failures > 0)
        {
            const auto testName = result->unitTestName + " / " + result->subcategoryName;
            const auto testSummary = juce::String (result->failures)
                                   + " test failure" + (result->failures > 1 ? "s" : "");
            const auto newLineAndTab = juce::newLine + "\t";

            failures.push_back (testName + ": " + testSummary + newLineAndTab
                                + result->messages.joinIntoString (newLineAndTab));
        }
    }

    logger.writeToLog (juce::newLine + juce::String::repeatedString ("-", 65));

    if (! failures.empty())
    {
        logger.writeToLog ("Test failure summary:");

        for (const auto& failure : failures)
            logger.writeToLog (juce::newLine + failure);

        return 1;
    }

    logger.writeToLog ("All tests completed successfully");
    return 0;
}
