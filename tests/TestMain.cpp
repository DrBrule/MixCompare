#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

int main()
{
    // AudioProcessorValueTreeState needs a message manager to exist.
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory ("MixCompare");

    auto failures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    juce::Logger::writeToLog (failures == 0 ? "All tests passed" : juce::String (failures) + " failure(s)");
    return failures == 0 ? 0 : 1;
}
