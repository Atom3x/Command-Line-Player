#pragma once
#include <algorithm>
#include <vector>

#include "AudioBuffer.hpp"

template <typename T>
bool AudioBuffer<T>::push(const std::vector<T>& data) {
    const size_t dataSize = data.size();
    if (dataSize == 0) return true;

    if (dataSize > spaceAvailable()) return false;
    if (writeIndex > readIndex) {
        size_t memoryToTheEnd = buffer.size() - writeIndex;
        if (dataSize > memoryToTheEnd) {
            std::copy(data.begin(), data.begin() + memoryToTheEnd, buffer.begin() + writeIndex);
            std::copy(data.begin() + memoryToTheEnd, data.end(), buffer.begin());
        }
        else {
            std::copy(data.begin(), data.end(), buffer.begin() + writeIndex);
        }
    }
    else {
        std::copy(data.begin(), data.end(), buffer.begin() + writeIndex);
    }
    writeIndex = (writeIndex + dataSize) % buffer.size();

    return true;
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