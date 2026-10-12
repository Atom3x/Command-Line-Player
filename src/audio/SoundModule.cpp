#include "SoundModule.hpp"

void SoundModule::soundCallback(void* userdata, uint8_t* stream, int len) {
    SoundModule* sm = static_cast<SoundModule*>(userdata);

    std::memset(stream, 0, len);

    if (!sm->shouldPlay.load() || sm->isPaused.load()) return;

    std::lock_guard<std::mutex> bufferLock(sm->callbackBufferMutex);
    
    int samplesNeeded = len / sizeof(uint16_t);
    int samplesAvailable = std::min(samplesNeeded, static_cast<int>(sm->callbackBuffer.size()));

    if (samplesAvailable > 0) {
        std::vector<uint16_t> tempBuffer(samplesAvailable);
        std::copy(sm->callbackBuffer.begin(), sm->callbackBuffer.begin() + samplesAvailable, tempBuffer.begin());
        sm->callbackBuffer.erase(sm->callbackBuffer.begin(), sm->callbackBuffer.begin() + samplesAvailable);

        int sdlVolume = (sm->volume * SDL_MIX_MAXVOLUME) / 100;
        SDL_MixAudioFormat(stream, reinterpret_cast<uint8_t*>(tempBuffer.data()), 
                           AUDIO_S16SYS, samplesAvailable * sizeof(uint16_t), sdlVolume);
    }
}

SoundModule::SoundModule() {
	if (SDL_Init(SDL_INIT_AUDIO) != 0) {
		std::cerr << "SDL init error: " << SDL_GetError();
		return;
	}
	mp3dec_init(&mp3d);

	musicThread = std::thread([this] {
		std::unique_lock<std::mutex> lock(playCvMutex);

		while (!exitThread.load()) {
            bool hasMusic = false;
            {
                std::lock_guard<std::mutex> queueLock(musicQueueMutex);
                hasMusic = !musicQueue.empty();
            }

            if (!hasMusic) {
                playCv.wait(lock);
                continue;
            }

            if (exitThread.load()) break;

            if (deviceId != 0) {
                SDL_CloseAudioDevice(deviceId);
                deviceId = 0;
            }
            
            {
                std::unique_lock<std::mutex> lock(musicQueueMutex);
                if (musicQueue.empty()) continue;
                currentSong = musicQueue.front();
                musicQueue.pop_front();
            }

            double duration = songDuration(currentSong);
            {
                std::lock_guard<std::mutex> timeLock(timeMutex);
                currentSongDuration = std::chrono::duration<double>(duration);
                timeElapsed = std::chrono::seconds(0);
            }

            std::ifstream file(currentSong, std::ios::binary);
            
            if (!file) continue;

            file.seekg(0, std::ios::end);
            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);

            songBuffer.resize(size);
            if (!file.read(reinterpret_cast<char*>(songBuffer.data()), size)) {
                {
                    std::lock_guard<std::mutex> stopLockGuard(stopCvMutex);
                    shouldPlay.store(false);
                }
                stopCv.notify_one();
                continue;
            }

            short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
            mp3dec_frame_info_t info;
            uint8_t* musicData = songBuffer.data();
            size_t remaining = songBuffer.size();
            specInitialized = false;
            {
                std::lock_guard<std::mutex> stopLockGuard(stopCvMutex);
                shouldPlay.store(true);
            }
            {
                std::lock_guard<std::mutex> bufferLock(callbackBufferMutex);
                callbackBuffer.clear();
            }

            while (remaining > 0 && shouldPlay.load()) {
                if (isPaused.load()) {
                    if (deviceId != 0) SDL_PauseAudioDevice(deviceId, 1);
                   
                    std::unique_lock<std::mutex> pauseLock(pauseMutex);
                    pauseCv.wait(pauseLock, [this] { return !isPaused.load() || !shouldPlay.load(); });

                    if (!shouldPlay.load()) break;
                    if (deviceId != 0) SDL_PauseAudioDevice(deviceId, 0);
                }
                    
                if (seekRequested.load()) {
                    std::lock_guard<std::mutex> seekLock(seekMutex);
                    seekRequested.store(false);
                    if (progressSeek.load()) seekByProgress(musicData, remaining);
                    else seekBySeconds(musicData, remaining);
                }

                int samples = mp3dec_decode_frame(&mp3d, musicData, remaining, pcm, &info);

                if (!shouldPlay.load()) break;

                if (info.frame_bytes == 0) {
                    musicData++;
                    remaining--;
                    continue;
                }

                if (!specInitialized) {
                    spec.freq = info.hz;
                    spec.format = AUDIO_S16SYS;
                    spec.channels = info.channels;
                    spec.samples = 4096;
                    spec.callback = soundCallback;
                    spec.userdata = this;

                    deviceId = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
                    if (deviceId == 0) {
                        {
                            std::lock_guard<std::mutex> stopLockGuard(stopCvMutex);
                            stop();
                        }
                        stopCv.notify_one();
                        continue;
                    }
                    SDL_PauseAudioDevice(deviceId, 0);

                    specInitialized = true;
                }

                if (samples > 0) {
                    double frameDuration = static_cast<double>(samples) / info.hz;

                    {
                        std::lock_guard<std::mutex> bufferLock(callbackBufferMutex);
                        callbackBuffer.insert(callbackBuffer.end(), pcm, pcm + (samples * info.channels));
                    }

                    while (callbackBuffer.size() > static_cast<uint32_t>(spec.freq * spec.channels)
                        && shouldPlay.load())
                    {
                        SDL_Delay(5);
                    }

                    {
                        std::lock_guard<std::mutex> timeLock(timeMutex);
                        timeElapsed += std::chrono::duration<double>(frameDuration);
                    }
                    
                }

                musicData += info.frame_bytes;
                remaining -= info.frame_bytes;
            }

            while (!callbackBuffer.empty() && shouldPlay.load()) {
                SDL_Delay(10);
            }
            
            {
                std::lock_guard<std::mutex> bufferLock(callbackBufferMutex);
                std::vector<uint16_t>().swap(callbackBuffer);
                std::vector<uint8_t>().swap(songBuffer);
            }

            if (songEndingCallback != nullptr && shouldPlay.load()) {
                songEndingCallback();
            }

            bool hasMoreSongs = false;
            {
                std::lock_guard<std::mutex> queueLock(musicQueueMutex);
                hasMoreSongs = !musicQueue.empty();
            }

            if (hasMoreSongs) {
                continue;
            }

            if (deviceId != 0) {
                SDL_CloseAudioDevice(deviceId);
                deviceId = 0;
            }

            {
                std::lock_guard<std::mutex> stopLockGuard(stopCvMutex);
                stop();
            }
            stopCv.notify_one();
			
		}
	});
}

