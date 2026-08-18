#include "model.hpp"
#include "persistence.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <unistd.h>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace

int main() {
    using namespace fms;

    expect(std::abs(fmRatio(9) - 1.125) < 0.0001, "fractional FM ratio");
    expect(std::abs(fmRatio(23) - 2.975) < 0.0001, "upper fractional FM ratio");
    expect(noteName(60) == "C4", "MIDI note display");

    constexpr std::uint16_t majorMask =
        (1u << 0) | (1u << 2) | (1u << 4) | (1u << 5) |
        (1u << 7) | (1u << 9) | (1u << 11);
    expect(quantizeNote(61, 0, majorMask) == 62, "scale quantization favors upward note");

    AppState state = makeDefaultState();
    expect(state.tracks[0].steps[0].active, "starter pattern has a downbeat");
    expect(state.tracks[4].steps[2].noise.narrow, "starter pattern uses narrow noise");
    expect(!state.tracks[0].steps[0].advancedFm.enabled,
           "advanced synthesis is disabled for legacy defaults");
    expect(state.tracks[0].steps[0].advancedFm.unisonVoices == 1 &&
               state.tracks[0].steps[0].advancedFm.filterMode == AdvancedFilterMode::Off &&
               state.tracks[0].steps[0].advancedFm.driveMode == AdvancedDriveMode::Off,
           "advanced defaults are neutral");
    expect(state.controller.enabled &&
               state.controller.buttons[static_cast<std::size_t>(ControllerAction::Confirm)] !=
                   kControllerButtonUnbound,
           "controller defaults are device-independent and bound");

    const PerformanceState snapshot = capturePerformance(state);
    state.bpm = 299;
    state.tracks[0].steps[0].active = false;
    restorePerformance(state, snapshot);
    expect(state.bpm == 120 && state.tracks[0].steps[0].active, "snapshot restore");

    state.bpm = 900;
    state.scaleMask = 0;
    state.tracks[0].length = 0;
    state.tracks[0].steps[0].condition = 0;
    state.tracks[0].steps[0].fm.modRatio = 255;
    state.tracks[0].steps[0].noise.rate = 240;
    state.tracks[0].steps[0].advancedFm.algorithm =
        static_cast<AdvancedFmAlgorithm>(255);
    state.tracks[0].steps[0].advancedFm.operators[0].shape =
        static_cast<AdvancedOscShape>(255);
    state.tracks[0].steps[0].advancedFm.operators[0].ratio = 255;
    state.tracks[0].steps[0].advancedFm.operators[0].detune = -128;
    state.tracks[0].steps[0].advancedFm.unisonVoices = 0;
    state.tracks[0].steps[0].advancedFm.modulation[0].depth = -128;
    state.controller.buttons[0] = 200;
    sanitize(state);
    expect(state.bpm == 300, "tempo is sanitized");
    expect(state.scaleMask != 0, "empty scale is sanitized");
    expect(state.tracks[0].length == 1, "track length is sanitized");
    expect(state.tracks[0].steps[0].condition == 1, "condition is sanitized");
    expect(state.tracks[0].steps[0].fm.modRatio == 127, "FM values are sanitized");
    expect(state.tracks[0].steps[0].noise.rate == 127, "noise values are sanitized");
    expect(state.tracks[0].steps[0].advancedFm.algorithm ==
               AdvancedFmAlgorithm::Algorithm1 &&
               state.tracks[0].steps[0].advancedFm.operators[0].shape ==
                   AdvancedOscShape::Sine,
           "advanced enum values are sanitized");
    expect(state.tracks[0].steps[0].advancedFm.operators[0].ratio == 127 &&
               state.tracks[0].steps[0].advancedFm.operators[0].detune == -64 &&
               state.tracks[0].steps[0].advancedFm.unisonVoices == 1 &&
               state.tracks[0].steps[0].advancedFm.modulation[0].depth == -127,
           "advanced numeric values are sanitized");
    expect(state.controller.buttons[0] == kControllerButtonUnbound,
           "invalid controller buttons are sanitized to unbound");

    TrackData a;
    TrackData b;
    randomizeTrack(a, 2, 0x12345678u);
    randomizeTrack(b, 2, 0x12345678u);
    bool same = true;
    for (int i = 0; i < kStepCount; ++i) {
        same = same && a.steps[i].active == b.steps[i].active &&
               a.steps[i].note == b.steps[i].note &&
               a.steps[i].fm.modDepth == b.steps[i].fm.modDepth &&
               a.steps[i].advancedFm == b.steps[i].advancedFm;
    }
    expect(same, "seeded randomizer is deterministic");

    const auto path = std::filesystem::temp_directory_path() /
                      ("fms-linux-roundtrip-" +
                       std::to_string(static_cast<long long>(::getpid())) + ".fms");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    state = makeDefaultState();
    state.bpm = 173;
    state.lightTheme = true;
    state.tracks[2].steps[5].active = true;
    state.tracks[2].steps[5].note = 91;
    state.tracks[1].direction = static_cast<Direction>(255);
    state.tracks[1].steps[0].pan = static_cast<Pan>(255);
    state.patterns[2][77].occupied = true;
    state.patterns[2][77].track = state.tracks[2];
    std::string error;
    expect(saveState(state, path.string(), error), "state save: " + error);
    AppState loaded;
    expect(loadState(loaded, path.string(), error), "state load: " + error);
    expect(loaded.bpm == 173 && loaded.lightTheme, "global values round-trip");
    expect(loaded.tracks[2].steps[5].note == 91, "step values round-trip");
    expect(loaded.tracks[1].direction == Direction::Forward &&
               loaded.tracks[1].steps[0].pan == Pan::Center,
           "invalid in-memory enums are normalized before saving");
    expect(loaded.patterns[2][77].occupied && loaded.patterns[2][77].track.steps[5].note == 91,
           "pattern library round-trips");

    if (std::filesystem::exists(path)) {
        std::fstream corrupt(path, std::ios::in | std::ios::out | std::ios::binary);
        corrupt.seekp(20);
        const char byte = static_cast<char>(0xA5);
        corrupt.write(&byte, 1);
        corrupt.close();
        AppState untouched = makeDefaultState();
        const auto before = untouched.bpm;
        expect(!loadState(untouched, path.string(), error), "corrupt save is rejected");
        expect(untouched.bpm == before, "failed load leaves destination unchanged");
    }

    error.clear();
    expect(saveState(state, path.string(), error), "state re-save for payload check: " + error);
    if (std::filesystem::exists(path)) {
        std::fstream corrupt(path, std::ios::in | std::ios::out | std::ios::binary);
        corrupt.seekg(40);
        char byte = 0;
        corrupt.read(&byte, 1);
        byte = static_cast<char>(static_cast<unsigned char>(byte) ^ 0x5Au);
        corrupt.seekp(40);
        corrupt.write(&byte, 1);
        corrupt.close();
        AppState untouched = makeDefaultState();
        const auto before = untouched.bpm;
        expect(!loadState(untouched, path.string(), error), "payload corruption is rejected");
        expect(untouched.bpm == before, "payload failure leaves destination unchanged");
    }

    error.clear();
    expect(saveState(state, path.string(), error), "state re-save for truncation check: " + error);
    std::filesystem::resize_file(path, 12u, ignored);
    AppState untouched = makeDefaultState();
    const auto before = untouched.bpm;
    expect(!loadState(untouched, path.string(), error), "truncated save is rejected");
    expect(untouched.bpm == before, "truncated load leaves destination unchanged");
    std::filesystem::remove(path, ignored);

    if (failures == 0) {
        std::cout << "All FMS model and persistence tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << failures << " test(s) failed.\n";
    return EXIT_FAILURE;
}
