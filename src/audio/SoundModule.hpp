#pragma once
#include "../common/headers.hpp"
#include "AudioBuffer.hpp"
#include "common/MP3Tags.hpp"

class SoundModule {
private:
    mp3dec_t mp3d = {};
    std::thread musicThread;
    std::filesystem::path currentSong;
    std::list<std::filesystem::path> musicQueue = {};
    AudioBuffer<int16_t> pcmQueue{131072};
    std::chrono::duration<double> timeElapsed = std::chrono::seconds(0), currentSongDuration = std::chrono::seconds(0);

    std::condition_variable playCv, pauseCv, stopCv, pcmSpaceCv;
    std::mutex musicQueueMutex, playCvMutex, stopCvMutex, pauseMutex, timeMutex, seekMutex, pcmMutex;

    uint32_t deviceId = 0;
    bool specInitialized = false;
    SDL_AudioSpec spec = {};
    std::atomic<uint8_t> volume = 100;
    std::atomic<bool> isPaused = false, shouldPlay = false, exitThread = false, seekRequested = false, progressSeek = false;
    std::atomic<int> newSeekPosition = 0;
    std::atomic<double> seekToTime = 0;
    std::function<void()> songEndingCallback;
    
    void seekByProgress(uint8_t*& musicData, size_t& remaining);
    void seekBySeconds(uint8_t*& musicData, size_t& remaining);
    double songDuration(const std::filesystem::path& pathToSong);
    double songDuration();

    static void soundCallback(void* userdata, uint8_t* stream, int len);
public:
    SoundModule();
    ~SoundModule();

    void setOnSongFinishedCallback(std::function<void()> callback);

    void play(const std::filesystem::path& pathToSong);
    void pause();
    void stop();
    double getSongDuration();
    double getTimeElapsed();
    double getProgress();
    bool paused();
    void seekTo(int newProgressPoint);
    void seekToSeconds(int secondsFromCurrentPoint);
    void changeVolume(int newVolume);
    std::wstring fromDoubleToTime(const double& seconds);
};