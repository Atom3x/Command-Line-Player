
cmake_minimum_required(VERSION 4.3)
project(Command_Line_Player)

set(CMAKE_CXX_STANDARD 20)

include_directories(src)
include_directories(src/third_party/minimp3)

find_package(ftxui CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

add_executable(Command_Line_Player
        src/main.cpp
        src/ui/ButtonStyles.cpp
        src/ui/ButtonStyles.h
        src/player/Player.cpp
        src/player/Player.hpp
        src/audio/SoundModule.cpp
        src/audio/SoundModule.hpp
        src/filesystem/FilesystemModule.cpp
        src/filesystem/FilesystemModule.h
        src/common/headers.hpp
        src/audio/minimp3_implementation.cpp
        src/third_party/minimp3/minimp3.h
        src/third_party/minimp3/minimp3_ex.h
)

target_link_libraries(Command_Line_Player
        PRIVATE ftxui::screen
        PRIVATE ftxui::dom
        PRIVATE ftxui::component
        PRIVATE PkgConfig::SDL2
)
