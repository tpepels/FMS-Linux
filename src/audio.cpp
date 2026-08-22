#include "audio.hpp"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>

namespace fms {
namespace {

constexpr int kDefaultSampleRate = 48000;
constexpr std::uint16_t kDefaultBufferFrames = 512;
constexpr int kOutputChannels = 2;
constexpr double kPi = 3.14159265358979323846264338327950288;
constexpr double kTau = kPi * 2.0;
constexpr double kTicksPerStep = 6.0;
constexpr std::size_t kPendingSteps = 32;
constexpr std::size_t kEchoCapacity = 4096;
constexpr std::size_t kPreviewCapacity = 64;
constexpr std::size_t kPatternCommandCapacity = 32;
constexpr std::size_t kColumnCommandCapacity = 8;
constexpr std::size_t kGlobalSettingsCommandCapacity = 16;
constexpr std::size_t kScheduledTransportEventCapacity =
    kColumnCommandCapacity + kGlobalSettingsCommandCapacity;
constexpr double kGlobalBoundaryTicks = static_cast<double>(kStepCount) * kTicksPerStep;
constexpr double kTickEpsilon = 1.0e-9;

template <typename T>
constexpr T clampValue(T value, T low, T high) noexcept {
    return value < low ? low : (value > high ? high : value);
}

bool scheduledValueSupersedes(std::uint64_t newerOrder, double newerTick,
                              std::uint64_t olderOrder, double olderTick) noexcept {
    // A later-submitted change replaces an older change at the same boundary,
    // or an older future change when the new request is immediate. If the
    // newer request targets a later boundary, retain both chronological events.
    return newerOrder > olderOrder && newerTick <= olderTick + kTickEpsilon;
}

std::uint32_t xorshift32(std::uint32_t& state) noexcept {
    if (state == 0u) state = 0x6D2B79F5u;
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return state;
}

double midiFrequency(double note) noexcept {
    note = clampValue(note, -24.0, 151.0);
    return 440.0 * std::exp2((note - 69.0) / 12.0);
}

double attackSeconds(std::uint8_t value) noexcept {
    if (value == 0u) return 0.0;
    const double normalized = static_cast<double>(value) / 127.0;
    return 0.0015 + normalized * normalized * 2.5;
}

double holdSeconds(std::uint8_t value) noexcept {
    if (value == 0u) return 0.0;
    return 0.004 + static_cast<double>(value) * (1.45 / 127.0);
}

double releaseSeconds(std::uint8_t value) noexcept {
    if (value == 0u) return 0.0;
    const double normalized = static_cast<double>(value) / 127.0;
    return 0.006 + normalized * normalized * 3.5;
}

double modSeconds(std::uint8_t value) noexcept {
    if (value == 0u) return 0.0;
    const double normalized = static_cast<double>(value) / 127.0;
    return 0.001 + normalized * normalized * 2.0;
}

double sweepSeconds(std::uint8_t value) noexcept {
    if (value == 0u) return 0.0;
    const double normalized = static_cast<double>(value) / 127.0;
    return 0.002 + normalized * normalized * 2.0;
}

double portamentoSeconds(std::uint8_t value) noexcept {
    if (value == 0u) return 0.0;
    const double normalized = static_cast<double>(value) / 127.0;
    return 0.003 + normalized * normalized * 2.25;
}

std::uint64_t secondsToFrames(double seconds, int sampleRate) noexcept {
    if (seconds <= 0.0 || sampleRate <= 0) return 0u;
    const double frames = seconds * static_cast<double>(sampleRate);
    return static_cast<std::uint64_t>(std::max(1.0, std::round(frames)));
}

float levelGain(std::uint8_t level) noexcept {
    const float normalized = static_cast<float>(level) / 127.0f;
    return normalized * normalized;
}

void panGains(Pan pan, float& left, float& right) noexcept {
    constexpr float centerGain = 0.70710678118f;
    switch (pan) {
        case Pan::Left:
            left = 1.0f;
            right = 0.0f;
            break;
        case Pan::Right:
            left = 0.0f;
            right = 1.0f;
            break;
        case Pan::Center:
        default:
            left = centerGain;
            right = centerGain;
            break;
    }
}

struct AmpEnvelope {
    enum class Stage : std::uint8_t { Off, Attack, Hold, Release };

    Stage stage = Stage::Off;
    std::uint64_t elapsed = 0u;
    float level = 0.0f;
    float releaseStart = 0.0f;

    void trigger(std::uint64_t attackFrames) noexcept {
        elapsed = 0u;
        releaseStart = 1.0f;
        if (attackFrames == 0u) {
            stage = Stage::Hold;
            level = 1.0f;
        } else {
            stage = Stage::Attack;
            level = 0.0f;
        }
    }

    void silence() noexcept {
        stage = Stage::Off;
        elapsed = 0u;
        level = 0.0f;
        releaseStart = 0.0f;
    }

    bool active() const noexcept { return stage != Stage::Off; }

    float tick(std::uint64_t attackFrames, std::uint64_t holdFrames,
               std::uint64_t releaseFrames) noexcept {
        switch (stage) {
            case Stage::Off:
                return 0.0f;

            case Stage::Attack: {
                if (attackFrames == 0u) {
                    stage = Stage::Hold;
                    elapsed = 0u;
                    level = 1.0f;
                    return level;
                }
                ++elapsed;
                const double fraction = static_cast<double>(elapsed) /
                                        static_cast<double>(attackFrames);
                level = static_cast<float>(clampValue(fraction, 0.0, 1.0));
                if (elapsed >= attackFrames) {
                    stage = Stage::Hold;
                    elapsed = 0u;
                    level = 1.0f;
                }
                return level;
            }

            case Stage::Hold:
                level = 1.0f;
                if (elapsed >= holdFrames) {
                    stage = Stage::Release;
                    elapsed = 0u;
                    releaseStart = level;
                    if (releaseFrames == 0u) silence();
                } else {
                    ++elapsed;
                }
                return level;

            case Stage::Release: {
                if (releaseFrames == 0u) {
                    silence();
                    return 0.0f;
                }
                ++elapsed;
                const double fraction = static_cast<double>(elapsed) /
                                        static_cast<double>(releaseFrames);
                level = releaseStart *
                        static_cast<float>(1.0 - clampValue(fraction, 0.0, 1.0));
                if (elapsed >= releaseFrames || level <= 0.000001f) silence();
                return level;
            }
        }
        silence();
        return 0.0f;
    }
};

struct AdsrRuntime {
    enum class Stage : std::uint8_t { Off, Attack, Decay, Sustain, Release };

    Stage stage = Stage::Off;
    std::uint64_t elapsed = 0u;
    float level = 0.0f;
    float releaseStart = 0.0f;

    void trigger(std::uint64_t attackFrames) noexcept {
        elapsed = 0u;
        releaseStart = 1.0f;
        if (attackFrames == 0u) {
            stage = Stage::Decay;
            level = 1.0f;
        } else {
            stage = Stage::Attack;
            level = 0.0f;
        }
    }

    void silence() noexcept {
        stage = Stage::Off;
        elapsed = 0u;
        level = 0.0f;
        releaseStart = 0.0f;
    }

    bool active() const noexcept { return stage != Stage::Off; }

    float tick(std::uint64_t attackFrames, std::uint64_t decayFrames,
               std::uint64_t sustainFrames, std::uint64_t releaseFrames,
               float sustainLevel) noexcept {
        sustainLevel = clampValue(sustainLevel, 0.0f, 1.0f);
        switch (stage) {
            case Stage::Off:
                return 0.0f;

            case Stage::Attack: {
                ++elapsed;
                const double fraction = attackFrames == 0u
                                            ? 1.0
                                            : static_cast<double>(elapsed) /
                                                  static_cast<double>(attackFrames);
                level = static_cast<float>(clampValue(fraction, 0.0, 1.0));
                if (elapsed >= attackFrames) {
                    stage = Stage::Decay;
                    elapsed = 0u;
                    level = 1.0f;
                }
                return level;
            }

            case Stage::Decay: {
                if (decayFrames == 0u) {
                    stage = Stage::Sustain;
                    elapsed = 0u;
                    level = sustainLevel;
                    return level;
                }
                ++elapsed;
                const float fraction = static_cast<float>(clampValue(
                    static_cast<double>(elapsed) / static_cast<double>(decayFrames), 0.0, 1.0));
                level = 1.0f + (sustainLevel - 1.0f) * fraction;
                if (elapsed >= decayFrames) {
                    stage = Stage::Sustain;
                    elapsed = 0u;
                    level = sustainLevel;
                }
                return level;
            }

            case Stage::Sustain:
                level = sustainLevel;
                if (elapsed >= sustainFrames) {
                    stage = Stage::Release;
                    elapsed = 0u;
                    releaseStart = level;
                    if (releaseFrames == 0u) silence();
                } else {
                    ++elapsed;
                }
                return level;

            case Stage::Release: {
                if (releaseFrames == 0u) {
                    silence();
                    return 0.0f;
                }
                ++elapsed;
                const double fraction = static_cast<double>(elapsed) /
                                        static_cast<double>(releaseFrames);
                level = releaseStart *
                        static_cast<float>(1.0 - clampValue(fraction, 0.0, 1.0));
                if (elapsed >= releaseFrames || level <= 0.000001f) silence();
                return level;
            }
        }
        silence();
        return 0.0f;
    }
};

struct AdvancedOperatorState {
    double phase = 0.0;
    float previous = 0.0f;
    std::uint32_t noiseState = 1u;
    float noiseValue = 0.0f;
};

struct AdvancedUnisonState {
    std::array<AdvancedOperatorState, 4> operators {};
};

struct AdvancedModRuntime {
    double phase = 0.0;
    std::uint32_t randomState = 1u;
    float heldValue = 0.0f;
};

struct AdvancedFilterChannel {
    float integrator1 = 0.0f;
    float integrator2 = 0.0f;

    void reset() noexcept {
        integrator1 = 0.0f;
        integrator2 = 0.0f;
    }

    float process(float input, AdvancedFilterMode mode, float cutoff,
                  float resonance, int sampleRate) noexcept {
        if (mode == AdvancedFilterMode::Off || sampleRate <= 0) return input;
        cutoff = clampValue(cutoff, 20.0f,
                            static_cast<float>(sampleRate) * 0.45f);
        resonance = clampValue(resonance, 0.0f, 1.0f);
        const float g = std::tan(static_cast<float>(kPi) * cutoff /
                                 static_cast<float>(sampleRate));
        const float damping = 2.0f - resonance * 1.98f;
        const float denominator = 1.0f + g * (g + damping);
        const float a1 = denominator > 0.000001f ? 1.0f / denominator : 0.0f;
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float v3 = input - integrator2;
        const float band = a1 * integrator1 + a2 * v3;
        const float low = integrator2 + a2 * integrator1 + a3 * v3;
        integrator1 = clampValue(2.0f * band - integrator1, -16.0f, 16.0f);
        integrator2 = clampValue(2.0f * low - integrator2, -16.0f, 16.0f);
        const float high = input - damping * band - low;
        float result = input;
        switch (mode) {
            case AdvancedFilterMode::Off: result = input; break;
            case AdvancedFilterMode::LowPass: result = low; break;
            case AdvancedFilterMode::HighPass: result = high; break;
            case AdvancedFilterMode::BandPass: result = band; break;
            case AdvancedFilterMode::Notch: result = low + high; break;
        }
        if (!std::isfinite(result)) {
            reset();
            return 0.0f;
        }
        return clampValue(result, -16.0f, 16.0f);
    }
};

struct AdvancedFilterState {
    AdvancedFilterChannel left {};
    AdvancedFilterChannel right {};

    void reset() noexcept {
        left.reset();
        right.reset();
    }
};

struct AdvancedModValues {
    double pitch = 0.0;
    float level = 1.0f;
    float pan = 0.0f;
    float filterCutoff = 0.0f;
    float resonance = 0.0f;
    float drive = 0.0f;
    std::array<float, 4> operatorLevel {};
    std::array<double, 4> operatorRatio {};
};

float advancedWaveform(AdvancedOscShape shape, double phase, float noiseValue) noexcept {
    phase = std::fmod(phase, kTau);
    if (phase < 0.0) phase += kTau;
    const double normalized = phase / kTau;
    switch (shape) {
        case AdvancedOscShape::Sine:
            return static_cast<float>(std::sin(phase));
        case AdvancedOscShape::Triangle:
            return static_cast<float>(1.0 - 4.0 * std::fabs(normalized - 0.5));
        case AdvancedOscShape::Saw:
            return static_cast<float>(normalized * 2.0 - 1.0);
        case AdvancedOscShape::Square:
            return normalized < 0.5 ? 1.0f : -1.0f;
        case AdvancedOscShape::Pulse:
            return normalized < 0.25 ? 1.0f : -1.0f;
        case AdvancedOscShape::Noise:
            return noiseValue;
    }
    return 0.0f;
}

float applyAdvancedDrive(float input, AdvancedDriveMode mode, float amount) noexcept {
    amount = clampValue(amount, 0.0f, 1.0f);
    if (mode == AdvancedDriveMode::Off || amount <= 0.0f) return input;
    const float gain = 1.0f + amount * 15.0f;
    const float driven = clampValue(input, -16.0f, 16.0f) * gain;
    float output = driven;
    switch (mode) {
        case AdvancedDriveMode::Off:
            output = input;
            break;
        case AdvancedDriveMode::SoftClip: {
            const float normalization = std::tanh(gain);
            output = normalization > 0.000001f ? std::tanh(driven) / normalization
                                               : driven;
            break;
        }
        case AdvancedDriveMode::HardClip:
            output = clampValue(driven, -1.0f, 1.0f);
            break;
        case AdvancedDriveMode::Wavefold: {
            float wrapped = std::fmod(driven + 1.0f, 4.0f);
            if (wrapped < 0.0f) wrapped += 4.0f;
            if (wrapped > 2.0f) wrapped = 4.0f - wrapped;
            output = wrapped - 1.0f;
            break;
        }
    }
    return std::isfinite(output) ? clampValue(output, -4.0f, 4.0f) : 0.0f;
}

struct FmVoice {
    bool active = false;
    bool chordVoice = false;
    bool preview = false;
    int ownerTrack = -1;
    int noteOffset = 0;
    Step step {};
    AmpEnvelope amp {};
    double carrierPhase = 0.0;
    double modPhase = 0.0;
    double currentMidi = 60.0;
    double targetMidi = 60.0;
    double glideIncrement = 0.0;
    std::uint64_t glideRemaining = 0u;
    std::uint64_t age = 0u;
    std::uint64_t attackFrames = 0u;
    std::uint64_t holdFrames = 0u;
    std::uint64_t releaseFrames = 0u;
    std::uint64_t modAttackFrames = 0u;
    std::uint64_t modReleaseFrames = 0u;
    std::uint64_t sweepFrames = 0u;
    float previousMod = 0.0f;
    float previousCarrier = 0.0f;
    AdsrRuntime advancedAmp {};
    std::uint64_t advancedAttackFrames = 0u;
    std::uint64_t advancedDecayFrames = 0u;
    std::uint64_t advancedSustainFrames = 0u;
    std::uint64_t advancedReleaseFrames = 0u;
    std::array<AdvancedUnisonState, 4> advancedUnison {};
    std::array<AdvancedModRuntime, 4> advancedModulation {};
    AdvancedFilterState advancedFilter {};

    void updateDurations(int sampleRate) noexcept {
        attackFrames = secondsToFrames(attackSeconds(step.fm.ampAttack), sampleRate);
        holdFrames = secondsToFrames(holdSeconds(step.fm.ampHold), sampleRate);
        releaseFrames = secondsToFrames(releaseSeconds(step.fm.ampRelease), sampleRate);
        modAttackFrames = secondsToFrames(modSeconds(step.fm.modAttack), sampleRate);
        modReleaseFrames = secondsToFrames(modSeconds(step.fm.modRelease), sampleRate);
        sweepFrames = secondsToFrames(sweepSeconds(step.fm.sweepRelease), sampleRate);
        advancedAttackFrames = secondsToFrames(
            attackSeconds(step.advancedFm.ampEnvelope.attack), sampleRate);
        advancedDecayFrames = secondsToFrames(
            releaseSeconds(step.advancedFm.ampEnvelope.decay), sampleRate);
        advancedSustainFrames = secondsToFrames(holdSeconds(step.fm.ampHold), sampleRate);
        advancedReleaseFrames = secondsToFrames(
            releaseSeconds(step.advancedFm.ampEnvelope.release), sampleRate);
    }

    void resetAdvancedState(int track, int midi) noexcept {
        advancedFilter.reset();
        for (std::size_t voice = 0u; voice < advancedUnison.size(); ++voice) {
            for (std::size_t op = 0u;
                 op < advancedUnison[voice].operators.size(); ++op) {
                AdvancedOperatorState& state = advancedUnison[voice].operators[op];
                state.phase = 0.0;
                state.previous = 0.0f;
                state.noiseState = 0x9E3779B9u ^
                    static_cast<std::uint32_t>((track + 1) * 0x45D9F3Bu) ^
                    static_cast<std::uint32_t>((midi + 1) * 0x27D4EB2Du) ^
                    static_cast<std::uint32_t>((voice + 1u) * 0x85EBCA6Bu) ^
                    static_cast<std::uint32_t>((op + 1u) * 0xC2B2AE35u);
                const std::uint32_t bits = xorshift32(state.noiseState);
                state.noiseValue = static_cast<float>(bits & 0xFFFFu) / 32767.5f - 1.0f;
            }
        }
        for (std::size_t index = 0u; index < advancedModulation.size(); ++index) {
            AdvancedModRuntime& runtime = advancedModulation[index];
            runtime.phase = 0.0;
            runtime.randomState = 0xA341316Cu ^
                static_cast<std::uint32_t>((track + 1) * 0x9E3779B9u) ^
                static_cast<std::uint32_t>((midi + 1) * 0x7F4A7C15u) ^
                static_cast<std::uint32_t>((index + 1u) * 0x6D2B79F5u);
            const std::uint32_t bits = xorshift32(runtime.randomState);
            runtime.heldValue = static_cast<float>(bits & 0xFFFFu) / 32767.5f - 1.0f;
        }
    }

    void stop() noexcept {
        active = false;
        chordVoice = false;
        preview = false;
        ownerTrack = -1;
        amp.silence();
        advancedAmp.silence();
        advancedFilter.reset();
        previousMod = 0.0f;
        previousCarrier = 0.0f;
    }

