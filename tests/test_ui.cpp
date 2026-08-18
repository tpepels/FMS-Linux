#include "audio.hpp"
#include "model.hpp"
#include "ui.hpp"

#include <SDL.h>

#include <cstdlib>
#include <iostream>
#include <mutex>

namespace {

void key(fms::UiController& ui, SDL_Scancode code, SDL_Keymod modifiers = KMOD_NONE) {
    SDL_Event event {};
    event.type = SDL_KEYDOWN;
    event.key.type = SDL_KEYDOWN;
    event.key.state = SDL_PRESSED;
    event.key.repeat = 0;
    event.key.keysym.scancode = code;
    event.key.keysym.mod = static_cast<Uint16>(modifiers);
    ui.handleEvent(event);
}

bool check(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

template <typename Function>
auto readState(const fms::SharedState& shared, Function function) {
    std::lock_guard<std::mutex> lock(shared.mutex);
    return function(shared.app);
}

} // namespace

int main() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        std::cerr << "FAIL: SDL init: " << SDL_GetError() << '\n';
        return EXIT_FAILURE;
    }
    SDL_Window* window = SDL_CreateWindow("FMS UI test", 0, 0, 960, 570, SDL_WINDOW_HIDDEN);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE) : nullptr;
    if (!window || !renderer) {
        std::cerr << "FAIL: hidden renderer: " << SDL_GetError() << '\n';
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    bool okay = true;
    {
        fms::SharedState shared;
        shared.app = fms::makeDefaultState();
        fms::AudioEngine audio;
        okay &= check(audio.open(shared), "dummy audio opens for UI integration");
        fms::UiController ui(shared, audio);

        const std::uint8_t originalLevel = readState(
            shared, [](const fms::AppState& app) { return app.tracks[0].steps[0].level; });
        key(ui, SDL_SCANCODE_RETURN);
        okay &= check(readState(shared, [](const fms::AppState& app) {
                           return !app.tracks[0].steps[0].active;
                       }),
                       "Enter removes a grid trigger");
        key(ui, SDL_SCANCODE_RETURN);
        okay &= check(readState(shared, [](const fms::AppState& app) {
                           return app.tracks[0].steps[0].active;
                       }),
                       "Enter places a grid trigger");
        key(ui, SDL_SCANCODE_RIGHTBRACKET);
        key(ui, SDL_SCANCODE_EQUALS);
        okay &= check(readState(shared, [originalLevel](const fms::AppState& app) {
                           return app.tracks[0].steps[0].level == originalLevel + 1;
                       }),
                      "parameter selection and fine edit mutate selected step");

        key(ui, SDL_SCANCODE_TAB); // Echo
        key(ui, SDL_SCANCODE_EQUALS);
        okay &= check(readState(shared, [](const fms::AppState& app) {
                           return app.tracks[0].echo.repeats == 1;
                       }),
                       "Echo editor mutates repeats");
        key(ui, SDL_SCANCODE_TAB); // Transpose
        key(ui, SDL_SCANCODE_EQUALS);
        okay &= check(readState(shared, [](const fms::AppState& app) {
                           return app.tracks[0].transpose.values[0] == 1;
                       }),
                      "Transpose editor mutates lane");
        key(ui, SDL_SCANCODE_TAB); // Mod
        key(ui, SDL_SCANCODE_EQUALS);
        okay &= check(readState(shared, [](const fms::AppState& app) {
                           return app.tracks[0].modulator.targetTrack == 1;
                       }),
                      "Mod editor mutates routing");
        key(ui, SDL_SCANCODE_TAB); // Scale
        key(ui, SDL_SCANCODE_EQUALS);
        okay &= check(readState(shared, [](const fms::AppState& app) {
                           return app.scaleRoot == 1;
                       }),
                       "Scale editor mutates root");
        key(ui, SDL_SCANCODE_TAB); // Data

        key(ui, SDL_SCANCODE_RIGHT);
        key(ui, SDL_SCANCODE_RETURN, KMOD_SHIFT);
        okay &= check(readState(shared, [](const fms::AppState& app) {
                           return app.patterns[0][1].occupied;
                       }),
                       "Data editor stores a pattern");
        key(ui, SDL_SCANCODE_K);
        key(ui, SDL_SCANCODE_RIGHT);
        key(ui, SDL_SCANCODE_RETURN, KMOD_SHIFT);
        okay &= check(readState(shared, [](const fms::AppState& app) {
                           return !app.patterns[0][2].occupied;
                       }),
                       "Locked bank rejects pattern writes");
        key(ui, SDL_SCANCODE_K);
        key(ui, SDL_SCANCODE_RETURN, KMOD_SHIFT);
        okay &= check(readState(shared, [](const fms::AppState& app) {
                           return app.patterns[0][2].occupied;
                       }),
                       "Unlocked bank accepts pattern writes");
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.tracks[0].steps[0].note = 99;
        }
        key(ui, SDL_SCANCODE_RETURN);
        okay &= check(readState(shared, [](const fms::AppState& app) {
                           return app.tracks[0].steps[0].note != 99;
                       }),
                       "Stopped pattern load replaces track");

        key(ui, SDL_SCANCODE_TAB); // Grid
        key(ui, SDL_SCANCODE_M, KMOD_SHIFT);
        okay &= check(readState(shared, [](const fms::AppState& app) {
                           return app.tracks[0].solo && !app.tracks[0].muted;
                       }),
                      "Shift+M solos and unmutes the track");
        key(ui, SDL_SCANCODE_M);
        okay &= check(readState(shared, [](const fms::AppState& app) {
                           return app.tracks[0].muted && !app.tracks[0].solo;
                       }),
                      "M mutes and clears contradictory solo state");
        key(ui, SDL_SCANCODE_M, KMOD_SHIFT);
        okay &= check(readState(shared, [](const fms::AppState& app) {
                           return app.tracks[0].solo && !app.tracks[0].muted;
                       }),
                      "solo overrides a prior mute in the editor");
        key(ui, SDL_SCANCODE_F2);
        key(ui, SDL_SCANCODE_F3);
        okay &= check(readState(shared, [](const fms::AppState& app) {
                           return app.lightTheme && app.accent == 1;
                       }),
                      "theme and accent shortcuts update persisted state");

        key(ui, SDL_SCANCODE_S);
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.bpm = 211;
        }
        key(ui, SDL_SCANCODE_S);
        okay &= check(readState(shared, [](const fms::AppState& app) {
                           return app.bpm == 120;
                       }),
                       "snapshot swaps the prior performance state");
        key(ui, SDL_SCANCODE_S, KMOD_CTRL);
        okay &= check(ui.consumeSaveRequest(), "Ctrl+S emits one save request");
        okay &= check(!ui.consumeSaveRequest(), "save request is consumable once");

        ui.update(1.0 / 60.0);
        SDL_ClearError();
        ui.render(renderer, 960, 570);
        SDL_RenderPresent(renderer);
        okay &= check(SDL_GetError()[0] == '\0', "minimum-size UI render completes cleanly");
        audio.close();
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (!okay) return EXIT_FAILURE;
    std::cout << "FMS UI event and render integration tests passed.\n";
    return EXIT_SUCCESS;
}
