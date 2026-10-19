#pragma once
#include "../common/headers.hpp"

class SoundModule {
private:
    mp3dec_t mp3d = {};
    std::thread musicThread;
    std::filesystem::path currentSong;
    std::list<std::filesystem::path> musicQueue = {};
    std::vector<uint8_t> songBuffer;
    std::vector<uint16_t> callbackBuffer;
    std::chrono::duration<double> timeElapsed = std::chrono::seconds(0), currentSongDuration = std::chrono::seconds(0);

    std::condition_variable playCv, pauseCv, stopCv, seekCv;
    std::mutex musicQueueMutex, playCvMutex, stopCvMutex, seekCvMutex, pauseMutex, timeMutex, seekMutex, callbackBufferMutex;

    uint32_t deviceId = 0;
    bool specInitialized = false;
    SDL_AudioSpec spec = {};
    int8_t volume = 100;
    std::atomic<bool> isPaused = false, shouldPlay = false, exitThread = false, seekRequested = false, 
                      volumeChangeRequested = false, progressSeek = false;
    std::atomic<int> newSeekPosition = 0, seekToTime = 0;
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