SoundModule::~SoundModule() {
    {
        std::lock_guard<std::mutex> stopLock(stopCvMutex);
        stopCv.notify_one();
    }

    playCv.notify_one();
    pauseCv.notify_one();

    exitThread.store(true);
    if (musicThread.joinable()) musicThread.join();

    if (deviceId != 0) {
        SDL_CloseAudioDevice(deviceId);
        deviceId = 0;
    }

    {
        std::lock_guard<std::mutex> bufferLock(callbackBufferMutex);
        callbackBuffer.clear();
    }
    currentSong.clear();
    songBuffer.clear();

    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SoundModule::setOnSongFinishedCallback(std::function<void()> callback) {
    songEndingCallback = callback;
}

void SoundModule::play(const std::filesystem::path& pathToSong) {
    if (shouldPlay.load()) {
        shouldPlay.store(false);

        std::unique_lock<std::mutex> stopLock(stopCvMutex);
        stopCv.wait(stopLock, [this]() { return !shouldPlay.load(); });
    }

    {
        std::lock_guard<std::mutex> timeLock(timeMutex);
        timeElapsed = std::chrono::seconds(0);
        currentSongDuration = std::chrono::duration<double>(songDuration(pathToSong));
    }
    {
        std::lock_guard<std::mutex> queueLock(musicQueueMutex);
        musicQueue.emplace_front(pathToSong);
    }
    {
        std::lock_guard<std::mutex> pauseLock(pauseMutex);
        isPaused.store(false);
    }

    pauseCv.notify_one();
    playCv.notify_one();
}

void SoundModule::pause() {
    std::lock_guard<std::mutex> pauseLock(pauseMutex);
    isPaused.store(!isPaused.load());
    pauseCv.notify_one();
}

void SoundModule::stop() {
    {
        std::lock_guard<std::mutex> timeLock(timeMutex);
        timeElapsed = std::chrono::seconds(0);
        currentSongDuration = std::chrono::seconds(0);
    }
    
    {
        std::lock_guard<std::mutex> pauseLock(pauseMutex);
        isPaused.store(false);
    }

    {
        std::lock_guard<std::mutex> bufferLock(callbackBufferMutex);
        std::vector<uint16_t>().swap(callbackBuffer);
        std::vector<uint8_t>().swap(songBuffer);
    }

    currentSong.clear();
    shouldPlay.store(false);
}

double SoundModule::getSongDuration() {
    std::lock_guard<std::mutex> timeLock(timeMutex);
    if (currentSongDuration == std::chrono::seconds(0)) return 0.0;
    return currentSongDuration.count();
}

void SoundModule::seekByProgress(uint8_t*& musicData, size_t& remaining) {
    MP3_Header header = recieveHeader(songBuffer.data());
    size_t songStart = 10 + header.tagSize;

    float newProgress = static_cast<float>(newSeekPosition.load()) / 100.0f;
    if (newProgress > 0.99f) {
        newProgress = 0.99f;
    }

    size_t songSize = songBuffer.size() - songStart;
    size_t newPosInBuffer = static_cast<size_t>(songSize * newProgress) + songStart;

    uint8_t* newMusicData = songBuffer.data() + newPosInBuffer;
    size_t newRemaining = songSize - (newPosInBuffer - songStart);

    while (newRemaining > 0) {
        if ((*newMusicData & 0xFF) == 0xFF)
            break;
        newRemaining--;
        newMusicData++;
        newPosInBuffer++;
    }
    if (newRemaining > 0) {
        remaining = newRemaining;
        musicData = newMusicData;

        {
            std::lock_guard<std::mutex> timeLock(timeMutex);
            timeElapsed = std::chrono::duration<double>(
                (static_cast<double>(newPosInBuffer - songStart) / songSize) * currentSongDuration.count()
            );
        }

        {
            std::lock_guard<std::mutex> bufferLock(callbackBufferMutex);
            callbackBuffer.clear();
        }
    }
}

void SoundModule::seekBySeconds(uint8_t*& musicData, size_t& remaining) {
    double targetTime = seekToTime.load();
    double totalDuration = getSongDuration();

    if (targetTime < 0) targetTime = 0;
    if (targetTime > totalDuration) targetTime = totalDuration;

    double progress = targetTime / totalDuration;

    MP3_Header header = recieveHeader(songBuffer.data());
    size_t songStart = 10 + header.tagSize;
    size_t songSize = songBuffer.size() - songStart;
    size_t newPosInBuffer = static_cast<size_t>(songSize * progress) + songStart;

    uint8_t* newMusicData = songBuffer.data() + newPosInBuffer;
    size_t newRemaining = songSize - (newPosInBuffer - songStart);

    while (newRemaining > 0) {
        if ((*newMusicData & 0xFF) == 0xFF)
            break;
        newRemaining--;
        newMusicData++;
        newPosInBuffer++;
    }

    if (newRemaining > 0) {
        remaining = newRemaining;
        musicData = newMusicData;

        {
            std::lock_guard<std::mutex> timeLock(timeMutex);
            timeElapsed = std::chrono::duration<double>(targetTime);
        }

        {
            std::lock_guard<std::mutex> bufferLock(callbackBufferMutex);
            callbackBuffer.clear();
        }
    }
}

double SoundModule::songDuration(const std::filesystem::path& pathToSong) {
    std::ifstream file(pathToSong, std::ios::binary);
    if (!file) return 0.0;

    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) return 0.0;

    std::streamsize remaining = buffer.size();
    uint8_t* musicData = buffer.data();

    mp3dec_t durationDecoder;
    mp3dec_init(&durationDecoder);

    mp3dec_frame_info_t frameInfo;
    uint64_t totalSamplesPerChannel = 0;
    int sampleRate = 0;

    while (remaining > 0) {
        int samples = mp3dec_decode_frame(&durationDecoder, musicData, remaining, nullptr, &frameInfo);
        if (frameInfo.frame_bytes == 0) {
            musicData++;
            remaining--;
            continue;
        }

        if (samples > 0) {
            if (sampleRate == 0) {
                sampleRate = frameInfo.hz;
            }
            totalSamplesPerChannel += samples;
        }

        musicData += frameInfo.frame_bytes;
        remaining -= frameInfo.frame_bytes;
    }

    return static_cast<double>(totalSamplesPerChannel / sampleRate);
}

