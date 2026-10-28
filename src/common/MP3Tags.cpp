#include "MP3Tags.hpp"

std::optional<FrameInfo> parseFirstFrame(const uint8_t *buf, const size_t len) {
    if (len == 0 || buf == nullptr) return std::nullopt;

    mp3dec_t decoder;
    mp3dec_init(&decoder);
    mp3dec_frame_info_t info;
    mp3dec_decode_frame(&decoder, buf, static_cast<int>(len), nullptr, &info);
    if (info.hz == 0 || info.frame_bytes == 0) return std::nullopt;

    FrameInfo frameInfo;
    frameInfo.offset = static_cast<size_t>(info.frame_offset);
    frameInfo.hz = info.hz;
    frameInfo.channels = info.channels;
    frameInfo.layer = info.layer;
    frameInfo.bitrateKbps = info.bitrate_kbps;

    uint8_t mpegHeader[4];
    std::memcpy(mpegHeader, buf + info.frame_offset, sizeof(mpegHeader));
    frameInfo.mpeg1 = ((mpegHeader[1] >> 3) & 0x03) == 0x03;
    frameInfo.hasCrc = !static_cast<bool>(mpegHeader[1] & 0x01);
    frameInfo.mono = ((mpegHeader[3] >> 6) & 0x03) == 0x03;

    frameInfo.samplesPerFrame = (info.layer == 1) ? 384 : (info.layer == 2 || frameInfo.mpeg1) ? 1152 : 576;

    return frameInfo;
}

std::optional<VbrInfo> parseXing(const uint8_t *buf, const size_t len, const FrameInfo &f) {
    if (len == 0 || buf == nullptr) return std::nullopt;

    const uint8_t sideInfoSize = (f.mpeg1) ? (f.mono ? 17 : 32) : (f.mono ? 9 : 17);
    size_t pos = f.offset + 4 + (f.hasCrc ? 2 : 0) + sideInfoSize;

    if (pos + 8 > len) return std::nullopt;
    if (std::memcmp(buf + pos, "Xing", 4) != 0 &&
        std::memcmp(buf + pos, "Info", 4) != 0) return std::nullopt;

    VbrInfo VBR;

    uint32_t beFlags = 0;
    std::memcpy(&beFlags, buf + pos + 4, 4);
    const uint32_t flags = swapEndian32(beFlags);
    pos += 8;

    if (flags & 0x01) {
        if (pos + 4 > len) return std::nullopt;
        uint32_t beFrames = 0;
        std::memcpy(&beFrames, buf + pos, 4);
        VBR.frames = swapEndian32(beFrames);
        pos += 4;
    }

    if (flags & 0x02) {
        if (pos + 4 > len) return std::nullopt;
        uint32_t beBytes = 0;
        std::memcpy(&beBytes, buf + pos, 4);
        VBR.bytes = swapEndian32(beBytes);
        pos += 4;
    }

    if (flags & 0x04) {
        if (pos + 100 > len) return std::nullopt;
        std::memcpy(VBR.toc, buf + pos, 100);
        VBR.hasToc = true;
    }

    if (VBR.frames == 0) return std::nullopt;
    return VBR;
}

std::optional<VbrInfo> parseVbri(const uint8_t *buf, const size_t len, const FrameInfo &f) {
    if (len < f.offset + 32 + 4 || buf == nullptr) return std::nullopt;

    const uint8_t* VBRI = buf + f.offset + 32 + 4;
    if (std::memcmp(VBRI, "VBRI", 4) != 0) return std::nullopt;

    VbrInfo VBR { .frames = swapEndian32(*(VBRI + 14)), .bytes = swapEndian32(*(VBRI + 10)) };
    if (VBR.frames == 0) return std::nullopt;
    return VBR;
}

MP3_Header receiveHeader(uint8_t bytes[10]) {
    MP3_Header header;
    std::memcpy(header.fileID, bytes, 3);
    header.verMajor = bytes[3];
    header.verMinor = bytes[4];
    header.flags = bytes[5];
    header.tagSize = ((bytes[6] & 0x7F) << 21 | (bytes[7] & 0x7F) << 14 | (bytes[8] & 0x7F) << 7 | (bytes[9] & 0x7F));
    return header;
}

std::optional<AudioBounds> findAudioBounds(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::nullopt;

    f.seekg(0, std::ios::end);
    const uint64_t size = static_cast<uint64_t>(f.tellg());
    f.seekg(0, std::ios::beg);

    uint64_t start = 0, end = size;

    uint8_t h[10];
    if (f.read(reinterpret_cast<char*>(h), 10)) {
        MP3_Header header = receiveHeader(h);
        if (std::memcmp(header.fileID, "ID3", 3) == 0) {
            start = 10 + static_cast<uint64_t>(header.tagSize) + ((header.flags & 0x10) ? 10 : 0);
        }
    }
    f.clear();

    if (size >= start + 128) {
        char t[3];
        f.seekg(static_cast<std::streamoff>(size - 128));
        if (f.read(t, 3) && std::memcmp(t, "TAG", 3) == 0) end = size - 128;
    }

    if (start >= end) return std::nullopt;
    return AudioBounds{start, end};
}

MpegVersion receiveMPEGVersion(const uint8_t bit) {
    const uint8_t mask = (bit >> 3) & 0x03;
    switch (mask) {
        case 0:
        case 2:
            return MpegVersion::MPEG2;
        case 3:
            return MpegVersion::MPEG1;
        default:
            return MpegVersion::ERROR;
    }
}

int bitrateIndex(const MpegVersion version, const uint8_t bit) {
    const uint8_t mask = (bit >> 4) & 0x0F;
    switch (version) {
        case MpegVersion::MPEG1:
            return mpeg1Layer3[mask];
        case MpegVersion::MPEG2:
            return mpeg2Layer3[mask];
        default:
            return 0;
    }
}
