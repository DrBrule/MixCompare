#include "analysis/SourceAnalyser.h"

namespace mixcompare
{

SourceAnalyser::SourceAnalyser()
{
    queueData.clear();
}

void SourceAnalyser::prepare (double sampleRate) noexcept
{
    loudness.prepare (sampleRate);
    levels.prepare (sampleRate);
    spectrum.prepare (sampleRate);
    displaySpectrum.prepare (sampleRate);
    stereo.prepare (sampleRate);
    queue.reset();
}

void SourceAnalyser::processAudio (const float* left, const float* right, int numChannels, int numSamples) noexcept
{
    const float* const channels[] { left, right };
    loudness.process (channels, numChannels, numSamples);
    levels.process (left, right, numSamples);

    // If the analysis thread has fallen behind, the newest audio is dropped. That only costs
    // the displays a moment, and the audio thread must not wait for them.
    const auto write = queue.write (juce::jmin (numSamples, queue.getFreeSpace()));

    if (write.blockSize1 > 0)
    {
        queueData.copyFrom (0, write.startIndex1, left, write.blockSize1);
        queueData.copyFrom (1, write.startIndex1, right, write.blockSize1);
    }

    if (write.blockSize2 > 0)
    {
        queueData.copyFrom (0, write.startIndex2, left + write.blockSize1, write.blockSize2);
        queueData.copyFrom (1, write.startIndex2, right + write.blockSize1, write.blockSize2);
    }
}

void SourceAnalyser::drain() noexcept
{
    const auto read = queue.read (queue.getNumReady());

    const auto feed = [this] (int start, int size)
    {
        if (size > 0)
        {
            spectrum.push (queueData.getReadPointer (0, start), queueData.getReadPointer (1, start), size);
            displaySpectrum.push (queueData.getReadPointer (0, start), queueData.getReadPointer (1, start), size);
            stereo.push (queueData.getReadPointer (0, start), queueData.getReadPointer (1, start), size);
        }
    };

    feed (read.startIndex1, read.blockSize1);
    feed (read.startIndex2, read.blockSize2);
}

void SourceAnalyser::requestReset() noexcept
{
    loudness.requestReset();
    levels.requestReset();
    spectrum.requestReset();
    stereo.requestReset();
}

} // namespace mixcompare