    void setPitch(double midi, std::uint8_t portamento, bool mayGlide,
                  int sampleRate) noexcept {
        targetMidi = clampValue(midi, 0.0, 127.0);
        const std::uint64_t glideFrames =
            secondsToFrames(portamentoSeconds(portamento), sampleRate);
        if (!mayGlide || glideFrames == 0u || !std::isfinite(currentMidi)) {
            currentMidi = targetMidi;
            glideRemaining = 0u;
            glideIncrement = 0.0;
            return;
        }
        glideRemaining = glideFrames;
        glideIncrement = (targetMidi - currentMidi) / static_cast<double>(glideFrames);
    }

    void trigger(int track, const Step& newStep, double midi, int offset,
                 bool isChord, bool isPreview, int sampleRate) noexcept {
        const bool mayGlide = active && ownerTrack == track;
        step = newStep;
        ownerTrack = track;
        noteOffset = offset;
        chordVoice = isChord;
        preview = isPreview;
        updateDurations(sampleRate);
        setPitch(midi, step.portamento, mayGlide, sampleRate);
        carrierPhase = 0.0;
        modPhase = 0.0;
        previousMod = 0.0f;
        previousCarrier = 0.0f;
        age = 0u;
        amp.trigger(attackFrames);
        advancedAmp.trigger(advancedAttackFrames);
        resetAdvancedState(track, static_cast<int>(std::lround(midi)));
        active = true;
    }

    void change(const Step& newStep, double midi, int sampleRate) noexcept {
        if (!active) return;
        const bool wasAdvanced = step.advancedFm.enabled;
        step = newStep;
        updateDurations(sampleRate);
        setPitch(midi, step.portamento, true, sampleRate);
        if (step.advancedFm.enabled && !wasAdvanced) {
            advancedAmp.trigger(advancedAttackFrames);
            resetAdvancedState(ownerTrack, static_cast<int>(std::lround(midi)));
        } else if (!step.advancedFm.enabled && wasAdvanced && !amp.active()) {
            amp.trigger(attackFrames);
        }
    }

    float sampleAdvancedModSource(std::size_t index, const AdvancedModSlot& slot,
                                  float envelopeLevel, int sampleRate) noexcept {
        if (index >= advancedModulation.size() || sampleRate <= 0) return 0.0f;
        AdvancedModRuntime& runtime = advancedModulation[index];
        if (slot.source == AdvancedModSource::Off) return 0.0f;
        if (slot.source == AdvancedModSource::AmpEnvelope) return envelopeLevel;

        const double normalizedRate = static_cast<double>(slot.rate) / 127.0;
        const double frequency = 0.05 * std::exp2(normalizedRate * std::log2(600.0));
        const double phase = runtime.phase;
        float value = 0.0f;
        switch (slot.source) {
            case AdvancedModSource::Off:
                value = 0.0f;
                break;
            case AdvancedModSource::SineLfo:
                value = static_cast<float>(std::sin(kTau * phase));
                break;
            case AdvancedModSource::TriangleLfo:
                value = static_cast<float>(1.0 - 4.0 * std::fabs(phase - 0.5));
                break;
            case AdvancedModSource::SawLfo:
                value = static_cast<float>(phase * 2.0 - 1.0);
                break;
            case AdvancedModSource::SquareLfo:
                value = phase < 0.5 ? 1.0f : -1.0f;
                break;
            case AdvancedModSource::SampleAndHold:
                value = runtime.heldValue;
                break;
            case AdvancedModSource::AmpEnvelope:
                value = envelopeLevel;
                break;
        }

        runtime.phase += frequency / static_cast<double>(sampleRate);
        if (runtime.phase >= 1.0) {
            runtime.phase -= std::floor(runtime.phase);
            if (slot.source == AdvancedModSource::SampleAndHold) {
                const std::uint32_t bits = xorshift32(runtime.randomState);
                runtime.heldValue =
                    static_cast<float>(bits & 0xFFFFu) / 32767.5f - 1.0f;
            }
        }
        return std::isfinite(value) ? clampValue(value, -1.0f, 1.0f) : 0.0f;
    }

    AdvancedModValues advancedModValues(float envelopeLevel, int sampleRate) noexcept {
        AdvancedModValues values;
        for (std::size_t index = 0u; index < step.advancedFm.modulation.size(); ++index) {
            const AdvancedModSlot& slot = step.advancedFm.modulation[index];
            if (slot.source == AdvancedModSource::Off ||
                slot.destination == AdvancedModDestination::None || slot.depth == 0) {
                continue;
            }
            const float source = sampleAdvancedModSource(index, slot, envelopeLevel, sampleRate);
            const float amount = source * static_cast<float>(slot.depth) / 127.0f;
            switch (slot.destination) {
                case AdvancedModDestination::None: break;
                case AdvancedModDestination::Pitch:
                    values.pitch += static_cast<double>(amount) * 24.0;
                    break;
                case AdvancedModDestination::Level:
                    values.level += amount;
                    break;
                case AdvancedModDestination::Pan:
                    values.pan += amount;
                    break;
                case AdvancedModDestination::FilterCutoff:
                    values.filterCutoff += amount * 64.0f;
                    break;
                case AdvancedModDestination::Resonance:
                    values.resonance += amount * 64.0f;
                    break;
                case AdvancedModDestination::Drive:
                    values.drive += amount * 64.0f;
                    break;
                case AdvancedModDestination::Operator1Level:
                case AdvancedModDestination::Operator2Level:
                case AdvancedModDestination::Operator3Level:
                case AdvancedModDestination::Operator4Level: {
                    const std::size_t op = static_cast<std::size_t>(slot.destination) -
                        static_cast<std::size_t>(AdvancedModDestination::Operator1Level);
                    values.operatorLevel[op] += amount * 127.0f;
                    break;
                }
                case AdvancedModDestination::Operator1Ratio:
                case AdvancedModDestination::Operator2Ratio:
                case AdvancedModDestination::Operator3Ratio:
                case AdvancedModDestination::Operator4Ratio: {
                    const std::size_t op = static_cast<std::size_t>(slot.destination) -
                        static_cast<std::size_t>(AdvancedModDestination::Operator1Ratio);
                    values.operatorRatio[op] += static_cast<double>(amount) * 4.0;
                    break;
                }
            }
        }
        values.pitch = clampValue(values.pitch, -48.0, 48.0);
        values.level = clampValue(values.level, 0.0f, 2.0f);
        values.pan = clampValue(values.pan, -1.0f, 1.0f);
        return values;
    }

    float renderAdvancedAlgorithm(std::size_t unisonIndex, double midi,
                                  const AdvancedModValues& modulation,
                                  int sampleRate) noexcept {
        if (unisonIndex >= advancedUnison.size() || sampleRate <= 0) return 0.0f;
        AdvancedUnisonState& unison = advancedUnison[unisonIndex];
        const AdvancedFmPatch& patch = step.advancedFm;

        const auto renderOperator = [&](std::size_t index, float phaseModulation) noexcept {
            AdvancedOperatorState& state = unison.operators[index];
            const AdvancedFmOperator& op = patch.operators[index];
            const double detuneSemitones = static_cast<double>(op.detune) / 64.0;
            const double ratio = clampValue(
                fmRatio(op.ratio) + modulation.operatorRatio[index], 0.0, 32.0);
            const double oscillatorFrequency = clampValue(
                midiFrequency(midi + detuneSemitones) * ratio, 0.0,
                static_cast<double>(sampleRate) * 0.45);
            const float feedback = static_cast<float>(op.feedback) * (5.0f / 127.0f);
            const float operatorLevel = clampValue(
                static_cast<float>(op.level) + modulation.operatorLevel[index],
                0.0f, 127.0f) / 127.0f;
            const double phase = state.phase + static_cast<double>(phaseModulation) * 8.0 +
                                 static_cast<double>(state.previous * feedback);
            float value = advancedWaveform(op.shape, phase, state.noiseValue) * operatorLevel;
            if (!std::isfinite(value)) value = 0.0f;
            value = clampValue(value, -2.0f, 2.0f);
            state.previous = value;

            state.phase += kTau * oscillatorFrequency / static_cast<double>(sampleRate);
            if (state.phase >= kTau) {
                state.phase = std::fmod(state.phase, kTau);
                if (op.shape == AdvancedOscShape::Noise) {
                    const std::uint32_t bits = xorshift32(state.noiseState);
                    state.noiseValue =
                        static_cast<float>(bits & 0xFFFFu) / 32767.5f - 1.0f;
                }
            }
            return value;
        };

        float a = 0.0f;
        float b = 0.0f;
        float c = 0.0f;
        float d = 0.0f;
        float output = 0.0f;
        switch (patch.algorithm) {
            case AdvancedFmAlgorithm::Algorithm1:
                a = renderOperator(0u, 0.0f);
                b = renderOperator(1u, a);
                c = renderOperator(2u, b);
                output = renderOperator(3u, c);
                break;
            case AdvancedFmAlgorithm::Algorithm2:
                a = renderOperator(0u, 0.0f);
                b = renderOperator(1u, 0.0f);
                c = renderOperator(2u, (a + b) * 0.5f);
                output = renderOperator(3u, c);
                break;
            case AdvancedFmAlgorithm::Algorithm3:
                a = renderOperator(0u, 0.0f);
                b = renderOperator(1u, a);
                c = renderOperator(2u, 0.0f);
                d = renderOperator(3u, c);
                output = (b + d) * 0.5f;
                break;
            case AdvancedFmAlgorithm::Algorithm4:
                a = renderOperator(0u, 0.0f);
                b = renderOperator(1u, 0.0f);
                c = renderOperator(2u, 0.0f);
                output = renderOperator(3u, (a + b + c) / 3.0f);
                break;
            case AdvancedFmAlgorithm::Algorithm5:
                a = renderOperator(0u, 0.0f);
                b = renderOperator(1u, a);
                c = renderOperator(2u, 0.0f);
                output = renderOperator(3u, (b + c) * 0.5f);
                break;
            case AdvancedFmAlgorithm::Algorithm6:
                a = renderOperator(0u, 0.0f);
                b = renderOperator(1u, a);
                c = renderOperator(2u, b);
                d = renderOperator(3u, b);
                output = (c + d) * 0.5f;
                break;
            case AdvancedFmAlgorithm::Algorithm7:
                a = renderOperator(0u, 0.0f);
                b = renderOperator(1u, a);
                c = renderOperator(2u, a);
                d = renderOperator(3u, a);
                output = (b + c + d) / 3.0f;
                break;
            case AdvancedFmAlgorithm::Algorithm8:
                a = renderOperator(0u, 0.0f);
                b = renderOperator(1u, 0.0f);
                c = renderOperator(2u, (a + b) * 0.5f);
                d = renderOperator(3u, (a + b) * 0.5f);
                output = (c + d) * 0.5f;
                break;
            case AdvancedFmAlgorithm::Algorithm9:
                a = renderOperator(0u, 0.0f);
                b = renderOperator(1u, a);
                c = renderOperator(2u, b);
                d = renderOperator(3u, 0.0f);
                output = (c + d) * 0.5f;
                break;
            case AdvancedFmAlgorithm::Algorithm10:
                a = renderOperator(0u, 0.0f);
                b = renderOperator(1u, a);
                c = renderOperator(2u, 0.0f);
                d = renderOperator(3u, 0.0f);
                output = (b + c + d) / 3.0f;
                break;
            case AdvancedFmAlgorithm::Algorithm11:
                a = renderOperator(0u, 0.0f);
                b = renderOperator(1u, 0.0f);
                c = renderOperator(2u, (a + b) * 0.5f);
                d = renderOperator(3u, c);
                output = (a + d) * 0.5f;
                break;
            case AdvancedFmAlgorithm::Algorithm12:
                a = renderOperator(0u, 0.0f);
                b = renderOperator(1u, 0.0f);
                c = renderOperator(2u, 0.0f);
                d = renderOperator(3u, 0.0f);
                output = (a + b + c + d) * 0.25f;
                break;
        }
        return std::isfinite(output) ? clampValue(output, -4.0f, 4.0f) : 0.0f;
    }

    void renderAdvancedStereo(int sampleRate, float& left, float& right) noexcept {
        left = 0.0f;
        right = 0.0f;
        if (!active || sampleRate <= 0) return;

        if (glideRemaining > 0u) {
            currentMidi += glideIncrement;
            --glideRemaining;
            if (glideRemaining == 0u) currentMidi = targetMidi;
        }

        const float sustain = static_cast<float>(step.advancedFm.ampEnvelope.sustain) / 127.0f;
        const float envelopeLevel = advancedAmp.tick(
            advancedAttackFrames, advancedDecayFrames, advancedSustainFrames,
            advancedReleaseFrames, sustain);
        if (!advancedAmp.active()) {
            stop();
            return;
        }

        const AdvancedModValues modulation = advancedModValues(envelopeLevel, sampleRate);
        const int voiceCount = clampValue(static_cast<int>(step.advancedFm.unisonVoices), 1, 4);
        const float width = static_cast<float>(step.advancedFm.unisonWidth) / 127.0f;
        const double detuneSemitones =
            static_cast<double>(step.advancedFm.unisonDetune) / 127.0 * 0.5;
        for (int voice = 0; voice < voiceCount; ++voice) {
            const float position = voiceCount <= 1
                                       ? 0.0f
                                       : -1.0f + 2.0f * static_cast<float>(voice) /
                                                     static_cast<float>(voiceCount - 1);
            const double voiceMidi = currentMidi + modulation.pitch +
                                     static_cast<double>(position) * detuneSemitones;
            const float sample = renderAdvancedAlgorithm(
                static_cast<std::size_t>(voice), voiceMidi, modulation, sampleRate);
            const float pan = clampValue(position * width + modulation.pan, -1.0f, 1.0f);
            const float angle = (pan + 1.0f) * static_cast<float>(kPi) * 0.25f;
            left += sample * std::cos(angle);
            right += sample * std::sin(angle);
        }

        const float gain = envelopeLevel * levelGain(step.level) * modulation.level /
                           static_cast<float>(voiceCount);
        left *= gain;
        right *= gain;
        const float drive = clampValue(
            (static_cast<float>(step.advancedFm.driveAmount) + modulation.drive) / 127.0f,
            0.0f, 1.0f);
        left = applyAdvancedDrive(left, step.advancedFm.driveMode, drive);
        right = applyAdvancedDrive(right, step.advancedFm.driveMode, drive);

        const float cutoffParameter = clampValue(
            static_cast<float>(step.advancedFm.filterCutoff) + modulation.filterCutoff,
            0.0f, 127.0f);
        const float cutoff = 20.0f * std::exp2(cutoffParameter / 127.0f *
                                               std::log2(1000.0f));
        const float resonance = clampValue(
            (static_cast<float>(step.advancedFm.resonance) + modulation.resonance) / 127.0f,
            0.0f, 1.0f);
        left = advancedFilter.left.process(left, step.advancedFm.filterMode, cutoff,
                                           resonance, sampleRate);
        right = advancedFilter.right.process(right, step.advancedFm.filterMode, cutoff,
                                              resonance, sampleRate);

        if (step.pan == Pan::Left) {
            left = (left + right) * 0.70710678118f;
            right = 0.0f;
        } else if (step.pan == Pan::Right) {
            right = (left + right) * 0.70710678118f;
            left = 0.0f;
        }
        if (!std::isfinite(left)) left = 0.0f;
        if (!std::isfinite(right)) right = 0.0f;
        left = clampValue(left, -8.0f, 8.0f);
        right = clampValue(right, -8.0f, 8.0f);
        ++age;
    }

    void renderStereo(int sampleRate, float& left, float& right) noexcept {
        if (step.advancedFm.enabled) {
            renderAdvancedStereo(sampleRate, left, right);
            return;
        }
        const float sample = render(sampleRate);
        float leftGain = 0.0f;
        float rightGain = 0.0f;
        panGains(step.pan, leftGain, rightGain);
        left = sample * leftGain;
        right = sample * rightGain;
    }

    float render(int sampleRate) noexcept {
        if (!active || sampleRate <= 0) return 0.0f;

        if (glideRemaining > 0u) {
            currentMidi += glideIncrement;
            --glideRemaining;
            if (glideRemaining == 0u) currentMidi = targetMidi;
        }

        const float ampLevel = amp.tick(attackFrames, holdFrames, releaseFrames);
        if (!amp.active()) {
            stop();
            return 0.0f;
        }

        const float modEnd = static_cast<float>(step.fm.modEnd) / 127.0f;
        float modEnvelope = modEnd;
        if (modAttackFrames > 0u && age < modAttackFrames) {
            modEnvelope = static_cast<float>(static_cast<double>(age) /
                                             static_cast<double>(modAttackFrames));
        } else {
            const std::uint64_t releaseAge =
                age > modAttackFrames ? age - modAttackFrames : 0u;
            if (modReleaseFrames > 0u && releaseAge < modReleaseFrames) {
                const float fraction = static_cast<float>(
                    static_cast<double>(releaseAge) / static_cast<double>(modReleaseFrames));
                modEnvelope = 1.0f + (modEnd - 1.0f) * fraction;
            }
        }

        double sweep = 0.0;
        if (sweepFrames > 0u && age < sweepFrames) {
            const double remaining = 1.0 - static_cast<double>(age) /
                                               static_cast<double>(sweepFrames);
            sweep = static_cast<double>(step.fm.sweepDepth) * remaining;
        }
        const double frequency = midiFrequency(currentMidi + sweep);
        const double ratio = clampValue(fmRatio(step.fm.modRatio), 0.0, 31.0);
        const double carrierIncrement = kTau * frequency / static_cast<double>(sampleRate);
        const double modIncrement = carrierIncrement * ratio;
        const float feedback = static_cast<float>(step.fm.modFeedback) * (5.0f / 127.0f);
        const float depth = static_cast<float>(step.fm.modDepth) * (13.0f / 127.0f);

        float sample = 0.0f;
        if (step.mode == SynthMode::Parallel) {
            const float carrier = static_cast<float>(
                std::sin(carrierPhase + static_cast<double>(previousCarrier * feedback)));
            const float modulator = static_cast<float>(
                std::sin(modPhase + static_cast<double>(previousMod * feedback)));
            previousCarrier = carrier;
            previousMod = modulator;
            sample = 0.5f * (carrier + modulator) * modEnvelope;
        } else {
            const float modulator = static_cast<float>(
                std::sin(modPhase + static_cast<double>(previousMod * feedback)));
            previousMod = modulator;
            sample = static_cast<float>(
                std::sin(carrierPhase + static_cast<double>(modulator * depth * modEnvelope)));
            previousCarrier = sample;
        }

        carrierPhase += carrierIncrement;
        modPhase += modIncrement;
        if (carrierPhase >= kTau || carrierPhase <= -kTau)
            carrierPhase = std::fmod(carrierPhase, kTau);
        if (modPhase >= kTau || modPhase <= -kTau) modPhase = std::fmod(modPhase, kTau);
        ++age;

        const float output = sample * ampLevel * levelGain(step.level);
        return std::isfinite(output) ? output : 0.0f;
    }
};

struct NoiseVoice {
    bool active = false;
    bool preview = false;
    int ownerTrack = 4;
    Step step {};
    AmpEnvelope amp {};
    std::uint16_t lfsr = 0x7FFFu;
    float heldSample = -1.0f;
    double phase = 0.0;
    double currentRate = 54.0;
    double targetRate = 54.0;
    double glideIncrement = 0.0;
    std::uint64_t glideRemaining = 0u;
    std::uint64_t attackFrames = 0u;
    std::uint64_t holdFrames = 0u;
    std::uint64_t releaseFrames = 0u;

