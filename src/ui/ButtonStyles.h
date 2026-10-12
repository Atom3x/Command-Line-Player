#pragma once
#include "../common/headers.hpp"

using namespace ftxui;

enum class PlaybackMode {
    Normal,
    Repeat,
    RepeatOne,
    Shuffle
};

struct ButtonGroupState {
    PlaybackMode currentMode = PlaybackMode::Normal;

    bool isActive(PlaybackMode mode) const {
        return mode == currentMode;
    }

    void setMode(PlaybackMode mode) {
        currentMode = mode;
    }
};

ButtonOption ButtonTextCentred();
ButtonOption ButtonCentredTextMutualSwitch(std::shared_ptr<ButtonGroupState> groupStates, PlaybackMode buttonMode);