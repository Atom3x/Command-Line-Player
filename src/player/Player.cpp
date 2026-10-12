#include "Player.hpp"
#include <algorithm>

void Player::handleSongEnding() {
    if (musicNames.empty()) {
        currentlyPlaying.clear();
        return;
    }

    switch (groupStates->currentMode) {
    case PlaybackMode::Normal:
        if (selectedSongIndex + 1 < musicNames.size()) {
            selectedSongIndex++;
            currentlyPlaying = musicNames[selectedSongIndex];
            sm.play(fm.getPathByName(musicNames[selectedSongIndex]));
        }
        else {
            currentlyPlaying.clear();
        }
        break;
    case PlaybackMode::Repeat:
        selectedSongIndex++;
        if (selectedSongIndex >= musicNames.size()) {
            selectedSongIndex = 0;
        }
        currentlyPlaying = musicNames[selectedSongIndex];
        sm.play(fm.getPathByName(musicNames[selectedSongIndex]));
        break;
    case PlaybackMode::RepeatOne:
        sm.play(fm.getPathByName(musicNames[selectedSongIndex]));
        break;
    case PlaybackMode::Shuffle:
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(0, musicNames.size() - 1);
        int newIndex = 0;
        do {
            newIndex = dist(gen);
        } while (newIndex == selectedSongIndex && musicNames.size() > 1);

        selectedSongIndex = newIndex;
        currentlyPlaying = musicNames[selectedSongIndex];
        sm.play(fm.getPathByName(musicNames[selectedSongIndex]));

        break;
    }
    screen.Post(Event::Custom);
}

