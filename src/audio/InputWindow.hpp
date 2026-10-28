#ifndef COMMAND_LINE_PLAYER_INPUTWINDOW_H
#define COMMAND_LINE_PLAYER_INPUTWINDOW_H
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iosfwd>
#include <vector>


class InputWindow {
private:
    std::ifstream inputFile;
    std::vector<uint8_t> buffer;
    size_t pos = 0, len = 0;
    uint64_t fileEnd = 0, nextRead = 0;
    bool reachedEnd = false;
public:
    explicit InputWindow(size_t bufferSize = 128 * 1024) : buffer(bufferSize) {};

    InputWindow(InputWindow const&) = delete;
    InputWindow& operator=(InputWindow const&) = delete;

    bool open(const std::filesystem::path& p, uint64_t start, uint64_t end);
    void reset(uint64_t absoluteOffset);
    void refill();
    void consume(size_t n) { pos += n; }

    const uint8_t* data() const { return buffer.data() + pos; }
    size_t size() const { return len - pos; }
    bool eof() const { return reachedEnd; }
    uint64_t filePosition() const { return nextRead - size(); };
};


#endif //COMMAND_LINE_PLAYER_INPUTWINDOW_H
