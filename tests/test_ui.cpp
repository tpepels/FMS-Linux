#include "audio.hpp"
#include "model.hpp"
#include "ui.hpp"

#include <SDL.h>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace
{

    void key(fms::UiController &ui, SDL_Scancode code, SDL_Keymod modifiers = KMOD_NONE)
    {
        SDL_Event event{};
        event.type = SDL_KEYDOWN;
        event.key.type = SDL_KEYDOWN;
        event.key.state = SDL_PRESSED;
        event.key.repeat = 0;
        event.key.keysym.scancode = code;
        event.key.keysym.mod = static_cast<Uint16>(modifiers);
        ui.handleEvent(event);
    }

    void click(fms::UiController &ui, int x, int y, std::uint8_t button = SDL_BUTTON_LEFT)
    {
        SDL_Event event{};
        event.type = SDL_MOUSEBUTTONDOWN;
        event.button.type = SDL_MOUSEBUTTONDOWN;
        event.button.button = button;
        event.button.x = x;
        event.button.y = y;
        ui.handleEvent(event);
    }

    void moveMouse(fms::UiController &ui, int x, int y)
    {
        SDL_Event event{};
        event.type = SDL_MOUSEMOTION;
        event.motion.type = SDL_MOUSEMOTION;
        event.motion.x = x;
        event.motion.y = y;
        ui.handleEvent(event);
    }

    void leaveMouse(fms::UiController &ui)
    {
        SDL_Event event{};
        event.type = SDL_WINDOWEVENT;
        event.window.type = SDL_WINDOWEVENT;
        event.window.event = SDL_WINDOWEVENT_LEAVE;
        ui.handleEvent(event);
    }

    void textInput(fms::UiController &ui, const char *text)
    {
        SDL_Event event{};
        event.type = SDL_TEXTINPUT;
        event.text.type = SDL_TEXTINPUT;
        SDL_strlcpy(event.text.text, text, sizeof(event.text.text));
        ui.handleEvent(event);
    }

    void controllerButton(fms::UiController &ui, std::uint8_t button, bool pressed = true)
    {
        SDL_Event event{};
        event.type = pressed ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
        event.cbutton.type = event.type;
        event.cbutton.state = pressed ? SDL_PRESSED : SDL_RELEASED;
        event.cbutton.which = 77;
        event.cbutton.button = button;
        ui.handleEvent(event);
    }

    void dropFile(fms::UiController &ui, const char *path)
    {
        SDL_Event event{};
        event.type = SDL_DROPFILE;
        event.drop.type = SDL_DROPFILE;
        event.drop.file = SDL_strdup(path);
        ui.handleEvent(event);
    }

    bool check(bool condition, const char *message)
    {
        if (!condition)
            std::cerr << "FAIL: " << message << '\n';
        return condition;
    }

    template <typename Function>
    auto readState(const fms::SharedState &shared, Function function)
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        return function(shared.app);
    }

    template <typename Predicate>
    bool waitUntil(fms::UiController &ui, Predicate predicate, int timeoutMilliseconds = 1200)
    {
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(timeoutMilliseconds);
        while (std::chrono::steady_clock::now() < deadline)
        {
            ui.update(0.002);
            if (predicate())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        ui.update(0.002);
        return predicate();
    }

    bool renderClean(fms::UiController &ui, SDL_Renderer *renderer, int width, int height,
                     const char *captureName = nullptr)
    {
        if (SDL_Window *window = SDL_RenderGetWindow(renderer))
            SDL_SetWindowSize(window, width, height);
        SDL_ClearError();
        ui.render(renderer, width, height);
        SDL_RenderPresent(renderer);
        bool clean = SDL_GetError()[0] == '\0';
        const char *captureDirectory = std::getenv("FMS_CAPTURE_UI");
        if (clean && captureDirectory && captureName)
        {
            int outputWidth = 0;
            int outputHeight = 0;
            clean = SDL_GetRendererOutputSize(renderer, &outputWidth, &outputHeight) == 0;
            SDL_Surface *surface = clean
                                       ? SDL_CreateRGBSurfaceWithFormat(
                                             0, outputWidth, outputHeight, 32,
                                             SDL_PIXELFORMAT_ARGB8888)
                                       : nullptr;
            clean = clean && surface != nullptr;
            if (clean)
                clean = SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                             surface->pixels, surface->pitch) == 0;
            if (clean)
            {
                const std::string path = std::string(captureDirectory) + "/" +
                                         captureName + ".bmp";
                clean = SDL_SaveBMP(surface, path.c_str()) == 0;
            }
            if (surface)
                SDL_FreeSurface(surface);
        }
        return clean;
    }

} // namespace

