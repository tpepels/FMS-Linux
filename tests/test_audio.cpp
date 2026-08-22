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

    const auto hasSettlement = [](const fms::TransportStatus& status,
                                  fms::TransportCommandFamily family,
                                  fms::TransportSettlementOutcome outcome,
                                  std::uint64_t token, int track = -1) {
        return std::any_of(
            status.settlements.begin(), status.settlements.end(),
            [=](const fms::TransportSettlement& settlement) {
                return settlement.sequence != 0u && settlement.family == family &&
                       settlement.outcome == outcome && settlement.token == token &&
                       (family != fms::TransportCommandFamily::Track ||
                        settlement.track == track);
            });
    };
    const auto hasComponentSettlement = [](const fms::TransportStatus& status,
                                           fms::TransportCommandFamily family,
                                           fms::TransportSettlementOutcome outcome,
                                           std::uint64_t token,
                                           std::uint8_t trackMask, bool tempo,
                                           bool scale) {
        return std::any_of(
            status.settlements.begin(), status.settlements.end(),
            [=](const fms::TransportSettlement& settlement) {
                return settlement.sequence != 0u && settlement.family == family &&
                       settlement.outcome == outcome && settlement.token == token &&
                       settlement.appliedTrackMask == trackMask &&
                       settlement.appliedTempo == tempo &&
                       settlement.appliedScale == scale;
            });
    };

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
        reset.settledPatternGenerations[0] < cancelledToken ||
        !hasSettlement(reset, fms::TransportCommandFamily::Track,
                       fms::TransportSettlementOutcome::Cancelled,
                       cancelledToken, 0) ||
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

    // Every advanced routing must produce bounded, non-zero audio from the
    // same deterministic patch. Reset between previews so the peak belongs to
    // the algorithm under test rather than a previous voice tail.
    audio.setRunning(false);
    fms::Step advancedStep;
    advancedStep.active = true;
    advancedStep.note = 60;
    advancedStep.level = 127;
    advancedStep.fm.ampHold = 18;
    advancedStep.advancedFm.enabled = true;
    advancedStep.advancedFm.ampEnvelope.attack = 0;
    advancedStep.advancedFm.ampEnvelope.decay = 3;
    advancedStep.advancedFm.ampEnvelope.sustain = 112;
    advancedStep.advancedFm.ampEnvelope.release = 12;
    for (auto& op : advancedStep.advancedFm.operators) {
        op.shape = fms::AdvancedOscShape::Sine;
        op.ratio = 8;
        op.level = 112;
        op.feedback = 8;
        op.detune = 0;
    }
    bool algorithmsAudible = true;
    for (int algorithm = 0; algorithm < 12; ++algorithm) {
        audio.reset();
        SDL_Delay(15);
        advancedStep.advancedFm.algorithm =
            static_cast<fms::AdvancedFmAlgorithm>(algorithm);
        audio.preview(0, advancedStep, 60);
        SDL_Delay(35);
        const fms::TransportStatus rendered = audio.status();
        const float algorithmPeak = std::max(std::fabs(rendered.peakLeft),
                                             std::fabs(rendered.peakRight));
        algorithmsAudible = algorithmsAudible && std::isfinite(algorithmPeak) &&
                            algorithmPeak > 0.000001f && algorithmPeak <= 1.0f;
    }
    if (!algorithmsAudible) {
        std::cerr << "FAIL: one or more advanced FM algorithms produced silent/non-finite audio\n";
        okay = false;
    }
    audio.reset();
    SDL_Delay(15);
    advancedStep.advancedFm.algorithm = fms::AdvancedFmAlgorithm::Algorithm12;
    advancedStep.advancedFm.operators[0].shape = fms::AdvancedOscShape::Triangle;
    audio.preview(0, advancedStep, 60);
    SDL_Delay(35);
    const fms::TransportStatus triangleRendered = audio.status();
    const float trianglePeak = std::max(std::fabs(triangleRendered.peakLeft),
                                        std::fabs(triangleRendered.peakRight));
    if (!std::isfinite(trianglePeak) || trianglePeak <= 0.000001f || trianglePeak > 1.0f) {
        std::cerr << "FAIL: triangle advanced oscillator produced invalid audio\n";
        okay = false;
    }

    // Exercise all expensive/extreme paths together: six oscillator shapes,
    // maximum feedback/ratio, four-voice unison, resonant filtering, folding,
    // and four independently routed modulation slots.
    audio.reset();
    SDL_Delay(20);
    advancedStep.advancedFm.algorithm = fms::AdvancedFmAlgorithm::Algorithm8;
    advancedStep.advancedFm.filterMode = fms::AdvancedFilterMode::Notch;
    advancedStep.advancedFm.filterCutoff = 127;
    advancedStep.advancedFm.resonance = 127;
    advancedStep.advancedFm.driveMode = fms::AdvancedDriveMode::Wavefold;
    advancedStep.advancedFm.driveAmount = 127;
    advancedStep.advancedFm.unisonVoices = 4;
    advancedStep.advancedFm.unisonDetune = 127;
    advancedStep.advancedFm.unisonWidth = 127;
    const std::array<fms::AdvancedOscShape, 4> extremeShapes {
        fms::AdvancedOscShape::Noise, fms::AdvancedOscShape::Pulse,
        fms::AdvancedOscShape::Saw, fms::AdvancedOscShape::Square,
    };
    for (std::size_t index = 0; index < advancedStep.advancedFm.operators.size(); ++index) {
        auto& op = advancedStep.advancedFm.operators[index];
        op.shape = extremeShapes[index];
        op.ratio = 127;
        op.level = 127;
        op.feedback = 127;
        op.detune = index % 2u == 0u ? -64 : 63;
    }
    const std::array<fms::AdvancedModSource, 4> sources {
        fms::AdvancedModSource::SineLfo, fms::AdvancedModSource::SampleAndHold,
        fms::AdvancedModSource::SquareLfo, fms::AdvancedModSource::AmpEnvelope,
    };
    const std::array<fms::AdvancedModDestination, 4> destinations {
        fms::AdvancedModDestination::Pitch,
        fms::AdvancedModDestination::FilterCutoff,
        fms::AdvancedModDestination::Operator1Ratio,
        fms::AdvancedModDestination::Drive,
    };
    for (std::size_t index = 0; index < advancedStep.advancedFm.modulation.size(); ++index) {
        auto& slot = advancedStep.advancedFm.modulation[index];
        slot.source = sources[index];
        slot.rate = 127;
        slot.depth = index % 2u == 0u ? 127 : -127;
        slot.destination = destinations[index];
    }
    for (int preview = 0; preview < 16; ++preview) {
        audio.preview(preview % fms::kFmTrackCount, advancedStep, 108 - preview);
    }
    SDL_Delay(120);
    const fms::TransportStatus stressed = audio.status();
    if (!std::isfinite(stressed.peakLeft) || !std::isfinite(stressed.peakRight) ||
        std::fabs(stressed.peakLeft) > 1.0f || std::fabs(stressed.peakRight) > 1.0f ||
        std::max(std::fabs(stressed.peakLeft), std::fabs(stressed.peakRight)) <= 0.000001f) {
        std::cerr << "FAIL: advanced FM extreme stress escaped the finite soft limit\n";
        okay = false;
    }

    // Immediate in-place loading preserves one track's published phase; reset
    // mode clears only that track and leaves the other clocks untouched.
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.app.bpm = 300;
        for (int track = 0; track < 2; ++track) {
            auto& data = shared.app.tracks[static_cast<std::size_t>(track)];
            data.length = 4;
            data.rateIndex = 8;
            data.direction = fms::Direction::Forward;
            for (auto& step : data.steps) step.active = true;
        }
        ++shared.app.editRevision;
        audio.reset();
    }
    SDL_Delay(35);
    audio.setRunning(true);
    SDL_Delay(90);
    audio.setRunning(false);
    SDL_Delay(20);
    const fms::TransportStatus beforeImmediate = audio.status();
    fms::TrackData inPlace;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        inPlace = shared.app.tracks[0];
    }
    inPlace.steps[0].note = 88;
    const bool inPlaceAccepted =
        audio.loadTrackImmediate(0, inPlace, fms::TrackLoadMode::InPlace);
    const std::uint64_t inPlaceToken = audio.status().submittedPatternGenerations[0];
    fms::TransportStatus inPlaceApplied = audio.status();
    for (int attempt = 0;
         attempt < 40 && inPlaceApplied.appliedPatternGenerations[0] != inPlaceToken;
         ++attempt) {
        SDL_Delay(5);
        inPlaceApplied = audio.status();
    }
    bool inPlaceVisible = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        inPlaceVisible = shared.app.tracks[0].steps[0].note == 88;
    }
    if (!inPlaceAccepted || !inPlaceVisible ||
        inPlaceApplied.appliedPatternGenerations[0] != inPlaceToken ||
        inPlaceApplied.playheads[0] != beforeImmediate.playheads[0] ||
        inPlaceApplied.loops[0] != beforeImmediate.loops[0] ||
        inPlaceApplied.playheads[1] != beforeImmediate.playheads[1] ||
        inPlaceApplied.loops[1] != beforeImmediate.loops[1]) {
        std::cerr << "FAIL: immediate in-place load did not preserve track phase/isolation\n";
        okay = false;
    }

    fms::TrackData resetTrack = inPlace;
    resetTrack.length = 3;
    resetTrack.direction = fms::Direction::Reverse;
    resetTrack.steps[2].note = 93;
    const bool localResetAccepted =
        audio.loadTrackImmediate(0, resetTrack, fms::TrackLoadMode::Reset);
    const std::uint64_t localResetToken = audio.status().submittedPatternGenerations[0];
    fms::TransportStatus localReset = audio.status();
    for (int attempt = 0;
         attempt < 40 && localReset.appliedPatternGenerations[0] != localResetToken;
         ++attempt) {
        SDL_Delay(5);
        localReset = audio.status();
    }
    if (!localResetAccepted || localReset.appliedPatternGenerations[0] != localResetToken ||
        localReset.playheads[0] != -1 || localReset.loops[0] != 0u ||
        localReset.playheads[1] != beforeImmediate.playheads[1] ||
        localReset.loops[1] != beforeImmediate.loops[1]) {
        std::cerr << "FAIL: immediate reset was not track-local\n";
        okay = false;
    }

    // A masked immediate column is one transaction: selected patterns and
    // optional BPM/scale settings commit together; unmasked tracks are intact.
    std::array<fms::TrackData, fms::kTrackCount> immediateColumn;
    int untouchedNote = 0;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        immediateColumn = shared.app.tracks;
        untouchedNote = shared.app.tracks[1].steps[0].note;
    }
    immediateColumn[0].steps[0].note = 101;
    immediateColumn[2].steps[0].note = 103;
    fms::TimedGlobalSettings immediateSettings;
    immediateSettings.applyTempo = true;
    immediateSettings.bpm = 177;
    immediateSettings.applyScale = true;
    immediateSettings.scaleRoot = 5;
    immediateSettings.scaleMask = 0x0AB5u;
    const bool immediateColumnAccepted = audio.loadPatternColumnImmediate(
        immediateColumn, fms::TrackLoadMode::Reset, 0x05u, immediateSettings);
    const std::uint64_t immediateColumnToken = audio.status().submittedColumnGeneration;
    fms::TransportStatus immediateColumnStatus = audio.status();
    for (int attempt = 0;
         attempt < 50 && immediateColumnStatus.appliedColumnGeneration != immediateColumnToken;
         ++attempt) {
        SDL_Delay(5);
        immediateColumnStatus = audio.status();
    }
    bool immediateColumnVisible = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        immediateColumnVisible = shared.app.tracks[0].steps[0].note == 101 &&
            shared.app.tracks[2].steps[0].note == 103 &&
            shared.app.tracks[1].steps[0].note == untouchedNote &&
            shared.app.bpm == 177 && shared.app.scaleRoot == 5 &&
            shared.app.scaleMask == 0x0AB5u;
    }
    if (!immediateColumnAccepted || !immediateColumnVisible ||
        immediateColumnStatus.appliedColumnGeneration != immediateColumnToken ||
        immediateColumnStatus.playheads[0] != -1 ||
        immediateColumnStatus.playheads[2] != -1) {
        std::cerr << "FAIL: masked immediate column/global transaction was not atomic\n";
        okay = false;
    }

    fms::TimedGlobalSettings stoppedGlobal;
    stoppedGlobal.applyTempo = true;
    stoppedGlobal.bpm = 190;
    const bool stoppedGlobalAccepted = audio.queueGlobalSettings(stoppedGlobal);
    const std::uint64_t stoppedGlobalToken =
        audio.status().submittedGlobalSettingsGeneration;
    fms::TransportStatus stoppedGlobalStatus = audio.status();
    for (int attempt = 0;
         attempt < 40 &&
         stoppedGlobalStatus.appliedGlobalSettingsGeneration != stoppedGlobalToken;
         ++attempt) {
        SDL_Delay(5);
        stoppedGlobalStatus = audio.status();
    }
    bool stoppedGlobalVisible = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        stoppedGlobalVisible = shared.app.bpm == 190;
    }
    if (!stoppedGlobalAccepted || !stoppedGlobalVisible ||
        stoppedGlobalStatus.appliedGlobalSettingsGeneration != stoppedGlobalToken) {
        std::cerr << "FAIL: stopped queued global settings did not apply/acknowledge\n";
        okay = false;
    }

    // Newer column cues supersede older armed per-track cues for their mask.
    // The replacement uses a one-step fast loop after applying, so a stale cue
    // would overwrite it many times during the final wait.
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.app.bpm = 300;
        auto& slow = shared.app.tracks[0];
        slow.length = 16;
        slow.rateIndex = 0;
        slow.direction = fms::Direction::Forward;
        slow.steps[0].active = true;
        slow.steps[0].note = 55;
        ++shared.app.editRevision;
        audio.reset();
    }
    SDL_Delay(35);
    audio.setRunning(true);
    SDL_Delay(700);
    fms::TrackData staleCue;
    std::array<fms::TrackData, fms::kTrackCount> boundaryColumn;
    int boundaryUntouchedNote = 0;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        staleCue = shared.app.tracks[0];
        boundaryColumn = shared.app.tracks;
        boundaryUntouchedNote = shared.app.tracks[1].steps[0].note;
    }
    staleCue.length = 1;
    staleCue.rateIndex = 8;
    staleCue.steps[0].note = 64;
    boundaryColumn[0] = staleCue;
    boundaryColumn[0].steps[0].note = 99;
    fms::TimedGlobalSettings boundarySettings;
    boundarySettings.applyTempo = true;
    boundarySettings.bpm = 240;
    boundarySettings.applyScale = true;
    boundarySettings.scaleRoot = 2;
    boundarySettings.scaleMask = 0x06ADu;
    const bool staleAccepted = audio.queuePattern(0, staleCue);
    const std::uint64_t staleToken = audio.status().submittedPatternGenerations[0];
    const bool boundaryAccepted =
        audio.queuePatternColumn(boundaryColumn, 0x01u, boundarySettings);
    const std::uint64_t boundaryToken = audio.status().submittedColumnGeneration;
    fms::TransportStatus boundaryStatus = audio.status();
    for (int attempt = 0;
         attempt < 100 && boundaryStatus.appliedColumnGeneration != boundaryToken;
         ++attempt) {
        SDL_Delay(15);
        boundaryStatus = audio.status();
    }
    SDL_Delay(350);
    boundaryStatus = audio.status();
    bool boundaryVisible = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        boundaryVisible = shared.app.tracks[0].steps[0].note == 99 &&
            shared.app.tracks[1].steps[0].note == boundaryUntouchedNote &&
            shared.app.bpm == 240 && shared.app.scaleRoot == 2 &&
            shared.app.scaleMask == 0x06ADu;
    }
    if (!staleAccepted || !boundaryAccepted || !boundaryVisible ||
        boundaryStatus.appliedColumnGeneration != boundaryToken ||
        boundaryStatus.appliedPatternGenerations[0] == staleToken ||
        boundaryStatus.settledPatternGenerations[0] < staleToken ||
        !hasSettlement(boundaryStatus, fms::TransportCommandFamily::Track,
                       fms::TransportSettlementOutcome::Cancelled,
                       staleToken, 0)) {
        std::cerr << "FAIL: atomic boundary column/settings cue or supersession failed\n";
        okay = false;
    }

    // Reverse ordering is also last-command-wins: an older masked column must
    // drop a track superseded by a newer immediate command before its boundary.
    std::array<fms::TrackData, fms::kTrackCount> olderColumn;
    fms::TrackData newerTrack;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        olderColumn = shared.app.tracks;
        newerTrack = shared.app.tracks[0];
    }
    olderColumn[0].steps[0].note = 70;
    newerTrack.steps[0].note = 110;
    const bool olderColumnAccepted = audio.queuePatternColumn(olderColumn, 0x01u);
    const std::uint64_t olderColumnToken = audio.status().submittedColumnGeneration;
    const bool newerTrackAccepted =
        audio.loadTrackImmediate(0, newerTrack, fms::TrackLoadMode::InPlace);
    const std::uint64_t newerTrackToken = audio.status().submittedPatternGenerations[0];
    fms::TransportStatus reverseOrderStatus = audio.status();
    for (int attempt = 0;
         attempt < 120 &&
         (reverseOrderStatus.settledColumnGeneration < olderColumnToken ||
          reverseOrderStatus.appliedPatternGenerations[0] != newerTrackToken);
         ++attempt) {
        SDL_Delay(10);
        reverseOrderStatus = audio.status();
    }
    SDL_Delay(250);
    bool newerTrackStayedVisible = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        newerTrackStayedVisible = shared.app.tracks[0].steps[0].note == 110;
    }
    if (!olderColumnAccepted || !newerTrackAccepted || !newerTrackStayedVisible ||
        reverseOrderStatus.appliedColumnGeneration == olderColumnToken ||
        reverseOrderStatus.settledColumnGeneration < olderColumnToken ||
        reverseOrderStatus.appliedPatternGenerations[0] != newerTrackToken ||
        !hasSettlement(reverseOrderStatus, fms::TransportCommandFamily::Column,
                       fms::TransportSettlementOutcome::Cancelled,
                       olderColumnToken)) {
        std::cerr << "FAIL: newer per-track command was overwritten by an older column\n";
        okay = false;
    }

    // Commands sharing one boundary compose per element. Disjoint masks and
    // settings survive while a later command replaces only the fields it owns.
    audio.setRunning(false);
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.app.bpm = 300;
        audio.reset();
    }
    SDL_Delay(35);
    audio.setRunning(true);
    std::array<fms::TrackData, fms::kTrackCount> composedA;
    std::array<fms::TrackData, fms::kTrackCount> composedB;
    std::array<fms::TrackData, fms::kTrackCount> composedC;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        composedA = shared.app.tracks;
        composedB = shared.app.tracks;
        composedC = shared.app.tracks;
    }
    composedA[0].steps[0].note = 31;
    composedA[2].steps[0].note = 32;
    composedB[1].steps[0].note = 42;
    composedC[0].steps[0].note = 53;
    fms::TimedGlobalSettings composedTempoA;
    composedTempoA.applyTempo = true;
    composedTempoA.bpm = 141;
    fms::TimedGlobalSettings composedScaleB;
    composedScaleB.applyScale = true;
    composedScaleB.scaleRoot = 8;
    composedScaleB.scaleMask = 0x05ADu;
    fms::TimedGlobalSettings composedTempoC;
    composedTempoC.applyTempo = true;
    composedTempoC.bpm = 241;
    bool composedAcceptedA = false;
    bool composedAcceptedB = false;
    bool composedAcceptedC = false;
    std::uint64_t composedTokenA = 0u;
    std::uint64_t composedTokenB = 0u;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        composedAcceptedA = audio.queuePatternColumn(composedA, 0x05u, composedTempoA);
        composedTokenA = audio.status().submittedColumnGeneration;
        composedAcceptedB = audio.queuePatternColumn(composedB, 0x02u, composedScaleB);
        composedTokenB = audio.status().submittedColumnGeneration;
        composedAcceptedC = audio.queuePatternColumn(composedC, 0x01u, composedTempoC);
    }
    const std::uint64_t composedToken = audio.status().submittedColumnGeneration;
    fms::TransportStatus composedStatus = audio.status();
    for (int attempt = 0;
         attempt < 220 && composedStatus.appliedColumnGeneration != composedToken;
         ++attempt) {
        SDL_Delay(5);
        composedStatus = audio.status();
    }
    bool composedVisible = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        composedVisible = shared.app.tracks[0].steps[0].note == 53 &&
            shared.app.tracks[1].steps[0].note == 42 &&
            shared.app.tracks[2].steps[0].note == 32 && shared.app.bpm == 241 &&
            shared.app.scaleRoot == 8 && shared.app.scaleMask == 0x05ADu;
    }
    if (!composedAcceptedA || !composedAcceptedB || !composedAcceptedC ||
        !composedVisible || composedStatus.appliedColumnGeneration != composedToken ||
        !hasComponentSettlement(composedStatus,
                                fms::TransportCommandFamily::Column,
                                fms::TransportSettlementOutcome::Applied,
                                composedTokenA, 0x04u, false, false) ||
        !hasComponentSettlement(composedStatus,
                                fms::TransportCommandFamily::Column,
                                fms::TransportSettlementOutcome::Applied,
                                composedTokenB, 0x02u, false, true) ||
        !hasComponentSettlement(composedStatus,
                                fms::TransportCommandFamily::Column,
                                fms::TransportSettlementOutcome::Applied,
                                composedToken, 0x01u, true, false)) {
        std::cerr << "FAIL: same-boundary column composition/LWW semantics failed\n";
        okay = false;
    }

    // The global-settings API has the same field-wise composition semantics.
    audio.setRunning(false);
    SDL_Delay(20);
    fms::TimedGlobalSettings globalTempoA;
    globalTempoA.applyTempo = true;
    globalTempoA.bpm = 151;
    fms::TimedGlobalSettings globalScale;
    globalScale.applyScale = true;
    globalScale.scaleRoot = 6;
    globalScale.scaleMask = 0x09B5u;
    fms::TimedGlobalSettings globalTempoB;
    globalTempoB.applyTempo = true;
    globalTempoB.bpm = 251;
    bool globalAcceptedA = false;
    bool globalAcceptedScale = false;
    bool globalAcceptedB = false;
    std::uint64_t globalTokenA = 0u;
    std::uint64_t globalTokenScale = 0u;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        globalAcceptedA = audio.queueGlobalSettings(globalTempoA);
        globalTokenA = audio.status().submittedGlobalSettingsGeneration;
        globalAcceptedScale = audio.queueGlobalSettings(globalScale);
        globalTokenScale = audio.status().submittedGlobalSettingsGeneration;
        globalAcceptedB = audio.queueGlobalSettings(globalTempoB);
    }
    const std::uint64_t composedGlobalToken =
        audio.status().submittedGlobalSettingsGeneration;
    fms::TransportStatus composedGlobalStatus = audio.status();
    for (int attempt = 0;
         attempt < 60 &&
         composedGlobalStatus.appliedGlobalSettingsGeneration != composedGlobalToken;
         ++attempt) {
        SDL_Delay(5);
        composedGlobalStatus = audio.status();
    }
    bool composedGlobalVisible = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        composedGlobalVisible = shared.app.bpm == 251 && shared.app.scaleRoot == 6 &&
            shared.app.scaleMask == 0x09B5u;
    }
    if (!globalAcceptedA || !globalAcceptedScale || !globalAcceptedB ||
        !composedGlobalVisible ||
        composedGlobalStatus.appliedGlobalSettingsGeneration != composedGlobalToken ||
        !hasSettlement(composedGlobalStatus,
                       fms::TransportCommandFamily::GlobalSettings,
                       fms::TransportSettlementOutcome::Cancelled,
                       globalTokenA) ||
        !hasComponentSettlement(composedGlobalStatus,
                                fms::TransportCommandFamily::GlobalSettings,
                                fms::TransportSettlementOutcome::Applied,
                                globalTokenScale, 0u, false, true) ||
        !hasComponentSettlement(composedGlobalStatus,
                                fms::TransportCommandFamily::GlobalSettings,
                                fms::TransportSettlementOutcome::Applied,
                                composedGlobalToken, 0u, true, false)) {
        std::cerr << "FAIL: same-boundary global field composition/LWW semantics failed\n";
        okay = false;
    }

    // Cross-API submission order, rather than queue-drain order, owns an
    // overlapping setting. In this direction the newer column wins tempo and
    // the older global command still contributes its disjoint scale field.
    std::array<fms::TrackData, fms::kTrackCount> crossColumn;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        crossColumn = shared.app.tracks;
    }
    crossColumn[2].steps[0].note = 64;
    fms::TimedGlobalSettings olderGlobal;
    olderGlobal.applyTempo = true;
    olderGlobal.bpm = 161;
    olderGlobal.applyScale = true;
    olderGlobal.scaleRoot = 4;
    olderGlobal.scaleMask = 0x06D5u;
    fms::TimedGlobalSettings newerColumnSettings;
    newerColumnSettings.applyTempo = true;
    newerColumnSettings.bpm = 261;
    bool olderGlobalAccepted = false;
    bool newerColumnAccepted = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        olderGlobalAccepted = audio.queueGlobalSettings(olderGlobal);
        newerColumnAccepted = audio.loadPatternColumnImmediate(
            crossColumn, fms::TrackLoadMode::InPlace, 0x04u, newerColumnSettings);
    }
    const std::uint64_t crossColumnToken = audio.status().submittedColumnGeneration;
    const std::uint64_t crossGlobalToken =
        audio.status().submittedGlobalSettingsGeneration;
    fms::TransportStatus crossStatus = audio.status();
    for (int attempt = 0;
         attempt < 60 &&
         (crossStatus.appliedColumnGeneration != crossColumnToken ||
          crossStatus.appliedGlobalSettingsGeneration != crossGlobalToken);
         ++attempt) {
        SDL_Delay(5);
        crossStatus = audio.status();
    }
    bool crossVisible = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        crossVisible = shared.app.tracks[2].steps[0].note == 64 &&
            shared.app.bpm == 261 && shared.app.scaleRoot == 4 &&
            shared.app.scaleMask == 0x06D5u;
    }
    if (!olderGlobalAccepted || !newerColumnAccepted || !crossVisible ||
        crossStatus.appliedColumnGeneration != crossColumnToken ||
        crossStatus.appliedGlobalSettingsGeneration != crossGlobalToken) {
        std::cerr << "FAIL: newer column did not supersede older global tempo by order\n";
        okay = false;
    }

    // Reverse the API order: a newer global tempo replaces the column tempo,
    // while the column's pattern and disjoint scale still commit atomically.
    crossColumn[3].steps[0].note = 75;
    fms::TimedGlobalSettings olderColumnSettings;
    olderColumnSettings.applyTempo = true;
    olderColumnSettings.bpm = 171;
    olderColumnSettings.applyScale = true;
    olderColumnSettings.scaleRoot = 2;
    olderColumnSettings.scaleMask = 0x0AD5u;
    fms::TimedGlobalSettings newerGlobal;
    newerGlobal.applyTempo = true;
    newerGlobal.bpm = 271;
    bool olderColumnSettingsAccepted = false;
    bool newerGlobalAccepted = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        olderColumnSettingsAccepted = audio.loadPatternColumnImmediate(
            crossColumn, fms::TrackLoadMode::InPlace, 0x08u, olderColumnSettings);
        newerGlobalAccepted = audio.queueGlobalSettings(newerGlobal);
    }
    const std::uint64_t reverseCrossColumnToken =
        audio.status().submittedColumnGeneration;
    const std::uint64_t reverseCrossGlobalToken =
        audio.status().submittedGlobalSettingsGeneration;
    fms::TransportStatus reverseCrossStatus = audio.status();
    for (int attempt = 0;
         attempt < 60 &&
         (reverseCrossStatus.appliedColumnGeneration != reverseCrossColumnToken ||
          reverseCrossStatus.appliedGlobalSettingsGeneration != reverseCrossGlobalToken);
         ++attempt) {
        SDL_Delay(5);
        reverseCrossStatus = audio.status();
    }
    bool reverseCrossVisible = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        reverseCrossVisible = shared.app.tracks[3].steps[0].note == 75 &&
            shared.app.bpm == 271 && shared.app.scaleRoot == 2 &&
            shared.app.scaleMask == 0x0AD5u;
    }
    if (!olderColumnSettingsAccepted || !newerGlobalAccepted ||
        !reverseCrossVisible ||
        reverseCrossStatus.appliedColumnGeneration != reverseCrossColumnToken ||
        reverseCrossStatus.appliedGlobalSettingsGeneration != reverseCrossGlobalToken) {
        std::cerr << "FAIL: newer global did not supersede older column tempo by order\n";
        okay = false;
    }

    // A newer immediate event replaces only overlapping parts of an older
    // future event. The older event's disjoint track is retained when stopping
    // makes both events due.
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.app.bpm = 300;
        audio.reset();
    }
    SDL_Delay(35);
    audio.setRunning(true);
    SDL_Delay(120);
    std::array<fms::TrackData, fms::kTrackCount> futureColumn;
    std::array<fms::TrackData, fms::kTrackCount> immediateOverride;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        futureColumn = shared.app.tracks;
        immediateOverride = shared.app.tracks;
    }
    futureColumn[0].steps[0].note = 81;
    futureColumn[1].steps[0].note = 82;
    immediateOverride[0].steps[0].note = 91;
    fms::TimedGlobalSettings futureTempo;
    futureTempo.applyTempo = true;
    futureTempo.bpm = 180;
    fms::TimedGlobalSettings immediateTempo;
    immediateTempo.applyTempo = true;
    immediateTempo.bpm = 280;
    bool futureAccepted = false;
    bool immediateOverrideAccepted = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        futureAccepted = audio.queuePatternColumn(futureColumn, 0x03u, futureTempo);
        audio.setRunning(false);
        immediateOverrideAccepted = audio.loadPatternColumnImmediate(
            immediateOverride, fms::TrackLoadMode::InPlace, 0x01u, immediateTempo);
    }
    const std::uint64_t immediateOverrideToken =
        audio.status().submittedColumnGeneration;
    fms::TransportStatus immediateOverrideStatus = audio.status();
    for (int attempt = 0;
         attempt < 80 &&
         immediateOverrideStatus.appliedColumnGeneration != immediateOverrideToken;
         ++attempt) {
        SDL_Delay(5);
        immediateOverrideStatus = audio.status();
    }
    SDL_Delay(20);
    bool immediateOverrideVisible = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        immediateOverrideVisible = shared.app.tracks[0].steps[0].note == 91 &&
            shared.app.tracks[1].steps[0].note == 82 && shared.app.bpm == 280;
    }
    if (!futureAccepted || !immediateOverrideAccepted || !immediateOverrideVisible ||
        immediateOverrideStatus.appliedColumnGeneration != immediateOverrideToken) {
        std::cerr << "FAIL: immediate event did not split/supersede an older future event\n";
        okay = false;
    }

    // Conversely, a later-submitted future event must not erase an earlier
    // immediate one. Observe the immediate state before the boundary, then the
    // queued state and its generation acknowledgement at the boundary.
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.app.bpm = 300;
        audio.reset();
    }
    SDL_Delay(35);
    std::array<fms::TrackData, fms::kTrackCount> immediateFirst;
    std::array<fms::TrackData, fms::kTrackCount> futureSecond;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        immediateFirst = shared.app.tracks;
        futureSecond = shared.app.tracks;
    }
    immediateFirst[2].steps[0].note = 101;
    futureSecond[2].steps[0].note = 102;
    fms::TimedGlobalSettings immediateScale;
    immediateScale.applyScale = true;
    immediateScale.scaleRoot = 1;
    immediateScale.scaleMask = 0x05B5u;
    fms::TimedGlobalSettings futureScale;
    futureScale.applyScale = true;
    futureScale.scaleRoot = 9;
    futureScale.scaleMask = 0x0D53u;
    bool immediateFirstAccepted = false;
    bool futureSecondAccepted = false;
    std::uint64_t immediateFirstToken = 0u;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        immediateFirstAccepted = audio.loadPatternColumnImmediate(
            immediateFirst, fms::TrackLoadMode::InPlace, 0x04u, immediateScale);
        immediateFirstToken = audio.status().submittedColumnGeneration;
        audio.setRunning(true);
        futureSecondAccepted =
            audio.queuePatternColumn(futureSecond, 0x04u, futureScale);
    }
    const std::uint64_t futureSecondToken = audio.status().submittedColumnGeneration;
    fms::TransportStatus immediateFirstStatus = audio.status();
    for (int attempt = 0;
         attempt < 60 &&
         immediateFirstStatus.appliedColumnGeneration != immediateFirstToken;
         ++attempt) {
        SDL_Delay(5);
        immediateFirstStatus = audio.status();
    }
    bool immediateFirstVisible = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        immediateFirstVisible = shared.app.tracks[2].steps[0].note == 101 &&
            shared.app.scaleRoot == 1 && shared.app.scaleMask == 0x05B5u;
    }
    fms::TransportStatus futureSecondStatus = audio.status();
    for (int attempt = 0;
         attempt < 140 &&
         futureSecondStatus.appliedColumnGeneration != futureSecondToken;
         ++attempt) {
        SDL_Delay(10);
        futureSecondStatus = audio.status();
    }
    bool futureSecondVisible = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        futureSecondVisible = shared.app.tracks[2].steps[0].note == 102 &&
            shared.app.scaleRoot == 9 && shared.app.scaleMask == 0x0D53u;
    }
    if (!immediateFirstAccepted || !futureSecondAccepted || !immediateFirstVisible ||
        !futureSecondVisible ||
        immediateFirstStatus.appliedColumnGeneration != immediateFirstToken ||
        futureSecondStatus.appliedColumnGeneration != futureSecondToken) {
        std::cerr << "FAIL: later-boundary event erased or skipped the immediate event\n";
        okay = false;
    }

    // Targeted cancellation retires only the selected newest intent. An older
    // disjoint track cue remains live, and a cancellation that loses the race
    // to an already-applied command cannot rewrite its Applied outcome.
    audio.setRunning(false);
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.app.bpm = 300;
        for (int track = 0; track < 2; ++track) {
            auto& data = shared.app.tracks[static_cast<std::size_t>(track)];
            data.length = 1;
            data.rateIndex = 8;
            data.direction = fms::Direction::Forward;
        }
        audio.reset();
    }
    SDL_Delay(35);
    fms::TrackData retainedTrackCue;
    fms::TrackData cancelledTrackCue;
    int trackOneNoteBeforeCancel = 0;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        retainedTrackCue = shared.app.tracks[0];
        cancelledTrackCue = shared.app.tracks[1];
        trackOneNoteBeforeCancel = shared.app.tracks[1].steps[0].note;
    }
    retainedTrackCue.steps[0].note = 120;
    cancelledTrackCue.steps[0].note = 121;
    bool retainedTrackAccepted = false;
    bool cancelledTrackAccepted = false;
    bool targetedTrackCancelAccepted = false;
    std::uint64_t retainedTrackToken = 0u;
    std::uint64_t cancelledTrackToken = 0u;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        retainedTrackAccepted = audio.queuePattern(0, retainedTrackCue);
        retainedTrackToken = audio.status().submittedPatternGenerations[0];
        cancelledTrackAccepted = audio.queuePattern(1, cancelledTrackCue);
        cancelledTrackToken = audio.status().submittedPatternGenerations[1];
        targetedTrackCancelAccepted = audio.cancelTransportCommand(
            fms::TransportCommandFamily::Track, cancelledTrackToken, 1);
    }
    audio.setRunning(true);
    fms::TransportStatus targetedTrackStatus = audio.status();
    for (int attempt = 0;
         attempt < 100 &&
         (targetedTrackStatus.settledPatternGenerations[0] < retainedTrackToken ||
          targetedTrackStatus.settledPatternGenerations[1] < cancelledTrackToken);
         ++attempt) {
        SDL_Delay(5);
        targetedTrackStatus = audio.status();
    }
    bool targetedTrackVisible = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        targetedTrackVisible = shared.app.tracks[0].steps[0].note == 120 &&
            shared.app.tracks[1].steps[0].note == trackOneNoteBeforeCancel;
    }
    const bool lateAppliedCancelAccepted = audio.cancelTransportCommand(
        fms::TransportCommandFamily::Track, retainedTrackToken, 0);
    SDL_Delay(20);
    targetedTrackStatus = audio.status();
    if (!retainedTrackAccepted || !cancelledTrackAccepted ||
        !targetedTrackCancelAccepted || !lateAppliedCancelAccepted ||
        !targetedTrackVisible ||
        !hasSettlement(targetedTrackStatus, fms::TransportCommandFamily::Track,
                       fms::TransportSettlementOutcome::Applied,
                       retainedTrackToken, 0) ||
        hasSettlement(targetedTrackStatus, fms::TransportCommandFamily::Track,
                      fms::TransportSettlementOutcome::Cancelled,
                      retainedTrackToken, 0) ||
        !hasSettlement(targetedTrackStatus, fms::TransportCommandFamily::Track,
                       fms::TransportSettlementOutcome::Cancelled,
                       cancelledTrackToken, 1)) {
        std::cerr << "FAIL: exact track cancellation affected a disjoint cue or race outcome\n";
        okay = false;
    }

    // The same exact-token cancellation works for composed column and global
    // commands without disturbing an older disjoint receipt at that boundary.
    audio.setRunning(false);
    audio.reset();
    SDL_Delay(35);
    std::array<fms::TrackData, fms::kTrackCount> retainedColumn;
    std::array<fms::TrackData, fms::kTrackCount> cancelledColumnIntent;
    int cancelledColumnNoteBefore = 0;
    std::uint8_t scaleRootBeforeGlobalCancel = 0u;
    std::uint16_t scaleMaskBeforeGlobalCancel = 0u;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        retainedColumn = shared.app.tracks;
        cancelledColumnIntent = shared.app.tracks;
        cancelledColumnNoteBefore = shared.app.tracks[4].steps[0].note;
        scaleRootBeforeGlobalCancel = shared.app.scaleRoot;
        scaleMaskBeforeGlobalCancel = shared.app.scaleMask;
    }
    retainedColumn[3].steps[0].note = 122;
    cancelledColumnIntent[4].steps[0].note = 123;
    bool retainedColumnAccepted = false;
    bool cancelledColumnIntentAccepted = false;
    bool targetedColumnCancelAccepted = false;
    std::uint64_t retainedColumnToken = 0u;
    std::uint64_t cancelledColumnToken = 0u;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        retainedColumnAccepted = audio.loadPatternColumnImmediate(
            retainedColumn, fms::TrackLoadMode::InPlace, 0x08u);
        retainedColumnToken = audio.status().submittedColumnGeneration;
        cancelledColumnIntentAccepted = audio.loadPatternColumnImmediate(
            cancelledColumnIntent, fms::TrackLoadMode::InPlace, 0x10u);
        cancelledColumnToken = audio.status().submittedColumnGeneration;
        targetedColumnCancelAccepted = audio.cancelTransportCommand(
            fms::TransportCommandFamily::Column, cancelledColumnToken);
    }
    fms::TimedGlobalSettings retainedGlobalTempo;
    retainedGlobalTempo.applyTempo = true;
    retainedGlobalTempo.bpm = 222;
    fms::TimedGlobalSettings cancelledGlobalScale;
    cancelledGlobalScale.applyScale = true;
    cancelledGlobalScale.scaleRoot = 7;
    cancelledGlobalScale.scaleMask = 0x0881u;
    bool retainedGlobalAccepted = false;
    bool cancelledGlobalIntentAccepted = false;
    bool targetedGlobalCancelAccepted = false;
    std::uint64_t retainedGlobalToken = 0u;
    std::uint64_t cancelledGlobalToken = 0u;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        retainedGlobalAccepted = audio.queueGlobalSettings(retainedGlobalTempo);
        retainedGlobalToken = audio.status().submittedGlobalSettingsGeneration;
        cancelledGlobalIntentAccepted = audio.queueGlobalSettings(cancelledGlobalScale);
        cancelledGlobalToken = audio.status().submittedGlobalSettingsGeneration;
        targetedGlobalCancelAccepted = audio.cancelTransportCommand(
            fms::TransportCommandFamily::GlobalSettings, cancelledGlobalToken);
    }
    fms::TransportStatus targetedCompositeStatus = audio.status();
    for (int attempt = 0;
         attempt < 100 &&
         (targetedCompositeStatus.settledColumnGeneration < cancelledColumnToken ||
          targetedCompositeStatus.settledGlobalSettingsGeneration <
              cancelledGlobalToken);
         ++attempt) {
        SDL_Delay(5);
        targetedCompositeStatus = audio.status();
    }
    bool targetedCompositeVisible = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        targetedCompositeVisible = shared.app.tracks[3].steps[0].note == 122 &&
            shared.app.tracks[4].steps[0].note == cancelledColumnNoteBefore &&
            shared.app.bpm == 222 && shared.app.scaleRoot == scaleRootBeforeGlobalCancel &&
            shared.app.scaleMask == scaleMaskBeforeGlobalCancel;
    }
    if (!retainedColumnAccepted || !cancelledColumnIntentAccepted ||
        !targetedColumnCancelAccepted || !retainedGlobalAccepted ||
        !cancelledGlobalIntentAccepted || !targetedGlobalCancelAccepted ||
        !targetedCompositeVisible ||
        !hasComponentSettlement(targetedCompositeStatus,
                                fms::TransportCommandFamily::Column,
                                fms::TransportSettlementOutcome::Applied,
                                retainedColumnToken, 0x08u, false, false) ||
        !hasSettlement(targetedCompositeStatus,
                       fms::TransportCommandFamily::Column,
                       fms::TransportSettlementOutcome::Cancelled,
                       cancelledColumnToken) ||
        !hasComponentSettlement(targetedCompositeStatus,
                                fms::TransportCommandFamily::GlobalSettings,
                                fms::TransportSettlementOutcome::Applied,
                                retainedGlobalToken, 0u, true, false) ||
        !hasSettlement(targetedCompositeStatus,
                       fms::TransportCommandFamily::GlobalSettings,
                       fms::TransportSettlementOutcome::Cancelled,
                       cancelledGlobalToken) ||
        audio.cancelTransportCommand(fms::TransportCommandFamily::Track,
                                     cancelledTrackToken + 1000u, 1)) {
        std::cerr << "FAIL: targeted column/global cancellation disturbed older intents\n";
        okay = false;
    }

    // Reset cancels scheduled column/global transactions without falsely
    // acknowledging them. Commands submitted in the new epoch remain valid,
    // even if the callback has not observed that reset yet.
    std::array<fms::TrackData, fms::kTrackCount> cancelledColumn;
    int noteBeforeCancelledColumn = 0;
    int bpmBeforeCancelledSettings = 0;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        cancelledColumn = shared.app.tracks;
        noteBeforeCancelledColumn = shared.app.tracks[4].steps[0].note;
        bpmBeforeCancelledSettings = shared.app.bpm;
    }
    cancelledColumn[4].steps[0].note = 113;
    fms::TimedGlobalSettings cancelledColumnSettings;
    cancelledColumnSettings.applyTempo = true;
    cancelledColumnSettings.bpm = 133;
    fms::TimedGlobalSettings cancelledGlobalSettings;
    cancelledGlobalSettings.applyScale = true;
    cancelledGlobalSettings.scaleRoot = 11;
    cancelledGlobalSettings.scaleMask = 0x0801u;
    bool cancelledColumnAccepted = false;
    bool cancelledGlobalAccepted = false;
    std::uint64_t cancelledColumnGeneration = 0u;
    std::uint64_t cancelledGlobalGeneration = 0u;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        cancelledColumnAccepted =
            audio.queuePatternColumn(cancelledColumn, 0x10u, cancelledColumnSettings);
        cancelledGlobalAccepted = audio.queueGlobalSettings(cancelledGlobalSettings);
        cancelledColumnGeneration = audio.status().submittedColumnGeneration;
        cancelledGlobalGeneration = audio.status().submittedGlobalSettingsGeneration;
        audio.reset();
    }
    SDL_Delay(80);
    const fms::TransportStatus cancelledTransportStatus = audio.status();
    bool cancelledTransportStayedOut = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        cancelledTransportStayedOut =
            shared.app.tracks[4].steps[0].note == noteBeforeCancelledColumn &&
            shared.app.bpm == bpmBeforeCancelledSettings;
    }
    if (!cancelledColumnAccepted || !cancelledGlobalAccepted ||
        !cancelledTransportStayedOut ||
        cancelledTransportStatus.appliedColumnGeneration == cancelledColumnGeneration ||
        cancelledTransportStatus.appliedGlobalSettingsGeneration ==
            cancelledGlobalGeneration ||
        cancelledTransportStatus.settledColumnGeneration < cancelledColumnGeneration ||
        cancelledTransportStatus.settledGlobalSettingsGeneration <
            cancelledGlobalGeneration ||
        !hasSettlement(cancelledTransportStatus,
                       fms::TransportCommandFamily::Column,
                       fms::TransportSettlementOutcome::Cancelled,
                       cancelledColumnGeneration) ||
        !hasSettlement(cancelledTransportStatus,
                       fms::TransportCommandFamily::GlobalSettings,
                       fms::TransportSettlementOutcome::Cancelled,
                       cancelledGlobalGeneration)) {
        std::cerr << "FAIL: reset applied or acknowledged a cancelled transport event\n";
        okay = false;
    }

    audio.setRunning(false);
    std::array<fms::TrackData, fms::kTrackCount> postResetColumn;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        postResetColumn = shared.app.tracks;
    }
    postResetColumn[4].steps[0].note = 114;
    fms::TimedGlobalSettings postResetScale;
    postResetScale.applyScale = true;
    postResetScale.scaleRoot = 10;
    postResetScale.scaleMask = 0x0401u;
    bool postResetColumnAccepted = false;
    bool postResetGlobalAccepted = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        audio.reset();
        postResetColumnAccepted = audio.loadPatternColumnImmediate(
            postResetColumn, fms::TrackLoadMode::Reset, 0x10u);
        postResetGlobalAccepted = audio.queueGlobalSettings(postResetScale);
    }
    const std::uint64_t postResetColumnGeneration =
        audio.status().submittedColumnGeneration;
    const std::uint64_t postResetGlobalGeneration =
        audio.status().submittedGlobalSettingsGeneration;
    fms::TransportStatus postResetTransportStatus = audio.status();
    for (int attempt = 0;
         attempt < 80 &&
         (postResetTransportStatus.appliedColumnGeneration !=
              postResetColumnGeneration ||
          postResetTransportStatus.appliedGlobalSettingsGeneration !=
              postResetGlobalGeneration);
         ++attempt) {
        SDL_Delay(5);
        postResetTransportStatus = audio.status();
    }
    bool postResetTransportVisible = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        postResetTransportVisible = shared.app.tracks[4].steps[0].note == 114 &&
            shared.app.scaleRoot == 10 && shared.app.scaleMask == 0x0401u;
    }
    if (!postResetColumnAccepted || !postResetGlobalAccepted ||
        !postResetTransportVisible ||
        postResetTransportStatus.appliedColumnGeneration !=
            postResetColumnGeneration ||
        postResetTransportStatus.appliedGlobalSettingsGeneration !=
            postResetGlobalGeneration) {
        std::cerr << "FAIL: post-reset transport commands were lost or unacknowledged\n";
        okay = false;
    }

    // The UI mutation gate fences the callback before history snapshots are
    // taken. A due loop cue must remain armed and unsettled while the gate is
    // raised, then commit normally once the UI transaction releases it.
    audio.setRunning(false);
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.app.bpm = 300;
        auto& gatedTrack = shared.app.tracks[0];
        gatedTrack.length = 1;
        gatedTrack.rateIndex = 8;
        gatedTrack.direction = fms::Direction::Forward;
        gatedTrack.steps[0].note = 41;
        ++shared.app.editRevision;
        audio.reset();
    }
    SDL_Delay(45);
    fms::TrackData gatedCue;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        gatedCue = shared.app.tracks[0];
    }
    gatedCue.steps[0].note = 115;
    audio.setRunning(true);
    shared.uiMutationInProgress.store(true, std::memory_order_release);
    {
        // Paired with the audio-side post-lock gate check, this lock/unlock is
        // the UI's in-flight callback fence.
        std::lock_guard<std::mutex> lock(shared.mutex);
    }
    const bool gatedCueAccepted = audio.queuePattern(0, gatedCue);
    const std::uint64_t gatedCueToken =
        audio.status().submittedPatternGenerations[0];
    SDL_Delay(160);
    const fms::TransportStatus gatedCueDeferred = audio.status();
    bool gatedCueStayedOut = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        gatedCueStayedOut = shared.app.tracks[0].steps[0].note == 41;
    }
    if (!gatedCueAccepted || !gatedCueStayedOut ||
        gatedCueDeferred.appliedPatternGenerations[0] == gatedCueToken ||
        gatedCueDeferred.settledPatternGenerations[0] >= gatedCueToken ||
        hasSettlement(gatedCueDeferred, fms::TransportCommandFamily::Track,
                      fms::TransportSettlementOutcome::Applied,
                      gatedCueToken, 0) ||
        hasSettlement(gatedCueDeferred, fms::TransportCommandFamily::Track,
                      fms::TransportSettlementOutcome::Cancelled,
                      gatedCueToken, 0)) {
        std::cerr << "FAIL: UI mutation gate did not defer a loop-boundary track cue\n";
        okay = false;
    }
    shared.uiMutationInProgress.store(false, std::memory_order_release);
    fms::TransportStatus gatedCueApplied = audio.status();
    for (int attempt = 0;
         attempt < 80 &&
         gatedCueApplied.appliedPatternGenerations[0] != gatedCueToken;
         ++attempt) {
        SDL_Delay(5);
        gatedCueApplied = audio.status();
    }
    bool gatedCueVisible = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        gatedCueVisible = shared.app.tracks[0].steps[0].note == 115;
    }
    if (!gatedCueVisible ||
        gatedCueApplied.appliedPatternGenerations[0] != gatedCueToken ||
        !hasComponentSettlement(gatedCueApplied,
                                fms::TransportCommandFamily::Track,
                                fms::TransportSettlementOutcome::Applied,
                                gatedCueToken, 0x01u, false, false)) {
        std::cerr << "FAIL: deferred loop-boundary track cue did not apply after gate release\n";
        okay = false;
    }

    // Immediate per-track loads use a separate callback commit path and obey
    // the same defer-without-settlement contract.
    audio.setRunning(false);
    SDL_Delay(25);
    fms::TrackData gatedImmediate;
    int immediateNoteBeforeGate = 0;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        gatedImmediate = shared.app.tracks[1];
        immediateNoteBeforeGate = shared.app.tracks[1].steps[0].note;
    }
    gatedImmediate.steps[0].note = 116;
    shared.uiMutationInProgress.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
    }
    const bool gatedImmediateAccepted = audio.loadTrackImmediate(
        1, gatedImmediate, fms::TrackLoadMode::InPlace);
    const std::uint64_t gatedImmediateToken =
        audio.status().submittedPatternGenerations[1];
    SDL_Delay(80);
    const fms::TransportStatus gatedImmediateDeferred = audio.status();
    bool gatedImmediateStayedOut = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        gatedImmediateStayedOut =
            shared.app.tracks[1].steps[0].note == immediateNoteBeforeGate;
    }
    if (!gatedImmediateAccepted || !gatedImmediateStayedOut ||
        gatedImmediateDeferred.appliedPatternGenerations[1] ==
            gatedImmediateToken ||
        gatedImmediateDeferred.settledPatternGenerations[1] >=
            gatedImmediateToken ||
        hasSettlement(gatedImmediateDeferred,
                      fms::TransportCommandFamily::Track,
                      fms::TransportSettlementOutcome::Applied,
                      gatedImmediateToken, 1) ||
        hasSettlement(gatedImmediateDeferred,
                      fms::TransportCommandFamily::Track,
                      fms::TransportSettlementOutcome::Cancelled,
                      gatedImmediateToken, 1)) {
        std::cerr << "FAIL: UI mutation gate did not defer an immediate track load\n";
        okay = false;
    }
    shared.uiMutationInProgress.store(false, std::memory_order_release);
    fms::TransportStatus gatedImmediateApplied = audio.status();
    for (int attempt = 0;
         attempt < 60 &&
         gatedImmediateApplied.appliedPatternGenerations[1] !=
             gatedImmediateToken;
         ++attempt) {
        SDL_Delay(5);
        gatedImmediateApplied = audio.status();
    }
    bool gatedImmediateVisible = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        gatedImmediateVisible = shared.app.tracks[1].steps[0].note == 116;
    }
    if (!gatedImmediateVisible ||
        gatedImmediateApplied.appliedPatternGenerations[1] !=
            gatedImmediateToken ||
        !hasComponentSettlement(gatedImmediateApplied,
                                fms::TransportCommandFamily::Track,
                                fms::TransportSettlementOutcome::Applied,
                                gatedImmediateToken, 0x02u, false, false)) {
        std::cerr << "FAIL: deferred immediate track load did not apply after gate release\n";
        okay = false;
    }

    // Column and global-settings receipts share the atomic transport-event
    // commit path. Both remain pending behind the gate and commit their exact
    // components together after release.
    std::array<fms::TrackData, fms::kTrackCount> gatedColumn;
    int columnNoteBeforeGate = 0;
    int bpmBeforeGate = 0;
    std::uint8_t scaleRootBeforeGate = 0;
    std::uint16_t scaleMaskBeforeGate = 0u;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        gatedColumn = shared.app.tracks;
        columnNoteBeforeGate = shared.app.tracks[2].steps[0].note;
        bpmBeforeGate = shared.app.bpm;
        scaleRootBeforeGate = shared.app.scaleRoot;
        scaleMaskBeforeGate = shared.app.scaleMask;
    }
    gatedColumn[2].steps[0].note = 117;
    fms::TimedGlobalSettings gatedSettings;
    gatedSettings.applyTempo = true;
    gatedSettings.bpm = 211;
    gatedSettings.applyScale = true;
    gatedSettings.scaleRoot = 7;
    gatedSettings.scaleMask = 0x0891u;
    shared.uiMutationInProgress.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
    }
    const bool gatedColumnAccepted = audio.loadPatternColumnImmediate(
        gatedColumn, fms::TrackLoadMode::InPlace, 0x04u);
    const bool gatedSettingsAccepted = audio.queueGlobalSettings(gatedSettings);
    const fms::TransportStatus gatedCompositeSubmitted = audio.status();
    const std::uint64_t gatedColumnToken =
        gatedCompositeSubmitted.submittedColumnGeneration;
    const std::uint64_t gatedSettingsToken =
        gatedCompositeSubmitted.submittedGlobalSettingsGeneration;
    SDL_Delay(80);
    const fms::TransportStatus gatedCompositeDeferred = audio.status();
    bool gatedCompositeStayedOut = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        gatedCompositeStayedOut =
            shared.app.tracks[2].steps[0].note == columnNoteBeforeGate &&
            shared.app.bpm == bpmBeforeGate &&
            shared.app.scaleRoot == scaleRootBeforeGate &&
            shared.app.scaleMask == scaleMaskBeforeGate;
    }
    if (!gatedColumnAccepted || !gatedSettingsAccepted ||
        !gatedCompositeStayedOut ||
        gatedCompositeDeferred.appliedColumnGeneration == gatedColumnToken ||
        gatedCompositeDeferred.appliedGlobalSettingsGeneration ==
            gatedSettingsToken ||
        gatedCompositeDeferred.settledColumnGeneration >= gatedColumnToken ||
        gatedCompositeDeferred.settledGlobalSettingsGeneration >=
            gatedSettingsToken ||
        hasSettlement(gatedCompositeDeferred,
                      fms::TransportCommandFamily::Column,
                      fms::TransportSettlementOutcome::Applied,
                      gatedColumnToken) ||
        hasSettlement(gatedCompositeDeferred,
                      fms::TransportCommandFamily::Column,
                      fms::TransportSettlementOutcome::Cancelled,
                      gatedColumnToken) ||
        hasSettlement(gatedCompositeDeferred,
                      fms::TransportCommandFamily::GlobalSettings,
                      fms::TransportSettlementOutcome::Applied,
                      gatedSettingsToken) ||
        hasSettlement(gatedCompositeDeferred,
                      fms::TransportCommandFamily::GlobalSettings,
                      fms::TransportSettlementOutcome::Cancelled,
                      gatedSettingsToken)) {
        std::cerr << "FAIL: UI mutation gate did not defer a column/global event\n";
        okay = false;
    }
    shared.uiMutationInProgress.store(false, std::memory_order_release);
    fms::TransportStatus gatedCompositeApplied = audio.status();
    for (int attempt = 0;
         attempt < 60 &&
         (gatedCompositeApplied.appliedColumnGeneration != gatedColumnToken ||
          gatedCompositeApplied.appliedGlobalSettingsGeneration !=
              gatedSettingsToken);
         ++attempt) {
        SDL_Delay(5);
        gatedCompositeApplied = audio.status();
    }
    bool gatedCompositeVisible = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        gatedCompositeVisible = shared.app.tracks[2].steps[0].note == 117 &&
            shared.app.bpm == 211 && shared.app.scaleRoot == 7 &&
            shared.app.scaleMask == 0x0891u;
    }
    if (!gatedCompositeVisible ||
        gatedCompositeApplied.appliedColumnGeneration != gatedColumnToken ||
        gatedCompositeApplied.appliedGlobalSettingsGeneration !=
            gatedSettingsToken ||
        !hasComponentSettlement(gatedCompositeApplied,
                                fms::TransportCommandFamily::Column,
                                fms::TransportSettlementOutcome::Applied,
                                gatedColumnToken, 0x04u, false, false) ||
        !hasComponentSettlement(gatedCompositeApplied,
                                fms::TransportCommandFamily::GlobalSettings,
                                fms::TransportSettlementOutcome::Applied,
                                gatedSettingsToken, 0u, true, true)) {
        std::cerr << "FAIL: deferred column/global event did not apply after gate release\n";
        okay = false;
    }

    audio.close();
    SDL_Quit();
    if (!okay) return EXIT_FAILURE;
    std::cout << "FMS real-time audio smoke test passed.\n";
    return EXIT_SUCCESS;
}