    void updateDurations(int sampleRate) noexcept {
        attackFrames = secondsToFrames(attackSeconds(step.noise.ampAttack), sampleRate);
        holdFrames = secondsToFrames(holdSeconds(step.noise.ampHold), sampleRate);
        releaseFrames = secondsToFrames(releaseSeconds(step.noise.ampRelease), sampleRate);
    }

    void stop() noexcept {
        active = false;
        preview = false;
        amp.silence();
    }

    void setRate(double rate, std::uint8_t portamento, bool mayGlide,
                 int sampleRate) noexcept {
        targetRate = clampValue(rate, 0.0, 127.0);
        const std::uint64_t glideFrames =
            secondsToFrames(portamentoSeconds(portamento), sampleRate);
        if (!mayGlide || glideFrames == 0u || !std::isfinite(currentRate)) {
            currentRate = targetRate;
            glideRemaining = 0u;
            glideIncrement = 0.0;
            return;
        }
        glideRemaining = glideFrames;
        glideIncrement = (targetRate - currentRate) / static_cast<double>(glideFrames);
    }

    void trigger(const Step& newStep, double rate, bool isPreview, int sampleRate) noexcept {
        const bool mayGlide = active;
        step = newStep;
        preview = isPreview;
        ownerTrack = 4;
        updateDurations(sampleRate);
        setRate(rate, step.portamento, mayGlide, sampleRate);
        phase = 0.0;
        lfsr = 0x7FFFu;
        heldSample = (lfsr & 1u) != 0u ? -1.0f : 1.0f;
        amp.trigger(attackFrames);
        active = true;
    }

    void change(const Step& newStep, double rate, int sampleRate) noexcept {
        if (!active) return;
        step = newStep;
        updateDurations(sampleRate);
        setRate(rate, step.portamento, true, sampleRate);
    }

    float render(int sampleRate) noexcept {
        if (!active || sampleRate <= 0) return 0.0f;
        if (glideRemaining > 0u) {
            currentRate += glideIncrement;
            --glideRemaining;
            if (glideRemaining == 0u) currentRate = targetRate;
        }

        const float ampLevel = amp.tick(attackFrames, holdFrames, releaseFrames);
        if (!amp.active()) {
            stop();
            return 0.0f;
        }

        const double updateFrequency = clampValue(
            30.0 * std::exp2(currentRate / 15.0), 25.0,
            static_cast<double>(sampleRate) * 0.48);
        phase += updateFrequency / static_cast<double>(sampleRate);
        while (phase >= 1.0) {
            phase -= 1.0;
            const std::uint16_t feedback = static_cast<std::uint16_t>((lfsr ^ (lfsr >> 1u)) & 1u);
            lfsr = static_cast<std::uint16_t>((lfsr >> 1u) | (feedback << 14u));
            if (step.noise.narrow) {
                lfsr = static_cast<std::uint16_t>((lfsr & ~(1u << 6u)) | (feedback << 6u));
            }
            heldSample = (lfsr & 1u) != 0u ? -1.0f : 1.0f;
        }

        const float output = heldSample * ampLevel * levelGain(step.level);
        return std::isfinite(output) ? output : 0.0f;
    }
};

struct ScheduledStep {
    double nominalTick = 0.0;
    double dueTick = 0.0;
    Step step {};
    EchoSettings echo {};
    int stepIndex = 0;
    int transpose = 0;
    double rate = 1.0;
    std::uint64_t loop = 0u;
    std::uint64_t loopsAfter = 0u;
    std::uint64_t ordinal = 0u;
};

struct TrackRuntime {
    int cursor = 0;
    int pingDirection = 1;
    std::uint64_t ordinal = 0u;
    std::uint64_t scheduleLoop = 0u;
    std::uint64_t conditionLoop = 0u;
    std::uint64_t randomSteps = 0u;
    double nextNominalTick = 0.0;
    std::array<ScheduledStep, kPendingSteps> pending {};
    std::size_t pendingCount = 0u;
    int transposeIndex = 0;
    int transposeRepeats = 0;
    bool loopBoundaryPending = false;
    bool clampReplacementLead = false;
    double replacementBoundaryTick = 0.0;
};

struct ModRuntime {
    std::uint64_t counter = 0u;
    std::uint64_t randomCycle = std::numeric_limits<std::uint64_t>::max();
    float randomValue = 0.0f;
};

struct EchoEvent {
    double dueTick = 0.0;
    std::uint64_t order = 0u;
    int track = 0;
    int note = 60;
    Step step {};
};

bool echoComesBefore(const EchoEvent& left, const EchoEvent& right) noexcept {
    if (left.dueTick != right.dueTick) return left.dueTick < right.dueTick;
    if (left.track != right.track) return left.track < right.track;
    return left.order < right.order;
}

struct EchoHeap {
    std::array<EchoEvent, kEchoCapacity> events {};
    std::size_t size = 0u;

    void clear() noexcept { size = 0u; }

    void siftDown(std::size_t index) noexcept {
        for (;;) {
            const std::size_t left = index * 2u + 1u;
            if (left >= size) break;
            const std::size_t right = left + 1u;
            std::size_t child = left;
            if (right < size && echoComesBefore(events[right], events[left])) child = right;
            if (!echoComesBefore(events[child], events[index])) break;
            std::swap(events[index], events[child]);
            index = child;
        }
    }

    void removeTrack(int track) noexcept {
        std::size_t write = 0u;
        for (std::size_t read = 0u; read < size; ++read) {
            if (events[read].track == track) continue;
            if (write != read) events[write] = events[read];
            ++write;
        }
        size = write;
        for (std::size_t parent = size / 2u; parent > 0u; --parent) {
            siftDown(parent - 1u);
        }
    }

    bool push(const EchoEvent& event) noexcept {
        if (size >= events.size()) return false;
        std::size_t index = size++;
        events[index] = event;
        while (index > 0u) {
            const std::size_t parent = (index - 1u) / 2u;
            if (!echoComesBefore(events[index], events[parent])) break;
            std::swap(events[index], events[parent]);
            index = parent;
        }
        return true;
    }

    const EchoEvent* front() const noexcept {
        return size == 0u ? nullptr : &events[0];
    }

    EchoEvent pop() noexcept {
        EchoEvent result = events[0];
        --size;
        if (size == 0u) return result;
        events[0] = events[size];
        siftDown(0u);
        return result;
    }
};

struct PreviewCommand {
    int track = 0;
    int note = -1;
    Step step {};
};

struct PreviewSlot {
    std::atomic<std::size_t> sequence {0u};
    PreviewCommand command {};
};

enum class TrackCommandKind : std::uint8_t { Cue, ImmediateInPlace, ImmediateReset };

struct PatternCommand {
    int track = 0;
    std::uint64_t resetGeneration = 0u;
    std::uint64_t token = 0u;
    std::uint64_t order = 0u;
    TrackCommandKind kind = TrackCommandKind::Cue;
    TrackData pattern {};
};

struct PatternCommandSlot {
    std::atomic<std::size_t> sequence {0u};
    PatternCommand command {};
};

struct ArmedPattern {
    bool active = false;
    std::uint64_t resetGeneration = 0u;
    std::uint64_t token = 0u;
    std::uint64_t order = 0u;
    TrackCommandKind kind = TrackCommandKind::Cue;
    TrackData pattern {};
};

struct ColumnCommand {
    std::uint64_t resetGeneration = 0u;
    std::uint64_t token = 0u;
    std::uint64_t order = 0u;
    std::uint8_t trackMask = 0u;
    TrackCommandKind kind = TrackCommandKind::Cue;
    bool applyImmediately = false;
    TimedGlobalSettings settings {};
    std::array<TrackData, kTrackCount> patterns {};
};

struct ColumnCommandSlot {
    std::atomic<std::size_t> sequence {0u};
    ColumnCommand command {};
};

struct ColumnReceipt {
    std::uint64_t token = 0u;
    std::uint64_t order = 0u;
    std::uint8_t trackMask = 0u;
    bool tempo = false;
    bool scale = false;
    bool settled = false;
};

struct GlobalSettingsReceipt {
    std::uint64_t token = 0u;
    std::uint64_t order = 0u;
    bool tempo = false;
    bool scale = false;
    bool settled = false;
};

struct ArmedColumn {
    bool active = false;
    std::uint64_t resetGeneration = 0u;
    std::uint8_t trackMask = 0u;
    std::array<std::uint64_t, kTrackCount> trackOrders {};
    std::array<TrackCommandKind, kTrackCount> trackKinds {};
    double targetTick = 0.0;
    std::uint64_t tempoOrder = 0u;
    std::uint64_t scaleOrder = 0u;
    TimedGlobalSettings settings {};
    std::array<TrackData, kTrackCount> patterns {};
    std::array<ColumnReceipt, kColumnCommandCapacity> columnReceipts {};
    std::size_t columnReceiptCount = 0u;
    std::array<GlobalSettingsReceipt, kGlobalSettingsCommandCapacity>
        globalSettingsReceipts {};
    std::size_t globalSettingsReceiptCount = 0u;
};

struct GlobalSettingsCommand {
    std::uint64_t resetGeneration = 0u;
    std::uint64_t token = 0u;
    std::uint64_t order = 0u;
    bool applyImmediately = false;
    TimedGlobalSettings settings {};
};

struct GlobalSettingsCommandSlot {
    std::atomic<std::size_t> sequence {0u};
    GlobalSettingsCommand command {};
};

struct SettlementSlot {
    std::atomic<std::uint64_t> sequence {0u};
    std::atomic<std::uint64_t> token {0u};
    std::atomic<int> track {-1};
    std::atomic<std::uint8_t> family {
        static_cast<std::uint8_t>(TransportCommandFamily::Track)};
    std::atomic<std::uint8_t> outcome {
        static_cast<std::uint8_t>(TransportSettlementOutcome::Cancelled)};
    std::atomic<std::uint8_t> appliedTrackMask {0u};
    std::atomic<bool> appliedTempo {false};
    std::atomic<bool> appliedScale {false};
};

template <typename Slot, std::size_t Capacity, typename Command>
bool enqueueBounded(std::array<Slot, Capacity>& slots, std::atomic<std::size_t>& enqueue,
                    const Command& command) noexcept {
    std::size_t position = enqueue.load(std::memory_order_relaxed);
    for (;;) {
        Slot& slot = slots[position % slots.size()];
        const std::size_t sequence = slot.sequence.load(std::memory_order_acquire);
        const std::intptr_t difference = static_cast<std::intptr_t>(sequence) -
                                         static_cast<std::intptr_t>(position);
        if (difference == 0) {
            if (enqueue.compare_exchange_weak(position, position + 1u,
                                              std::memory_order_relaxed,
                                              std::memory_order_relaxed)) {
                slot.command = command;
                slot.sequence.store(position + 1u, std::memory_order_release);
                return true;
            }
        } else if (difference < 0) {
            return false;
        } else {
            position = enqueue.load(std::memory_order_relaxed);
        }
    }
}

template <typename Slot, std::size_t Capacity, typename Command>
bool dequeueBounded(std::array<Slot, Capacity>& slots, std::size_t& dequeue,
                    Command& command) noexcept {
    Slot& slot = slots[dequeue % slots.size()];
    const std::size_t sequence = slot.sequence.load(std::memory_order_acquire);
    const std::intptr_t difference = static_cast<std::intptr_t>(sequence) -
                                     static_cast<std::intptr_t>(dequeue + 1u);
    if (difference != 0) return false;
    command = slot.command;
    slot.sequence.store(dequeue + slots.size(), std::memory_order_release);
    ++dequeue;
    return true;
}

template <typename Slot, std::size_t Capacity, typename Command>
bool peekBounded(const std::array<Slot, Capacity>& slots, std::size_t dequeue,
                 Command& command) noexcept {
    const Slot& slot = slots[dequeue % slots.size()];
    const std::size_t sequence = slot.sequence.load(std::memory_order_acquire);
    const std::intptr_t difference = static_cast<std::intptr_t>(sequence) -
                                     static_cast<std::intptr_t>(dequeue + 1u);
    if (difference != 0) return false;
    command = slot.command;
    return true;
}

struct ModValues {
    int level = 0;
    int pan = 0;
    int note = 0;
    int modDepth = 0;
    int modFeedback = 0;
    int sweep = 0;
    int noiseRate = 0;
};

} // namespace

struct AudioEngine::Impl {
    SDL_AudioDeviceID device = 0u;
    SDL_AudioSpec obtained {};
    SharedState* shared = nullptr;
    PerformanceState performance {};
    int sampleRate = kDefaultSampleRate;
    std::string errorMessage;
    std::atomic<bool> isAvailable {false};
    std::atomic<bool> shouldRun {false};
    std::atomic<std::uint64_t> resetGeneration {1u};
    std::uint64_t observedResetGeneration = 0u;

    std::array<std::atomic<int>, kTrackCount> publishedPlayheads;
    std::array<std::atomic<std::uint64_t>, kTrackCount> publishedLoops;
    std::array<std::atomic<std::uint64_t>, kTrackCount> submittedPatternGenerations;
    std::array<std::atomic<std::uint64_t>, kTrackCount> publishedPatternGenerations;
    std::array<std::atomic<std::uint64_t>, kTrackCount> nextPatternTokens;
    std::atomic<std::uint64_t> submittedColumnGeneration {0u};
    std::atomic<std::uint64_t> publishedColumnGeneration {0u};
    std::atomic<std::uint64_t> nextColumnToken {0u};
    std::atomic<std::uint64_t> submittedGlobalSettingsGeneration {0u};
    std::atomic<std::uint64_t> publishedGlobalSettingsGeneration {0u};
    std::atomic<std::uint64_t> nextGlobalSettingsToken {0u};
    std::array<std::atomic<std::uint64_t>, kTrackCount>
        settledPatternGenerations;
    std::atomic<std::uint64_t> settledColumnGeneration {0u};
    std::atomic<std::uint64_t> settledGlobalSettingsGeneration {0u};
    std::array<SettlementSlot, kTransportSettlementCapacity> settlementSlots {};
    std::atomic<std::uint64_t> latestSettlementSequence {0u};
    std::uint64_t nextSettlementSequence = 0u;
    std::array<std::array<std::atomic<std::uint64_t>,
                          kTransportSettlementCapacity>,
               kTrackCount>
        requestedPatternCancellations {};
    std::array<std::atomic<std::uint64_t>, kTransportSettlementCapacity>
        requestedColumnCancellations {};
    std::array<std::atomic<std::uint64_t>, kTransportSettlementCapacity>
        requestedGlobalSettingsCancellations {};
    std::atomic<std::uint64_t> nextReplacementOrder {0u};
    std::atomic<float> publishedPeakLeft {0.0f};
    std::atomic<float> publishedPeakRight {0.0f};
    std::atomic<std::uint64_t> publishedFrames {0u};

    std::array<FmVoice, kFmTrackCount> fmVoices {};
    NoiseVoice noiseVoice {};
    std::array<TrackRuntime, kTrackCount> trackRuntime {};
    std::array<ModRuntime, kTrackCount> modRuntime {};
    EchoHeap echoes {};
    std::uint64_t echoOrder = 0u;
    double transportTick = 0.0;
    std::uint32_t randomState = 0xA341316Cu;

    std::array<PreviewSlot, kPreviewCapacity> previewSlots {};
    std::atomic<std::size_t> previewEnqueue {0u};
    std::size_t previewDequeue = 0u;
    std::array<PatternCommandSlot, kPatternCommandCapacity> patternCommandSlots {};
    std::atomic<std::size_t> patternCommandEnqueue {0u};
    std::size_t patternCommandDequeue = 0u;
    std::array<ArmedPattern, kTrackCount> armedPatterns {};
    std::array<ColumnCommandSlot, kColumnCommandCapacity> columnCommandSlots {};
    std::atomic<std::size_t> columnCommandEnqueue {0u};
    std::size_t columnCommandDequeue = 0u;
    std::array<ArmedColumn, kScheduledTransportEventCapacity> armedColumns {};
    std::array<GlobalSettingsCommandSlot, kGlobalSettingsCommandCapacity>
        globalSettingsCommandSlots {};
    std::atomic<std::size_t> globalSettingsCommandEnqueue {0u};
    std::size_t globalSettingsCommandDequeue = 0u;

    Impl() {
        for (int track = 0; track < kTrackCount; ++track) {
            publishedPlayheads[static_cast<std::size_t>(track)].store(-1,
                                                                      std::memory_order_relaxed);
            publishedLoops[static_cast<std::size_t>(track)].store(0u,
                                                                  std::memory_order_relaxed);
            publishedPatternGenerations[static_cast<std::size_t>(track)].store(
                0u, std::memory_order_relaxed);
            submittedPatternGenerations[static_cast<std::size_t>(track)].store(
                0u, std::memory_order_relaxed);
            nextPatternTokens[static_cast<std::size_t>(track)].store(
                0u, std::memory_order_relaxed);
            settledPatternGenerations[static_cast<std::size_t>(track)].store(
                0u, std::memory_order_relaxed);
        }
        initializePreviewQueue();
        initializePatternCommandQueue();
        initializeColumnCommandQueue();
        initializeGlobalSettingsCommandQueue();
        initializeSettlementState();
    }