double SoundModule::songDuration() {
    return songDuration(currentSong);
}

double SoundModule::getTimeElapsed() {
    std::lock_guard<std::mutex> timeLock(timeMutex);
    return timeElapsed.count();
}

double SoundModule::getProgress() {
    std::lock_guard<std::mutex> timeLock(timeMutex);
    if (currentSongDuration == std::chrono::seconds(0)) return 0.0;
    double progress = timeElapsed.count() / currentSongDuration.count();
    if (progress > 1.0) progress = 1.0;
    
    return progress;
}

bool SoundModule::paused() {
    return isPaused.load();
}

void SoundModule::seekTo(int newProgressPoint) {
    if (!shouldPlay.load()) return;
    if (newProgressPoint < 0) newProgressPoint = 0;
    if (newProgressPoint > 100) newProgressPoint = 100;

    std::lock_guard<std::mutex> seekLock(seekMutex);
    newSeekPosition.store(newProgressPoint);
    progressSeek.store(true);
    seekRequested.store(true);
}

void SoundModule::seekToSeconds(int secondsFromCurrentPoint) {
    if (!shouldPlay.load()) return;

    double currentTime = getTimeElapsed();
    double newTime = currentTime + secondsFromCurrentPoint;

    if (newTime < 0) newTime = 0;
    double maxTime = getSongDuration();
    if (newTime > maxTime) newTime = maxTime;

    std::lock_guard<std::mutex> seekLock(seekMutex);
    seekToTime.store(newTime);
    progressSeek.store(false);          
    seekRequested.store(true);
}

void SoundModule::changeVolume(int newVolume) {
    if (newVolume < 0) newVolume = 0;
    if (newVolume > 100) newVolume = 100;
    volume = static_cast<uint8_t>(newVolume);
}

std::wstring SoundModule::fromDoubleToTime(const double& seconds) {
    if (seconds <= 0) return L"00:00";
    int min = static_cast<int>(seconds) / 60, sec = static_cast<int>(seconds) % 60;
    
    std::wostringstream resultStr;
    resultStr << std::setfill(L'0') << std::setw(2) << min << L":" << std::setw(2) << sec;
    return resultStr.str();
}


