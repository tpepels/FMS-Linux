#include "model.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <random>

namespace fms {
namespace {

template <typename T>
T clamped(T value, T low, T high) {
    return std::max(low, std::min(high, value));
}

void sanitizeAdvancedFm(AdvancedFmPatch& patch) {
    if (static_cast<std::uint8_t>(patch.algorithm) >
        static_cast<std::uint8_t>(AdvancedFmAlgorithm::Algorithm12)) {
        patch.algorithm = AdvancedFmAlgorithm::Algorithm1;
    }
    for (AdvancedFmOperator& op : patch.operators) {
        if (static_cast<std::uint8_t>(op.shape) >
            static_cast<std::uint8_t>(AdvancedOscShape::Noise)) {
            op.shape = AdvancedOscShape::Sine;
        }
        op.ratio = clamped<std::uint8_t>(op.ratio, 0, 127);
        op.level = clamped<std::uint8_t>(op.level, 0, 127);
        op.feedback = clamped<std::uint8_t>(op.feedback, 0, 127);
        op.detune = clamped<std::int8_t>(op.detune, -64, 63);
    }
    patch.ampEnvelope.attack = clamped<std::uint8_t>(patch.ampEnvelope.attack, 0, 127);
    patch.ampEnvelope.decay = clamped<std::uint8_t>(patch.ampEnvelope.decay, 0, 127);
    patch.ampEnvelope.sustain = clamped<std::uint8_t>(patch.ampEnvelope.sustain, 0, 127);
    patch.ampEnvelope.release = clamped<std::uint8_t>(patch.ampEnvelope.release, 0, 127);
    if (static_cast<std::uint8_t>(patch.filterMode) >
        static_cast<std::uint8_t>(AdvancedFilterMode::Notch)) {
        patch.filterMode = AdvancedFilterMode::Off;
    }
    patch.filterCutoff = clamped<std::uint8_t>(patch.filterCutoff, 0, 127);
    patch.resonance = clamped<std::uint8_t>(patch.resonance, 0, 127);
    if (static_cast<std::uint8_t>(patch.driveMode) >
        static_cast<std::uint8_t>(AdvancedDriveMode::Wavefold)) {
        patch.driveMode = AdvancedDriveMode::Off;
    }
    patch.driveAmount = clamped<std::uint8_t>(patch.driveAmount, 0, 127);
    patch.unisonVoices = clamped<std::uint8_t>(patch.unisonVoices, 1, 4);
    patch.unisonDetune = clamped<std::uint8_t>(patch.unisonDetune, 0, 127);
    patch.unisonWidth = clamped<std::uint8_t>(patch.unisonWidth, 0, 127);
    for (AdvancedModSlot& slot : patch.modulation) {
        if (static_cast<std::uint8_t>(slot.source) >
            static_cast<std::uint8_t>(AdvancedModSource::AmpEnvelope)) {
            slot.source = AdvancedModSource::Off;
        }
        slot.rate = clamped<std::uint8_t>(slot.rate, 0, 127);
        slot.depth = clamped<std::int8_t>(slot.depth, -127, 127);
        if (static_cast<std::uint8_t>(slot.destination) >
            static_cast<std::uint8_t>(AdvancedModDestination::Operator4Ratio)) {
            slot.destination = AdvancedModDestination::None;
        }
    }
}

void sanitizeController(ControllerSettings& controller) {
    for (std::uint8_t& button : controller.buttons) {
        if (button > kControllerButtonMax && button != kControllerButtonUnbound) {
            button = kControllerButtonUnbound;
        }
    }
}

void sanitizePatternMetadata(PatternMetadata& metadata) {
    bool terminated = false;
    for (std::size_t index = 0; index < kPatternMetadataNameLength; ++index) {
        char& character = metadata.name[index];
        if (terminated || character == '\0') {
            character = '\0';
            terminated = true;
            continue;
        }
        const auto raw = static_cast<unsigned char>(character);
        if (raw < 0x20U || raw > 0x7EU) character = ' ';
    }
    metadata.name[kPatternMetadataNameLength] = '\0';
    if (metadata.color > 5U) metadata.color = 0;
}

template <typename Random>
void randomizeAdvancedFm(AdvancedFmPatch& patch, bool fmTrack, Random& random) {
    std::uniform_int_distribution<int> percent(0, 99);
    std::uniform_int_distribution<int> byte(0, 127);
    std::uniform_int_distribution<int> bipolar(-64, 63);
    std::uniform_int_distribution<int> shape(
        0, static_cast<int>(AdvancedOscShape::Noise));
    std::uniform_int_distribution<int> algorithm(
        0, static_cast<int>(AdvancedFmAlgorithm::Algorithm12));
    std::uniform_int_distribution<int> filter(
        0, static_cast<int>(AdvancedFilterMode::Notch));
    std::uniform_int_distribution<int> drive(
        0, static_cast<int>(AdvancedDriveMode::Wavefold));
    std::uniform_int_distribution<int> source(
        0, static_cast<int>(AdvancedModSource::AmpEnvelope));
    std::uniform_int_distribution<int> destination(
        0, static_cast<int>(AdvancedModDestination::Operator4Ratio));
    std::uniform_int_distribution<int> unison(1, 4);

    patch.enabled = fmTrack && percent(random) < 28;
    patch.algorithm = static_cast<AdvancedFmAlgorithm>(algorithm(random));
    for (AdvancedFmOperator& op : patch.operators) {
        op.shape = static_cast<AdvancedOscShape>(shape(random));
        op.ratio = static_cast<std::uint8_t>(byte(random));
        op.level = static_cast<std::uint8_t>(byte(random));
        op.feedback = static_cast<std::uint8_t>(byte(random));
        op.detune = static_cast<std::int8_t>(bipolar(random));
    }
    patch.ampEnvelope.attack = static_cast<std::uint8_t>(byte(random));
    patch.ampEnvelope.decay = static_cast<std::uint8_t>(byte(random));
    patch.ampEnvelope.sustain = static_cast<std::uint8_t>(byte(random));
    patch.ampEnvelope.release = static_cast<std::uint8_t>(byte(random));
    patch.filterMode = static_cast<AdvancedFilterMode>(filter(random));
    patch.filterCutoff = static_cast<std::uint8_t>(byte(random));
    patch.resonance = static_cast<std::uint8_t>(byte(random));
    patch.driveMode = static_cast<AdvancedDriveMode>(drive(random));
    patch.driveAmount = static_cast<std::uint8_t>(byte(random));
    patch.unisonVoices = static_cast<std::uint8_t>(unison(random));
    patch.unisonDetune = static_cast<std::uint8_t>(byte(random));
    patch.unisonWidth = static_cast<std::uint8_t>(byte(random));
    for (AdvancedModSlot& slot : patch.modulation) {
        slot.source = static_cast<AdvancedModSource>(source(random));
        slot.rate = static_cast<std::uint8_t>(byte(random));
        slot.depth = static_cast<std::int8_t>(bipolar(random));
        slot.destination = static_cast<AdvancedModDestination>(destination(random));
    }
}

Step bassStep(int note, int level, Pan pan = Pan::Center) {
    Step step;
    step.active = true;
    step.note = static_cast<std::uint8_t>(note);
    step.level = static_cast<std::uint8_t>(level);
    step.pan = pan;
    step.fm.ampAttack = 0;
    step.fm.ampHold = 22;
    step.fm.ampRelease = 30;
    step.fm.modRatio = 8;
    step.fm.modDepth = 48;
    step.fm.modFeedback = 6;
    step.fm.modRelease = 36;
    step.fm.sweepDepth = -3;
    return step;
}

Step pluckStep(int note, int level, Pan pan) {
    Step step;
    step.active = true;
    step.note = static_cast<std::uint8_t>(note);
    step.level = static_cast<std::uint8_t>(level);
    step.pan = pan;
    step.fm.ampAttack = 0;
    step.fm.ampHold = 5;
    step.fm.ampRelease = 24;
    step.fm.modRatio = 17;
    step.fm.modDepth = 64;
    step.fm.modFeedback = 10;
    step.fm.modRelease = 16;
    step.echo = true;
    return step;
}

void sanitizeStep(Step& step) {
    if (static_cast<std::uint8_t>(step.pan) > static_cast<std::uint8_t>(Pan::Right)) {
        step.pan = Pan::Center;
    }
    if (static_cast<std::uint8_t>(step.mode) > static_cast<std::uint8_t>(SynthMode::Parallel)) {
        step.mode = SynthMode::FM;
    }
    step.note = clamped<std::uint8_t>(step.note, 12, 119);
    step.level = clamped<std::uint8_t>(step.level, 0, 127);
    step.portamento = clamped<std::uint8_t>(step.portamento, 0, 127);
    step.condition = clamped<std::uint8_t>(step.condition, 1, 8);
    step.microTicks = clamped<std::int8_t>(step.microTicks, -6, 6);
    for (auto& interval : step.chord) interval = clamped<std::int8_t>(interval, 0, 24);
    step.fm.ampAttack = clamped<std::uint8_t>(step.fm.ampAttack, 0, 127);
    step.fm.ampHold = clamped<std::uint8_t>(step.fm.ampHold, 0, 127);
    step.fm.ampRelease = clamped<std::uint8_t>(step.fm.ampRelease, 0, 127);
    step.fm.modRatio = clamped<std::uint8_t>(step.fm.modRatio, 0, 127);
    step.fm.modDepth = clamped<std::uint8_t>(step.fm.modDepth, 0, 127);
    step.fm.modFeedback = clamped<std::uint8_t>(step.fm.modFeedback, 0, 127);
    step.fm.modAttack = clamped<std::uint8_t>(step.fm.modAttack, 0, 127);
    step.fm.modRelease = clamped<std::uint8_t>(step.fm.modRelease, 0, 127);
    step.fm.modEnd = clamped<std::uint8_t>(step.fm.modEnd, 0, 127);
    step.fm.sweepDepth = clamped<std::int8_t>(step.fm.sweepDepth, -64, 63);
    step.fm.sweepRelease = clamped<std::uint8_t>(step.fm.sweepRelease, 0, 127);
    step.noise.ampAttack = clamped<std::uint8_t>(step.noise.ampAttack, 0, 127);
    step.noise.ampHold = clamped<std::uint8_t>(step.noise.ampHold, 0, 127);
    step.noise.ampRelease = clamped<std::uint8_t>(step.noise.ampRelease, 0, 127);
    step.noise.rate = clamped<std::uint8_t>(step.noise.rate, 0, 127);
    sanitizeAdvancedFm(step.advancedFm);
}

void sanitizeTrack(TrackData& track) {
    if (static_cast<std::uint8_t>(track.direction) >
        static_cast<std::uint8_t>(Direction::Random)) {
        track.direction = Direction::Forward;
    }
    if (static_cast<std::uint8_t>(track.echo.pan) >
        static_cast<std::uint8_t>(EchoPan::PingPong)) {
        track.echo.pan = EchoPan::Original;
    }
    if (static_cast<std::uint8_t>(track.transpose.advance) >
        static_cast<std::uint8_t>(TransposeAdvance::Trigger)) {
        track.transpose.advance = TransposeAdvance::Pattern;
    }
    if (static_cast<std::uint8_t>(track.modulator.destination) >
        static_cast<std::uint8_t>(ModDest::NoiseRate)) {
        track.modulator.destination = ModDest::ModDepth;
    }
    if (static_cast<std::uint8_t>(track.modulator.wave) >
        static_cast<std::uint8_t>(ModWave::Random)) {
        track.modulator.wave = ModWave::Triangle;
    }
    track.length = clamped<std::uint8_t>(track.length, 1, kStepCount);
    track.rateIndex = clamped<std::uint8_t>(track.rateIndex, 0, 8);
    track.shuffle = clamped<std::uint8_t>(track.shuffle, 0, 50);
    track.transpose.length = clamped<std::uint8_t>(track.transpose.length, 1, 8);
    track.transpose.rate = clamped<std::uint8_t>(track.transpose.rate, 1, 16);
    track.echo.repeats = clamped<std::uint8_t>(track.echo.repeats, 0, 8);
    track.echo.speedTicks = clamped<std::uint8_t>(track.echo.speedTicks, 1, 96);
    track.echo.transposeModulo = clamped<std::uint8_t>(track.echo.transposeModulo, 1, 8);
    track.modulator.targetTrack = static_cast<std::uint8_t>(
        track.modulator.targetTrack % static_cast<std::uint8_t>(kTrackCount));
    track.modulator.speed = clamped<std::uint8_t>(track.modulator.speed, 1, 64);
    track.modulator.offset = clamped<std::uint8_t>(track.modulator.offset, 0, 63);
    for (auto& value : track.transpose.values) value = clamped<std::int8_t>(value, -48, 48);
    for (auto& step : track.steps) sanitizeStep(step);
}

} // namespace

AppState makeDefaultState() {
    AppState state;
    for (int track = 0; track < kTrackCount; ++track) {
        state.tracks[track].modulator.targetTrack = static_cast<std::uint8_t>(track);
        for (int step = 0; step < kStepCount; ++step) {
            state.tracks[track].steps[step].note = static_cast<std::uint8_t>(48 + track * 7);
            state.tracks[track].steps[step].pan =
                track == 0 ? Pan::Center : (track % 2 ? Pan::Left : Pan::Right);
        }
    }

    // A restrained starter pattern makes the instrument audible on first launch.
    state.tracks[0].steps[0] = bassStep(36, 116);
    state.tracks[0].steps[4] = bassStep(36, 108);
    state.tracks[0].steps[8] = bassStep(43, 114);
    state.tracks[0].steps[12] = bassStep(34, 108);
    state.tracks[0].echo.repeats = 0;

    state.tracks[1].steps[2] = pluckStep(60, 74, Pan::Left);
    state.tracks[1].steps[6] = pluckStep(63, 72, Pan::Left);
    state.tracks[1].steps[10] = pluckStep(67, 76, Pan::Left);
    state.tracks[1].steps[14] = pluckStep(70, 70, Pan::Left);
    state.tracks[1].echo.repeats = 2;
    state.tracks[1].echo.speedTicks = 8;

    state.tracks[2].steps[0] = pluckStep(72, 66, Pan::Right);
    state.tracks[2].steps[0].chord = {4, 7, 0};
    state.tracks[2].steps[8] = pluckStep(70, 62, Pan::Right);
    state.tracks[2].steps[8].chord = {3, 7, 0};
    state.tracks[2].echo.repeats = 1;
    state.tracks[2].echo.speedTicks = 12;

    state.tracks[3].steps[3] = pluckStep(79, 50, Pan::Right);
    state.tracks[3].steps[7] = pluckStep(82, 46, Pan::Right);
    state.tracks[3].steps[11] = pluckStep(79, 48, Pan::Right);
    state.tracks[3].steps[15] = pluckStep(86, 44, Pan::Right);
    state.tracks[3].shuffle = 12;

    auto& noise = state.tracks[4];
    for (int i : {0, 4, 8, 12}) {
        noise.steps[i].active = true;
        noise.steps[i].level = 92;
        noise.steps[i].noise.ampHold = 2;
        noise.steps[i].noise.ampRelease = 16;
        noise.steps[i].noise.rate = 36;
    }
    for (int i : {2, 6, 10, 14}) {
        noise.steps[i].active = true;
        noise.steps[i].level = 46;
        noise.steps[i].pan = i % 4 ? Pan::Right : Pan::Left;
        noise.steps[i].noise.ampHold = 1;
        noise.steps[i].noise.ampRelease = 9;
        noise.steps[i].noise.rate = 88;
        noise.steps[i].noise.narrow = true;
    }

    const std::array<const char*, 8> names {
        "INIT", "LIVE", "IDEA", "BASS", "LEAD", "PERC", "USER", "ARCH"
    };
    for (int bank = 0; bank < 8; ++bank) {
        for (int i = 0; i < 4; ++i) {
            state.banks[bank].name[i] = names[bank][i];
        }
        state.banks[bank].name[4] = '\0';
    }
    state.banks[0].hasTempo = true;
    state.banks[0].hasScale = true;

    for (int slot = 0; slot < kPaletteSize; ++slot) {
        Step fm = slot < 4 ? bassStep(48, 100) : pluckStep(60, 92, Pan::Center);
        fm.fm.modRatio = static_cast<std::uint8_t>(8 + slot * 5);
        fm.fm.modDepth = static_cast<std::uint8_t>(24 + slot * 7);
        fm.fm.modFeedback = static_cast<std::uint8_t>(slot * 3);
        fm.fm.ampHold = static_cast<std::uint8_t>(4 + slot * 2);
        fm.fm.ampRelease = static_cast<std::uint8_t>(15 + slot * 3);
        fm.mode = slot >= 11 ? SynthMode::Parallel : SynthMode::FM;
        state.fmPalette[slot] = fm;

        Step noiseSound;
        noiseSound.active = true;
        noiseSound.level = 92;
        noiseSound.noise.ampAttack = static_cast<std::uint8_t>(slot / 5);
        noiseSound.noise.ampHold = static_cast<std::uint8_t>(1 + slot * 2);
        noiseSound.noise.ampRelease = static_cast<std::uint8_t>(7 + slot * 4);
        noiseSound.noise.rate = static_cast<std::uint8_t>(12 + slot * 8);
        noiseSound.noise.narrow = slot >= 8;
        state.noisePalette[slot] = noiseSound;
    }

    for (int track = 0; track < kTrackCount; ++track) {
        state.patterns[track][0].occupied = true;
        state.patterns[track][0].track = state.tracks[track];
    }
    constexpr std::array<char, kPatternMetadataNameLength + 1> starterName {
        'S', 'T', 'A', 'R', 'T', 'E', 'R', '\0'
    };
    state.patternMetadata[0].name = starterName;
    state.patternMetadata[0].color = 1;
    return state;
}

PerformanceState capturePerformance(const AppState& state) {
    PerformanceState result;
    result.bpm = state.bpm;
    result.scaleRoot = state.scaleRoot;
    result.scaleMask = state.scaleMask;
    result.tracks = state.tracks;
    return result;
}

void restorePerformance(AppState& state, const PerformanceState& snapshot) {
    state.bpm = snapshot.bpm;
    state.scaleRoot = snapshot.scaleRoot;
    state.scaleMask = snapshot.scaleMask;
    state.tracks = snapshot.tracks;
    ++state.editRevision;
}

void sanitize(AppState& state) {
    state.bpm = clamped<std::uint16_t>(state.bpm, 30, 300);
    state.scaleRoot %= 12;
    state.scaleMask &= 0x0FFF;
    if (state.scaleMask == 0) state.scaleMask = 1;
    state.accent %= 6;
    for (int trackIndex = 0; trackIndex < kTrackCount; ++trackIndex) {
        sanitizeTrack(state.tracks[trackIndex]);
        for (auto& pattern : state.patterns[trackIndex]) sanitizeTrack(pattern.track);
    }
    for (auto& metadata : state.patternMetadata) sanitizePatternMetadata(metadata);
    for (auto& step : state.fmPalette) sanitizeStep(step);
    for (auto& step : state.noisePalette) sanitizeStep(step);
    for (auto& bank : state.banks) {
        bank.name[4] = '\0';
        bank.tempo = clamped<std::uint16_t>(bank.tempo, 30, 300);
        bank.scaleRoot %= 12;
        bank.scaleMask &= 0x0FFF;
        if (bank.scaleMask == 0) bank.scaleMask = 1;
    }
    sanitizeController(state.controller);
}

void randomizeTrack(TrackData& track, int trackIndex, std::uint32_t seed) {
    std::mt19937 random(seed);
    // Keep the legacy random stream unchanged. Advanced fields use an
    // independent stream so disabling advanced synthesis preserves historical
    // randomizer output for every legacy field.
    std::mt19937 advancedRandom(seed ^ 0xD1B54A35u);
    std::uniform_int_distribution<int> chance(0, 99);
    std::uniform_int_distribution<int> degree(0, 7);
    static constexpr std::array<int, 8> minor {0, 2, 3, 5, 7, 8, 10, 12};
    for (int i = 0; i < kStepCount; ++i) {
        auto& step = track.steps[i];
        const int threshold = trackIndex == 4 ? 38 : 27;
        step.active = chance(random) < threshold;
        step.trigless = false;
        step.note = static_cast<std::uint8_t>(clamped(42 + trackIndex * 6 + minor[degree(random)], 12, 119));
        step.level = static_cast<std::uint8_t>(70 + chance(random) * 48 / 100);
        step.pan = static_cast<Pan>(chance(random) < 24 ? (chance(random) & 1 ? 0 : 2) : 1);
        step.fm.modRatio = static_cast<std::uint8_t>(8 + chance(random) * 32 / 100);
        step.fm.modDepth = static_cast<std::uint8_t>(20 + chance(random) * 80 / 100);
        step.fm.modFeedback = static_cast<std::uint8_t>(chance(random) * 38 / 100);
        step.fm.ampHold = static_cast<std::uint8_t>(4 + chance(random) * 28 / 100);
        step.fm.ampRelease = static_cast<std::uint8_t>(12 + chance(random) * 42 / 100);
        step.noise.rate = static_cast<std::uint8_t>(15 + chance(random) * 100 / 100);
        step.noise.narrow = chance(random) < 35;
        randomizeAdvancedFm(step.advancedFm, trackIndex < kFmTrackCount, advancedRandom);
    }
}

int quantizeNote(int midiNote, std::uint8_t root, std::uint16_t mask) {
    midiNote = clamped(midiNote, 0, 127);
    if ((mask & 0x0FFF) == 0x0FFF) return midiNote;
    for (int distance = 0; distance < 12; ++distance) {
        const int up = clamped(midiNote + distance, 0, 127);
        const int upDegree = (up - root + 120) % 12;
        if (mask & (1u << upDegree)) return up;
        const int down = clamped(midiNote - distance, 0, 127);
        const int downDegree = (down - root + 120) % 12;
        if (mask & (1u << downDegree)) return down;
    }
    return midiNote;
}

double rateMultiplier(std::uint8_t index) {
    static constexpr std::array<double, 9> values {0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 1.5, 2.0, 3.0, 4.0};
    return values[std::min<std::size_t>(index, values.size() - 1)];
}

double fmRatio(std::uint8_t encoded) {
    static constexpr std::array<double, 8> fractions {0.0, 0.125, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 0.75, 0.975};
    return static_cast<double>(encoded / 8) + fractions[encoded % 8];
}

std::string noteName(int midiNote) {
    static constexpr std::array<const char*, 12> names {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    midiNote = clamped(midiNote, 0, 127);
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%s%d", names[midiNote % 12], midiNote / 12 - 1);
    return buffer;
}

const char* directionName(Direction direction) {
    switch (direction) {
        case Direction::Forward: return "FWD";
        case Direction::PingPong: return "PING";
        case Direction::Reverse: return "REV";
        case Direction::Random: return "RAND";
    }
    return "FWD";
}

const char* rateName(std::uint8_t index) {
    static constexpr std::array<const char*, 9> names {
        "1/4", "1/3", "1/2", "2/3", "1X", "3/2", "2X", "3X", "4X"
    };
    return names[std::min<std::size_t>(index, names.size() - 1)];
}

} // namespace fms