Player::Player() {
    if (!std::filesystem::exists(appPath / "music")) {
        std::filesystem::create_directory(appPath / "music");
    }
    fm.readMusicList();

    sm.setOnSongFinishedCallback([this]() {
        screen.Post([this]() {
            handleSongEnding();
        });
    });

    auto name = Renderer([&] {
        return hbox(text(L"MP3 Player") | center | flex | bold) | border | xflex;
    });

    auto terminalSize = Terminal::Size();

    for (const auto& [name, file] : fm.getMusicList())
        musicNames.push_back(name);
    std::sort(musicNames.begin(), musicNames.end());

    auto menu = Menu(&musicNames, &selectedSongIndex, MenuOption::Vertical());
    auto refreshButton = Button(L"Refresh playlist!", [&]() {
        selectedSongIndex = 0;
        musicNames.clear();
        fm.getMusicList();
        for (const auto& [name, file] : fm.getMusicList())
            musicNames.push_back(name);
        std::sort(musicNames.begin(), musicNames.end());
    });

    auto musicPaneControls = Container::Vertical({ menu, refreshButton });

    auto musicPane = Renderer(musicPaneControls, [&] {
        return vbox(
            text("Music List") | center | bold,
            separator(),
            menu->Render(),
            filler(),
            refreshButton->Render()
        ) | border | size(WIDTH, EQUAL, terminalSize.dimx / 3);
    });

    auto playButton = Button(L"▶", [&] {
        if (sm.paused()) {
            sm.pause();
            return;
        }
        if (!musicNames.empty()) {
            currentlyPlaying = musicNames[selectedSongIndex];
            sm.play(fm.getPathByName(musicNames[selectedSongIndex]));
        }
    }, ButtonTextCentred());

    auto pauseButton = Button(L"∥", [&] {
        if (!currentlyPlaying.empty()) sm.pause();
    }, ButtonTextCentred());

    auto stopButton = Button(L"■", [&] {
        currentlyPlaying.clear();
        sm.stop();
    }, ButtonTextCentred());

    Box volumeSliderBox;
    auto volumeSlider = Slider<int>({
        .value = &volumeSliderValue,
        .min = 0,
        .max = 100,
        .increment = 1,
        .direction = Direction::Up
    }) | CatchEvent([&](Event event) {
        auto mouse = event.mouse();
        if (mouse.x >= volumeSliderBox.x_min && mouse.x <= volumeSliderBox.x_max &&
            mouse.y >= volumeSliderBox.y_min && mouse.y <= volumeSliderBox.y_max || userVolumeDragging) {
            if (event.is_mouse()) {
                if (event.mouse().button == Mouse::Left) {
                    if (event.mouse().motion == Mouse::Pressed) {
                        userVolumeDragging = true;
                    } else if (event.mouse().motion == Mouse::Released) {
                        if (userVolumeDragging) {
                            sm.changeVolume(volumeSliderValue);
                            userVolumeDragging = false;
                        }
                    }
                }
            }
        }
        return false;
    }) | Renderer([&](Element e) {
        return e | reflect(volumeSliderBox);
    });

    Box volumeUpButtonBox;
    auto volumeUpButton = Button(L"+", [&] {
        if (volumeSliderValue < 100) {
            volumeSliderValue++;
            sm.changeVolume(volumeSliderValue);
        }
    }, ButtonTextCentred()) | CatchEvent([&](Event event) {
        auto mouse = event.mouse();
        if (mouse.x >= volumeUpButtonBox.x_min && mouse.x <= volumeUpButtonBox.x_max &&
            mouse.y >= volumeUpButtonBox.y_min && mouse.y <= volumeUpButtonBox.y_max) {
            if (event.is_mouse()) {
                if (event.mouse().button == Mouse::Left) {
                    if (event.mouse().motion == Mouse::Pressed) {
                        if (!soundButtonHoldingUp.load()) {
                            holdingStart = std::chrono::steady_clock::now();
                            soundButtonHoldingUp.store(true);
                        }
                    } else if (event.mouse().motion == Mouse::Released) {
                        if (soundButtonHoldingUp.load()) {
                            soundButtonHoldingUp.store(false);
                            volumeChangedTimes = 0;
                            volumeChangeStep = 200;
                        }
                    }
                }
            }
        }
        return false;
    }) | Renderer([&](Element e) {
        return e | reflect(volumeUpButtonBox);
    });

    Box volumeDownButtonBox;
    auto volumeDownButton = Button(L"-", [&] {
        if (volumeSliderValue > 0) {
            volumeSliderValue--;
            sm.changeVolume(volumeSliderValue);
        }
    }, ButtonTextCentred()) | CatchEvent([&](Event event) {
        auto mouse = event.mouse();
        if (mouse.x >= volumeDownButtonBox.x_min && mouse.x <= volumeDownButtonBox.x_max &&
            mouse.y >= volumeDownButtonBox.y_min && mouse.y <= volumeDownButtonBox.y_max) {
            if (event.is_mouse()) {
                if (event.mouse().button == Mouse::Left) {
                    if (event.mouse().motion == Mouse::Pressed) {
                        if (!soundButtonHoldingDown.load()) {
                            holdingStart = std::chrono::steady_clock::now();
                            soundButtonHoldingDown.store(true);
                        }
                    } else if (event.mouse().motion == Mouse::Released) {
                        if (soundButtonHoldingDown.load()) {
                            soundButtonHoldingDown.store(false);
                            volumeChangedTimes = 0;
                            volumeChangeStep = 200;
                        }
                    }
                }
            }
        }
        return false;
    }) | Renderer([&](Element e) {
        return e | reflect(volumeDownButtonBox);
    });

    Box songProgressSliderBox;
    auto songProgressSlider = Slider("", &songProgressSliderValue, 0, 100, 1)
    | CatchEvent([&](Event event) {
        auto mouse = event.mouse();
        if (mouse.x >= songProgressSliderBox.x_min && mouse.x <= songProgressSliderBox.x_max &&
            mouse.y >= songProgressSliderBox.y_min && mouse.y <= songProgressSliderBox.y_max || userSongProgressDragging) {
            if (event.is_mouse()) {
                if (event.mouse().button == Mouse::Left) {
                    if (event.mouse().motion == Mouse::Pressed) {
                        userSongProgressDragging = true;
                    } else if (event.mouse().motion == Mouse::Released) {
                        if (userSongProgressDragging) {
                            sm.seekTo(songProgressSliderValue);
                            userSongProgressDragging = false;
                        }
                    }
                }
            }
        }
        return false;
    }) | Renderer([&](Element e) {
        return e | reflect(songProgressSliderBox);
    });

    Box seekForwardButtonBox;
    auto seekForwardButton = Button(L"▶▶", [&] {}, ButtonTextCentred()) 
    | CatchEvent([&](Event event) {
        auto mouse = event.mouse();
        if (mouse.x >= seekForwardButtonBox.x_min && mouse.x <= seekForwardButtonBox.x_max &&
            mouse.y >= seekForwardButtonBox.y_min && mouse.y <= seekForwardButtonBox.y_max) {
            if (event.is_mouse()) {
                if (event.mouse().button == Mouse::Left && event.mouse().motion == Mouse::Pressed) {
                    if (mouse.control) sm.seekToSeconds(30);
                    else sm.seekToSeconds(10);
                    return true;
                }
            }
        }
        return false;
    }) | Renderer([&](Element e) {
       return e | reflect(seekForwardButtonBox);
    });

    Box seekBackwardButtonBox;
    auto seekBackwardButton = Button(L"◀◀", [&] {}, ButtonTextCentred()) 
    | CatchEvent([&](Event event) {
        auto mouse = event.mouse();
        if (mouse.x >= seekBackwardButtonBox.x_min && mouse.x <= seekBackwardButtonBox.x_max &&
            mouse.y >= seekBackwardButtonBox.y_min && mouse.y <= seekBackwardButtonBox.y_max) {
            if (event.is_mouse()) {
                if (event.mouse().button == Mouse::Left && event.mouse().motion == Mouse::Pressed) {
                    if (mouse.control) sm.seekToSeconds(-30);
                    else sm.seekToSeconds(-10);
                    return true;
                }
            }
        }
        return false;
    }) | Renderer([&](Element e) {
       return e | reflect(seekBackwardButtonBox);
    });

    auto repeatOneButton = Button(L"↻", [&] { 
            if (!groupStates->isActive(PlaybackMode::RepeatOne)) groupStates->setMode(PlaybackMode::RepeatOne);
            else groupStates->setMode(PlaybackMode::Normal);
        }, 
        ButtonCentredTextMutualSwitch(groupStates, PlaybackMode::RepeatOne));

    auto repeatAllButton = Button(L"⟳", [&] { 
            if (!groupStates->isActive(PlaybackMode::Repeat)) groupStates->setMode(PlaybackMode::Repeat);
            else groupStates->setMode(PlaybackMode::Normal);
        },
        ButtonCentredTextMutualSwitch(groupStates, PlaybackMode::Repeat));

    auto shuffleButton = Button(L"⤨", [&] { 
            if (!groupStates->isActive(PlaybackMode::Shuffle)) groupStates->setMode(PlaybackMode::Shuffle);
            else groupStates->setMode(PlaybackMode::Normal); 
        },
        ButtonCentredTextMutualSwitch(groupStates, PlaybackMode::Shuffle));

    auto playerPaneControls = Container::Vertical({
        playButton,
        pauseButton,
        stopButton,
        songProgressSlider,
        volumeUpButton,
        volumeDownButton,
        volumeSlider,
        seekForwardButton,
        seekBackwardButton,
        repeatOneButton,
        repeatAllButton,
        shuffleButton
    });

    auto playerPane = Renderer(playerPaneControls, [&] {
        return vbox(
            filler(),
            vbox(
                hbox(repeatOneButton->Render(), repeatAllButton->Render(), shuffleButton->Render()),
                songProgressSlider->Render() | flex | border,
                text(
                    (currentlyPlaying.empty()) ? L"Nothing playing yet" : currentlyPlaying
                ) | center,
                text(sm.fromDoubleToTime(sm.getTimeElapsed()) + L"/" + sm.fromDoubleToTime(sm.getSongDuration())) | center,
                hbox( 
                    seekBackwardButton->Render() | flex,
                    seekForwardButton->Render() | flex
                ) | size(WIDTH, EQUAL, 30)| center 
            ),
            filler(),
            hbox(
                vbox(
                    filler() | size(HEIGHT, EQUAL, 1),
                    hbox(
                        playButton->Render() | flex,
                        pauseButton->Render() | flex | bold,
                        stopButton->Render() | flex | color(Color::Red)
                    ) | flex,
                    filler() | size(HEIGHT, EQUAL, 1)
                ) | flex | size(HEIGHT, EQUAL, 5),
                vbox(
                    volumeUpButton->Render() | flex,
                    text(std::to_wstring(volumeSliderValue)) | center,
                    volumeDownButton->Render() | flex
                ) | size(WIDTH, EQUAL, 5),
                volumeSlider->Render() | size(HEIGHT, EQUAL, 5) | border
            )
        ) | border | size(WIDTH, EQUAL, 2 * terminalSize.dimx / 3);
    });

    layout = Container::Vertical({ 
        name, 
        Container::Horizontal({ musicPane, playerPane }) | flex 
    });

    timerThread = std::thread([&] {
        std::chrono::steady_clock::time_point lastVolumeChange;

        while (!stopUpdateTimer.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(volumeChangeStep));
            if (soundButtonHoldingUp.load()) {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - holdingStart).count() >= 500 &&
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - lastVolumeChange).count() >= volumeChangeStep &&
                    volumeSliderValue < 100) {
                    volumeSliderValue++;
                    sm.changeVolume(volumeSliderValue);
                    lastVolumeChange = std::chrono::steady_clock::now();
                    if (++volumeChangedTimes == 5 && volumeChangeStep >= 50) {
                        volumeChangedTimes = 0;
                        volumeChangeStep /= 2;
                    }
                }
            } 
            else if (soundButtonHoldingDown.load()) {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - holdingStart).count() >= 500 &&
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - lastVolumeChange).count() >= volumeChangeStep &&
                    volumeSliderValue > 0) {
                    volumeSliderValue--;
                    sm.changeVolume(volumeSliderValue);
                    lastVolumeChange = std::chrono::steady_clock::now();
                    if (++volumeChangedTimes == 5 && volumeChangeStep >= 50) {
                        volumeChangedTimes = 0;
                        volumeChangeStep /= 2;
                    }
                }
            }
            if (!userSongProgressDragging) songProgressSliderValue = sm.getProgress() * 100;
            screen.Post(Event::Custom);
        }
    });

    screen.Loop(layout);
}

Player::~Player() {
    stopUpdateTimer.store(true);
    if (timerThread.joinable()) timerThread.join();
}
