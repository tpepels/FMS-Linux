#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace fms {

constexpr int kTrackCount = 5;
constexpr int kFmTrackCount = 4;
constexpr int kStepCount = 16;
constexpr int kPatternCount = 128;
constexpr int kPaletteSize = 14;
constexpr int kPpqn = 24;

enum class Pan : std::uint8_t { Left, Center, Right };
enum class Direction : std::uint8_t { Forward, PingPong, Reverse, Random };
enum class SynthMode : std::uint8_t { FM, Parallel };
enum class EchoPan : std::uint8_t { Original, Left, Right, PingPong };
enum class ModWave : std::uint8_t { RampDown, RampUp, Triangle, Square, Random };
enum class ModDest : std::uint8_t {
    Level,
    Pan,
    Note,
    ModDepth,
    ModFeedback,
    Sweep,
    NoiseRate,
};
enum class TransposeAdvance : std::uint8_t { Pattern, Step, Trigger };

enum class AdvancedFmAlgorithm : std::uint8_t {
    Algorithm1,
    Algorithm2,
    Algorithm3,
    Algorithm4,
    Algorithm5,
    Algorithm6,
    Algorithm7,
    Algorithm8,
    Algorithm9,
    Algorithm10,
    Algorithm11,
    Algorithm12,
};

enum class AdvancedOscShape : std::uint8_t {
    Sine,
    Triangle,
    Saw,
    Square,
    Pulse,
    Noise,
};

enum class AdvancedFilterMode : std::uint8_t {
    Off,
    LowPass,
    HighPass,
    BandPass,
    Notch,
};

enum class AdvancedDriveMode : std::uint8_t {
    Off,
    SoftClip,
    HardClip,
    Wavefold,
};

enum class AdvancedModSource : std::uint8_t {
    Off,
    SineLfo,
    TriangleLfo,
    SawLfo,
    SquareLfo,
    SampleAndHold,
    AmpEnvelope,
};

enum class AdvancedModDestination : std::uint8_t {
    None,
    Pitch,
    Level,
    Pan,
    FilterCutoff,
    Resonance,
    Drive,
    Operator1Level,
    Operator2Level,
    Operator3Level,
    Operator4Level,
    Operator1Ratio,
    Operator2Ratio,
    Operator3Ratio,
    Operator4Ratio,
};

struct AdsrEnvelope {
    std::uint8_t attack = 0;
    std::uint8_t decay = 0;
    std::uint8_t sustain = 127;
    std::uint8_t release = 32;

    bool operator==(const AdsrEnvelope&) const = default;
};

struct AdvancedFmOperator {
    AdvancedOscShape shape = AdvancedOscShape::Sine;
    // Uses the same integer*8 + fractional encoding as FmPatch::modRatio.
    std::uint8_t ratio = 8;
    std::uint8_t level = 127;
    std::uint8_t feedback = 0;
    // Bipolar fine detune. The audio engine maps -64..63 to approximately
    // -100..+100 cents.
    std::int8_t detune = 0;

    bool operator==(const AdvancedFmOperator&) const = default;
};

struct AdvancedModSlot {
    AdvancedModSource source = AdvancedModSource::Off;
    std::uint8_t rate = 0;
    std::int8_t depth = 0;
    AdvancedModDestination destination = AdvancedModDestination::None;

    bool operator==(const AdvancedModSlot&) const = default;
};

struct AdvancedFmPatch {
    // Disabled is the compatibility default: the legacy FmPatch render path is
    // used without consulting any advanced field.
    bool enabled = false;
    AdvancedFmAlgorithm algorithm = AdvancedFmAlgorithm::Algorithm1;
    std::array<AdvancedFmOperator, 4> operators {{
        {AdvancedOscShape::Sine, 8, 96, 0, 0},
        {AdvancedOscShape::Sine, 8, 96, 0, 0},
        {AdvancedOscShape::Sine, 8, 96, 0, 0},
        {AdvancedOscShape::Sine, 8, 127, 0, 0},
    }};
    AdsrEnvelope ampEnvelope {};
    AdvancedFilterMode filterMode = AdvancedFilterMode::Off;
    std::uint8_t filterCutoff = 127;
    std::uint8_t resonance = 0;
    AdvancedDriveMode driveMode = AdvancedDriveMode::Off;
    std::uint8_t driveAmount = 0;
    std::uint8_t unisonVoices = 1;
    std::uint8_t unisonDetune = 0;
    std::uint8_t unisonWidth = 0;
    std::array<AdvancedModSlot, 4> modulation {};

    bool operator==(const AdvancedFmPatch&) const = default;
};

enum class ControllerAction : std::uint8_t {
    NavigateUp,
    NavigateDown,
    NavigateLeft,
    NavigateRight,
    Confirm,
    Clear,
    ParameterPrevious,
    ParameterNext,
    ValueDecrease,
    ValueIncrease,
    CoarseModifier,
    AlternateModifier,
    Transport,
    Copy,
    Paste,
    Randomize,
    Palette,
    CycleView,
    Count,
};

constexpr std::size_t kControllerActionCount =
    static_cast<std::size_t>(ControllerAction::Count);
constexpr std::uint8_t kControllerButtonUnbound = 0xFFu;
constexpr std::uint8_t kControllerButtonMax = 31u;

