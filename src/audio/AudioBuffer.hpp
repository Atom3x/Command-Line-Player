#ifndef COMMAND_LINE_PLAYER_AUDIOBUFFER_H
#define COMMAND_LINE_PLAYER_AUDIOBUFFER_H
#pragma once
#include <stdexcept>
#include <vector>


template <typename T>
class AudioBuffer {
private:
    size_t AUDIO_BUFFER_SIZE = 4 * 1024 * 1024;
    size_t readIndex = 0, writeIndex = 0;
    std::vector<T> buffer;
public:
    explicit AudioBuffer(size_t bufferSize) : buffer(bufferSize) {
        if (bufferSize == 0) {
            throw std::invalid_argument("AudioBuffer size cannot be zero");
        }
    }
    bool push(const std::vector<T>& data);

    template <typename OutputIt>
    bool pop(OutputIt destination, const size_t size);

    [[nodiscard]] size_t spaceAvailable() const;
    [[nodiscard]] size_t dataSize() const;
};

#include "AudioBuffer.tpp"
#endif //COMMAND_LINE_PLAYER_AUDIOBUFFER_H
