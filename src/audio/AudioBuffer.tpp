#pragma once
#include <algorithm>
#include <vector>

#include "AudioBuffer.hpp"

template<typename T>
bool AudioBuffer<T>::push(const T *data, const size_t size) {
    if (size == 0) return true;
    if (size > spaceAvailable()) return false;

    const size_t toEnd = buffer.size() - writeIndex;
    const size_t first = std::min(size, toEnd);

    std::copy(data, data + first, buffer.begin() + writeIndex);
    std::copy(data + first, data, buffer.begin());

    writeIndex = (writeIndex + size) % buffer.size();
    return true;
}

template <typename T>
bool AudioBuffer<T>::push(const std::vector<T>& data) {
    return push(data.data(), data.size());
}

template <typename T>
void AudioBuffer<T>::clear() {
    readIndex = writeIndex = 0;
}

template<typename T>
bool AudioBuffer<T>::empty() const {
    return writeIndex == readIndex;
}

template<typename T>
AudioBuffer<T>::operator bool() const {
    return empty();
}

template <typename T>
template <typename OutputIt>
bool AudioBuffer<T>::pop(OutputIt destination, const size_t size) {
    if (size == 0) return true;
    if (size > dataSize()) return false;

    if (writeIndex > readIndex) {
        std::copy(buffer.begin() + readIndex,buffer.begin() + readIndex + size, destination);
    }
    else if (writeIndex < readIndex) {
        size_t dataToTheEnd = buffer.size() - readIndex;

        if (size <= dataToTheEnd) {
            std::copy(buffer.begin() + readIndex,buffer.begin() + readIndex + size, destination);
        }
        else {
            destination = std::copy(buffer.begin() + readIndex,buffer.end(), destination);
            std::copy(buffer.begin(),buffer.begin() + (size - dataToTheEnd), destination);
        }
    }

    readIndex = (readIndex + size) % buffer.size();
    return true;
}

template <typename T>
T* AudioBuffer<T>::writeData() {
    return buffer.data() + writeIndex;
}

template <typename T>
size_t AudioBuffer<T>::writableSize() const {
    if (writeIndex < readIndex) {
        return readIndex - writeIndex - 1;
    }

    return buffer.size() - writeIndex - (readIndex == 0 ? 1 : 0);
}

template <typename T>
bool AudioBuffer<T>::commitWrite(const size_t size) {
    if (size > writableSize()) return false;

    writeIndex = (writeIndex + size) % buffer.size();
    return true;
}

template <typename T>
size_t AudioBuffer<T>::spaceAvailable() const {
    if (readIndex == writeIndex) return buffer.size() - 1;
    if (writeIndex > readIndex) return (buffer.size() - writeIndex) + readIndex - 1;
    if (writeIndex < readIndex) return readIndex - writeIndex - 1;
    return 0;
}

template <typename T>
size_t AudioBuffer<T>::dataSize() const {
    if (writeIndex >= readIndex) return writeIndex - readIndex;
    return buffer.size() - readIndex + writeIndex;
}