    void initializePreviewQueue() noexcept {
        previewEnqueue.store(0u, std::memory_order_relaxed);
        previewDequeue = 0u;
        for (std::size_t i = 0u; i < previewSlots.size(); ++i) {
            previewSlots[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    bool enqueuePreview(const PreviewCommand& command) noexcept {
        std::size_t position = previewEnqueue.load(std::memory_order_relaxed);
        for (;;) {
            PreviewSlot& slot = previewSlots[position % previewSlots.size()];
            const std::size_t sequence = slot.sequence.load(std::memory_order_acquire);
            const std::intptr_t difference = static_cast<std::intptr_t>(sequence) -
                                             static_cast<std::intptr_t>(position);
            if (difference == 0) {
                if (previewEnqueue.compare_exchange_weak(position, position + 1u,
                                                         std::memory_order_relaxed,
                                                         std::memory_order_relaxed)) {
                    slot.command = command;
                    slot.sequence.store(position + 1u, std::memory_order_release);
                    return true;
                }
            } else if (difference < 0) {
                return false;
            } else {
                position = previewEnqueue.load(std::memory_order_relaxed);
            }
        }
    }

    bool dequeuePreview(PreviewCommand& command) noexcept {
        PreviewSlot& slot = previewSlots[previewDequeue % previewSlots.size()];
        const std::size_t sequence = slot.sequence.load(std::memory_order_acquire);
        const std::intptr_t difference = static_cast<std::intptr_t>(sequence) -
                                         static_cast<std::intptr_t>(previewDequeue + 1u);
        if (difference != 0) return false;
        command = slot.command;
        slot.sequence.store(previewDequeue + previewSlots.size(), std::memory_order_release);
        ++previewDequeue;
        return true;
    }

    void initializePatternCommandQueue() noexcept {
        patternCommandEnqueue.store(0u, std::memory_order_relaxed);
        patternCommandDequeue = 0u;
        for (std::size_t i = 0u; i < patternCommandSlots.size(); ++i) {
            patternCommandSlots[i].sequence.store(i, std::memory_order_relaxed);
        }
        for (ArmedPattern& pattern : armedPatterns) {
            pattern.active = false;
            pattern.resetGeneration = 0u;
            pattern.token = 0u;
            pattern.order = 0u;
            pattern.kind = TrackCommandKind::Cue;
        }
    }

    void initializeColumnCommandQueue() noexcept {
        columnCommandEnqueue.store(0u, std::memory_order_relaxed);
        columnCommandDequeue = 0u;
        for (std::size_t i = 0u; i < columnCommandSlots.size(); ++i) {
            columnCommandSlots[i].sequence.store(i, std::memory_order_relaxed);
        }
        for (ArmedColumn& event : armedColumns) event = ArmedColumn {};
    }

    void initializeGlobalSettingsCommandQueue() noexcept {
        globalSettingsCommandEnqueue.store(0u, std::memory_order_relaxed);
        globalSettingsCommandDequeue = 0u;
        for (std::size_t i = 0u; i < globalSettingsCommandSlots.size(); ++i) {
            globalSettingsCommandSlots[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    void initializeSettlementState() noexcept {
        nextSettlementSequence = 0u;
        latestSettlementSequence.store(0u, std::memory_order_relaxed);
        settledColumnGeneration.store(0u, std::memory_order_relaxed);
        settledGlobalSettingsGeneration.store(0u, std::memory_order_relaxed);
        for (std::atomic<std::uint64_t>& generation : settledPatternGenerations) {
            generation.store(0u, std::memory_order_relaxed);
        }
        for (SettlementSlot& slot : settlementSlots) {
            slot.sequence.store(0u, std::memory_order_relaxed);
            slot.token.store(0u, std::memory_order_relaxed);
            slot.track.store(-1, std::memory_order_relaxed);
            slot.family.store(
                static_cast<std::uint8_t>(TransportCommandFamily::Track),
                std::memory_order_relaxed);
            slot.outcome.store(
                static_cast<std::uint8_t>(TransportSettlementOutcome::Cancelled),
                std::memory_order_relaxed);
            slot.appliedTrackMask.store(0u, std::memory_order_relaxed);
            slot.appliedTempo.store(false, std::memory_order_relaxed);
            slot.appliedScale.store(false, std::memory_order_relaxed);
        }
        for (auto& trackCancellations : requestedPatternCancellations) {
            for (std::atomic<std::uint64_t>& cancellation : trackCancellations) {
                cancellation.store(0u, std::memory_order_relaxed);
            }
        }
        for (std::atomic<std::uint64_t>& cancellation :
             requestedColumnCancellations) {
            cancellation.store(0u, std::memory_order_relaxed);
        }
        for (std::atomic<std::uint64_t>& cancellation :
             requestedGlobalSettingsCancellations) {
            cancellation.store(0u, std::memory_order_relaxed);
        }
    }

    void publishSettlement(TransportCommandFamily family,
                           TransportSettlementOutcome outcome,
                           std::uint64_t token, int track = -1,
                           std::uint8_t appliedTrackMask = 0u,
                           bool appliedTempo = false,
                           bool appliedScale = false) noexcept {
        if (token == 0u) return;
        const std::uint64_t sequence = ++nextSettlementSequence;
        SettlementSlot& slot = settlementSlots[static_cast<std::size_t>(
            (sequence - 1u) % settlementSlots.size())];
        slot.sequence.store(0u, std::memory_order_release);
        slot.token.store(token, std::memory_order_relaxed);
        slot.track.store(track, std::memory_order_relaxed);
        slot.family.store(static_cast<std::uint8_t>(family),
                          std::memory_order_relaxed);
        slot.outcome.store(static_cast<std::uint8_t>(outcome),
                           std::memory_order_relaxed);
        slot.appliedTrackMask.store(appliedTrackMask, std::memory_order_relaxed);
        slot.appliedTempo.store(appliedTempo, std::memory_order_relaxed);
        slot.appliedScale.store(appliedScale, std::memory_order_relaxed);
        slot.sequence.store(sequence, std::memory_order_release);
        latestSettlementSequence.store(sequence, std::memory_order_release);
        switch (family) {
        case TransportCommandFamily::Track:
            if (track >= 0 && track < kTrackCount) {
                publishGeneration(
                    settledPatternGenerations[static_cast<std::size_t>(track)], token);
            }
            break;
        case TransportCommandFamily::Column:
            publishGeneration(settledColumnGeneration, token);
            break;
        case TransportCommandFamily::GlobalSettings:
            publishGeneration(settledGlobalSettingsGeneration, token);
            break;
        }
    }

    bool cancellationRequested(TransportCommandFamily family, std::uint64_t token,
                               int track = -1) const noexcept {
        if (token == 0u) return false;
        const std::size_t index = static_cast<std::size_t>(
            (token - 1u) % kTransportSettlementCapacity);
        switch (family) {
        case TransportCommandFamily::Track:
            return track >= 0 && track < kTrackCount &&
                requestedPatternCancellations[static_cast<std::size_t>(track)][index]
                        .load(std::memory_order_acquire) == token;
        case TransportCommandFamily::Column:
            return requestedColumnCancellations[index].load(
                       std::memory_order_acquire) == token;
        case TransportCommandFamily::GlobalSettings:
            return requestedGlobalSettingsCancellations[index].load(
                       std::memory_order_acquire) == token;
        }
        return false;
    }

    void requestCancellation(TransportCommandFamily family, std::uint64_t token,
                             int track = -1) noexcept {
        const std::size_t index = static_cast<std::size_t>(
            (token - 1u) % kTransportSettlementCapacity);
        switch (family) {
        case TransportCommandFamily::Track:
            requestedPatternCancellations[static_cast<std::size_t>(track)][index]
                .store(token, std::memory_order_release);
            break;
        case TransportCommandFamily::Column:
            requestedColumnCancellations[index].store(token,
                                                      std::memory_order_release);
            break;
        case TransportCommandFamily::GlobalSettings:
            requestedGlobalSettingsCancellations[index].store(
                token, std::memory_order_release);
            break;
        }
    }

    bool settlementPublished(TransportCommandFamily family, std::uint64_t token,
                             int track = -1) const noexcept {
        for (const SettlementSlot& slot : settlementSlots) {
            const std::uint64_t before = slot.sequence.load(std::memory_order_acquire);
            if (before == 0u) continue;
            const std::uint64_t settledToken =
                slot.token.load(std::memory_order_relaxed);
            const int settledTrack = slot.track.load(std::memory_order_relaxed);
            const auto settledFamily = static_cast<TransportCommandFamily>(
                slot.family.load(std::memory_order_relaxed));
            const std::uint64_t after = slot.sequence.load(std::memory_order_acquire);
            if (before == after && settledToken == token && settledFamily == family &&
                (family != TransportCommandFamily::Track || settledTrack == track)) {
                return true;
            }
        }
        return false;
    }

    bool enqueuePatternCommand(const PatternCommand& command) noexcept {
        std::size_t position = patternCommandEnqueue.load(std::memory_order_relaxed);
        for (;;) {
            PatternCommandSlot& slot =
                patternCommandSlots[position % patternCommandSlots.size()];
            const std::size_t sequence = slot.sequence.load(std::memory_order_acquire);
            const std::intptr_t difference = static_cast<std::intptr_t>(sequence) -
                                             static_cast<std::intptr_t>(position);
            if (difference == 0) {
                if (patternCommandEnqueue.compare_exchange_weak(
                        position, position + 1u, std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    slot.command = command;
                    slot.sequence.store(position + 1u, std::memory_order_release);
                    return true;
                }
            } else if (difference < 0) {
                return false;
            } else {
                position = patternCommandEnqueue.load(std::memory_order_relaxed);
            }
        }
    }

    bool dequeuePatternCommand(PatternCommand& command) noexcept {
        PatternCommandSlot& slot =
            patternCommandSlots[patternCommandDequeue % patternCommandSlots.size()];
        const std::size_t sequence = slot.sequence.load(std::memory_order_acquire);
        const std::intptr_t difference = static_cast<std::intptr_t>(sequence) -
                                         static_cast<std::intptr_t>(patternCommandDequeue + 1u);
        if (difference != 0) return false;
        command = slot.command;
        slot.sequence.store(patternCommandDequeue + patternCommandSlots.size(),
                            std::memory_order_release);
        ++patternCommandDequeue;
        return true;
    }

    bool peekPatternCommand(PatternCommand& command) const noexcept {
        const PatternCommandSlot& slot =
            patternCommandSlots[patternCommandDequeue % patternCommandSlots.size()];
        const std::size_t sequence = slot.sequence.load(std::memory_order_acquire);
        const std::intptr_t difference = static_cast<std::intptr_t>(sequence) -
                                         static_cast<std::intptr_t>(
                                             patternCommandDequeue + 1u);
        if (difference != 0) return false;
        command = slot.command;
        return true;
    }

    void drainPatternCommands() noexcept {
        PatternCommand command;
        while (peekPatternCommand(command)) {
            if (command.resetGeneration > observedResetGeneration) return;
            if (!dequeuePatternCommand(command)) return;
            if (command.track < 0 || command.track >= kTrackCount) {
                continue;
            }
            if (command.resetGeneration < observedResetGeneration) {
                publishSettlement(TransportCommandFamily::Track,
                                  TransportSettlementOutcome::Cancelled,
                                  command.token, command.track);
                continue;
            }
            const std::size_t trackIndex = static_cast<std::size_t>(command.track);
            const std::uint64_t settled =
                settledPatternGenerations[trackIndex].load(std::memory_order_relaxed);
            if (command.token <= settled) continue;
            const std::uint64_t applied =
                publishedPatternGenerations[trackIndex].load(
                    std::memory_order_relaxed);
            if (command.token <= applied) continue;
            if (cancellationRequested(TransportCommandFamily::Track,
                                      command.token, command.track)) {
                publishSettlement(TransportCommandFamily::Track,
                                  TransportSettlementOutcome::Cancelled,
                                  command.token, command.track);
                continue;
            }
            ArmedPattern& armed = armedPatterns[static_cast<std::size_t>(command.track)];
            const std::uint8_t trackBit = static_cast<std::uint8_t>(1u << command.track);
            bool supersededByColumn = false;
            for (ArmedColumn& event : armedColumns) {
                if (!event.active || event.resetGeneration != command.resetGeneration ||
                    (event.trackMask & trackBit) == 0u) {
                    continue;
                }
                const std::size_t index = static_cast<std::size_t>(command.track);
                if (event.trackOrders[index] > command.order) {
                    supersededByColumn = true;
                } else if (command.order > event.trackOrders[index]) {
                    event.trackMask = static_cast<std::uint8_t>(
                        event.trackMask & static_cast<std::uint8_t>(~trackBit));
                    event.trackOrders[index] = 0u;
                }
            }
            if (supersededByColumn) {
                publishSettlement(TransportCommandFamily::Track,
                                  TransportSettlementOutcome::Cancelled,
                                  command.token, command.track);
                continue;
            }
            if (armed.active && command.token <= armed.token) {
                publishSettlement(TransportCommandFamily::Track,
                                  TransportSettlementOutcome::Cancelled,
                                  command.token, command.track);
                continue;
            }
            if (armed.active) {
                publishSettlement(TransportCommandFamily::Track,
                                  TransportSettlementOutcome::Cancelled,
                                  armed.token, command.track);
            }
            armed.pattern = command.pattern;
            armed.resetGeneration = command.resetGeneration;
            armed.token = command.token;
            armed.order = command.order;
            armed.kind = command.kind;
            armed.active = true;
        }
    }

    double nextGlobalBoundary() const noexcept {
        const double currentBar = std::floor(
            std::max(0.0, transportTick) / kGlobalBoundaryTicks);
        return (currentBar + 1.0) * kGlobalBoundaryTicks;
    }

    ArmedColumn* findTransportEvent(std::uint64_t generation,
                                    double targetTick) noexcept {
        for (ArmedColumn& event : armedColumns) {
            if (event.active && event.resetGeneration == generation &&
                std::fabs(event.targetTick - targetTick) <= kTickEpsilon) {
                return &event;
            }
        }
        return nullptr;
    }

    ArmedColumn* reserveTransportEvent(std::uint64_t generation,
                                       double targetTick) noexcept {
        if (ArmedColumn* event = findTransportEvent(generation, targetTick)) {
            return event;
        }
        for (ArmedColumn& event : armedColumns) {
            if (event.active) continue;
            event = ArmedColumn {};
            event.active = true;
            event.resetGeneration = generation;
            event.targetTick = targetTick;
            return &event;
        }
        return nullptr;
    }

    void scheduleColumnTrack(ArmedColumn& destination, const ColumnCommand& command,
                             int track) noexcept {
        const std::size_t index = static_cast<std::size_t>(track);
        const std::uint8_t trackBit = static_cast<std::uint8_t>(1u << track);
        ArmedPattern& trackCommand = armedPatterns[index];
        if (trackCommand.active && trackCommand.order > command.order) return;
        if (trackCommand.active && trackCommand.order < command.order) {
            publishSettlement(TransportCommandFamily::Track,
                              TransportSettlementOutcome::Cancelled,
                              trackCommand.token, track);
            trackCommand = ArmedPattern {};
        }

        bool superseded = false;
        for (const ArmedColumn& event : armedColumns) {
            if (!event.active || event.resetGeneration != command.resetGeneration ||
                (event.trackMask & trackBit) == 0u) {
                continue;
            }
            if (scheduledValueSupersedes(event.trackOrders[index], event.targetTick,
                                         command.order, destination.targetTick)) {
                superseded = true;
                break;
            }
        }
        if (superseded) return;

        for (ArmedColumn& event : armedColumns) {
            if (!event.active || event.resetGeneration != command.resetGeneration ||
                (event.trackMask & trackBit) == 0u) {
                continue;
            }
            if (scheduledValueSupersedes(command.order, destination.targetTick,
                                         event.trackOrders[index], event.targetTick)) {
                event.trackMask = static_cast<std::uint8_t>(
                    event.trackMask & static_cast<std::uint8_t>(~trackBit));
                event.trackOrders[index] = 0u;
            }
        }
        destination.trackMask = static_cast<std::uint8_t>(
            destination.trackMask | trackBit);
        destination.trackOrders[index] = command.order;
        destination.trackKinds[index] = command.kind;
        destination.patterns[index] = command.patterns[index];
        if (command.kind == TrackCommandKind::Cue &&
            command.resetGeneration == observedResetGeneration) {
            prunePendingFromBoundary(track, destination.targetTick);
        }
    }

    void scheduleTempo(ArmedColumn& destination, std::uint64_t generation,
                       std::uint64_t order, std::uint16_t bpm) noexcept {
        for (const ArmedColumn& event : armedColumns) {
            if (!event.active || event.resetGeneration != generation ||
                !event.settings.applyTempo) {
                continue;
            }
            if (scheduledValueSupersedes(event.tempoOrder, event.targetTick, order,
                                         destination.targetTick)) {
                return;
            }
        }
        for (ArmedColumn& event : armedColumns) {
            if (!event.active || event.resetGeneration != generation ||
                !event.settings.applyTempo) {
                continue;
            }
            if (scheduledValueSupersedes(order, destination.targetTick,
                                         event.tempoOrder, event.targetTick)) {
                event.settings.applyTempo = false;
                event.tempoOrder = 0u;
            }
        }
        destination.settings.applyTempo = true;
        destination.settings.bpm = bpm;
        destination.tempoOrder = order;
    }

    void scheduleScale(ArmedColumn& destination, std::uint64_t generation,
                       std::uint64_t order, std::uint8_t root,
                       std::uint16_t mask) noexcept {
        for (const ArmedColumn& event : armedColumns) {
            if (!event.active || event.resetGeneration != generation ||
                !event.settings.applyScale) {
                continue;
            }
            if (scheduledValueSupersedes(event.scaleOrder, event.targetTick, order,
                                         destination.targetTick)) {
                return;
            }
        }
        for (ArmedColumn& event : armedColumns) {
            if (!event.active || event.resetGeneration != generation ||
                !event.settings.applyScale) {
                continue;
            }
            if (scheduledValueSupersedes(order, destination.targetTick,
                                         event.scaleOrder, event.targetTick)) {
                event.settings.applyScale = false;
                event.scaleOrder = 0u;
            }
        }
        destination.settings.applyScale = true;
        destination.settings.scaleRoot = root;
        destination.settings.scaleMask = mask;
        destination.scaleOrder = order;
    }

    static std::uint8_t receiptTrackMask(const ArmedColumn& event,
                                         const ColumnReceipt& receipt) noexcept {
        std::uint8_t result = 0u;
        for (int track = 0; track < kTrackCount; ++track) {
            const std::size_t index = static_cast<std::size_t>(track);
            const std::uint8_t trackBit = static_cast<std::uint8_t>(1u << track);
            if ((receipt.trackMask & trackBit) != 0u &&
                (event.trackMask & trackBit) != 0u &&
                event.trackOrders[index] == receipt.order) {
                result = static_cast<std::uint8_t>(result | trackBit);
            }
        }
        return result;
    }

    static bool receiptOwnsTempo(const ArmedColumn& event,
                                 std::uint64_t order, bool requested) noexcept {
        return requested && event.settings.applyTempo && event.tempoOrder == order;
    }

    static bool receiptOwnsScale(const ArmedColumn& event,
                                 std::uint64_t order, bool requested) noexcept {
        return requested && event.settings.applyScale && event.scaleOrder == order;
    }

    void settleSupersededTransportReceipts() noexcept {
        for (ArmedColumn& event : armedColumns) {
            if (!event.active) continue;
            for (std::size_t index = 0u; index < event.columnReceiptCount; ++index) {
                ColumnReceipt& receipt = event.columnReceipts[index];
                if (receipt.settled) continue;
                const bool ownsComponent = receiptTrackMask(event, receipt) != 0u ||
                    receiptOwnsTempo(event, receipt.order, receipt.tempo) ||
                    receiptOwnsScale(event, receipt.order, receipt.scale);
                if (ownsComponent) continue;
                receipt.settled = true;
                publishSettlement(TransportCommandFamily::Column,
                                  TransportSettlementOutcome::Cancelled,
                                  receipt.token);
            }
            for (std::size_t index = 0u;
                 index < event.globalSettingsReceiptCount; ++index) {
                GlobalSettingsReceipt& receipt = event.globalSettingsReceipts[index];
                if (receipt.settled) continue;
                const bool ownsComponent =
                    receiptOwnsTempo(event, receipt.order, receipt.tempo) ||
                    receiptOwnsScale(event, receipt.order, receipt.scale);
                if (ownsComponent) continue;
                receipt.settled = true;
                publishSettlement(TransportCommandFamily::GlobalSettings,
                                  TransportSettlementOutcome::Cancelled,
                                  receipt.token);
            }
        }
    }

    void applyRequestedCancellations() noexcept {
        for (int track = 0; track < kTrackCount; ++track) {
            ArmedPattern& pattern = armedPatterns[static_cast<std::size_t>(track)];
            if (!pattern.active ||
                !cancellationRequested(TransportCommandFamily::Track,
                                       pattern.token, track)) {
                continue;
            }
            publishSettlement(TransportCommandFamily::Track,
                              TransportSettlementOutcome::Cancelled,
                              pattern.token, track);
            pattern = ArmedPattern {};
        }
        for (ArmedColumn& event : armedColumns) {
            if (!event.active) continue;
            for (std::size_t index = 0u; index < event.columnReceiptCount; ++index) {
                ColumnReceipt& receipt = event.columnReceipts[index];
                if (receipt.settled ||
                    !cancellationRequested(TransportCommandFamily::Column,
                                           receipt.token)) {
                    continue;
                }
                for (int track = 0; track < kTrackCount; ++track) {
                    const std::size_t trackIndex = static_cast<std::size_t>(track);
                    const std::uint8_t trackBit =
                        static_cast<std::uint8_t>(1u << track);
                    if ((event.trackMask & trackBit) != 0u &&
                        event.trackOrders[trackIndex] == receipt.order) {
                        event.trackMask = static_cast<std::uint8_t>(
                            event.trackMask & static_cast<std::uint8_t>(~trackBit));
                        event.trackOrders[trackIndex] = 0u;
                    }
                }
                if (event.settings.applyTempo && event.tempoOrder == receipt.order) {
                    event.settings.applyTempo = false;
                    event.tempoOrder = 0u;
                }
                if (event.settings.applyScale && event.scaleOrder == receipt.order) {
                    event.settings.applyScale = false;
                    event.scaleOrder = 0u;
                }
                receipt.settled = true;
                publishSettlement(TransportCommandFamily::Column,
                                  TransportSettlementOutcome::Cancelled,
                                  receipt.token);
            }
            for (std::size_t index = 0u;
                 index < event.globalSettingsReceiptCount; ++index) {
                GlobalSettingsReceipt& receipt = event.globalSettingsReceipts[index];
                if (receipt.settled ||
                    !cancellationRequested(TransportCommandFamily::GlobalSettings,
                                           receipt.token)) {
                    continue;
                }
                if (event.settings.applyTempo && event.tempoOrder == receipt.order) {
                    event.settings.applyTempo = false;
                    event.tempoOrder = 0u;
                }
                if (event.settings.applyScale && event.scaleOrder == receipt.order) {
                    event.settings.applyScale = false;
                    event.scaleOrder = 0u;
                }
                receipt.settled = true;
                publishSettlement(TransportCommandFamily::GlobalSettings,
                                  TransportSettlementOutcome::Cancelled,
                                  receipt.token);
            }
        }
        settleSupersededTransportReceipts();
        for (ArmedColumn& event : armedColumns) {
            if (!event.active) continue;
            bool pendingReceipt = false;
            for (std::size_t index = 0u; index < event.columnReceiptCount; ++index) {
                pendingReceipt = pendingReceipt || !event.columnReceipts[index].settled;
            }
            for (std::size_t index = 0u;
                 index < event.globalSettingsReceiptCount; ++index) {
                pendingReceipt = pendingReceipt ||
                    !event.globalSettingsReceipts[index].settled;
            }
            if (!pendingReceipt) event = ArmedColumn {};
        }
    }

    void prunePendingFromBoundary(int track, double boundary) noexcept {
        if (track < 0 || track >= kTrackCount) return;
        TrackRuntime& runtime = trackRuntime[static_cast<std::size_t>(track)];
        std::size_t write = 0u;
        for (std::size_t read = 0u; read < runtime.pendingCount; ++read) {
            if (runtime.pending[read].nominalTick >= boundary - 1.0e-9) continue;
            if (write != read) runtime.pending[write] = runtime.pending[read];
            ++write;
        }
        runtime.pendingCount = write;
    }

    void drainColumnCommands() noexcept {
        ColumnCommand command;
        while (peekBounded(columnCommandSlots, columnCommandDequeue, command)) {
            if (command.resetGeneration > observedResetGeneration) return;
            if (command.resetGeneration < observedResetGeneration || command.trackMask == 0u) {
                (void)dequeueBounded(columnCommandSlots, columnCommandDequeue, command);
                publishSettlement(TransportCommandFamily::Column,
                                  TransportSettlementOutcome::Cancelled,
                                  command.token);
                continue;
            }
            const std::uint64_t settled =
                settledColumnGeneration.load(std::memory_order_relaxed);
            if (command.token <= settled) {
                (void)dequeueBounded(columnCommandSlots, columnCommandDequeue, command);
                continue;
            }
            const std::uint64_t applied =
                publishedColumnGeneration.load(std::memory_order_relaxed);
            if (command.token <= applied) {
                (void)dequeueBounded(columnCommandSlots, columnCommandDequeue, command);
                continue;
            }
            if (cancellationRequested(TransportCommandFamily::Column,
                                      command.token)) {
                (void)dequeueBounded(columnCommandSlots, columnCommandDequeue, command);
                publishSettlement(TransportCommandFamily::Column,
                                  TransportSettlementOutcome::Cancelled,
                                  command.token);
                continue;
            }
            const double targetTick = command.applyImmediately
                                          ? transportTick
                                          : nextGlobalBoundary();
            ArmedColumn* const destination =
                reserveTransportEvent(command.resetGeneration, targetTick);
            if (destination == nullptr) return;
            if (destination->columnReceiptCount >=
                destination->columnReceipts.size()) {
                return;
            }
            if (!dequeueBounded(columnCommandSlots, columnCommandDequeue, command)) return;
            destination->columnReceipts[destination->columnReceiptCount++] = ColumnReceipt {
                command.token,
                command.order,
                static_cast<std::uint8_t>(
                    command.trackMask &
                    static_cast<std::uint8_t>((1u << kTrackCount) - 1u)),
                command.settings.applyTempo,
                command.settings.applyScale,
            };

            const std::uint8_t commandMask = static_cast<std::uint8_t>(
                command.trackMask & static_cast<std::uint8_t>((1u << kTrackCount) - 1u));
            for (int track = 0; track < kTrackCount; ++track) {
                const std::uint8_t trackBit = static_cast<std::uint8_t>(1u << track);
                if ((commandMask & trackBit) == 0u) continue;
                scheduleColumnTrack(*destination, command, track);
            }

            if (command.settings.applyTempo) {
                scheduleTempo(*destination, command.resetGeneration, command.order,
                              command.settings.bpm);
            }
            if (command.settings.applyScale) {
                scheduleScale(*destination, command.resetGeneration, command.order,
                              command.settings.scaleRoot,
                              command.settings.scaleMask);
            }
        }
    }

    void drainGlobalSettingsCommands() noexcept {
        GlobalSettingsCommand command;
        while (peekBounded(globalSettingsCommandSlots, globalSettingsCommandDequeue,
                           command)) {
            if (command.resetGeneration > observedResetGeneration) return;
            if (command.resetGeneration < observedResetGeneration) {
                (void)dequeueBounded(globalSettingsCommandSlots,
                                     globalSettingsCommandDequeue, command);
                publishSettlement(TransportCommandFamily::GlobalSettings,
                                  TransportSettlementOutcome::Cancelled,
                                  command.token);
                continue;
            }
            const std::uint64_t settled =
                settledGlobalSettingsGeneration.load(std::memory_order_relaxed);
            if (command.token <= settled) {
                (void)dequeueBounded(globalSettingsCommandSlots,
                                     globalSettingsCommandDequeue, command);
                continue;
            }
            const std::uint64_t applied =
                publishedGlobalSettingsGeneration.load(std::memory_order_relaxed);
            if (command.token <= applied) {
                (void)dequeueBounded(globalSettingsCommandSlots,
                                     globalSettingsCommandDequeue, command);
                continue;
            }
            if (cancellationRequested(TransportCommandFamily::GlobalSettings,
                                      command.token)) {
                (void)dequeueBounded(globalSettingsCommandSlots,
                                     globalSettingsCommandDequeue, command);
                publishSettlement(TransportCommandFamily::GlobalSettings,
                                  TransportSettlementOutcome::Cancelled,
                                  command.token);
                continue;
            }
            const double targetTick = command.applyImmediately
                                          ? transportTick
                                          : nextGlobalBoundary();
            ArmedColumn* const destination =
                reserveTransportEvent(command.resetGeneration, targetTick);
            if (destination == nullptr) return;
            if (destination->globalSettingsReceiptCount >=
                destination->globalSettingsReceipts.size()) {
                return;
            }
            if (!dequeueBounded(globalSettingsCommandSlots,
                                globalSettingsCommandDequeue, command)) {
                return;
            }
            destination->globalSettingsReceipts[
                destination->globalSettingsReceiptCount++] = GlobalSettingsReceipt {
                command.token,
                command.order,
                command.settings.applyTempo,
                command.settings.applyScale,
            };
            if (command.settings.applyTempo) {
                scheduleTempo(*destination, command.resetGeneration, command.order,
                              command.settings.bpm);
            }
            if (command.settings.applyScale) {
                scheduleScale(*destination, command.resetGeneration, command.order,
                              command.settings.scaleRoot,
                              command.settings.scaleMask);
            }
        }
    }

    void cancelTransportEvent(ArmedColumn& event) noexcept {
        for (std::size_t index = 0u; index < event.columnReceiptCount; ++index) {
            if (event.columnReceipts[index].settled) continue;
            publishSettlement(TransportCommandFamily::Column,
                              TransportSettlementOutcome::Cancelled,
                              event.columnReceipts[index].token);
        }
        for (std::size_t index = 0u;
             index < event.globalSettingsReceiptCount; ++index) {
            if (event.globalSettingsReceipts[index].settled) continue;
            publishSettlement(TransportCommandFamily::GlobalSettings,
                              TransportSettlementOutcome::Cancelled,
                              event.globalSettingsReceipts[index].token);
        }
        event = ArmedColumn {};
    }

    void discardStaleArmedPatterns() noexcept {
        for (ArmedPattern& pattern : armedPatterns) {
            if (pattern.active && pattern.resetGeneration == observedResetGeneration) continue;
            if (pattern.active) {
                const int track = static_cast<int>(&pattern - armedPatterns.data());
                publishSettlement(TransportCommandFamily::Track,
                                  TransportSettlementOutcome::Cancelled,
                                  pattern.token, track);
            }
            pattern = ArmedPattern {};
        }
        for (ArmedColumn& event : armedColumns) {
            if (!event.active || event.resetGeneration == observedResetGeneration) continue;
            cancelTransportEvent(event);
        }
    }

    bool refreshPerformance(std::uint64_t& capturedResetGeneration) noexcept {
        if (shared == nullptr) return false;
        std::unique_lock<std::mutex> lock(shared->mutex, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        if (shared->uiMutationInProgress.load(std::memory_order_acquire)) return false;
        performance.bpm = shared->app.bpm;
        performance.scaleRoot = shared->app.scaleRoot;
        performance.scaleMask = shared->app.scaleMask;
        performance.tracks = shared->app.tracks;
        capturedResetGeneration = resetGeneration.load(std::memory_order_acquire);
        return true;
    }

    bool anySolo() const noexcept {
        for (const TrackData& track : performance.tracks) {
            if (track.solo && !track.muted) return true;
        }
        return false;
    }

    bool trackIsAudible(int track, bool soloActive) const noexcept {
        if (track < 0 || track >= kTrackCount) return false;
        const TrackData& data = performance.tracks[static_cast<std::size_t>(track)];
        return !data.muted && (!soloActive || data.solo);
    }

    void advanceTranspose(TrackRuntime& runtime, const TransposeSettings& settings) noexcept {
        const int rate = clampValue(static_cast<int>(settings.rate), 1, 16);
        ++runtime.transposeRepeats;
        if (runtime.transposeRepeats < rate) return;
        runtime.transposeRepeats = 0;
        const int length = clampValue(static_cast<int>(settings.length), 1, 8);
        runtime.transposeIndex = (runtime.transposeIndex + 1) % length;
    }

    void advanceCursor(int track, const TrackData& data, TrackRuntime& runtime) noexcept {
        const int length = clampValue(static_cast<int>(data.length), 1, kStepCount);
        runtime.cursor = clampValue(runtime.cursor, 0, length - 1);
        bool completedPattern = false;

        switch (data.direction) {
            case Direction::Forward:
                ++runtime.cursor;
                if (runtime.cursor >= length) {
                    runtime.cursor = 0;
                    ++runtime.scheduleLoop;
                    ++runtime.conditionLoop;
                    completedPattern = true;
                }
                break;

            case Direction::Reverse:
                --runtime.cursor;
                if (runtime.cursor < 0) {
                    runtime.cursor = length - 1;
                    ++runtime.scheduleLoop;
                    ++runtime.conditionLoop;
                    completedPattern = true;
                }
                break;

            case Direction::PingPong:
                if (length <= 1) {
                    runtime.cursor = 0;
                    ++runtime.scheduleLoop;
                    ++runtime.conditionLoop;
                    completedPattern = true;
                } else if (runtime.pingDirection > 0) {
                    if (runtime.cursor >= length - 1) {
                        runtime.pingDirection = -1;
                        runtime.cursor = length - 2;
                    } else {
                        ++runtime.cursor;
                    }
                } else if (runtime.cursor <= 0) {
                    runtime.pingDirection = 1;
                    runtime.cursor = 1;
                    ++runtime.scheduleLoop;
                    ++runtime.conditionLoop;
                    completedPattern = true;
                } else {
                    --runtime.cursor;
                }
                break;

            case Direction::Random:
                ++runtime.randomSteps;
                if (runtime.randomSteps >= static_cast<std::uint64_t>(length)) {
                    runtime.randomSteps = 0u;
                    ++runtime.scheduleLoop;
                    ++runtime.conditionLoop;
                    completedPattern = true;
                }
                runtime.cursor = static_cast<int>(xorshift32(randomState) %
                                                  static_cast<std::uint32_t>(length));
                break;
        }

        if (completedPattern && data.transpose.advance == TransposeAdvance::Pattern) {
            advanceTranspose(runtime, data.transpose);
        }
        if (completedPattern) runtime.loopBoundaryPending = true;
        (void)track;
    }

    void beginReplacementPattern(int track, const TrackData& data,
                                 TrackRuntime& runtime) noexcept {
        const int length = clampValue(static_cast<int>(data.length), 1, kStepCount);
        runtime.clampReplacementLead = true;
        runtime.replacementBoundaryTick = runtime.nextNominalTick;
        runtime.cursor = 0;
        runtime.pingDirection = 1;
        runtime.randomSteps = 0u;
        runtime.conditionLoop = 0u;
        runtime.transposeIndex = 0;
        runtime.transposeRepeats = 0;
        runtime.loopBoundaryPending = false;
        if (data.direction == Direction::Reverse) {
            runtime.cursor = length - 1;
        } else if (data.direction == Direction::Random) {
            runtime.cursor = static_cast<int>(xorshift32(randomState) %
                                              static_cast<std::uint32_t>(length));
        }
        modRuntime[static_cast<std::size_t>(track)] = ModRuntime {};
    }

    bool applyArmedPattern(int track, TrackRuntime& runtime) noexcept {
        ArmedPattern& armed = armedPatterns[static_cast<std::size_t>(track)];
        if (!armed.active || armed.kind != TrackCommandKind::Cue) {
            runtime.loopBoundaryPending = false;
            return true;
        }
        if (cancellationRequested(TransportCommandFamily::Track,
                                  armed.token, track)) {
            publishSettlement(TransportCommandFamily::Track,
                              TransportSettlementOutcome::Cancelled,
                              armed.token, track);
            armed = ArmedPattern {};
            runtime.loopBoundaryPending = false;
            return true;
        }
        if (armed.resetGeneration > observedResetGeneration) return false;
        if (armed.resetGeneration < observedResetGeneration) {
            publishSettlement(TransportCommandFamily::Track,
                              TransportSettlementOutcome::Cancelled,
                              armed.token, track);
            armed = ArmedPattern {};
            runtime.loopBoundaryPending = false;
            return true;
        }
        if (shared == nullptr ||
            resetGeneration.load(std::memory_order_acquire) != observedResetGeneration) {
            return false;
        }
        std::unique_lock<std::mutex> lock(shared->mutex, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        if (shared->uiMutationInProgress.load(std::memory_order_acquire)) return false;
        if (resetGeneration.load(std::memory_order_acquire) != observedResetGeneration) {
            return false;
        }
        if (cancellationRequested(TransportCommandFamily::Track,
                                  armed.token, track)) {
            publishSettlement(TransportCommandFamily::Track,
                              TransportSettlementOutcome::Cancelled,
                              armed.token, track);
            armed = ArmedPattern {};
            runtime.loopBoundaryPending = false;
            return true;
        }

        performance.tracks[static_cast<std::size_t>(track)] = armed.pattern;
        shared->app.tracks[static_cast<std::size_t>(track)] = armed.pattern;
        ++shared->app.editRevision;
        const std::uint64_t appliedToken = armed.token;
        armed.active = false;
        armed.resetGeneration = 0u;
        armed.token = 0u;
        beginReplacementPattern(track,
                                performance.tracks[static_cast<std::size_t>(track)], runtime);
        publishedPatternGenerations[static_cast<std::size_t>(track)].store(
            appliedToken, std::memory_order_release);
        publishSettlement(TransportCommandFamily::Track,
                          TransportSettlementOutcome::Applied,
                          appliedToken, track,
                          static_cast<std::uint8_t>(1u << track));
        return true;
    }

    void stopTrackVoices(int track) noexcept {
        if (track >= 0 && track < kFmTrackCount) {
            for (FmVoice& voice : fmVoices) {
                if (voice.active && voice.ownerTrack == track) voice.stop();
            }
        } else if (track == kFmTrackCount) {
            noiseVoice.stop();
        }
    }

    double nextStepBoundary() const noexcept {
        if (!shouldRun.load(std::memory_order_acquire)) return transportTick;
        const double steps = std::ceil((transportTick - 1.0e-9) / kTicksPerStep);
        return std::max(0.0, steps * kTicksPerStep);
    }

    void resetOneTrack(int track, const TrackData& data) noexcept {
        TrackRuntime& runtime = trackRuntime[static_cast<std::size_t>(track)];
        runtime = TrackRuntime {};
        runtime.nextNominalTick = nextStepBoundary();
        const int length = clampValue(static_cast<int>(data.length), 1, kStepCount);
        if (data.direction == Direction::Reverse) {
            runtime.cursor = length - 1;
        } else if (data.direction == Direction::Random) {
            runtime.cursor = static_cast<int>(xorshift32(randomState) %
                                              static_cast<std::uint32_t>(length));
        }
        modRuntime[static_cast<std::size_t>(track)] = ModRuntime {};
        echoes.removeTrack(track);
        stopTrackVoices(track);
        publishedPlayheads[static_cast<std::size_t>(track)].store(
            -1, std::memory_order_relaxed);
        publishedLoops[static_cast<std::size_t>(track)].store(
            0u, std::memory_order_relaxed);
    }

    void preserveTrackPositionForImmediateLoad(int track, const TrackData& data) noexcept {
        TrackRuntime& runtime = trackRuntime[static_cast<std::size_t>(track)];
        if (runtime.pendingCount > 0u) {
            std::size_t earliest = 0u;
            for (std::size_t index = 1u; index < runtime.pendingCount; ++index) {
                const ScheduledStep& candidate = runtime.pending[index];
                const ScheduledStep& current = runtime.pending[earliest];
                if (candidate.nominalTick < current.nominalTick ||
                    (candidate.nominalTick == current.nominalTick &&
                     candidate.ordinal < current.ordinal)) {
                    earliest = index;
                }
            }
            runtime.cursor = runtime.pending[earliest].stepIndex;
            runtime.nextNominalTick = runtime.pending[earliest].nominalTick;
            runtime.conditionLoop = runtime.pending[earliest].loop;
        }
        runtime.pendingCount = 0u;
        runtime.loopBoundaryPending = false;
        runtime.clampReplacementLead = false;
        const int length = clampValue(static_cast<int>(data.length), 1, kStepCount);
        runtime.cursor = clampValue(runtime.cursor, 0, length - 1);
    }

    bool applyImmediatePattern(int track) noexcept {
        ArmedPattern& armed = armedPatterns[static_cast<std::size_t>(track)];
        if (!armed.active || armed.kind == TrackCommandKind::Cue) return true;
        if (cancellationRequested(TransportCommandFamily::Track,
                                  armed.token, track)) {
            publishSettlement(TransportCommandFamily::Track,
                              TransportSettlementOutcome::Cancelled,
                              armed.token, track);
            armed = ArmedPattern {};
            return true;
        }
        if (armed.resetGeneration > observedResetGeneration) return false;
        if (armed.resetGeneration < observedResetGeneration) {
            publishSettlement(TransportCommandFamily::Track,
                              TransportSettlementOutcome::Cancelled,
                              armed.token, track);
            armed = ArmedPattern {};
            return true;
        }
        if (shared == nullptr ||
            resetGeneration.load(std::memory_order_acquire) != observedResetGeneration) {
            return false;
        }
        std::unique_lock<std::mutex> lock(shared->mutex, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        if (shared->uiMutationInProgress.load(std::memory_order_acquire)) return false;
        if (resetGeneration.load(std::memory_order_acquire) != observedResetGeneration) {
            return false;
        }
        if (cancellationRequested(TransportCommandFamily::Track,
                                  armed.token, track)) {
            publishSettlement(TransportCommandFamily::Track,
                              TransportSettlementOutcome::Cancelled,
                              armed.token, track);
            armed = ArmedPattern {};
            return true;
        }

        performance.tracks[static_cast<std::size_t>(track)] = armed.pattern;
        shared->app.tracks[static_cast<std::size_t>(track)] = armed.pattern;
        ++shared->app.editRevision;
        if (armed.kind == TrackCommandKind::ImmediateReset) {
            resetOneTrack(track, performance.tracks[static_cast<std::size_t>(track)]);
        } else {
            preserveTrackPositionForImmediateLoad(
                track, performance.tracks[static_cast<std::size_t>(track)]);
        }
        const std::uint64_t token = armed.token;
        armed = ArmedPattern {};
        publishedPatternGenerations[static_cast<std::size_t>(track)].store(
            token, std::memory_order_release);
        publishSettlement(TransportCommandFamily::Track,
                          TransportSettlementOutcome::Applied,
                          token, track,
                          static_cast<std::uint8_t>(1u << track));
        return true;
    }

    bool applyImmediatePatterns() noexcept {
        for (int track = 0; track < kTrackCount; ++track) {
            if (!applyImmediatePattern(track)) return false;
        }
        return true;
    }

    static void publishGeneration(std::atomic<std::uint64_t>& published,
                                  std::uint64_t token) noexcept {
        std::uint64_t previous = published.load(std::memory_order_relaxed);
        while (previous < token &&
               !published.compare_exchange_weak(previous, token,
                                                std::memory_order_release,
                                                std::memory_order_relaxed)) {
        }
    }

    ArmedColumn* earliestTransportEvent() noexcept {
        ArmedColumn* earliest = nullptr;
        for (ArmedColumn& event : armedColumns) {
            if (!event.active) continue;
            if (event.resetGeneration < observedResetGeneration) {
                cancelTransportEvent(event);
                continue;
            }
            if (event.resetGeneration > observedResetGeneration) continue;
            if (earliest == nullptr || event.targetTick < earliest->targetTick) {
                earliest = &event;
            }
        }
        return earliest;
    }

    bool applyDueTransportEvent() noexcept {
        applyRequestedCancellations();
        ArmedColumn* const due = earliestTransportEvent();
        if (due == nullptr) return true;
        const bool running = shouldRun.load(std::memory_order_acquire);
        if (running && transportTick + kTickEpsilon < due->targetTick) return true;
        if (shared == nullptr ||
            resetGeneration.load(std::memory_order_acquire) != observedResetGeneration) {
            return false;
        }
        std::unique_lock<std::mutex> lock(shared->mutex, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        if (shared->uiMutationInProgress.load(std::memory_order_acquire)) return false;
        if (resetGeneration.load(std::memory_order_acquire) != observedResetGeneration) {
            return false;
        }
        applyRequestedCancellations();
        if (!due->active) return true;

        const double boundary = running ? due->targetTick : transportTick;
        bool changed = false;
        for (int track = 0; track < kTrackCount; ++track) {
            if ((due->trackMask & static_cast<std::uint8_t>(1u << track)) == 0u)
                continue;
            ArmedPattern& trackCommand = armedPatterns[static_cast<std::size_t>(track)];
            if (trackCommand.active &&
                trackCommand.order > due->trackOrders[static_cast<std::size_t>(track)]) {
                const std::uint8_t trackBit = static_cast<std::uint8_t>(1u << track);
                due->trackMask = static_cast<std::uint8_t>(
                    due->trackMask & static_cast<std::uint8_t>(~trackBit));
                due->trackOrders[static_cast<std::size_t>(track)] = 0u;
                continue;
            }
            if (trackCommand.active &&
                trackCommand.order < due->trackOrders[static_cast<std::size_t>(track)]) {
                publishSettlement(TransportCommandFamily::Track,
                                  TransportSettlementOutcome::Cancelled,
                                  trackCommand.token, track);
                trackCommand = ArmedPattern {};
            }
            performance.tracks[static_cast<std::size_t>(track)] =
                due->patterns[static_cast<std::size_t>(track)];
            shared->app.tracks[static_cast<std::size_t>(track)] =
                due->patterns[static_cast<std::size_t>(track)];
            changed = true;
        }
        if (due->settings.applyTempo) {
            const std::uint16_t bpm = static_cast<std::uint16_t>(clampValue(
                static_cast<int>(due->settings.bpm), 30, 300));
            performance.bpm = bpm;
            shared->app.bpm = bpm;
            changed = true;
        }
        if (due->settings.applyScale) {
            const std::uint8_t root =
                static_cast<std::uint8_t>(due->settings.scaleRoot % 12u);
            std::uint16_t mask = static_cast<std::uint16_t>(
                due->settings.scaleMask & 0x0FFFu);
            if (mask == 0u) mask = 1u;
            performance.scaleRoot = root;
            performance.scaleMask = mask;
            shared->app.scaleRoot = root;
            shared->app.scaleMask = mask;
            changed = true;
        }
        if (changed) ++shared->app.editRevision;
        for (int track = 0; track < kTrackCount; ++track) {
            if ((due->trackMask & static_cast<std::uint8_t>(1u << track)) == 0u)
                continue;
            const ArmedPattern& trackCommand =
                armedPatterns[static_cast<std::size_t>(track)];
            if (trackCommand.active &&
                trackCommand.order > due->trackOrders[static_cast<std::size_t>(track)]) {
                continue;
            }
            TrackRuntime& runtime = trackRuntime[static_cast<std::size_t>(track)];
            const TrackData& data = performance.tracks[static_cast<std::size_t>(track)];
            const TrackCommandKind kind =
                due->trackKinds[static_cast<std::size_t>(track)];
            if (kind == TrackCommandKind::ImmediateReset) {
                resetOneTrack(track, data);
            } else if (kind == TrackCommandKind::ImmediateInPlace) {
                preserveTrackPositionForImmediateLoad(track, data);
            } else {
                if (!running) runtime.pendingCount = 0u;
                runtime.nextNominalTick = boundary;
                beginReplacementPattern(track, data, runtime);
            }
        }
        for (std::size_t receiptIndex = 0u;
            receiptIndex < due->columnReceiptCount; ++receiptIndex) {
            const ColumnReceipt& receipt = due->columnReceipts[receiptIndex];
            if (receipt.settled) continue;
            std::uint8_t appliedTrackMask = 0u;
            for (int track = 0; track < kTrackCount; ++track) {
                const std::size_t index = static_cast<std::size_t>(track);
                const std::uint8_t trackBit = static_cast<std::uint8_t>(1u << track);
                if ((receipt.trackMask & trackBit) != 0u &&
                    (due->trackMask & trackBit) != 0u &&
                    due->trackOrders[index] == receipt.order) {
                    appliedTrackMask = static_cast<std::uint8_t>(
                        appliedTrackMask | trackBit);
                }
            }
            const bool appliedTempo = receipt.tempo && due->settings.applyTempo &&
                due->tempoOrder == receipt.order;
            const bool appliedScale = receipt.scale && due->settings.applyScale &&
                due->scaleOrder == receipt.order;
            const bool receiptApplied = appliedTrackMask != 0u || appliedTempo || appliedScale;
            if (receiptApplied) {
                publishGeneration(publishedColumnGeneration, receipt.token);
            }
            publishSettlement(TransportCommandFamily::Column,
                              receiptApplied ? TransportSettlementOutcome::Applied
                                             : TransportSettlementOutcome::Cancelled,
                              receipt.token, -1, appliedTrackMask,
                              appliedTempo, appliedScale);
        }
        for (std::size_t receiptIndex = 0u;
             receiptIndex < due->globalSettingsReceiptCount; ++receiptIndex) {
            const GlobalSettingsReceipt& receipt =
                due->globalSettingsReceipts[receiptIndex];
            if (receipt.settled) continue;
            const bool appliedTempo = receipt.tempo && due->settings.applyTempo &&
                due->tempoOrder == receipt.order;
            const bool appliedScale = receipt.scale && due->settings.applyScale &&
                due->scaleOrder == receipt.order;
            const bool receiptApplied = appliedTempo || appliedScale;
            if (receiptApplied) {
                publishGeneration(publishedGlobalSettingsGeneration, receipt.token);
            }
            publishSettlement(TransportCommandFamily::GlobalSettings,
                              receiptApplied ? TransportSettlementOutcome::Applied
                                             : TransportSettlementOutcome::Cancelled,
                              receipt.token, -1, 0u,
                              appliedTempo, appliedScale);
        }
        *due = ArmedColumn {};
        return true;
    }

    void scheduleOneTrack(int track) noexcept {
        TrackRuntime& runtime = trackRuntime[static_cast<std::size_t>(track)];

        for (std::size_t guard = 0u; guard < kPendingSteps; ++guard) {
            if (runtime.pendingCount >= runtime.pending.size()) return;

            ArmedPattern& armed = armedPatterns[static_cast<std::size_t>(track)];
            const bool armedForCurrentReset =
                armed.active && armed.kind == TrackCommandKind::Cue &&
                armed.resetGeneration == observedResetGeneration;
            const std::size_t trackIndex = static_cast<std::size_t>(track);
            const std::uint8_t trackBit = static_cast<std::uint8_t>(1u << track);
            for (const ArmedColumn& event : armedColumns) {
                if (!event.active ||
                    event.resetGeneration != observedResetGeneration ||
                    (event.trackMask & trackBit) == 0u ||
                    event.trackKinds[trackIndex] != TrackCommandKind::Cue) {
                    continue;
                }
                if (runtime.nextNominalTick >= event.targetTick - kTickEpsilon) return;
            }
            if (runtime.loopBoundaryPending && armedForCurrentReset &&
                runtime.nextNominalTick > transportTick + 1.0e-9) {
                // Commit only at the nominal loop boundary. A negative offset
                // on the replacement's first step cannot precede that atomic
                // transition, so it is clipped to the boundary below.
                return;
            }

            const TrackData& current = performance.tracks[static_cast<std::size_t>(track)];
            const double currentMultiplier =
                clampValue(rateMultiplier(current.rateIndex), 0.25, 4.0);
            const double schedulingHorizon = kTicksPerStep / currentMultiplier;
            if (runtime.nextNominalTick > transportTick + schedulingHorizon + 1.0e-9)
                return;

            if (runtime.loopBoundaryPending && !applyArmedPattern(track, runtime)) return;

            const TrackData& data = performance.tracks[static_cast<std::size_t>(track)];
            const double multiplier = clampValue(rateMultiplier(data.rateIndex), 0.25, 4.0);
            const double period = kTicksPerStep / multiplier;
            if (runtime.nextNominalTick > transportTick + period + 1.0e-9) return;

            const int length = clampValue(static_cast<int>(data.length), 1, kStepCount);
            runtime.cursor = clampValue(runtime.cursor, 0, length - 1);
            const int stepIndex = runtime.cursor;
            const Step& sourceStep = data.steps[static_cast<std::size_t>(stepIndex)];
            ScheduledStep event;
            event.nominalTick = runtime.nextNominalTick;
            event.step = sourceStep;
            event.echo = data.echo;
            event.stepIndex = stepIndex;
            event.rate = multiplier;
            event.loop = runtime.conditionLoop;
            event.ordinal = runtime.ordinal;
            event.transpose = data.transpose.values[static_cast<std::size_t>(
                clampValue(runtime.transposeIndex, 0, 7))];

            const double microOffset = static_cast<double>(sourceStep.microTicks) / multiplier;
            const double shuffleOffset = (runtime.ordinal & 1u) != 0u
                                             ? period * static_cast<double>(
                                                            clampValue(static_cast<int>(data.shuffle),
                                                                       0, 50)) /
                                                   100.0
                                             : 0.0;
            event.dueTick = std::max(0.0, runtime.nextNominalTick + microOffset + shuffleOffset);
            if (runtime.clampReplacementLead) {
                event.dueTick = std::max(event.dueTick, runtime.replacementBoundaryTick);
                runtime.clampReplacementLead = false;
            }

            const std::uint8_t condition = clampValue<std::uint8_t>(sourceStep.condition, 1u, 8u);
            const bool conditionPasses =
                (runtime.conditionLoop % static_cast<std::uint64_t>(condition)) == 0u;
            const bool willTrigger = sourceStep.active && !sourceStep.trigless && conditionPasses;
            if (data.transpose.advance == TransposeAdvance::Step) {
                advanceTranspose(runtime, data.transpose);
            } else if (data.transpose.advance == TransposeAdvance::Trigger && willTrigger) {
                advanceTranspose(runtime, data.transpose);
            }

            ++runtime.ordinal;
            runtime.nextNominalTick += period;
            advanceCursor(track, data, runtime);
            event.loopsAfter = runtime.scheduleLoop;
            runtime.pending[runtime.pendingCount++] = event;
        }
    }

    int findDueStep(int track) const noexcept {
        const TrackRuntime& runtime = trackRuntime[static_cast<std::size_t>(track)];
        int best = -1;
        double bestTick = std::numeric_limits<double>::infinity();
        std::uint64_t bestOrdinal = std::numeric_limits<std::uint64_t>::max();
        for (std::size_t index = 0u; index < runtime.pendingCount; ++index) {
            const ScheduledStep& event = runtime.pending[index];
            if (event.dueTick <= transportTick + 1.0e-9 &&
                (event.dueTick < bestTick ||
                 (event.dueTick == bestTick && event.ordinal < bestOrdinal))) {
                best = static_cast<int>(index);
                bestTick = event.dueTick;
                bestOrdinal = event.ordinal;
            }
        }
        return best;
    }

    ScheduledStep removePending(int track, int index) noexcept {
        TrackRuntime& runtime = trackRuntime[static_cast<std::size_t>(track)];
        const std::size_t position = static_cast<std::size_t>(index);
        ScheduledStep result = runtime.pending[position];
        --runtime.pendingCount;
        for (std::size_t move = position; move < runtime.pendingCount; ++move) {
            runtime.pending[move] = runtime.pending[move + 1u];
        }
        return result;
    }

    float waveformValue(ModWave wave, std::uint64_t position, int speed,
                        ModRuntime& runtime) noexcept {
        const std::uint64_t period = static_cast<std::uint64_t>(std::max(speed, 1));
        const std::uint64_t phaseStep = position % period;
        const float phase = static_cast<float>(phaseStep) / static_cast<float>(period);
        switch (wave) {
            case ModWave::RampDown:
                return 1.0f - 2.0f * phase;
            case ModWave::RampUp:
                return -1.0f + 2.0f * phase;
            case ModWave::Triangle:
                return 1.0f - 4.0f * std::fabs(phase - 0.5f);
            case ModWave::Square:
                return phase < 0.5f ? 1.0f : -1.0f;
            case ModWave::Random: {
                const std::uint64_t cycle = position / period;
                if (cycle != runtime.randomCycle) {
                    runtime.randomCycle = cycle;
                    const std::uint32_t bits = xorshift32(randomState);
                    runtime.randomValue = static_cast<float>(bits & 0xFFFFu) / 32767.5f - 1.0f;
                }
                return runtime.randomValue;
            }
        }
        return 0.0f;
    }

    ModValues sampleModulators(int targetTrack) noexcept {
        ModValues values;
        for (int origin = 0; origin < kTrackCount; ++origin) {
            const ModulatorSettings& settings =
                performance.tracks[static_cast<std::size_t>(origin)].modulator;
            if (static_cast<int>(settings.targetTrack % kTrackCount) != targetTrack) continue;
            ModRuntime& runtime = modRuntime[static_cast<std::size_t>(origin)];
            const int speed = clampValue(static_cast<int>(settings.speed), 1, 64);
            const std::uint64_t position = runtime.counter +
                                           static_cast<std::uint64_t>(settings.offset);
            const float wave = waveformValue(settings.wave, position, speed, runtime);
            const int amount = static_cast<int>(std::lround(
                wave * static_cast<float>(settings.depth)));
            switch (settings.destination) {
                case ModDest::Level: values.level += amount; break;
                case ModDest::Pan: values.pan += amount; break;
                case ModDest::Note: values.note += amount; break;
                case ModDest::ModDepth: values.modDepth += amount; break;
                case ModDest::ModFeedback: values.modFeedback += amount; break;
                case ModDest::Sweep: values.sweep += amount; break;
                case ModDest::NoiseRate: values.noiseRate += amount; break;
            }
            ++runtime.counter;
        }
        return values;
    }

    void applyModulation(Step& step, const ModValues& modulation) const noexcept {
        step.level = static_cast<std::uint8_t>(
            clampValue(static_cast<int>(step.level) + modulation.level, 0, 127));
        step.note = static_cast<std::uint8_t>(
            clampValue(static_cast<int>(step.note) + modulation.note, 0, 127));
        step.fm.modDepth = static_cast<std::uint8_t>(
            clampValue(static_cast<int>(step.fm.modDepth) + modulation.modDepth, 0, 127));
        step.fm.modFeedback = static_cast<std::uint8_t>(clampValue(
            static_cast<int>(step.fm.modFeedback) + modulation.modFeedback, 0, 127));
        step.fm.sweepDepth = static_cast<std::int8_t>(clampValue(
            static_cast<int>(step.fm.sweepDepth) + modulation.sweep, -127, 127));
        step.noise.rate = static_cast<std::uint8_t>(
            clampValue(static_cast<int>(step.noise.rate) + modulation.noiseRate, 0, 127));

        const int basePan = step.pan == Pan::Left ? -64 : (step.pan == Pan::Right ? 64 : 0);
        const int modulatedPan = basePan + modulation.pan;
        step.pan = modulatedPan < -21 ? Pan::Left : (modulatedPan > 21 ? Pan::Right : Pan::Center);
    }

    int quantizedNote(int note) const noexcept {
        return quantizeNote(clampValue(note, 0, 127),
                            static_cast<std::uint8_t>(performance.scaleRoot % 12u),
                            static_cast<std::uint16_t>(performance.scaleMask & 0x0FFFu));
    }

    bool canUseHomeVoice(int track, bool force) const noexcept {
        if (track < 0 || track >= kFmTrackCount) return false;
        if (force) return true;
        const FmVoice& voice = fmVoices[static_cast<std::size_t>(track)];
        return !voice.active || !voice.chordVoice || voice.ownerTrack <= track;
    }

    int allocateChordVoice(int sourceTrack,
                           const std::array<bool, kFmTrackCount>& alreadyUsed) const noexcept {
        for (int candidate = 0; candidate < kFmTrackCount; ++candidate) {
            if (candidate == sourceTrack || alreadyUsed[static_cast<std::size_t>(candidate)]) continue;
            if (!fmVoices[static_cast<std::size_t>(candidate)].active) return candidate;
        }
        for (int candidate = 0; candidate < sourceTrack; ++candidate) {
            if (alreadyUsed[static_cast<std::size_t>(candidate)]) continue;
            const FmVoice& voice = fmVoices[static_cast<std::size_t>(candidate)];
            if (voice.ownerTrack <= sourceTrack) return candidate;
        }
        return -1;
    }

    void triggerFm(int track, const Step& step, int baseNote, bool isPreview,
                   bool force = false) noexcept {
        if (track < 0 || track >= kFmTrackCount || !canUseHomeVoice(track, force)) return;
        std::array<bool, kFmTrackCount> used {};
        const int rootNote = quantizedNote(baseNote);
        fmVoices[static_cast<std::size_t>(track)].trigger(
            track, step, static_cast<double>(rootNote), 0, false, isPreview, sampleRate);
        used[static_cast<std::size_t>(track)] = true;

        for (std::int8_t intervalValue : step.chord) {
            const int interval = static_cast<int>(intervalValue);
            if (interval <= 0) continue;
            const int voiceIndex = allocateChordVoice(track, used);
            if (voiceIndex < 0) break;
            const int chordNote = quantizedNote(baseNote + interval);
            fmVoices[static_cast<std::size_t>(voiceIndex)].trigger(
                track, step, static_cast<double>(chordNote), interval, true, isPreview, sampleRate);
            used[static_cast<std::size_t>(voiceIndex)] = true;
        }
    }

    void changeFm(int track, const Step& step, int baseNote) noexcept {
        for (FmVoice& voice : fmVoices) {
            if (!voice.active || voice.ownerTrack != track) continue;
            const int note = quantizedNote(baseNote + voice.noteOffset);
            voice.change(step, static_cast<double>(note), sampleRate);
        }
    }

    void scheduleEchoes(int track, const Step& step, int baseNote,
                        const EchoSettings& settings, double trackRate,
                        double triggerTick) noexcept {
        if (!step.echo) return;
        const int repeats = clampValue(static_cast<int>(settings.repeats), 0, 8);
        const int speedTicks = clampValue(static_cast<int>(settings.speedTicks), 1, 96);
        const int modulo = clampValue(static_cast<int>(settings.transposeModulo), 1, 8);
        const double safeRate = clampValue(trackRate, 0.25, 4.0);
        for (int repeat = 1; repeat <= repeats; ++repeat) {
            EchoEvent event;
            event.dueTick = triggerTick +
                            static_cast<double>(repeat * speedTicks) / safeRate;
            event.order = echoOrder++;
            event.track = track;
            const int transposeStage = (repeat - 1) % modulo + 1;
            event.note = clampValue(baseNote + static_cast<int>(settings.transpose) *
                                                   transposeStage,
                                    0, 127);
            event.step = step;
            event.step.echo = false;
            event.step.trigless = false;
            event.step.active = true;
            event.step.level = static_cast<std::uint8_t>(clampValue(
                static_cast<int>(step.level) + static_cast<int>(settings.volumeDelta) * repeat,
                0, 127));
            event.step.fm.modDepth = static_cast<std::uint8_t>(clampValue(
                static_cast<int>(step.fm.modDepth) + static_cast<int>(settings.modDelta) * repeat,
                0, 127));
            event.step.fm.modFeedback = static_cast<std::uint8_t>(clampValue(
                static_cast<int>(step.fm.modFeedback) +
                    static_cast<int>(settings.feedbackDelta) * repeat,
                0, 127));
            switch (settings.pan) {
                case EchoPan::Original: break;
                case EchoPan::Left: event.step.pan = Pan::Left; break;
                case EchoPan::Right: event.step.pan = Pan::Right; break;
                case EchoPan::PingPong:
                    event.step.pan = (repeat & 1) != 0 ? Pan::Left : Pan::Right;
                    break;
            }
            if (!echoes.push(event)) break;
        }
    }

    void performEcho(const EchoEvent& event, bool soloActive) noexcept {
        if (!trackIsAudible(event.track, soloActive)) return;
        if (event.track < kFmTrackCount) {
            triggerFm(event.track, event.step, event.note, false);
        } else if (event.track == kFmTrackCount) {
            noiseVoice.trigger(event.step, static_cast<double>(event.step.noise.rate), false,
                               sampleRate);
        }
    }

    void performStep(int track, const ScheduledStep& event, bool soloActive) noexcept {
        publishedPlayheads[static_cast<std::size_t>(track)].store(event.stepIndex,
                                                                  std::memory_order_relaxed);
        const std::uint64_t previousLoops =
            publishedLoops[static_cast<std::size_t>(track)].load(std::memory_order_relaxed);
        publishedLoops[static_cast<std::size_t>(track)].store(
            std::max(previousLoops, event.loopsAfter), std::memory_order_relaxed);

        Step step = event.step;
        const std::uint8_t condition = clampValue<std::uint8_t>(step.condition, 1u, 8u);
        const bool conditionPasses =
            (event.loop % static_cast<std::uint64_t>(condition)) == 0u;
        if (!step.active || !conditionPasses) return;

        const ModValues modulation = sampleModulators(track);
        applyModulation(step, modulation);
        int baseNote = static_cast<int>(step.note);
        if (step.transpose) baseNote += event.transpose;
        baseNote = quantizedNote(baseNote);

        if (step.trigless) {
            if (track < kFmTrackCount) {
                changeFm(track, step, baseNote);
            } else if (track == kFmTrackCount) {
                noiseVoice.change(step, static_cast<double>(step.noise.rate), sampleRate);
            }
            return;
        }

        if (!trackIsAudible(track, soloActive)) return;
        if (track < kFmTrackCount) {
            triggerFm(track, step, baseNote, false);
        } else if (track == kFmTrackCount) {
            noiseVoice.trigger(step, static_cast<double>(step.noise.rate), false, sampleRate);
        }
        scheduleEchoes(track, step, baseNote, event.echo, event.rate, event.dueTick);
    }

    void processSequencerEvents(bool soloActive) noexcept {
        while (const EchoEvent* next = echoes.front()) {
            if (next->dueTick > transportTick + 1.0e-9) break;
            const EchoEvent event = echoes.pop();
            performEcho(event, soloActive);
        }

        // Higher-numbered FM tracks own the stronger stealing priority. Process
        // them first when multiple nominal steps land on the same sample.
        for (int track = kTrackCount - 1; track >= 0; --track) {
            for (;;) {
                const int index = findDueStep(track);
                if (index < 0) break;
                const ScheduledStep event = removePending(track, index);
                performStep(track, event, soloActive);
            }
        }
    }

    void processPreviews() noexcept {
        PreviewCommand command;
        while (dequeuePreview(command)) {
            const int track = clampValue(command.track, 0, kTrackCount - 1);
            Step step = command.step;
            step.active = true;
            step.trigless = false;
            const int requestedNote = command.note >= 0 ? command.note
                                                        : static_cast<int>(step.note);
            const int note = quantizedNote(requestedNote);
            if (track < kFmTrackCount) {
                triggerFm(track, step, note, true, true);
            } else {
                noiseVoice.trigger(step, static_cast<double>(step.noise.rate), true, sampleRate);
            }
        }
    }

    void resetTransport() noexcept {
        transportTick = 0.0;
        discardStaleArmedPatterns();
        echoes.clear();
        echoOrder = 0u;
        randomState = 0xA341316Cu;
        for (FmVoice& voice : fmVoices) voice.stop();
        noiseVoice.stop();
        publishedPeakLeft.store(0.0f, std::memory_order_relaxed);
        publishedPeakRight.store(0.0f, std::memory_order_relaxed);
        for (int track = 0; track < kTrackCount; ++track) {
            TrackRuntime& runtime = trackRuntime[static_cast<std::size_t>(track)];
            runtime = TrackRuntime {};
            const TrackData& data = performance.tracks[static_cast<std::size_t>(track)];
            const int length = clampValue(static_cast<int>(data.length), 1, kStepCount);
            if (data.direction == Direction::Reverse) runtime.cursor = length - 1;
            else if (data.direction == Direction::Random) {
                runtime.cursor = static_cast<int>(xorshift32(randomState) %
                                                  static_cast<std::uint32_t>(length));
            }
            modRuntime[static_cast<std::size_t>(track)] = ModRuntime {};
            publishedPlayheads[static_cast<std::size_t>(track)].store(-1,
                                                                      std::memory_order_relaxed);
            publishedLoops[static_cast<std::size_t>(track)].store(0u,
                                                                  std::memory_order_relaxed);
        }
    }

    void render(float* output, std::size_t frameCount) noexcept {
        std::uint64_t snapshotResetGeneration = observedResetGeneration;
        const bool refreshed = refreshPerformance(snapshotResetGeneration);
        const std::uint64_t requestedReset = resetGeneration.load(std::memory_order_acquire);
        bool resetDeferred = false;
        if (requestedReset != observedResetGeneration) {
            if (refreshed && snapshotResetGeneration == requestedReset) {
                observedResetGeneration = requestedReset;
                resetTransport();
            } else {
                resetDeferred = true;
            }
        }
        if (resetGeneration.load(std::memory_order_acquire) != observedResetGeneration) {
            resetDeferred = true;
        }
        if (!resetDeferred) {
            drainPatternCommands();
            drainColumnCommands();
            drainGlobalSettingsCommands();
            applyRequestedCancellations();
        }

        const bool running = shouldRun.load(std::memory_order_acquire);
        float blockPeakLeft = 0.0f;
        float blockPeakRight = 0.0f;

        for (std::size_t frame = 0u; frame < frameCount; ++frame) {
            processPreviews();
            bool commandsReady = !resetDeferred;
            if (commandsReady) commandsReady = applyDueTransportEvent();
            if (commandsReady) commandsReady = applyImmediatePatterns();
            const bool advanceTransport = running && commandsReady;
            if (advanceTransport) {
                for (int track = 0; track < kTrackCount; ++track) scheduleOneTrack(track);
            }
            // A queued pattern can change mute/solo exactly at this sample's
            // loop boundary, so derive audibility after scheduling commits it.
            const bool soloActive = anySolo();
            if (advanceTransport) {
                processSequencerEvents(soloActive);
            }

            float left = 0.0f;
            float right = 0.0f;
            for (FmVoice& voice : fmVoices) {
                float voiceLeft = 0.0f;
                float voiceRight = 0.0f;
                voice.renderStereo(sampleRate, voiceLeft, voiceRight);
                if (!voice.active && voiceLeft == 0.0f && voiceRight == 0.0f) continue;
                if (!voice.preview && !trackIsAudible(voice.ownerTrack, soloActive)) continue;
                left += voiceLeft;
                right += voiceRight;
            }

            const float noise = noiseVoice.render(sampleRate);
            if (noiseVoice.active &&
                (noiseVoice.preview || trackIsAudible(noiseVoice.ownerTrack, soloActive))) {
                float leftGain = 0.0f;
                float rightGain = 0.0f;
                panGains(noiseVoice.step.pan, leftGain, rightGain);
                left += noise * leftGain;
                right += noise * rightGain;
            }

            constexpr float masterGain = 0.24f;
            left = std::tanh(left * masterGain);
            right = std::tanh(right * masterGain);
            if (!std::isfinite(left)) left = 0.0f;
            if (!std::isfinite(right)) right = 0.0f;
            output[frame * 2u] = left;
            output[frame * 2u + 1u] = right;
            blockPeakLeft = std::max(blockPeakLeft, std::fabs(left));
            blockPeakRight = std::max(blockPeakRight, std::fabs(right));

            if (advanceTransport) {
                const int bpm = clampValue(static_cast<int>(performance.bpm), 30, 300);
                const double ticksPerFrame =
                    static_cast<double>(bpm) * static_cast<double>(kPpqn) /
                    (60.0 * static_cast<double>(sampleRate));
                transportTick += ticksPerFrame;
            }
        }

        const double decayTime = 0.45;
        const float decay = static_cast<float>(std::exp(
            -static_cast<double>(frameCount) /
            (std::max(1.0, static_cast<double>(sampleRate) * decayTime))));
        const float oldLeft = publishedPeakLeft.load(std::memory_order_relaxed);
        const float oldRight = publishedPeakRight.load(std::memory_order_relaxed);
        publishedPeakLeft.store(std::max(blockPeakLeft, oldLeft * decay),
                                std::memory_order_relaxed);
        publishedPeakRight.store(std::max(blockPeakRight, oldRight * decay),
                                 std::memory_order_relaxed);
        publishedFrames.fetch_add(static_cast<std::uint64_t>(frameCount),
                                  std::memory_order_relaxed);
    }

    static void audioCallback(void* userdata, Uint8* stream, int byteCount) noexcept {
        if (stream == nullptr || byteCount <= 0) return;
        SDL_memset(stream, 0, static_cast<std::size_t>(byteCount));
        auto* implementation = static_cast<Impl*>(userdata);
        if (implementation == nullptr) return;
        constexpr std::size_t bytesPerFrame = sizeof(float) * kOutputChannels;
        const std::size_t frameCount = static_cast<std::size_t>(byteCount) / bytesPerFrame;
        implementation->render(reinterpret_cast<float*>(stream), frameCount);
    }
};

AudioEngine::AudioEngine() : impl_(new Impl) {}

AudioEngine::~AudioEngine() {
    close();
    delete impl_;
    impl_ = nullptr;
}

bool AudioEngine::open(SharedState& state) {
    close();
    impl_->shared = &state;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        impl_->performance = capturePerformance(state.app);
    }
    impl_->sampleRate = kDefaultSampleRate;
    impl_->initializePreviewQueue();
    impl_->initializePatternCommandQueue();
    impl_->initializeColumnCommandQueue();
    impl_->initializeGlobalSettingsCommandQueue();
    impl_->initializeSettlementState();
    impl_->observedResetGeneration = impl_->resetGeneration.load(std::memory_order_relaxed);
    impl_->resetTransport();
    impl_->publishedPeakLeft.store(0.0f, std::memory_order_relaxed);
    impl_->publishedPeakRight.store(0.0f, std::memory_order_relaxed);
    impl_->publishedFrames.store(0u, std::memory_order_relaxed);
    for (int track = 0; track < kTrackCount; ++track) {
        impl_->submittedPatternGenerations[static_cast<std::size_t>(track)].store(
            0u, std::memory_order_relaxed);
        impl_->publishedPatternGenerations[static_cast<std::size_t>(track)].store(
            0u, std::memory_order_relaxed);
        impl_->nextPatternTokens[static_cast<std::size_t>(track)].store(
            0u, std::memory_order_relaxed);
    }
    impl_->submittedColumnGeneration.store(0u, std::memory_order_relaxed);
    impl_->publishedColumnGeneration.store(0u, std::memory_order_relaxed);
    impl_->nextColumnToken.store(0u, std::memory_order_relaxed);
    impl_->submittedGlobalSettingsGeneration.store(0u, std::memory_order_relaxed);
    impl_->publishedGlobalSettingsGeneration.store(0u, std::memory_order_relaxed);
    impl_->nextGlobalSettingsToken.store(0u, std::memory_order_relaxed);
    impl_->nextReplacementOrder.store(0u, std::memory_order_relaxed);
    impl_->errorMessage.clear();

    SDL_AudioSpec desired {};
    desired.freq = kDefaultSampleRate;
    desired.format = AUDIO_F32SYS;
    desired.channels = static_cast<Uint8>(kOutputChannels);
    desired.samples = kDefaultBufferFrames;
    desired.callback = &Impl::audioCallback;
    desired.userdata = impl_;

    impl_->device = SDL_OpenAudioDevice(
        nullptr, 0, &desired, &impl_->obtained,
        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (impl_->device == 0u) {
        impl_->errorMessage = SDL_GetError();
        impl_->shared = nullptr;
        return false;
    }
    if (impl_->obtained.format != AUDIO_F32SYS ||
        impl_->obtained.channels != static_cast<Uint8>(kOutputChannels) ||
        impl_->obtained.freq <= 0) {
        impl_->errorMessage = "audio device did not provide stereo 32-bit float output";
        SDL_CloseAudioDevice(impl_->device);
        impl_->device = 0u;
        impl_->shared = nullptr;
        return false;
    }

    impl_->sampleRate = impl_->obtained.freq;
    impl_->resetTransport();
    impl_->isAvailable.store(true, std::memory_order_release);
    SDL_PauseAudioDevice(impl_->device, 0);
    return true;
}

void AudioEngine::close() {
    if (impl_ == nullptr) return;
    impl_->shouldRun.store(false, std::memory_order_release);
    impl_->isAvailable.store(false, std::memory_order_release);
    if (impl_->device != 0u) {
        SDL_PauseAudioDevice(impl_->device, 1);
        SDL_CloseAudioDevice(impl_->device);
        impl_->device = 0u;
    }
    impl_->shared = nullptr;
}

void AudioEngine::setRunning(bool running) {
    if (impl_ == nullptr) return;
    if (running && !impl_->isAvailable.load(std::memory_order_acquire)) return;
    impl_->shouldRun.store(running, std::memory_order_release);
}

void AudioEngine::toggleRunning() {
    if (impl_ == nullptr) return;
    bool expected = impl_->shouldRun.load(std::memory_order_relaxed);
    for (;;) {
        const bool desired = !expected;
        if (desired && !impl_->isAvailable.load(std::memory_order_acquire)) return;
        if (impl_->shouldRun.compare_exchange_weak(expected, desired,
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed)) {
            return;
        }
    }
}

void AudioEngine::reset() {
    if (impl_ == nullptr) return;
    impl_->resetGeneration.fetch_add(1u, std::memory_order_release);
}

void AudioEngine::preview(int track, const Step& step, int note) {
    if (impl_ == nullptr) return;
    PreviewCommand command;
    command.track = clampValue(track, 0, kTrackCount - 1);
    command.note = note;
    command.step = step;
    (void)impl_->enqueuePreview(command);
}

bool AudioEngine::queuePattern(int track, const TrackData& pattern) {
    if (impl_ == nullptr || track < 0 || track >= kTrackCount ||
        !impl_->isAvailable.load(std::memory_order_acquire)) {
        return false;
    }
    PatternCommand command;
    command.track = track;
    command.resetGeneration = impl_->resetGeneration.load(std::memory_order_acquire);
    command.token = impl_->nextPatternTokens[static_cast<std::size_t>(track)].fetch_add(
                        1u, std::memory_order_relaxed) +
                    1u;
    command.order = impl_->nextReplacementOrder.fetch_add(1u, std::memory_order_relaxed) + 1u;
    command.kind = TrackCommandKind::Cue;
    command.pattern = pattern;
    if (!impl_->enqueuePatternCommand(command)) return false;

    auto& submitted = impl_->submittedPatternGenerations[static_cast<std::size_t>(track)];
    std::uint64_t previous = submitted.load(std::memory_order_relaxed);
    while (previous < command.token &&
           !submitted.compare_exchange_weak(previous, command.token,
                                            std::memory_order_release,
                                            std::memory_order_relaxed)) {
    }
    return true;
}

bool AudioEngine::loadTrackImmediate(int track, const TrackData& pattern,
                                     TrackLoadMode mode) {
    if (impl_ == nullptr || track < 0 || track >= kTrackCount ||
        !impl_->isAvailable.load(std::memory_order_acquire)) {
        return false;
    }
    PatternCommand command;
    command.track = track;
    command.resetGeneration = impl_->resetGeneration.load(std::memory_order_acquire);
    command.token = impl_->nextPatternTokens[static_cast<std::size_t>(track)].fetch_add(
                        1u, std::memory_order_relaxed) +
                    1u;
    command.order = impl_->nextReplacementOrder.fetch_add(1u, std::memory_order_relaxed) + 1u;
    command.kind = mode == TrackLoadMode::Reset ? TrackCommandKind::ImmediateReset
                                                : TrackCommandKind::ImmediateInPlace;
    command.pattern = pattern;
    if (!impl_->enqueuePatternCommand(command)) return false;

    auto& submitted = impl_->submittedPatternGenerations[static_cast<std::size_t>(track)];
    std::uint64_t previous = submitted.load(std::memory_order_relaxed);
    while (previous < command.token &&
           !submitted.compare_exchange_weak(previous, command.token,
                                            std::memory_order_release,
                                            std::memory_order_relaxed)) {
    }
    return true;
}

bool AudioEngine::queuePatternColumn(
    const std::array<TrackData, kTrackCount>& patterns, std::uint8_t trackMask,
    const TimedGlobalSettings& settings) {
    if (impl_ == nullptr || !impl_->isAvailable.load(std::memory_order_acquire)) return false;
    trackMask = static_cast<std::uint8_t>(
        trackMask & static_cast<std::uint8_t>((1u << kTrackCount) - 1u));
    if (trackMask == 0u) return false;

    ColumnCommand command;
    command.resetGeneration = impl_->resetGeneration.load(std::memory_order_acquire);
    command.token = impl_->nextColumnToken.fetch_add(1u, std::memory_order_relaxed) + 1u;
    command.order = impl_->nextReplacementOrder.fetch_add(1u, std::memory_order_relaxed) + 1u;
    command.trackMask = trackMask;
    command.kind = TrackCommandKind::Cue;
    command.applyImmediately = !impl_->shouldRun.load(std::memory_order_acquire);
    command.settings = settings;
    command.settings.bpm = static_cast<std::uint16_t>(
        clampValue(static_cast<int>(command.settings.bpm), 30, 300));
    command.settings.scaleRoot =
        static_cast<std::uint8_t>(command.settings.scaleRoot % 12u);
    command.settings.scaleMask =
        static_cast<std::uint16_t>(command.settings.scaleMask & 0x0FFFu);
    if (command.settings.scaleMask == 0u) command.settings.scaleMask = 1u;
    command.patterns = patterns;
    if (!enqueueBounded(impl_->columnCommandSlots, impl_->columnCommandEnqueue, command)) {
        return false;
    }

    std::uint64_t previous = impl_->submittedColumnGeneration.load(std::memory_order_relaxed);
    while (previous < command.token &&
           !impl_->submittedColumnGeneration.compare_exchange_weak(
               previous, command.token, std::memory_order_release,
               std::memory_order_relaxed)) {
    }
    return true;
}

bool AudioEngine::loadPatternColumnImmediate(
    const std::array<TrackData, kTrackCount>& patterns, TrackLoadMode mode,
    std::uint8_t trackMask, const TimedGlobalSettings& settings) {
    if (impl_ == nullptr || !impl_->isAvailable.load(std::memory_order_acquire)) return false;
    trackMask = static_cast<std::uint8_t>(
        trackMask & static_cast<std::uint8_t>((1u << kTrackCount) - 1u));
    if (trackMask == 0u) return false;

    ColumnCommand command;
    command.resetGeneration = impl_->resetGeneration.load(std::memory_order_acquire);
    command.token = impl_->nextColumnToken.fetch_add(1u, std::memory_order_relaxed) + 1u;
    command.order = impl_->nextReplacementOrder.fetch_add(1u, std::memory_order_relaxed) + 1u;
    command.trackMask = trackMask;
    command.kind = mode == TrackLoadMode::Reset ? TrackCommandKind::ImmediateReset
                                                : TrackCommandKind::ImmediateInPlace;
    command.applyImmediately = true;
    command.settings = settings;
    command.settings.bpm = static_cast<std::uint16_t>(
        clampValue(static_cast<int>(command.settings.bpm), 30, 300));
    command.settings.scaleRoot =
        static_cast<std::uint8_t>(command.settings.scaleRoot % 12u);
    command.settings.scaleMask =
        static_cast<std::uint16_t>(command.settings.scaleMask & 0x0FFFu);
    if (command.settings.scaleMask == 0u) command.settings.scaleMask = 1u;
    command.patterns = patterns;
    if (!enqueueBounded(impl_->columnCommandSlots, impl_->columnCommandEnqueue, command)) {
        return false;
    }

    std::uint64_t previous = impl_->submittedColumnGeneration.load(std::memory_order_relaxed);
    while (previous < command.token &&
           !impl_->submittedColumnGeneration.compare_exchange_weak(
               previous, command.token, std::memory_order_release,
               std::memory_order_relaxed)) {
    }
    return true;
}

bool AudioEngine::queueGlobalSettings(const TimedGlobalSettings& settings) {
    if (impl_ == nullptr || !impl_->isAvailable.load(std::memory_order_acquire) ||
        (!settings.applyTempo && !settings.applyScale)) {
        return false;
    }
    GlobalSettingsCommand command;
    command.resetGeneration = impl_->resetGeneration.load(std::memory_order_acquire);
    command.token = impl_->nextGlobalSettingsToken.fetch_add(
                        1u, std::memory_order_relaxed) +
                    1u;
    command.order = impl_->nextReplacementOrder.fetch_add(
                        1u, std::memory_order_relaxed) +
                    1u;
    command.applyImmediately = !impl_->shouldRun.load(std::memory_order_acquire);
    command.settings = settings;
    command.settings.bpm = static_cast<std::uint16_t>(
        clampValue(static_cast<int>(command.settings.bpm), 30, 300));
    command.settings.scaleRoot =
        static_cast<std::uint8_t>(command.settings.scaleRoot % 12u);
    command.settings.scaleMask =
        static_cast<std::uint16_t>(command.settings.scaleMask & 0x0FFFu);
    if (command.settings.scaleMask == 0u) command.settings.scaleMask = 1u;
    if (!enqueueBounded(impl_->globalSettingsCommandSlots,
                        impl_->globalSettingsCommandEnqueue, command)) {
        return false;
    }

    std::uint64_t previous =
        impl_->submittedGlobalSettingsGeneration.load(std::memory_order_relaxed);
    while (previous < command.token &&
           !impl_->submittedGlobalSettingsGeneration.compare_exchange_weak(
               previous, command.token, std::memory_order_release,
               std::memory_order_relaxed)) {
    }
    return true;
}

bool AudioEngine::cancelTransportCommand(TransportCommandFamily family,
                                         std::uint64_t token, int track) {
    if (impl_ == nullptr || token == 0u ||
        !impl_->isAvailable.load(std::memory_order_acquire)) {
        return false;
    }
    switch (family) {
    case TransportCommandFamily::Track:
        if (track < 0 || track >= kTrackCount ||
            token > impl_->submittedPatternGenerations[static_cast<std::size_t>(track)]
                        .load(std::memory_order_acquire)) {
            return false;
        }
        break;
    case TransportCommandFamily::Column:
        if (token > impl_->submittedColumnGeneration.load(std::memory_order_acquire)) {
            return false;
        }
        track = -1;
        break;
    case TransportCommandFamily::GlobalSettings:
        if (token >
            impl_->submittedGlobalSettingsGeneration.load(std::memory_order_acquire)) {
            return false;
        }
        track = -1;
        break;
    default:
        return false;
    }
    if (impl_->settlementPublished(family, token, track)) return true;
    impl_->requestCancellation(family, token, track);
    return true;
}

TransportStatus AudioEngine::status() const {
    TransportStatus result;
    if (impl_ == nullptr) return result;
    result.running = impl_->shouldRun.load(std::memory_order_acquire);
    for (int track = 0; track < kTrackCount; ++track) {
        result.playheads[static_cast<std::size_t>(track)] =
            impl_->publishedPlayheads[static_cast<std::size_t>(track)].load(
                std::memory_order_relaxed);
        result.loops[static_cast<std::size_t>(track)] =
            impl_->publishedLoops[static_cast<std::size_t>(track)].load(
                std::memory_order_relaxed);
        result.submittedPatternGenerations[static_cast<std::size_t>(track)] =
            impl_->submittedPatternGenerations[static_cast<std::size_t>(track)].load(
                std::memory_order_acquire);
        result.appliedPatternGenerations[static_cast<std::size_t>(track)] =
            impl_->publishedPatternGenerations[static_cast<std::size_t>(track)].load(
                std::memory_order_acquire);
        result.settledPatternGenerations[static_cast<std::size_t>(track)] =
            impl_->settledPatternGenerations[static_cast<std::size_t>(track)].load(
                std::memory_order_acquire);
    }
    result.submittedColumnGeneration =
        impl_->submittedColumnGeneration.load(std::memory_order_acquire);
    result.appliedColumnGeneration =
        impl_->publishedColumnGeneration.load(std::memory_order_acquire);
    result.submittedGlobalSettingsGeneration =
        impl_->submittedGlobalSettingsGeneration.load(std::memory_order_acquire);
    result.appliedGlobalSettingsGeneration =
        impl_->publishedGlobalSettingsGeneration.load(std::memory_order_acquire);
    result.settledColumnGeneration =
        impl_->settledColumnGeneration.load(std::memory_order_acquire);
    result.settledGlobalSettingsGeneration =
        impl_->settledGlobalSettingsGeneration.load(std::memory_order_acquire);
    result.latestSettlementSequence =
        impl_->latestSettlementSequence.load(std::memory_order_acquire);
    for (std::size_t index = 0u; index < impl_->settlementSlots.size(); ++index) {
        const SettlementSlot& slot = impl_->settlementSlots[index];
        const std::uint64_t before = slot.sequence.load(std::memory_order_acquire);
        if (before == 0u) continue;
        TransportSettlement settlement;
        settlement.sequence = before;
        settlement.token = slot.token.load(std::memory_order_relaxed);
        settlement.track = slot.track.load(std::memory_order_relaxed);
        settlement.family = static_cast<TransportCommandFamily>(
            slot.family.load(std::memory_order_relaxed));
        settlement.outcome = static_cast<TransportSettlementOutcome>(
            slot.outcome.load(std::memory_order_relaxed));
        settlement.appliedTrackMask =
            slot.appliedTrackMask.load(std::memory_order_relaxed);
        settlement.appliedTempo = slot.appliedTempo.load(std::memory_order_relaxed);
        settlement.appliedScale = slot.appliedScale.load(std::memory_order_relaxed);
        const std::uint64_t after = slot.sequence.load(std::memory_order_acquire);
        if (before == after) result.settlements[index] = settlement;
    }
    result.peakLeft = impl_->publishedPeakLeft.load(std::memory_order_relaxed);
    result.peakRight = impl_->publishedPeakRight.load(std::memory_order_relaxed);
    result.renderedFrames = impl_->publishedFrames.load(std::memory_order_relaxed);
    return result;
}

bool AudioEngine::available() const {
    return impl_ != nullptr && impl_->isAvailable.load(std::memory_order_acquire);
}

const std::string& AudioEngine::error() const {
    static const std::string empty;
    return impl_ != nullptr ? impl_->errorMessage : empty;
}

} // namespace fms
