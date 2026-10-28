#pragma once
#include "../common/headers.hpp"
#include "common/MP3Tags.hpp"

enum class FileType {
	NONE,
	DIR,
	MP3
};

struct PlayerFile {
	FileType type;
	std::filesystem::path path;

	PlayerFile() : type(FileType::NONE), path(L"") {}
	PlayerFile(FileType type, std::filesystem::path path) : type(type), path(path) {}
};


class FilesystemModule {
	std::filesystem::path currentPath = appPath / "music";
	std::unordered_map<std::wstring, PlayerFile> fileList = { };

	std::optional<std::wstring> recieveSongName(std::filesystem::path pathToSong);	
public:
	void readMusicList();
	std::filesystem::path getPathByName(std::wstring const& songName) const;
	const std::unordered_map<std::wstring, PlayerFile>& getMusicList();
};

