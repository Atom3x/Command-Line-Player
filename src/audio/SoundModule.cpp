#include "SoundModule.hpp"

#include "InputWindow.hpp"

void SoundModule::soundCallback(void* userdata, uint8_t* stream, int len) {
    SoundModule* sm = static_cast<SoundModule*>(userdata);

    std::memset(stream, 0, len);

    if (!sm->shouldPlay.load() || sm->isPaused.load()) return;

    const int samplesNeeded = len / sizeof(int16_t);
    int samplesAvailable = 0;

    {
        std::lock_guard bufferLock(sm->pcmMutex);

        samplesAvailable = std::min(samplesNeeded, static_cast<int>(sm->pcmQueue.dataSize()));

        if (samplesAvailable > 0)
            sm->pcmQueue.pop(reinterpret_cast<int16_t*>(stream), samplesAvailable);

        // if (samplesAvailable > 0) {
            // std::vector<uint16_t> tempBuffer(samplesAvailable);
            // std::copy(sm->callbackBuffer.begin(), sm->callbackBuffer.begin() + samplesAvailable, tempBuffer.begin());
            // sm->callbackBuffer.erase(sm->callbackBuffer.begin(), sm->callbackBuffer.begin() + samplesAvailable);
            //
            // int sdlVolume = (sm->volume * SDL_MIX_MAXVOLUME) / 100;
            // SDL_MixAudioFormat(stream, reinterpret_cast<uint8_t*>(tempBuffer.data()),
            //                    AUDIO_S16SYS, samplesAvailable * sizeof(uint16_t), sdlVolume);
        // }
    }
    sm->pcmSpaceCv.notify_one();

    int16_t* out = reinterpret_cast<int16_t*>(stream);
    int8_t volume = sm->volume.load();
    if (volume < 100) {
        for (int i = 0; i < samplesAvailable; i++) {
            out[i] = static_cast<int16_t>(static_cast<int>(out[i]) * volume / 100);        }
    }
}

SoundModule::SoundModule() {
	if (SDL_Init(SDL_INIT_AUDIO) != 0) {
		std::cerr << "SDL init error: " << SDL_GetError();
		return;
	}

	musicThread = std::thread([this] {
		std::unique_lock<std::mutex> lock(playCvMutex);

		while (!exitThread.load()) {
            bool hasMusic = false;
            {
                std::lock_guard queueLock(musicQueueMutex);
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
                std::lock_guard timeLock(timeMutex);
                currentSongDuration = std::chrono::duration<double>(duration);
                timeElapsed = std::chrono::seconds(0);
            }

		    std::optional<AudioBounds> bounds = findAudioBounds(currentSong);
		    if (!bounds) {
		        stop();
		        continue;
		    }

		    InputWindow window;
		    if (!window.open(currentSong, bounds->start, bounds->end)) {
		        stop();
		        continue;
		    }

		    mp3dec_init(&mp3d);
            short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
            mp3dec_frame_info_t info;

            specInitialized = false;
            {
                std::lock_guard stopLockGuard(stopCvMutex);
                shouldPlay.store(true);
            }
            {
                std::lock_guard bufferLock(pcmMutex);
                pcmQueue.clear();
            }

            while (shouldPlay.load()) {
                if (isPaused.load()) {
                    if (deviceId != 0) SDL_PauseAudioDevice(deviceId, 1);

                    std::unique_lock<std::mutex> pauseLock(pauseMutex);
                    pauseCv.wait(pauseLock, [this] { return !isPaused.load() || !shouldPlay.load(); });

                    if (!shouldPlay.load()) break;
                    if (deviceId != 0) SDL_PauseAudioDevice(deviceId, 0);
                }

                // if (seekRequested.load()) {
                //     std::lock_guard seekLock(seekMutex);
                //     seekRequested.store(false);
                //     if (progressSeek.load()) seekByProgress(musicData, remaining);
                //     else seekBySeconds(musicData, remaining);
                // }

                if (window.size() < 64 * 1024 && !window.eof()) window.refill();
                if (window.size() == 0 && window.eof()) break;

                int samples = mp3dec_decode_frame(&mp3d, window.data(), static_cast<int>(window.size()), pcm, &info);

                if (!shouldPlay.load()) break;

                if (info.frame_bytes == 0) {
                    if (window.eof()) break;
                    window.consume(1);
                    continue;
                }
                window.consume(info.frame_bytes);

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
                            std::lock_guard stopLockGuard(stopCvMutex);
                            stop();
                        }
                        stopCv.notify_one();
                        continue;
                    }
                    SDL_PauseAudioDevice(deviceId, 0);

                    specInitialized = true;
                }

                if (samples > 0) {
                    const double frameDuration = static_cast<double>(samples) / info.hz;
                    const size_t dataSize = info.channels * samples;

                    {
                        std::unique_lock bufferLock(pcmMutex);
                        while (!pcmSpaceCv.wait_for(bufferLock, std::chrono::milliseconds(10),
                            [&](){return pcmQueue.spaceAvailable() >= dataSize || !shouldPlay.load();})) {}

                        if (!shouldPlay.load()) break;
                        pcmQueue.push(pcm, dataSize);
                    }

                    {
                        std::lock_guard timeLock(timeMutex);
                        timeElapsed += std::chrono::duration<double>(frameDuration);
                    }

                }
            }

		    while (shouldPlay.load()) {
                {
                    std::lock_guard lk(pcmMutex);
                    if (pcmQueue.dataSize() == 0) break;
                }
                SDL_Delay(10);
            }

            {
                std::lock_guard bufferLock(pcmMutex);
                pcmQueue.clear();
                pcmSpaceCv.notify_all();
            }

            if (songEndingCallback != nullptr && shouldPlay.load()) {
                songEndingCallback();
            }

            bool hasMoreSongs = false;
            {
                std::lock_guard queueLock(musicQueueMutex);
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
                std::lock_guard stopLockGuard(stopCvMutex);
                stop();
            }
            stopCv.notify_one();

		}
	});
}

