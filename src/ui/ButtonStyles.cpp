#include "ButtonStyles.h"

ButtonOption ButtonTextCentred() {
    auto option = ButtonOption::Simple();
    option.transform = [](const EntryState& state) {
        auto buttonText = text(state.label) | center | flex | borderLight;
        if (state.focused) {
            buttonText |= inverted;
        }
        return buttonText;
    };
    return option;
}

ButtonOption ButtonCentredTextMutualSwitch(std::shared_ptr<ButtonGroupState> groupStates, PlaybackMode buttonMode) {
    auto option = ButtonOption::Simple();
    option.transform = [groupStates, buttonMode](const EntryState& state) {
        auto buttonText = text(state.label) | center | flex | borderLight;

        if (groupStates->isActive(buttonMode)) {
            buttonText |= color(Color::Green) | bold;
        } 
        else {
            buttonText |= color(Color::White);
            if (state.focused) {
                buttonText |= inverted;
            }
        }        

        return buttonText;
    };
    return option;
}


