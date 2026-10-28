#include "InputWindow.hpp"

#include <cstring>

bool InputWindow::open(const std::filesystem::path& p, uint64_t start, uint64_t end) {
    inputFile.open(p, std::ios::binary);
    if (!inputFile.is_open()) return false;
    fileEnd = end;
    reset(start);
    return true;
}

void InputWindow::reset(uint64_t absoluteOffset) {
    inputFile.clear();
    inputFile.seekg(absoluteOffset, std::ios::beg);
    len = pos = 0;
    nextRead = absoluteOffset;
    reachedEnd = (nextRead >= fileEnd);
    refill();
}

void InputWindow::refill() {
    if (reachedEnd) return;
    const size_t remaining = size();
    if (pos > 0) {
        std::memmove(buffer.data(), buffer.data() + pos, remaining);
        pos = 0;
        len = remaining;
    }

    const uint64_t fileLeft = fileEnd - nextRead;
    const size_t freeSpace = buffer.size() - len;
    const size_t toFill = std::min<uint64_t>(fileLeft, freeSpace);

    inputFile.read(reinterpret_cast<char*>(buffer.data() + len), toFill);
    const size_t got = static_cast<size_t>(inputFile.gcount());

    len += got;
    nextRead += got;

    if (nextRead >= fileEnd || got < toFill) reachedEnd = true;
}