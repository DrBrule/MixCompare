#include "sources/FilePlayer.h"

namespace mixcompare
{

namespace
{
constexpr juce::int64 maxDecodedBytes = 1536ll * 1024 * 1024;
constexpr juce::int64 minTrackSamples = 64;
} // namespace

FilePlayer::FilePlayer()
{
    formats.registerBasicFormats();
}

FilePlayer::~FilePlayer()
{
    loader.removeAllJobs (true, 10000);
}

void FilePlayer::prepare (double sampleRate) noexcept
{
    hostSampleRate.store (sampleRate);
}

void FilePlayer::process (float* const* channels, int numSamples, bool canAdvance) noexcept
{
    for (int channel = 0; channel < 2; ++channel)
        juce::FloatVectorOperations::clear (channels[channel], numSamples);

    const juce::SpinLock::ScopedTryLockType lock (trackLock);

    if (! lock.isLocked() || track == nullptr)
        return;

    const auto length = track->audio.getNumSamples();

    if (const auto seconds = pendingSeek.exchange (-1.0); seconds >= 0.0)
    {
        readPosition = juce::jlimit (0, length - 1, (int) (seconds * track->sampleRate));

        for (auto& interpolator : interpolators)
            interpolator.reset();
    }

    if (canAdvance)
    {
        const auto ratio = track->sampleRate / hostSampleRate.load();
        auto written = 0;

        while (written < numSamples)
        {
            const auto available = length - readPosition;
            const auto toWrite = juce::jmin (numSamples - written, (int) (available / ratio));

            if (toWrite <= 0)
            {
                readPosition = 0; // Files are always longer than one step, so this can't repeat.
                continue;
            }

            auto used = toWrite;

            // At a matching sample rate nothing touches the audio, so it's compared bit for bit.
            // Otherwise, past the end of the file the interpolator is fed silence, rather than
            // left to read on. Looping happens above once it has run out.
            for (size_t channel = 0; channel < 2; ++channel)
            {
                const auto* source = track->audio.getReadPointer ((int) channel, readPosition);

                if (juce::exactlyEqual (ratio, 1.0))
                    juce::FloatVectorOperations::copy (channels[channel] + written, source, toWrite);
                else
                    used = interpolators[channel].process (ratio, source, channels[channel] + written, toWrite, available, 0);
            }

            readPosition = juce::jmin (length, readPosition + used);
            written += toWrite;
        }
    }

    positionSeconds.store (readPosition / track->sampleRate);
}

void FilePlayer::load (const juce::File& file)
{
    {
        const juce::ScopedLock lock (detailsLock);
        requestedFile = file;
        error.clear();
    }

    status.store (Status::loading);
    loader.addJob ([this, file] { decode (file); });
}

juce::File FilePlayer::getFile() const
{
    const juce::ScopedLock lock (detailsLock);
    return requestedFile;
}

juce::String FilePlayer::getError() const
{
    const juce::ScopedLock lock (detailsLock);
    return error;
}

void FilePlayer::fail (const juce::String& message)
{
    const juce::ScopedLock lock (detailsLock);
    error = message;
    status.store (Status::failed);
}

void FilePlayer::decode (const juce::File& file)
{
    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
        return fail ("Can't read " + file.getFileName() + " as audio");

    if (reader->lengthInSamples < minTrackSamples || reader->sampleRate <= 0.0)
        return fail (file.getFileName() + " has no usable audio in it");

    if (reader->lengthInSamples * 2 * (juce::int64) sizeof (float) > maxDecodedBytes)
        return fail (file.getFileName() + " is too long to load");

    auto decoded = std::make_unique<Track>();
    decoded->sampleRate = reader->sampleRate;
    decoded->audio.setSize (2, (int) reader->lengthInSamples);

    if (! reader->read (&decoded->audio, 0, (int) reader->lengthInSamples, 0, true, true))
        return fail ("Couldn't decode " + file.getFileName());

    if (reader->numChannels == 1)
        decoded->audio.copyFrom (1, 0, decoded->audio, 0, 0, decoded->audio.getNumSamples());

    const auto seconds = decoded->audio.getNumSamples() / decoded->sampleRate;

    {
        const juce::SpinLock::ScopedLockType lock (trackLock);
        std::swap (track, decoded);
        readPosition = 0;

        for (auto& interpolator : interpolators)
            interpolator.reset();

        pendingSeek.store (-1.0);
        positionSeconds.store (0.0);
        lengthSeconds.store (seconds);
    }

    // The previous track is freed here as `decoded` goes out of scope, away from the audio thread.
    generation.fetch_add (1);
    status.store (Status::ready);
}

} // namespace mixcompare
