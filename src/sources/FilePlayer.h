#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <atomic>
#include <memory>

namespace mixcompare
{

// Plays a song, looping, from memory. Files are decoded whole on a background thread, so the
// audio thread never touches the disk and the song can be swapped while audio is running.
class FilePlayer
{
public:
    enum class Status { empty, loading, ready, failed };

    FilePlayer();
    ~FilePlayer();

    void prepare (double sampleRate) noexcept;

    // Audio thread. Writes two channels, or silence when stopped or nothing is loaded.
    void process (float* const* channels, int numSamples, bool canAdvance) noexcept;

    // Any other thread.
    void load (const juce::File& file);
    juce::File getFile() const;
    juce::String getError() const;
    juce::String getFileWildcard() const { return formats.getWildcardForAllFormats(); }

    Status getStatus() const noexcept { return status.load(); }
    bool isPlaying() const noexcept { return playing.load(); }
    void setPlaying (bool shouldPlay) noexcept { playing.store (shouldPlay); }
    void seek (double seconds) noexcept { pendingSeek.store (seconds); }
    double getPositionSeconds() const noexcept { return positionSeconds.load(); }
    double getLengthSeconds() const noexcept { return lengthSeconds.load(); }

    // Changes every time a new song starts, so listeners can discard what they knew of the last.
    int getGeneration() const noexcept { return generation.load(); }

private:
    struct Track
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 0.0;
    };

    void decode (const juce::File& file);
    void fail (const juce::String& error);

    juce::AudioFormatManager formats;

    juce::SpinLock trackLock; // Held only to swap the track, and the audio thread only ever tries it.
    std::unique_ptr<Track> track;
    std::array<juce::WindowedSincInterpolator, 2> interpolators;
    int readPosition = 0;

    std::atomic<double> hostSampleRate { 44100.0 };
    std::atomic<double> pendingSeek { -1.0 };
    std::atomic<double> positionSeconds { 0.0 };
    std::atomic<double> lengthSeconds { 0.0 };
    std::atomic<bool> playing { true };
    std::atomic<int> generation { 0 };
    std::atomic<Status> status { Status::empty };

    mutable juce::CriticalSection detailsLock;
    juce::File requestedFile;
    juce::String error;

    // Last, so that jobs have finished before anything they use is destroyed.
    juce::ThreadPool loader { 1 };
};

} // namespace mixcompare