struct ControllerSettings {
    bool enabled = true;
    // SDL controller button numbers, intentionally stored without depending on
    // SDL headers. 0xFF means unbound. Defaults follow SDL's standard mapping.
    std::array<std::uint8_t, kControllerActionCount> buttons {{
        11, 12, 13, 14, // d-pad navigation
        0, 1,           // confirm / clear
        9, 10,          // previous / next parameter
        12, 11,         // value decrease / increase
        2, 3,           // coarse / alternate modifiers
        6,              // transport
        4, 5,           // copy / paste
        7, 8, 15,       // randomize / palette / cycle view
    }};

    bool operator==(const ControllerSettings&) const = default;
};

struct FmPatch {
    std::uint8_t ampAttack = 2;
    std::uint8_t ampHold = 20;
    std::uint8_t ampRelease = 34;
    // 0..127 encodes integer * 8 + one of eight fractional ratios.
    std::uint8_t modRatio = 16;
    std::uint8_t modDepth = 42;
    std::uint8_t modFeedback = 4;
    std::uint8_t modAttack = 0;
    std::uint8_t modRelease = 28;
    std::uint8_t modEnd = 0;
    std::int8_t sweepDepth = 0;
    std::uint8_t sweepRelease = 24;

    bool operator==(const FmPatch&) const = default;
};

struct NoisePatch {
    std::uint8_t ampAttack = 0;
    std::uint8_t ampHold = 7;
    std::uint8_t ampRelease = 18;
    std::uint8_t rate = 54;
    bool narrow = false;

    bool operator==(const NoisePatch&) const = default;
};

struct Step {
    bool active = false;
    bool trigless = false;
    std::uint8_t note = 48;
    std::uint8_t level = 100;
    Pan pan = Pan::Center;
    std::uint8_t portamento = 0;
    std::uint8_t condition = 1;
    std::int8_t microTicks = 0;
    bool echo = true;
    bool transpose = true;
    SynthMode mode = SynthMode::FM;
    std::array<std::int8_t, 3> chord {0, 0, 0};
    FmPatch fm {};
    NoisePatch noise {};
    AdvancedFmPatch advancedFm {};

    bool operator==(const Step&) const = default;
};

struct EchoSettings {
    std::uint8_t repeats = 0;
    std::uint8_t speedTicks = 6;
    std::int8_t transpose = 0;
    std::uint8_t transposeModulo = 1;
    std::int8_t volumeDelta = -12;
    std::int8_t modDelta = -5;
    std::int8_t feedbackDelta = -2;
    EchoPan pan = EchoPan::PingPong;
};

struct TransposeSettings {
    std::array<std::int8_t, 8> values {0, 0, 0, 0, 0, 0, 0, 0};
    std::uint8_t length = 1;
    std::uint8_t rate = 1;
    TransposeAdvance advance = TransposeAdvance::Pattern;
};

struct ModulatorSettings {
    std::uint8_t targetTrack = 0;
    ModDest destination = ModDest::ModDepth;
    std::uint8_t speed = 8;
    ModWave wave = ModWave::Triangle;
    std::int8_t depth = 0;
    std::uint8_t offset = 0;
};

struct TrackData {
    std::array<Step, kStepCount> steps {};
    std::uint8_t length = kStepCount;
    std::uint8_t rateIndex = 4;
    Direction direction = Direction::Forward;
    std::uint8_t shuffle = 0;
    bool muted = false;
    bool solo = false;
    EchoSettings echo {};
    TransposeSettings transpose {};
    ModulatorSettings modulator {};
};

struct StoredPattern {
    bool occupied = false;
    TrackData track {};
};

struct BankSettings {
    std::array<char, 5> name {'B', 'A', 'N', 'K', '\0'};
    bool locked = false;
    bool hasTempo = false;
    bool hasScale = false;
    std::uint16_t tempo = 120;
    std::uint16_t scaleMask = 0x0FFF;
    std::uint8_t scaleRoot = 0;
};

struct AppState {
    std::uint16_t bpm = 120;
    std::uint8_t scaleRoot = 0;
    std::uint16_t scaleMask = 0x0FFF;
    bool lightTheme = false;
    std::uint8_t accent = 0;
    std::array<TrackData, kTrackCount> tracks {};
    std::array<std::array<StoredPattern, kPatternCount>, kTrackCount> patterns {};
    std::array<BankSettings, 8> banks {};
    std::array<Step, kPaletteSize> fmPalette {};
    std::array<Step, kPaletteSize> noisePalette {};
    ControllerSettings controller {};
    std::uint64_t editRevision = 1;
};

struct PerformanceState {
    std::uint16_t bpm = 120;
    std::uint8_t scaleRoot = 0;
    std::uint16_t scaleMask = 0x0FFF;
    std::array<TrackData, kTrackCount> tracks {};
};

struct SharedState {
    mutable std::mutex mutex;
    AppState app {};
};

AppState makeDefaultState();
PerformanceState capturePerformance(const AppState& state);
void restorePerformance(AppState& state, const PerformanceState& snapshot);
void sanitize(AppState& state);
void randomizeTrack(TrackData& track, int trackIndex, std::uint32_t seed);
int quantizeNote(int midiNote, std::uint8_t root, std::uint16_t mask);
double rateMultiplier(std::uint8_t index);
double fmRatio(std::uint8_t encoded);
std::string noteName(int midiNote);
const char* directionName(Direction direction);
const char* rateName(std::uint8_t index);

} // namespace fms