int main()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
    {
        std::cerr << "FAIL: SDL init: " << SDL_GetError() << '\n';
        return EXIT_FAILURE;
    }
    SDL_Window *window = SDL_CreateWindow("FMS UI test", 0, 0, 1280, 760, SDL_WINDOW_HIDDEN);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE) : nullptr;
    if (!window || !renderer)
    {
        std::cerr << "FAIL: hidden renderer: " << SDL_GetError() << '\n';
        if (renderer)
            SDL_DestroyRenderer(renderer);
        if (window)
            SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    bool okay = true;
    fms::SharedState shared;
    shared.app = fms::makeDefaultState();
    fms::AudioEngine audio;
    okay &= check(audio.open(shared), "dummy audio opens for UI integration");
    {
        fms::UiController ui(shared, audio);

        // The beginner guide is present on first launch, remains non-modal,
        // and dismissal is a persisted preference that can be reopened.
        okay &= check(!readState(shared, [](const fms::AppState &app)
                                 { return app.onboardingDismissed; }),
                      "fresh projects offer the quick-start guide");
        okay &= check(renderClean(ui, renderer, 1280, 760, "onboarding-compact"),
                      "quick-start guide renders over compact workspaces");
        key(ui, SDL_SCANCODE_F7);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.onboardingDismissed; }),
                      "F7 dismisses and persists the quick-start guide");
        key(ui, SDL_SCANCODE_F7); // Reopen without clearing the preference.
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.onboardingDismissed; }),
                      "reopening quick start does not erase its persisted dismissal");
        key(ui, SDL_SCANCODE_F7);
        ui.update(3.0); // Clear the dismissal toast for a documentation-ready frame.
        okay &= check(renderClean(ui, renderer, 1280, 760, "grid-clean"),
                      "dark Grid documentation frame renders without overlays");

        // Grid range editing, bulk scopes, clipboard, rotation, and track-wide controls.
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            for (auto &track : shared.app.tracks)
                for (auto &step : track.steps)
                    step.level = 50;
            shared.app.tracks[0].steps[0].note = 40;
            shared.app.tracks[0].steps[1].note = 41;
            shared.app.tracks[0].steps[0].active = true;
            shared.app.tracks[0].steps[1].active = true;
        }
        key(ui, SDL_SCANCODE_RIGHTBRACKET);     // Level.
        key(ui, SDL_SCANCODE_DOWN, KMOD_SHIFT); // Rectangle: FM1 steps 0-1.
        key(ui, SDL_SCANCODE_EQUALS);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.tracks[0].steps[0].level == 51 &&
                                         app.tracks[0].steps[1].level == 51 &&
                                         app.tracks[0].steps[2].level == 50; }),
                      "range value edit affects exactly the selected steps");
        key(ui, SDL_SCANCODE_F5); // Track scope.
        key(ui, SDL_SCANCODE_EQUALS);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.tracks[0].steps[0].level == 52 &&
                                         app.tracks[0].steps[15].level == 51 &&
                                         app.tracks[1].steps[0].level == 50; }),
                      "track scope edits all steps on the current track");
        key(ui, SDL_SCANCODE_F5); // All scope.
        key(ui, SDL_SCANCODE_EQUALS);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.tracks[0].steps[15].level == 52 &&
                                         app.tracks[1].steps[0].level == 51 &&
                                         app.tracks[4].steps[15].level == 51; }),
                      "all scope edits all five tracks");
        const auto allRandomRevision = readState(
            shared, [](const fms::AppState &app)
            { return app.editRevision; });
        key(ui, SDL_SCANCODE_R);
        okay &= check(readState(shared, [allRandomRevision](const fms::AppState &app)
                                { return app.editRevision == allRandomRevision; }),
                      "ALL-scope parameter randomization requires confirmation");
        key(ui, SDL_SCANCODE_R);
        okay &= check(readState(shared, [allRandomRevision](const fms::AppState &app)
                                { return app.editRevision > allRandomRevision; }),
                      "confirmed ALL-scope parameter randomization reaches every-track scope");
        key(ui, SDL_SCANCODE_F5); // Selection scope.
        key(ui, SDL_SCANCODE_ESCAPE);
        key(ui, SDL_SCANCODE_UP);               // Step 0.
        key(ui, SDL_SCANCODE_DOWN, KMOD_SHIFT); // Range 0-1.
        key(ui, SDL_SCANCODE_C);
        key(ui, SDL_SCANCODE_DOWN); // Collapse at step 2.
        key(ui, SDL_SCANCODE_V);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.tracks[0].steps[2].note == 40 &&
                                         app.tracks[0].steps[3].note == 41; }),
                      "rectangular range clipboard pastes its full shape");
        key(ui, SDL_SCANCODE_X);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return !app.tracks[0].steps[2].active; }),
                      "cut clears the selected step after copying it");
        key(ui, SDL_SCANCODE_V);
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.tracks[0].steps[0].note = 12;
            shared.app.tracks[0].steps[15].note = 99;
        }
        key(ui, SDL_SCANCODE_O);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.tracks[0].steps[0].note == 99; }),
                      "rotate shifts the current track when no range is active");
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            for (auto &track : shared.app.tracks)
            {
                track.direction = fms::Direction::Forward;
                track.shuffle = 0;
            }
        }
        key(ui, SDL_SCANCODE_D, KMOD_SHIFT);
        key(ui, SDL_SCANCODE_UP, static_cast<SDL_Keymod>(KMOD_ALT | KMOD_CTRL));
        okay &= check(readState(shared, [](const fms::AppState &app)
                                {
                           for (const auto& track : app.tracks)
                               if (track.direction != fms::Direction::PingPong || track.shuffle != 1) return false;
                           return true; }),
                      "direction and shuffle can be applied to all tracks");
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            for (auto &track : shared.app.tracks)
            {
                track.muted = true;
                track.solo = true;
            }
            shared.app.tracks[0].steps[2].level = 64;
        }
        key(ui, SDL_SCANCODE_U);
        key(ui, SDL_SCANCODE_R);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                {
                           const bool live = std::all_of(app.tracks.begin(), app.tracks.end(),
                               [](const fms::TrackData& track) { return !track.muted && !track.solo; });
                           return live && app.tracks[0].steps[2].level != 64; }),
                      "unmute-all and plain R parameter nudge are independent workflows");
        moveMouse(ui, 220, 250);
        ui.update(0.55);
        okay &= check(renderClean(ui, renderer, 1280, 760, "grid-tooltip"),
                      "hover help renders after a short dwell without blocking the Grid");

        // Sound palette stores/copies only synth fields, including the advanced engine.
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto &step = shared.app.tracks[0].steps[2];
            step.note = 73;
            step.level = 23;
            step.fm.modDepth = 55;
            step.advancedFm.enabled = true;
            step.advancedFm.algorithm = fms::AdvancedFmAlgorithm::Algorithm8;
        }
        key(ui, SDL_SCANCODE_P);
        key(ui, SDL_SCANCODE_RETURN, KMOD_SHIFT);
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto &step = shared.app.tracks[0].steps[2];
            step.fm.modDepth = 1;
            step.advancedFm.enabled = false;
            step.note = 75;
            step.level = 25;
        }
        key(ui, SDL_SCANCODE_RETURN);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                {
                           const auto& step = app.tracks[0].steps[2];
                           return step.fm.modDepth == 55 && step.advancedFm.enabled &&
                                  step.advancedFm.algorithm == fms::AdvancedFmAlgorithm::Algorithm8 &&
                                  step.note == 75 && step.level == 25; }),
                      "palette recall copies sound fields without sequence fields");
        key(ui, SDL_SCANCODE_RETURN, KMOD_CTRL);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.tracks[0].steps[15].fm.modDepth == 55 &&
                                         app.tracks[0].steps[15].advancedFm.enabled; }),
                      "palette can apply a sound to the whole track");
        key(ui, SDL_SCANCODE_DELETE);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return !app.fmPalette[0].active; }),
                      "palette user slots can be cleared");
        key(ui, SDL_SCANCODE_P);
        key(ui, SDL_SCANCODE_5);
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.tracks[4].steps[2].noise.rate = 37;
        }
        key(ui, SDL_SCANCODE_P);
        key(ui, SDL_SCANCODE_RETURN, KMOD_SHIFT);
        key(ui, SDL_SCANCODE_P);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.noisePalette[0].active && app.noisePalette[0].noise.rate == 37; }),
                      "noise owns an independent 14-slot palette");

        // Synth opens on a tactile beginner surface, then exposes every field
        // in a grouped Deep editor without sacrificing keyboard access.
        static constexpr std::array<fms::AdvancedFmAlgorithmTopology, 12>
            expectedTopologies{{
                {{{{0, 1}, {1, 2}, {2, 3}, {0, 0}}}, 3, 0x08u},
                {{{{0, 2}, {1, 2}, {2, 3}, {0, 0}}}, 3, 0x08u},
                {{{{0, 1}, {2, 3}, {0, 0}, {0, 0}}}, 2, 0x0Au},
                {{{{0, 3}, {1, 3}, {2, 3}, {0, 0}}}, 3, 0x08u},
                {{{{0, 1}, {1, 3}, {2, 3}, {0, 0}}}, 3, 0x08u},
                {{{{0, 1}, {1, 2}, {1, 3}, {0, 0}}}, 3, 0x0Cu},
                {{{{0, 1}, {0, 2}, {0, 3}, {0, 0}}}, 3, 0x0Eu},
                {{{{0, 2}, {1, 2}, {0, 3}, {1, 3}}}, 4, 0x0Cu},
                {{{{0, 1}, {1, 2}, {0, 0}, {0, 0}}}, 2, 0x0Cu},
                {{{{0, 1}, {0, 0}, {0, 0}, {0, 0}}}, 1, 0x0Eu},
                {{{{0, 2}, {1, 2}, {2, 3}, {0, 0}}}, 3, 0x09u},
                {{{{0, 0}, {0, 0}, {0, 0}, {0, 0}}}, 0, 0x0Fu},
            }};
        bool topologyMatchesDsp = true;
        for (int algorithm = 0; algorithm < 12; ++algorithm)
        {
            topologyMatchesDsp = topologyMatchesDsp &&
                                 fms::advancedFmAlgorithmTopology(
                                     static_cast<fms::AdvancedFmAlgorithm>(algorithm)) ==
                                     expectedTopologies[static_cast<std::size_t>(algorithm)];
        }
        okay &= check(topologyMatchesDsp,
                      "all 12 Basic routing diagrams match DSP modulation edges and carriers");
        key(ui, SDL_SCANCODE_1);
        key(ui, SDL_SCANCODE_TAB);    // Synth.
        key(ui, SDL_SCANCODE_EQUALS); // Enable.
        key(ui, SDL_SCANCODE_RIGHT);
        key(ui, SDL_SCANCODE_EQUALS); // Algorithm 2.
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto &patch = shared.app.tracks[0].steps[2].advancedFm;
            patch.ampEnvelope.release = 0;
            patch.filterMode = fms::AdvancedFilterMode::Off;
            patch.filterCutoff = 40;
        }
        click(ui, 540, 350); // Basic RELEASE macro.
        okay &= check(readState(shared, [](const fms::AppState &app)
                                {
                           const auto& patch = app.tracks[0].steps[2].advancedFm;
                           return patch.ampEnvelope.release == 1 && patch.enabled; }),
                      "Basic Synth macros are tactile mouse controls and enable the 4-op engine");
        click(ui, 540, 350, SDL_BUTTON_RIGHT);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.tracks[0].steps[2].advancedFm.ampEnvelope.release == 0; }),
                      "Basic Synth right-click turns a macro down");
        leaveMouse(ui);
        ui.update(3.0);
        okay &= check(renderClean(ui, renderer, 1280, 760, "synth-basic"),
                      "Basic Synth renders its routing diagram and evolving-sound macros");
        key(ui, SDL_SCANCODE_B); // Deep, cursor reset to Engine.
        key(ui, SDL_SCANCODE_EQUALS);
        key(ui, SDL_SCANCODE_RIGHT);
        key(ui, SDL_SCANCODE_EQUALS);
        key(ui, SDL_SCANCODE_RIGHT);
        key(ui, SDL_SCANCODE_RIGHT); // OP1 ratio, index 3.
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.tracks[0].steps[2].advancedFm.operators[0].ratio = 64;
        }
        key(ui, SDL_SCANCODE_R);
        const std::uint8_t firstNudge = readState(shared, [](const fms::AppState &app)
                                                  { return app.tracks[0].steps[2].advancedFm.operators[0].ratio; });
        key(ui, SDL_SCANCODE_R);
        okay &= check(firstNudge != 64 && readState(shared, [firstNudge](const fms::AppState &app)
                                                    { return app.tracks[0].steps[2].advancedFm.operators[0].ratio != firstNudge; }),
                      "Synth plain R performs a fresh incremental nudge per press");
        for (int index = 3; index < 48; ++index)
            key(ui, SDL_SCANCODE_RIGHTBRACKET);
        for (int amount = 0; amount < 16; ++amount)
            key(ui, SDL_SCANCODE_MINUS, KMOD_SHIFT);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.tracks[0].steps[2].advancedFm.modulation[3].depth == -127; }),
                      "Synth modulation depth exposes the full -127 endpoint");
        key(ui, SDL_SCANCODE_RIGHTBRACKET);
        key(ui, SDL_SCANCODE_EQUALS);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.tracks[0].steps[2].advancedFm.modulation[3].destination ==
                                         fms::AdvancedModDestination::Pitch; }),
                      "the final advanced Synth field is keyboard-accessible");
        leaveMouse(ui);
        ui.update(3.0);
        okay &= check(renderClean(ui, renderer, 1280, 760, "synth-deep"),
                      "Deep Synth renders all grouped engine fields without clipping errors");

        // Existing focused editors still follow Synth in the view cycle.
        key(ui, SDL_SCANCODE_TAB); // Echo.
        key(ui, SDL_SCANCODE_EQUALS);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.tracks[0].echo.repeats == 1; }),
                      "Echo editor mutates repeats");
        key(ui, SDL_SCANCODE_TAB); // Transpose.
        key(ui, SDL_SCANCODE_EQUALS);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.tracks[0].transpose.values[0] == 1; }),
                      "Transpose editor mutates its first lane");
        key(ui, SDL_SCANCODE_TAB); // Mod.
        key(ui, SDL_SCANCODE_EQUALS);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.tracks[0].modulator.targetTrack == 1; }),
                      "Mod editor mutates routing");
        key(ui, SDL_SCANCODE_TAB); // Scale.
        key(ui, SDL_SCANCODE_EQUALS);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.scaleRoot == 1; }),
                      "Scale editor mutates root");
        key(ui, SDL_SCANCODE_TAB); // Data.

        // Data: single/column save, explicit immediate modes, masked cue + bank globals, X/?.
        leaveMouse(ui);
        ui.update(3.0);
        okay &= check(renderClean(ui, renderer, 1280, 760, "data-perform"),
                      "Data Perform presents the load/queue action ribbon and pattern slots");
        key(ui, SDL_SCANCODE_RETURN, KMOD_SHIFT);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.patterns[0][0].occupied; }),
                      "Data saves the current track slot");
        key(ui, SDL_SCANCODE_A);
        key(ui, SDL_SCANCODE_RIGHT);
        key(ui, SDL_SCANCODE_RETURN, KMOD_SHIFT);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                {
                           for (int track = 0; track < fms::kTrackCount; ++track)
                               if (!app.patterns[static_cast<std::size_t>(track)][1].occupied) return false;
                           return true; }),
                      "Data whole-column save writes all five tracks");
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.patterns[0][1].track.steps[0].note = 55;
            shared.app.tracks[0].steps[0].note = 81;
        }
        key(ui, SDL_SCANCODE_RETURN, KMOD_SHIFT);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.patterns[0][1].track.steps[0].note == 55; }),
                      "overwriting an occupied whole-column slot requires confirmation");
        key(ui, SDL_SCANCODE_RETURN, KMOD_SHIFT);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.patterns[0][1].track.steps[0].note == 81; }),
                      "confirmed whole-column save overwrites the occupied slot");
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            for (int track = 0; track < fms::kTrackCount; ++track)
            {
                shared.app.patterns[static_cast<std::size_t>(track)][1].track.steps[0].note =
                    static_cast<std::uint8_t>(60 + track);
                shared.app.tracks[static_cast<std::size_t>(track)].steps[0].note = 20;
            }
        }
        key(ui, SDL_SCANCODE_RETURN);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                {
                           for (const auto &track : app.tracks)
                               if (track.steps[0].note != 20) return false;
                           return true; }),
                      "whole-column immediate load requires confirmation before replacing live tracks");
        key(ui, SDL_SCANCODE_RETURN);
        okay &= check(waitUntil(ui, [&]
                                { return readState(shared, [](const fms::AppState &app)
                                                   {
                               for (int track = 0; track < fms::kTrackCount; ++track)
                                   if (app.tracks[static_cast<std::size_t>(track)].steps[0].note != 60 + track)
                                       return false;
                               return true; }); }),
                      "Data whole-column immediate load replaces all five tracks atomically");
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            for (int track = 0; track < fms::kTrackCount; ++track)
            {
                shared.app.patterns[static_cast<std::size_t>(track)][2] =
                    shared.app.patterns[static_cast<std::size_t>(track)][1];
                shared.app.patterns[static_cast<std::size_t>(track)][2].track.steps[0].note =
                    static_cast<std::uint8_t>(70 + track);
            }
        }
        key(ui, SDL_SCANCODE_RIGHT);
        key(ui, SDL_SCANCODE_Q);
        okay &= check(waitUntil(ui, [&]
                                { return readState(shared, [](const fms::AppState &app)
                                                   {
                               for (int track = 0; track < fms::kTrackCount; ++track)
                                   if (app.tracks[static_cast<std::size_t>(track)].steps[0].note != 70 + track)
                                       return false;
                               return true; }); }),
                      "Data whole-column cue applies all five tracks on one boundary");
        const auto allDataRandomRevision = readState(
            shared, [](const fms::AppState &app)
            { return app.editRevision; });
        key(ui, SDL_SCANCODE_R);
        okay &= check(readState(shared, [allDataRandomRevision](const fms::AppState &app)
                                { return app.editRevision == allDataRandomRevision; }),
                      "all-track Data randomization requires confirmation");
        key(ui, SDL_SCANCODE_R);
        okay &= check(waitUntil(ui, [&]
                                { return readState(shared, [allDataRandomRevision](const fms::AppState &app)
                                                   { return app.editRevision > allDataRandomRevision; }); }),
                      "confirmed all-track Data randomization replaces the five live tracks");
        const auto allDataClearRevision = readState(
            shared, [](const fms::AppState &app)
            { return app.editRevision; });
        key(ui, SDL_SCANCODE_X);
        okay &= check(readState(shared, [allDataClearRevision](const fms::AppState &app)
                                { return app.editRevision == allDataClearRevision; }),
                      "whole-column Data clear requires confirmation");
        key(ui, SDL_SCANCODE_X);
        okay &= check(waitUntil(ui, [&]
                                { return readState(shared, [](const fms::AppState &app)
                                                   {
                               for (const auto &track : app.tracks)
                                   for (const auto &step : track.steps)
                                       if (step.active || step.trigless) return false;
                               return true; }); }),
                      "confirmed whole-column Data clear erases all five live tracks");
        key(ui, SDL_SCANCODE_A);
        key(ui, SDL_SCANCODE_LEFT); // Pattern 01.
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.patterns[0][1].track.steps[0].note = 84;
            shared.app.tracks[0].steps[0].note = 31;
        }
        key(ui, SDL_SCANCODE_I);
        key(ui, SDL_SCANCODE_RETURN);
        okay &= check(waitUntil(ui, [&]
                                { return readState(shared, [](const fms::AppState &app)
                                                   { return app.tracks[0].steps[0].note == 84; }); }),
                      "in-place pattern load applies through the audio command path");
        audio.setRunning(true);
        key(ui, SDL_SCANCODE_RIGHT);
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.patterns[0][2] = shared.app.patterns[0][1];
            shared.app.patterns[0][2].track.steps[0].note = 92;
        }
        key(ui, SDL_SCANCODE_RETURN);
        okay &= check(waitUntil(ui, [&]
                                { return readState(shared, [](const fms::AppState &app)
                                                   { return app.tracks[0].steps[0].note == 92; }); }, 300),
                      "running transport does not force Enter into cue mode");
        audio.setRunning(false);
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.bpm = 187;
        }
        key(ui, SDL_SCANCODE_B, KMOD_SHIFT);
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.bpm = 121;
            shared.app.patterns[0][3] = shared.app.patterns[0][2];
            shared.app.patterns[0][3].track.steps[0].note = 77;
            shared.app.tracks[1].steps[0].note = 66;
        }
        key(ui, SDL_SCANCODE_RIGHT);
        key(ui, SDL_SCANCODE_B, KMOD_CTRL);
        const std::uint64_t oldColumnGeneration = audio.status().submittedColumnGeneration;
        key(ui, SDL_SCANCODE_Q);
        okay &= check(audio.status().submittedColumnGeneration > oldColumnGeneration,
                      "Q submits a distinct global-boundary cue");
        okay &= check(waitUntil(ui, [&]
                                { return readState(shared, [](const fms::AppState &app)
                                                   { return app.tracks[0].steps[0].note == 77 && app.bpm == 187; }); }),
                      "pattern and armed BPM apply together on the cue boundary");
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.tracks[1].steps[0].note == 66; }),
                      "selected-track cue mask leaves every other track untouched");

        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.tracks[0].steps[0].active = true;
        }
        key(ui, SDL_SCANCODE_V); // Manage exposes X/? and metadata controls.
        for (int move = 0; move < 5; ++move)
            key(ui, SDL_SCANCODE_LEFT);
        key(ui, SDL_SCANCODE_RETURN);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.tracks[0].steps[0].active; }),
                      "Data X arms a broad clear before changing the working track");
        key(ui, SDL_SCANCODE_RETURN);
        okay &= check(waitUntil(ui, [&]
                                { return readState(shared, [](const fms::AppState &app)
                                                   { return std::none_of(app.tracks[0].steps.begin(), app.tracks[0].steps.end(),
                                                                         [](const fms::Step &step)
                                                                         { return step.active || step.trigless; }); }); }),
                      "Data X is a selectable clear operation");
        key(ui, SDL_SCANCODE_RIGHT);
        key(ui, SDL_SCANCODE_RETURN);
        okay &= check(waitUntil(ui, [&]
                                { return readState(shared, [](const fms::AppState &app)
                                                   { return app.tracks[0].steps[0].note != 48; }); }),
                      "Data ? is a selectable random-pattern operation");

        key(ui, SDL_SCANCODE_RIGHT); // Pattern 00.
        key(ui, SDL_SCANCODE_E);
        for (int character = 0; character < 7; ++character)
            key(ui, SDL_SCANCODE_BACKSPACE);
        textInput(ui, "DRIFT");
        key(ui, SDL_SCANCODE_RETURN);
        key(ui, SDL_SCANCODE_C);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return std::strncmp(app.patternMetadata[0].name.data(), "DRIFT", 5) == 0 &&
                                         app.patternMetadata[0].color == 2; }),
                      "Data Manage persists a shared short name and color for a whole-column slot");
        key(ui, SDL_SCANCODE_K);
        key(ui, SDL_SCANCODE_E);
        textInput(ui, "BLOCKED");
        key(ui, SDL_SCANCODE_C);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return std::strncmp(app.patternMetadata[0].name.data(), "DRIFT", 5) == 0 &&
                                         app.patternMetadata[0].color == 2; }),
                      "bank lock protects pattern names and colors as well as pattern data");
        key(ui, SDL_SCANCODE_K);
        key(ui, SDL_SCANCODE_LEFT); // Return to ? for the existing bank flow.
        leaveMouse(ui);
        ui.update(3.0);
        okay &= check(renderClean(ui, renderer, 1280, 760, "data-manage"),
                      "Data Manage renders operation slots and named, color-coded metadata");

        key(ui, SDL_SCANCODE_N);
        textInput(ui, "TEST");
        key(ui, SDL_SCANCODE_RETURN);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return std::strncmp(app.banks[0].name.data(), "TEST", 4) == 0; }),
                      "bank name editor persists four typed characters");
        key(ui, SDL_SCANCODE_K);
        key(ui, SDL_SCANCODE_N);
        textInput(ui, "FAIL");
        key(ui, SDL_SCANCODE_RETURN);
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.bpm = 199;
        }
        key(ui, SDL_SCANCODE_B, KMOD_SHIFT);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return std::strncmp(app.banks[0].name.data(), "TEST", 4) == 0 &&
                                         app.banks[0].tempo == 187; }),
                      "bank lock gates both rename and BPM stores");
        key(ui, SDL_SCANCODE_K);
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.scaleRoot = 6;
            shared.app.scaleMask = 0x0555;
        }
        key(ui, SDL_SCANCODE_G, KMOD_SHIFT);
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.scaleRoot = 0;
            shared.app.scaleMask = 0x0FFF;
        }
        key(ui, SDL_SCANCODE_G);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.scaleRoot == 6 && app.scaleMask == 0x0555; }),
                      "bank scale store and direct recall are available");
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.scaleRoot = 0;
            shared.app.scaleMask = 0x0FFF;
        }
        key(ui, SDL_SCANCODE_G, KMOD_CTRL); // Arm scale for the next pattern cue.
        const std::uint64_t oldGlobalGeneration = audio.status().submittedGlobalSettingsGeneration;
        key(ui, SDL_SCANCODE_B, KMOD_ALT); // Standalone timed BPM recall.
        okay &= check(waitUntil(ui, [&]
                                {
                           const auto status = audio.status();
                           return status.submittedGlobalSettingsGeneration > oldGlobalGeneration &&
                                  status.appliedGlobalSettingsGeneration ==
                                      status.submittedGlobalSettingsGeneration; }),
                      "standalone timed bank BPM recall is acknowledged");
        key(ui, SDL_SCANCODE_RIGHT); // ? -> pattern 00.
        key(ui, SDL_SCANCODE_Q);
        okay &= check(waitUntil(ui, [&]
                                { return readState(shared, [](const fms::AppState &app)
                                                   { return app.scaleRoot == 6 && app.scaleMask == 0x0555; }); }),
                      "standalone BPM ack preserves an unrelated armed scale cue");
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.bpm = 300;
            shared.app.patterns[0][4] = shared.app.patterns[0][0];
            shared.app.patterns[0][4].track.steps[0].note = 88;
            shared.app.patterns[1][5] = shared.app.patterns[1][1];
            shared.app.patterns[1][5].track.steps[0].note = 89;
        }
        audio.setRunning(true);
        for (int move = 0; move < 4; ++move)
            key(ui, SDL_SCANCODE_RIGHT); // Pattern 04.
        key(ui, SDL_SCANCODE_B, KMOD_CTRL);
        key(ui, SDL_SCANCODE_Q);            // Track 1 + BPM.
        key(ui, SDL_SCANCODE_B, KMOD_CTRL); // Leave only the accepted cue armed.
        key(ui, SDL_SCANCODE_2);
        key(ui, SDL_SCANCODE_RIGHT); // Pattern 05.
        key(ui, SDL_SCANCODE_G, KMOD_CTRL);
        key(ui, SDL_SCANCODE_Q); // Disjoint track 2 + scale on the same boundary.
        const std::uint64_t mergedColumnGeneration = audio.status().submittedColumnGeneration;
        okay &= check(waitUntil(ui, [&]
                                {
                           const auto status = audio.status();
                           return status.appliedColumnGeneration == mergedColumnGeneration &&
                               readState(shared, [](const fms::AppState& app) {
                                   return app.tracks[0].steps[0].note == 88 &&
                                          app.tracks[1].steps[0].note == 89 &&
                                          app.bpm == 187 && app.scaleRoot == 6 &&
                                          app.scaleMask == 0x0555;
                               }); }, 1800),
                      "disjoint column-backed cues compose and acknowledge one merged generation");
        ui.update(1.0 / 60.0); // Consume the merged ack and clear every column-backed marker.
        audio.setRunning(false);
        key(ui, SDL_SCANCODE_1);
        okay &= check(renderClean(ui, renderer, 1280, 760), "Data renders at logical size without overlap errors");

        // Accepted audio commands are real history transactions. Undoing a
        // not-yet-applied cue cancels its audio epoch and cannot race through.
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.bpm = 30;
            shared.app.tracks[0].length = 16;
            shared.app.tracks[0].steps[0].note = 43;
            shared.app.patterns[0][6] = shared.app.patterns[0][5];
            shared.app.patterns[0][6].occupied = true;
            shared.app.patterns[0][6].track.length = 16;
            shared.app.patterns[0][6].track.steps[0].note = 101;
        }
        audio.reset();
        SDL_Delay(30);
        audio.setRunning(true);
        key(ui, SDL_SCANCODE_RIGHT); // Pattern 06.
        ui.markSaved();
        okay &= check(!ui.isDirty(), "markSaved establishes a clean history boundary");
        key(ui, SDL_SCANCODE_Q);
        okay &= check(ui.isDirty(), "an accepted future cue is immediately represented as unsaved work");
        ui.markSaved();
        okay &= check(ui.isDirty(),
                      "saving before a future cue applies leaves its pending intent dirty");
        key(ui, SDL_SCANCODE_Z, KMOD_CTRL);
        SDL_Delay(80);
        ui.update(0.08);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.tracks[0].steps[0].note == 43; }) &&
                          !ui.isDirty(),
                      "undo before apply cancels the cue and restores its scoped state deterministically");

        // Queue acceptance participates in the same chronological action
        // order as ordinary edits. The later edit is undone first; only the
        // following Undo cancels the still-pending cue. A cancelled cue has no
        // replay payload, so the first Redo is intentionally consumed with a
        // requeue-from-Data message and the next Redo reaches the ordinary edit.
        audio.reset();
        SDL_Delay(30);
        audio.setRunning(true);
        ui.markSaved();
        key(ui, SDL_SCANCODE_Q);
        key(ui, SDL_SCANCODE_PERIOD); // Later unrelated edit: 30 -> 31 BPM.
        key(ui, SDL_SCANCODE_Z, KMOD_CTRL);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.bpm == 30 && app.tracks[0].steps[0].note == 43; }) &&
                          ui.isDirty(),
                      "Undo first reverts a later BPM edit while leaving the earlier cue pending");
        key(ui, SDL_SCANCODE_Z, KMOD_CTRL);
        okay &= check(waitUntil(ui, [&]
                                { return !ui.isDirty(); }),
                      "a second Undo targets and settles only the pending cue");
        key(ui, SDL_SCANCODE_Z, static_cast<SDL_Keymod>(KMOD_CTRL | KMOD_SHIFT));
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.bpm == 30 && app.tracks[0].steps[0].note == 43; }),
                      "Redo does not invent a replay payload for a never-applied cue");
        key(ui, SDL_SCANCODE_Z, static_cast<SDL_Keymod>(KMOD_CTRL | KMOD_SHIFT));
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.bpm == 31 && app.tracks[0].steps[0].note == 43; }),
                      "the next Redo still reaches the later ordinary edit");

        // The same ordering guarantee applies to edits in the queued track.
        // A cue that never applied must not rewind a newer track edit when it
        // is cancelled.
        key(ui, SDL_SCANCODE_COMMA); // Restore 30 BPM through normal history.
        audio.reset();
        SDL_Delay(30);
        audio.setRunning(true);
        ui.markSaved();
        key(ui, SDL_SCANCODE_Q);
        const auto queuedTrackRate = readState(
            shared, [](const fms::AppState &app)
            { return app.tracks[0].rateIndex; });
        key(ui, SDL_SCANCODE_RIGHT, KMOD_ALT); // Later same-track rate edit.
        okay &= check(readState(shared, [queuedTrackRate](const fms::AppState &app)
                                { return app.tracks[0].rateIndex != queuedTrackRate; }),
                      "same-track edit is accepted after a future cue");
        key(ui, SDL_SCANCODE_Z, KMOD_CTRL);
        okay &= check(readState(shared, [queuedTrackRate](const fms::AppState &app)
                                { return app.tracks[0].rateIndex == queuedTrackRate &&
                                         app.tracks[0].steps[0].note == 43; }) &&
                          ui.isDirty(),
                      "Undo first restores the later same-track edit while the cue stays pending");
        key(ui, SDL_SCANCODE_Z, KMOD_CTRL);
        okay &= check(waitUntil(ui, [&]
                                { return !ui.isDirty(); }) &&
                          readState(shared, [queuedTrackRate](const fms::AppState &app)
                                    { return app.tracks[0].rateIndex == queuedTrackRate &&
                                             app.tracks[0].steps[0].note == 43; }),
                      "cancelling an unapplied cue leaves later same-track history intact");

        // Redo order remains chronological when an unavailable cue marker
        // sits beside an older ordinary edit in the same redo stack.
        key(ui, SDL_SCANCODE_PERIOD); // Ordinary action A: 30 -> 31 BPM.
        key(ui, SDL_SCANCODE_Q);      // Later pending action Q.
        const std::uint64_t redoOrderCue =
            audio.status().submittedPatternGenerations[0];
        key(ui, SDL_SCANCODE_Z, KMOD_CTRL);
        okay &= check(waitUntil(ui, [&]
                                { return audio.status().settledPatternGenerations[0] >=
                                         redoOrderCue; }),
                      "newest pending cue can be cancelled above an older ordinary edit");
        key(ui, SDL_SCANCODE_Z, KMOD_CTRL);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.bpm == 30; }),
                      "Undo reaches the older edit after cue cancellation");
        key(ui, SDL_SCANCODE_Z, static_cast<SDL_Keymod>(KMOD_CTRL | KMOD_SHIFT));
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.bpm == 31; }),
                      "Redo restores the older edit before reporting the later unavailable cue");
        key(ui, SDL_SCANCODE_Z, static_cast<SDL_Keymod>(KMOD_CTRL | KMOD_SHIFT));
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.bpm == 31 && app.tracks[0].steps[0].note == 43; }),
                      "later unavailable cue consumes Redo without changing project state");

        // A receipt that lands between UI frames is finalized before the next
        // mutation snapshot. That makes the incoming pattern older than the
        // subsequent same-track edit in global Undo order.
        audio.setRunning(false);
        ui.markSaved();
        key(ui, SDL_SCANCODE_RETURN);
        const auto appliedWithoutUiDeadline = std::chrono::steady_clock::now() +
                                              std::chrono::milliseconds(1200);
        bool appliedWithoutUiUpdate = false;
        while (std::chrono::steady_clock::now() < appliedWithoutUiDeadline)
        {
            appliedWithoutUiUpdate = readState(shared, [](const fms::AppState &app)
                                               { return app.tracks[0].steps[0].note == 101; });
            if (appliedWithoutUiUpdate)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        okay &= check(appliedWithoutUiUpdate,
                      "immediate Data load applies before a later UI update");
        const auto preEditRate = readState(shared, [](const fms::AppState &app)
                                           { return app.tracks[0].rateIndex; });
        key(ui, preEditRate < 8 ? SDL_SCANCODE_RIGHT : SDL_SCANCODE_LEFT, KMOD_ALT);
        const auto editedRate = readState(shared, [](const fms::AppState &app)
                                          { return app.tracks[0].rateIndex; });
        key(ui, SDL_SCANCODE_Z, KMOD_CTRL);
        okay &= check(readState(shared, [preEditRate](const fms::AppState &app)
                                { return app.tracks[0].steps[0].note == 101 &&
                                         app.tracks[0].rateIndex == preEditRate; }),
                      "first Undo removes only the edit made after an applied receipt");
        key(ui, SDL_SCANCODE_Z, KMOD_CTRL);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.tracks[0].steps[0].note == 43; }),
                      "second Undo restores the state before the asynchronous load");
        key(ui, SDL_SCANCODE_Z, static_cast<SDL_Keymod>(KMOD_CTRL | KMOD_SHIFT));
        key(ui, SDL_SCANCODE_Z, static_cast<SDL_Keymod>(KMOD_CTRL | KMOD_SHIFT));
        okay &= check(readState(shared, [editedRate](const fms::AppState &app)
                                { return app.tracks[0].steps[0].note == 101 &&
                                         app.tracks[0].rateIndex == editedRate; }),
                      "Redo replays the applied load before its later same-track edit");

        // A later synchronous edit remains above/beside the asynchronous
        // boundary rather than being accidentally rolled back with it.
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.bpm = 300;
            shared.app.tracks[0].length = 1;
            shared.app.tracks[0].steps[0].note = 44;
            shared.app.patterns[0][7] = shared.app.patterns[0][6];
            shared.app.patterns[0][7].track.length = 1;
            shared.app.patterns[0][7].track.steps[0].note = 102;
        }
        audio.reset();
        SDL_Delay(30);
        audio.setRunning(true);
        key(ui, SDL_SCANCODE_RIGHT); // Pattern 07.
        ui.markSaved();
        key(ui, SDL_SCANCODE_Q);
        key(ui, SDL_SCANCODE_COMMA); // Unrelated BPM edit: 300 -> 299.
        okay &= check(waitUntil(ui, [&]
                                { return readState(shared, [](const fms::AppState &app)
                                                   { return app.tracks[0].steps[0].note == 102; }); }, 1800),
                      "future track cue applies after an interleaved edit");
        key(ui, SDL_SCANCODE_Z, KMOD_CTRL);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.tracks[0].steps[0].note == 44 && app.bpm == 299; }),
                      "undoing an applied cue preserves the unrelated interleaved BPM edit");
        key(ui, SDL_SCANCODE_Z, static_cast<SDL_Keymod>(KMOD_CTRL | KMOD_SHIFT));
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.tracks[0].steps[0].note == 102 && app.bpm == 299; }),
                      "redo reapplies only the queued track effect after interleaving");
        audio.setRunning(false);

        // Persistent controller mapping capture and synthetic mapped input.
        key(ui, SDL_SCANCODE_F4);
        key(ui, SDL_SCANCODE_RETURN);
        controllerButton(ui, 22);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.controller.buttons[0] == 22; }),
                      "mapping capture persists the next BUTTONDOWN");
        key(ui, SDL_SCANCODE_DELETE);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.controller.buttons[0] == fms::kControllerButtonUnbound; }),
                      "mapping overlay supports unbind");
        key(ui, SDL_SCANCODE_D);
        key(ui, SDL_SCANCODE_DOWN);
        key(ui, SDL_SCANCODE_DOWN);
        key(ui, SDL_SCANCODE_DOWN);
        key(ui, SDL_SCANCODE_RETURN);
        controllerButton(ui, 21);
        key(ui, SDL_SCANCODE_E);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return !app.controller.enabled && app.controller.buttons[3] == 21; }),
                      "controller enable state and custom binding persist");
        key(ui, SDL_SCANCODE_E);
        okay &= check(renderClean(ui, renderer, 960, 570), "controller map renders at minimum window size");
        key(ui, SDL_SCANCODE_F4);
        key(ui, SDL_SCANCODE_TAB); // Data -> Grid.
        controllerButton(ui, 21);
        key(ui, SDL_SCANCODE_M);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.tracks[1].muted; }),
                      "custom controller navigation binding drives the Grid cursor");
        controllerButton(ui, 2);
        key(ui, SDL_SCANCODE_F4);
        key(ui, SDL_SCANCODE_E);
        key(ui, SDL_SCANCODE_E);
        key(ui, SDL_SCANCODE_F4);
        controllerButton(ui, 2, false);

        // Project menu keeps its destructive actions scoped and confirmation guarded.
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.bpm = 177;
            shared.app.patterns[0][1].occupied = true;
            shared.app.tracks[0].steps[0].active = true;
            shared.app.tracks[4].steps[7].active = true;
        }
        key(ui, SDL_SCANCODE_BACKSPACE, static_cast<SDL_Keymod>(KMOD_CTRL | KMOD_SHIFT));
        okay &= check(renderClean(ui, renderer, 1280, 760), "Project menu renders at logical size");
        key(ui, SDL_SCANCODE_RETURN);
        key(ui, SDL_SCANCODE_RETURN);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                {
                           for (const auto& track : app.tracks)
                               for (const auto& step : track.steps)
                                   if (step.active) return false;
                           return app.bpm == 177 && app.patterns[0][1].occupied; }),
                      "Project Clear only resets working tracks and keeps pattern memory");
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.tracks[0].steps[1].active = true;
        }
        click(ui, 529, 35);  // Header Project button.
        click(ui, 500, 230); // Arm New.
        click(ui, 500, 300); // Select and arm Clear instead of executing it.
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.tracks[0].steps[1].active; }),
                      "Project mouse selection re-arms a different destructive action");
        click(ui, 500, 300); // Confirm Clear.
        okay &= check(readState(shared, [](const fms::AppState &app)
                                {
                           for (const auto& track : app.tracks)
                               for (const auto& step : track.steps)
                                   if (step.active) return false;
                           return true; }),
                      "Project mouse confirmation clears only on the second matching click");
        constexpr const char *droppedPath = "/tmp/FMS Ambient Project 07.fms";
        const auto beforeDrop = readState(
            shared, [](const fms::AppState &app)
            { return app.editRevision; });
        dropFile(ui, droppedPath);
        fms::ProjectRequest dropRequest;
        okay &= check(ui.consumeProjectRequest(dropRequest) &&
                          dropRequest.kind == fms::ProjectRequestKind::Open &&
                          dropRequest.path == droppedPath,
                      "dropping an arbitrary .fms path emits an exact Open request");
        okay &= check(readState(shared, [beforeDrop](const fms::AppState &app)
                                { return app.editRevision == beforeDrop; }),
                      "drop-to-open leaves project state untouched for host validation");
        dropFile(ui, "");
        fms::ProjectRequest emptyDropRequest;
        okay &= check(!ui.consumeProjectRequest(emptyDropRequest) &&
                          readState(shared, [beforeDrop](const fms::AppState &app)
                                    { return app.editRevision == beforeDrop; }),
                      "empty drop is reported without a request or project mutation");
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.bpm = 166;
            ++shared.app.editRevision;
        }
        audio.setRunning(true);
        ui.projectLoaded(droppedPath);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.bpm == 166; }) &&
                          !audio.status().running && !ui.isDirty(),
                      "projectLoaded preserves the host-installed state and publishes a stopped reset boundary");
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.bpm = 211;
            shared.app.scaleRoot = 8;
            shared.app.scaleMask = 0x0AAAu;
            shared.app.lightTheme = false;
            shared.app.accent = 0;
            shared.app.controller.enabled = false;
        }
        key(ui, SDL_SCANCODE_N, KMOD_CTRL);
        key(ui, SDL_SCANCODE_RETURN);
        key(ui, SDL_SCANCODE_RETURN);
        fms::ProjectRequest newRequest;
        okay &= check(ui.consumeProjectRequest(newRequest) &&
                          newRequest.kind == fms::ProjectRequestKind::New &&
                          newRequest.path.empty(),
                      "confirmed Project New emits a deferred host request");
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.bpm == 211 && app.scaleRoot == 8 &&
                                         app.scaleMask == 0x0AAAu && !app.controller.enabled; }),
                      "New request leaves current work untouched until recovery succeeds");
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            const bool lightTheme = shared.app.lightTheme;
            const std::uint8_t accent = shared.app.accent;
            const auto controller = shared.app.controller;
            const bool onboardingDismissed = shared.app.onboardingDismissed;
            shared.app = fms::makeDefaultState();
            shared.app.lightTheme = lightTheme;
            shared.app.accent = accent;
            shared.app.controller = controller;
            shared.app.onboardingDismissed = onboardingDismissed;
        }
        ui.projectStarted("/tmp/fms-ui-untitled.fms");
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.bpm == 120 && app.scaleRoot == 0 && app.scaleMask == 0x0FFF &&
                                         app.tracks[0].steps[0].active && app.patterns[0][0].occupied &&
                                         !app.patterns[0][1].occupied && !app.controller.enabled &&
                                         app.onboardingDismissed; }) &&
                          ui.isDirty(),
                      "host-installed New starts from the starter session, preserves preferences, and is unsaved");
        key(ui, SDL_SCANCODE_Z, KMOD_CTRL);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.bpm == 120 && app.patterns[0][0].occupied; }),
                      "projectStarted is a hard undo boundary that cannot restore the old project");
        key(ui, SDL_SCANCODE_RIGHTBRACKET); // Level.
        const auto undoLevel = readState(shared, [](const fms::AppState &app)
                                         { return app.tracks[0].steps[0].level; });
        key(ui, SDL_SCANCODE_EQUALS);
        key(ui, SDL_SCANCODE_Z, KMOD_CTRL);
        okay &= check(readState(shared, [undoLevel](const fms::AppState &app)
                                { return app.tracks[0].steps[0].level == undoLevel; }),
                      "Ctrl+Z restores the prior grid edit");
        key(ui, SDL_SCANCODE_Z, static_cast<SDL_Keymod>(KMOD_CTRL | KMOD_SHIFT));
        okay &= check(readState(shared, [undoLevel](const fms::AppState &app)
                                { return app.tracks[0].steps[0].level == static_cast<std::uint8_t>(undoLevel + 1u); }),
                      "Ctrl+Shift+Z reapplies the reverted grid edit");
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.tracks[0].steps[0].active = true;
            shared.app.tracks[0].steps[1].active = true;
        }
        key(ui, SDL_SCANCODE_F5); // Track scope.
        key(ui, SDL_SCANCODE_DELETE);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.tracks[0].steps[0].active && app.tracks[0].steps[1].active; }),
                      "broad clear requires a second activation");
        key(ui, SDL_SCANCODE_DELETE);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                {
                                    for (const auto &step : app.tracks[0].steps)
                                        if (step.active)
                                            return false;
                                    return true; }),
                      "confirmed broad clear applies to the active track");
        key(ui, SDL_SCANCODE_F5); // All scope.
        key(ui, SDL_SCANCODE_F5); // Selection scope.
        key(ui, SDL_SCANCODE_F9);
        okay &= check(renderClean(ui, renderer, 1280, 760), "Grid Compare renders cleanly");
        key(ui, SDL_SCANCODE_F9);
        key(ui, SDL_SCANCODE_S, static_cast<SDL_Keymod>(KMOD_CTRL | KMOD_SHIFT));
        textInput(ui, "flow-state");
        key(ui, SDL_SCANCODE_RETURN);
        fms::ProjectRequest saveAsRequest;
        okay &= check(ui.consumeProjectRequest(saveAsRequest) &&
                          saveAsRequest.kind == fms::ProjectRequestKind::SaveAs &&
                          saveAsRequest.path.find("flow-state") != std::string::npos &&
                          saveAsRequest.path.size() >= 4u &&
                          saveAsRequest.path.substr(saveAsRequest.path.size() - 4u) == ".fms",
                      "Ctrl+Shift+S emits a sanitized Save As project request");
        key(ui, SDL_SCANCODE_N, KMOD_CTRL);
        key(ui, SDL_SCANCODE_DOWN);
        key(ui, SDL_SCANCODE_DOWN);
        key(ui, SDL_SCANCODE_RETURN);
        okay &= check(ui.consumeSaveRequest(), "Project Save emits the existing save request");

        // Existing global shortcuts and final Grid/overlay renders remain intact.
        key(ui, SDL_SCANCODE_F2);
        key(ui, SDL_SCANCODE_F3);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.lightTheme && app.accent == 1; }),
                      "theme and single-accent shortcuts update persisted state");
        key(ui, SDL_SCANCODE_S);
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.app.bpm = 211;
        }
        key(ui, SDL_SCANCODE_S);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.bpm != 211; }),
                      "snapshot swaps the prior performance state");
        key(ui, SDL_SCANCODE_S, KMOD_CTRL);
        okay &= check(ui.consumeSaveRequest() && !ui.consumeSaveRequest(),
                      "Ctrl+S emits one consumable save request");
        key(ui, SDL_SCANCODE_DOWN, KMOD_SHIFT);
        leaveMouse(ui);
        ui.update(3.0);
        okay &= check(renderClean(ui, renderer, 1280, 760, "grid-status-range"),
                      "Grid with active range and persistent status badges renders cleanly");
        key(ui, SDL_SCANCODE_F6);
        okay &= check(renderClean(ui, renderer, 1280, 760, "hints-compact"),
                      "context hint panel renders from F6");
        click(ui, 1166, 88);
        okay &= check(renderClean(ui, renderer, 1280, 760), "context hint panel toggles from the header");

        key(ui, SDL_SCANCODE_K, KMOD_CTRL);
        okay &= check(renderClean(ui, renderer, 1280, 760, "command-palette"),
                      "command palette renders its discoverable action list");
        key(ui, SDL_SCANCODE_DOWN); // OPEN GRID.
        key(ui, SDL_SCANCODE_RETURN);
        okay &= check(renderClean(ui, renderer, 1280, 760),
                      "command palette executes the selected navigation action");

        key(ui, SDL_SCANCODE_F6);
        okay &= check(renderClean(ui, renderer, 1680, 760, "hints-wide"),
                      "wide windows reserve a non-overlapping context inspector");
        key(ui, SDL_SCANCODE_F6);
        key(ui, SDL_SCANCODE_K, KMOD_CTRL);
        for (int command = 0; command < 11; ++command)
            key(ui, SDL_SCANCODE_DOWN);
        key(ui, SDL_SCANCODE_RETURN); // QUICK START GUIDE.
        okay &= check(renderClean(ui, renderer, 1680, 760, "onboarding-wide"),
                      "quick start reopens from commands and occupies the wide inspector");
        key(ui, SDL_SCANCODE_F7);
        okay &= check(readState(shared, [](const fms::AppState &app)
                                { return app.onboardingDismissed; }),
                      "closing a reopened guide keeps its persisted dismissal");
        okay &= check(renderClean(ui, renderer, 1280, 760),
                      "workspace returns cleanly from the wide inspector layout");
        key(ui, SDL_SCANCODE_P);
        okay &= check(renderClean(ui, renderer, 1280, 760, "sound-palette"),
                      "sound palette overlay renders cleanly");
        key(ui, SDL_SCANCODE_P);
    }
    audio.close();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (!okay)
        return EXIT_FAILURE;
    std::cout << "FMS phase-2 UI workflow and render tests passed.\n";
    return EXIT_SUCCESS;
}
