#include "FilesystemModule.h"

void FilesystemModule::readMusicList() {
    if (!fileList.empty()) fileList.clear();

    for (const auto& entry : std::filesystem::directory_iterator(currentPath)) {
        if (std::filesystem::is_regular_file(entry.status())) {
            std::ifstream file(entry.path(), std::ios::binary);

            std::array<uint8_t, 10> headerData;
            if (!file.read(reinterpret_cast<char*>(headerData.data()), 10)) { 
                file.close();
                continue; 
            }

            MP3_Header header = recieveHeader(headerData.data());
            if (std::memcmp(header.fileID, "ID3", 3) != 0) {
                file.close();
                continue;
            }
            file.close();

            std::optional<std::wstring> songName = recieveSongName(entry.path());
            if (songName) fileList[*songName] = { FileType::MP3, entry.path() };
        }
        else if (std::filesystem::is_directory(entry.status())) {
            fileList[entry.path().filename().wstring()] = { FileType::DIR, entry.path() };
        }
    }
}

std::optional<std::wstring> FilesystemModule::recieveSongName(std::filesystem::path pathToSong) {
    std::ifstream file(pathToSong, std::ios::binary);
    if (!file) return L"";

    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::array<uint8_t, 10> headerData;
    if (!file.read(reinterpret_cast<char*>(headerData.data()), 10)) return std::nullopt;
    MP3_Header header = recieveHeader(headerData.data());
    if (std::memcmp(header.fileID, "ID3", 3) != 0) return std::nullopt;

    std::vector<uint8_t> data(header.tagSize);
    file.seekg(10, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(data.data()), header.tagSize)) return std::nullopt;

    size_t pos = 0;
    std::pair<std::wstring, std::wstring> songArtistAndName;
    while (pos < data.size()) {
        std::string tag(reinterpret_cast<char*>(&data[pos]), 4);

        uint32_t sectionSize = ((data[pos + 4] << 24) | (data[pos + 5] << 16) | (data[pos + 6] << 8) | (data[pos + 7]));

        if (!std::isalnum(tag[0])) break;
        if (tag != "TIT2" && tag != "TPE1") {
            pos += sectionSize + 10;
            continue;
        }

        uint32_t sectionStart = pos + 10;
        uint8_t encoding = data[sectionStart];
        std::wstring extractedText;

        switch (encoding) {
        case 0: {
            extractedText = std::wstring(data.begin() + sectionStart + 1, data.begin() + sectionStart + sectionSize);
            break;
        }

        case 1: {
            const wchar_t* namePtr = reinterpret_cast<wchar_t*>(&data[sectionStart + 1]);
            uint32_t textSize = sectionSize - 1;

            uint16_t bom = ((data[sectionStart + 1] << 8) | data[sectionStart + 2]);
            if (bom == 0xFEFF || bom == 0xFFFE) {
                textSize -= 1;
                namePtr += 1;
            }

            uint32_t charCount = textSize / 2;
            extractedText = std::wstring(namePtr, charCount);
            break;
        }

        case 2: {
            const wchar_t* namePtr = reinterpret_cast<wchar_t*>(&data[sectionStart + 1]);
            uint32_t charCount = (sectionSize - 1) / 2;
            extractedText = std::wstring(namePtr, charCount);
            break;
        }

        case 3: {
            std::string utf8str(reinterpret_cast<const char*>(&data[sectionStart + 1]), sectionSize - 1);
            std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
            extractedText = converter.from_bytes(utf8str);
            break;
        }
        }

        (tag == "TIT2") ? songArtistAndName.second = extractedText : songArtistAndName.first = extractedText;
        pos += sectionSize + 10;
    }

    if (songArtistAndName.first.empty()) return pathToSong.filename().wstring();
    if (songArtistAndName.second.empty()) return songArtistAndName.first;
   
    return songArtistAndName.first + L" - " + songArtistAndName.second;
}

std::filesystem::path FilesystemModule::getPathByName(std::wstring const& songName) const {
	return fileList.at(songName).path;
}

const std::unordered_map<std::wstring, PlayerFile>& FilesystemModule::getMusicList() {
    readMusicList();
    return fileList;
}