SoundModule::~SoundModule() {
    {
        std::lock_guard stopLock(stopCvMutex);
        stopCv.notify_one();
    }

    exitThread.store(true);
    shouldPlay.store(false);
    pcmSpaceCv.notify_all();
    pauseCv.notify_all();
    stopCv.notify_all();
    playCv.notify_all();

    if (musicThread.joinable()) musicThread.join();

    if (deviceId != 0) {
        SDL_CloseAudioDevice(deviceId);
        deviceId = 0;
    }

    {
        std::lock_guard bufferLock(pcmMutex);
        pcmQueue.clear();
    }
    currentSong.clear();

    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SoundModule::setOnSongFinishedCallback(std::function<void()> callback) {
    songEndingCallback = callback;
}

void SoundModule::play(const std::filesystem::path& pathToSong) {
    if (shouldPlay.load()) {
        shouldPlay.store(false);

        std::unique_lock stopLock(stopCvMutex);
        stopCv.wait(stopLock, [this]() { return !shouldPlay.load(); });
    }

    {
        std::lock_guard timeLock(timeMutex);
        timeElapsed = std::chrono::seconds(0);
    }
    {
        std::lock_guard queueLock(musicQueueMutex);
        musicQueue.emplace_front(pathToSong);
    }
    {
        std::lock_guard pauseLock(pauseMutex);
        isPaused.store(false);
    }

    pauseCv.notify_one();
    playCv.notify_one();
}

void SoundModule::pause() {
    std::lock_guard pauseLock(pauseMutex);
    isPaused.store(!isPaused.load());
    pauseCv.notify_one();
}

void SoundModule::stop() {
    {
        std::lock_guard timeLock(timeMutex);
        timeElapsed = std::chrono::seconds(0);
        currentSongDuration = std::chrono::seconds(0);
    }

    {
        std::lock_guard pauseLock(pauseMutex);
        isPaused.store(false);
    }

    {
        std::lock_guard bufferLock(pcmMutex);
        pcmQueue.clear();
        pcmSpaceCv.notify_all();
    }

    currentSong.clear();
    shouldPlay.store(false);
}

double SoundModule::getSongDuration() {
    std::lock_guard timeLock(timeMutex);
    if (currentSongDuration == std::chrono::seconds(0)) return 0.0;
    return currentSongDuration.count();
}

// void SoundModule::seekByProgress(uint8_t*& musicData, size_t& remaining) {
//     MP3_Header header = receiveHeader(songBuffer.data());
//     size_t songStart = 10 + header.tagSize;
//
//     float newProgress = static_cast<float>(newSeekPosition.load()) / 100.0f;
//     if (newProgress > 0.99f) {
//         newProgress = 0.99f;
//     }
//
//     size_t songSize = songBuffer.size() - songStart;
//     size_t newPosInBuffer = static_cast<size_t>(songSize * newProgress) + songStart;
//
//     uint8_t* newMusicData = songBuffer.data() + newPosInBuffer;
//     size_t newRemaining = songSize - (newPosInBuffer - songStart);
//
//     while (newRemaining > 0) {
//         if ((*newMusicData & 0xFF) == 0xFF)
//             break;
//         newRemaining--;
//         newMusicData++;
//         newPosInBuffer++;
//     }
//     if (newRemaining > 0) {
//         remaining = newRemaining;
//         musicData = newMusicData;
//
//         {
//             std::lock_guard timeLock(timeMutex);
//             timeElapsed = std::chrono::duration<double>(
//                 (static_cast<double>(newPosInBuffer - songStart) / songSize) * currentSongDuration.count()
//             );
//         }
//
//         {
//             std::lock_guard bufferLock(pcmMutex);
//             pcmQueue.clear();
//         }
//     }
// }
//
// void SoundModule::seekBySeconds(uint8_t*& musicData, size_t& remaining) {
//     double targetTime = seekToTime.load();
//     double totalDuration = getSongDuration();
//
//     if (targetTime < 0) targetTime = 0;
//     if (targetTime > totalDuration) targetTime = totalDuration;
//
//     double progress = targetTime / totalDuration;
//
//     MP3_Header header = receiveHeader(songBuffer.data());
//     size_t songStart = 10 + header.tagSize;
//     size_t songSize = songBuffer.size() - songStart;
//     size_t newPosInBuffer = static_cast<size_t>(songSize * progress) + songStart;
//
//     uint8_t* newMusicData = songBuffer.data() + newPosInBuffer;
//     size_t newRemaining = songSize - (newPosInBuffer - songStart);
//
//     while (newRemaining > 0) {
//         if ((*newMusicData & 0xFF) == 0xFF)
//             break;
//         newRemaining--;
//         newMusicData++;
//         newPosInBuffer++;
//     }
//
//     if (newRemaining > 0) {
//         remaining = newRemaining;
//         musicData = newMusicData;
//
//         {
//             std::lock_guard timeLock(timeMutex);
//             timeElapsed = std::chrono::duration<double>(targetTime);
//         }
//
//         {
//             std::lock_guard bufferLock(pcmMutex);
//             pcmQueue.clear();
//         }
//     }
// }

double SoundModule::songDuration(const std::filesystem::path& pathToSong) {
    std::optional<AudioBounds> bounds = findAudioBounds(pathToSong);
    if (!bounds) return 0.0;

    std::ifstream file(pathToSong, std::ios::binary);
    file.seekg(static_cast<std::streamoff>(bounds->start), std::ios::beg);

    int bufferSize = 8192;
    if (bounds->end - bounds->start < 8192)
        bufferSize = bounds->end - bounds->start;

    std::vector<uint8_t> buffer(bufferSize);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), bufferSize))
        return 0.0;

    std::optional<FrameInfo> frameInfo = parseFirstFrame(buffer.data(), bufferSize);
    if (!frameInfo) return 0.0;

    std::optional<VbrInfo> vbrInfo = parseXing(buffer.data(), bufferSize, *frameInfo);
    if (!vbrInfo) {
        vbrInfo = parseVbri(buffer.data(), bufferSize, *frameInfo);
        if (!vbrInfo)
            return static_cast<double>((bounds->end - bounds->start) * 8) / (frameInfo->bitrateKbps * 1000.0);
    }

    return static_cast<double>(vbrInfo->frames) * frameInfo->samplesPerFrame / frameInfo->hz;
}

double SoundModule::songDuration() {
    return songDuration(currentSong);
}

double SoundModule::getTimeElapsed() {
    double elapsed;
    {
        std::lock_guard lk(timeMutex);
        elapsed = timeElapsed.count();
    }
    size_t queued;
    {
        std::lock_guard lk(pcmMutex);
        queued = pcmQueue.dataSize();
    }
    if (spec.freq > 0 && spec.channels > 0)
        elapsed -= static_cast<double>(queued) / (spec.freq * spec.channels);
    return std::max(0.0, elapsed);
}

double SoundModule::getProgress() {
    std::lock_guard timeLock(timeMutex);
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

    std::lock_guard seekLock(seekMutex);
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

    std::lock_guard seekLock(seekMutex);
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


