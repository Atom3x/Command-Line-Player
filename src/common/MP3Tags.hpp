#pragma once
#include "headers.hpp"

static inline uint32_t swapEndian32(const uint32_t val) {
    return ((0xFF000000 & val) >> 24) | ((0x00FF0000 & val) >> 8)
    | ((0x0000FF00 & val) << 8) | ((0x000000FF & val) << 24);
}

#pragma pack(push, 1)
struct MP3_Header {
    char fileID[3];
    uint8_t verMajor;
    uint8_t verMinor;
    uint8_t flags;
    uint32_t tagSize;
};
#pragma pack(pop)

struct AudioBounds { uint64_t start, end; };

struct FrameInfo {
    size_t offset = 0;
    int hz = 0, channels = 0, layer = 0, bitrateKbps = 0;
    int samplesPerFrame = 0;
    bool mpeg1 = false, mono = false, hasCrc = false;
};

struct VbrInfo {
    uint32_t frames = 0;
    uint32_t bytes = 0;
    bool hasToc = false;
    uint8_t toc[100] = {};
};

std::optional<FrameInfo> parseFirstFrame(const uint8_t* buf, const size_t len);
std::optional<VbrInfo>   parseXing(const uint8_t* buf, const size_t len, const FrameInfo& f);
std::optional<VbrInfo>   parseVbri(const uint8_t* buf, const size_t len, const FrameInfo& f);

MP3_Header receiveHeader(uint8_t bytes[10]);
std::optional<AudioBounds> findAudioBounds(const std::filesystem::path& p);

enum class MpegVersion { MPEG1, MPEG2, ERROR };
MpegVersion receiveMPEGVersion(const uint8_t bit);

const int mpeg1Layer3[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
const int mpeg2Layer3[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
int bitrateIndex(const MpegVersion version, const uint8_t bit);