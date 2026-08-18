#pragma once

#include "audio.hpp"
#include "model.hpp"

#include <SDL.h>
#include <string>

namespace fms {

class UiController {
public:
    UiController(SharedState& state, AudioEngine& audio);
    ~UiController();
    UiController(const UiController&) = delete;
    UiController& operator=(const UiController&) = delete;

    // Returns false when the application should close.
    bool handleEvent(const SDL_Event& event);
    void update(double deltaSeconds);
    void render(SDL_Renderer* renderer, int width, int height);
    bool consumeSaveRequest();
    void showToast(const std::string& message, bool error = false);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace fms
