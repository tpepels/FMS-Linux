#include "audio.hpp"
#include "model.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <mutex>

int main() {
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        std::cerr << "FAIL: SDL audio init: " << SDL_GetError() << '\n';
        return EXIT_FAILURE;
    }

    fms::SharedState shared;
    shared.app = fms::makeDefaultState();
    // Short, fast tracks make loop-boundary queue checks deterministic while
    // keeping the audio test quick.
    shared.app.tracks[0].length = 2;
    shared.app.tracks[0].rateIndex = 8;
    shared.app.tracks[1].length = 2;
    shared.app.tracks[1].rateIndex = 8;
    fms::AudioEngine audio;
    if (!audio.open(shared)) {
        std::cerr << "FAIL: audio device: " << audio.error() << '\n';
        SDL_Quit();
        return EXIT_FAILURE;
    }

    audio.setRunning(true);
    SDL_Delay(450);
    const fms::TransportStatus playing = audio.status();
    const float peak = std::max(std::fabs(playing.peakLeft), std::fabs(playing.peakRight));
    const bool advanced = std::any_of(
        playing.playheads.begin(), playing.playheads.end(), [](int value) { return value >= 1; });
    bool okay = playing.running && playing.renderedFrames > 1000 && peak > 0.000001f &&
                std::isfinite(peak) && advanced;
    if (!okay) {
        std::cerr << "FAIL: sequencer callback did not produce advancing, finite audio (frames="
                  << playing.renderedFrames << ", peak=" << peak << ")\n";
    }

    fms::TrackData replacement;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        replacement = shared.app.tracks[0];
    }
    replacement.length = 3;
    replacement.direction = fms::Direction::Reverse;
    replacement.steps[2].active = true;
    replacement.steps[2].note = 77;
    const std::uint64_t otherTrackLoops = playing.loops[1];
    const bool queued = audio.queuePattern(0, replacement);
    const fms::TransportStatus submitted = audio.status();
    const std::uint64_t replacementToken = submitted.submittedPatternGenerations[0];
    if (!queued || replacementToken == 0u || audio.queuePattern(-1, replacement) ||
        audio.queuePattern(fms::kTrackCount, replacement)) {
        std::cerr << "FAIL: pattern queue did not validate and publish a command token\n";
        okay = false;
    }

    fms::TransportStatus applied = submitted;
    for (int attempt = 0;
         attempt < 80 && applied.appliedPatternGenerations[0] != replacementToken;
         ++attempt) {
        SDL_Delay(10);
        applied = audio.status();
    }
    bool replacementVisible = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        const fms::TrackData& track = shared.app.tracks[0];
        replacementVisible = track.length == 3 && track.direction == fms::Direction::Reverse &&
                             track.steps[2].note == 77;
    }
    if (applied.appliedPatternGenerations[0] != replacementToken || !replacementVisible ||
        applied.appliedPatternGenerations[1] != 0u || applied.loops[1] < otherTrackLoops) {
        std::cerr << "FAIL: queued replacement was not isolated to one track's loop boundary\n";
        okay = false;
    }

    audio.setRunning(false);
    SDL_Delay(30);
    fms::TrackData cancelled = replacement;
    cancelled.steps[2].note = 91;
    const bool cancelQueued = audio.queuePattern(0, cancelled);
    const std::uint64_t cancelledToken = audio.status().submittedPatternGenerations[0];
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        // Publish state and reset under one lock, as the stopped UI load does.
        // The callback must initialize its cursor from this exact snapshot.
        shared.app.tracks[0].length = 4;
        shared.app.tracks[0].rateIndex = 4;
        shared.app.tracks[0].direction = fms::Direction::Forward;
        ++shared.app.editRevision;
        audio.reset();
    }
    SDL_Delay(40);
    const fms::TransportStatus reset = audio.status();
    const bool resetPlayheads = std::all_of(
        reset.playheads.begin(), reset.playheads.end(), [](int value) { return value == -1; });
    if (reset.running || !resetPlayheads) {
        std::cerr << "FAIL: stopped reset did not clear transport state\n";
        okay = false;
    }
    bool cancelledStayedOut = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        cancelledStayedOut = shared.app.tracks[0].steps[2].note == 77;
    }
    if (!cancelQueued || reset.appliedPatternGenerations[0] == cancelledToken ||
        !cancelledStayedOut) {
        std::cerr << "FAIL: reset did not cancel a pending pattern replacement\n";
        okay = false;
    }

    audio.setRunning(true);
    fms::TransportStatus restarted = audio.status();
    for (int attempt = 0; attempt < 20 && restarted.playheads[0] < 0; ++attempt) {
        SDL_Delay(5);
        restarted = audio.status();
    }
    if (restarted.playheads[0] != 0) {
        std::cerr << "FAIL: reset restarted from stale track direction or length\n";
        okay = false;
    }
    audio.setRunning(false);
    SDL_Delay(20);

    // A command submitted after reset belongs to the new epoch and must not be
    // discarded merely because the callback has not observed that reset yet.
    fms::TrackData postReset;
    bool postResetQueued = false;
    std::uint64_t postResetToken = 0u;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.app.tracks[0].length = 1;
        shared.app.tracks[0].rateIndex = 8;
        shared.app.tracks[0].direction = fms::Direction::Forward;
        ++shared.app.editRevision;
        postReset = shared.app.tracks[0];
        postReset.length = 2;
        postReset.rateIndex = 0;
        postReset.steps[0].active = true;
        postReset.steps[0].note = 82;
        postReset.steps[0].microTicks = -6;
        audio.reset();
        postResetQueued = audio.queuePattern(0, postReset);
        postResetToken = audio.status().submittedPatternGenerations[0];
        SDL_Delay(30);
    }
    audio.setRunning(true);
    fms::TransportStatus postResetApplied = audio.status();
    for (int attempt = 0;
         attempt < 80 && postResetApplied.appliedPatternGenerations[0] != postResetToken;
         ++attempt) {
        SDL_Delay(10);
        postResetApplied = audio.status();
    }
    bool postResetVisible = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        postResetVisible = shared.app.tracks[0].length == 2 &&
                           shared.app.tracks[0].steps[0].note == 82;
    }
    if (!postResetQueued || postResetToken == cancelledToken ||
        postResetApplied.appliedPatternGenerations[0] != postResetToken ||
        !postResetVisible) {
        std::cerr << "FAIL: pattern queued after reset was lost with the new reset epoch\n";
        okay = false;
    }

    audio.setRunning(false);
    audio.reset();
    SDL_Delay(40);
    const fms::TransportStatus previewReset = audio.status();

    fms::Step previewStep;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        previewStep = shared.app.tracks[1].steps[2];
    }
    const std::uint64_t framesBeforePreview = previewReset.renderedFrames;
    audio.preview(1, previewStep, 72);
    SDL_Delay(60);
    const fms::TransportStatus previewed = audio.status();
    const float previewPeak = std::max(std::fabs(previewed.peakLeft),
                                       std::fabs(previewed.peakRight));
    if (previewed.renderedFrames <= framesBeforePreview || !std::isfinite(previewed.peakLeft) ||
        !std::isfinite(previewed.peakRight) || previewPeak <= 0.000001f) {
        std::cerr << "FAIL: stopped preview path did not produce finite audio\n";
        okay = false;
    }

    audio.setRunning(false);
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        fms::TrackData& ordered = shared.app.tracks[0];
        ordered.length = 3;
        ordered.rateIndex = 4;
        ordered.direction = fms::Direction::Forward;
        ordered.shuffle = 0;
        ordered.steps[0].microTicks = 6;
        ordered.steps[1].microTicks = 0;
        ordered.steps[2].microTicks = -6;
        ++shared.app.editRevision;
        audio.reset();
    }
    SDL_Delay(40);
    audio.setRunning(true);
    SDL_Delay(160);
    const fms::TransportStatus tied = audio.status();
    if (tied.playheads[0] != 2) {
        std::cerr << "FAIL: equal-due microtimed steps did not retain sequencer order\n";
        okay = false;
    }

    audio.close();
    SDL_Quit();
    if (!okay) return EXIT_FAILURE;
    std::cout << "FMS real-time audio smoke test passed.\n";
    return EXIT_SUCCESS;
}
