#pragma once
#define _CRT_SECURE_NO_WARNINGS
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

// STD HEADERS

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <array>
#include <filesystem>
#include <fstream>
#include <codecvt>
#include <thread>
#include <atomic>
#include <optional>
#include <list>
#include <condition_variable>
#include <mutex>
#include <random>
#include <cstring>


// UI HEADERS

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/screen_interactive.hpp>

// SOUND PROCESSING HEADERS

extern "C" {
	#include "minimp3.h"
	#include "minimp3_ex.h"
}

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_audio.h>

// GLOBAL VARS

static const std::filesystem::path appPath = std::filesystem::current_path();
constexpr std::size_t BLOCK_SIZE = 16 * 1024;

