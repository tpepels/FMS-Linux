#include "ui.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace fms {
namespace {

constexpr int kLogicalWidth = 1280;
constexpr int kLogicalHeight = 760;
constexpr int kMargin = 28;

struct Color {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a = 255;
};

struct Palette {
    Color outside;
    Color background;
    Color surface;
    Color raised;
    Color line;
    Color lineStrong;
    Color text;
    Color muted;
    Color faint;
    Color accent;
    Color accentDim;
};

constexpr std::array<Color, 6> kDarkAccents {{
    {142, 244, 139, 255}, {100, 226, 220, 255}, {246, 201, 102, 255},
    {192, 154, 247, 255}, {242, 139, 119, 255}, {119, 174, 247, 255},
}};

constexpr std::array<Color, 6> kLightAccents {{
    {27, 129, 65, 255}, {0, 125, 129, 255}, {153, 99, 0, 255},
    {112, 70, 169, 255}, {174, 66, 50, 255}, {42, 95, 169, 255},
}};

Palette makePalette(bool light, std::uint8_t accentIndex) {
    const std::size_t index = static_cast<std::size_t>(accentIndex % 6u);
    if (light) {
        const Color accent = kLightAccents[index];
        return {{221, 222, 213, 255}, {239, 239, 231, 255}, {234, 235, 226, 255},
                {226, 228, 218, 255}, {190, 194, 183, 255}, {146, 153, 143, 255},
                {25, 31, 29, 255}, {86, 96, 90, 255}, {132, 140, 133, 255}, accent,
                {accent.r, accent.g, accent.b, 36}};
    }
    const Color accent = kDarkAccents[index];
    return {{2, 5, 8, 255}, {7, 12, 17, 255}, {10, 17, 23, 255},
            {14, 23, 30, 255}, {34, 47, 53, 255}, {55, 70, 75, 255},
            {232, 233, 220, 255}, {119, 132, 127, 255}, {67, 80, 80, 255}, accent,
            {accent.r, accent.g, accent.b, 34}};
}

void setColor(SDL_Renderer* renderer, Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

void fillRect(SDL_Renderer* renderer, int x, int y, int width, int height, Color color) {
    if (width <= 0 || height <= 0) return;
    const SDL_Rect rect {x, y, width, height};
    setColor(renderer, color);
    SDL_RenderFillRect(renderer, &rect);
}

void strokeRect(SDL_Renderer* renderer, int x, int y, int width, int height, Color color) {
    if (width <= 0 || height <= 0) return;
    const SDL_Rect rect {x, y, width, height};
    setColor(renderer, color);
    SDL_RenderDrawRect(renderer, &rect);
}

void drawLine(SDL_Renderer* renderer, int x1, int y1, int x2, int y2, Color color) {
    setColor(renderer, color);
    SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
}

std::array<std::uint8_t, 5> glyph(char input) {
    char c = input;
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    switch (c) {
        case 'A': return {0x7e, 0x11, 0x11, 0x11, 0x7e};
        case 'B': return {0x7f, 0x49, 0x49, 0x49, 0x36};
        case 'C': return {0x3e, 0x41, 0x41, 0x41, 0x22};
        case 'D': return {0x7f, 0x41, 0x41, 0x22, 0x1c};
        case 'E': return {0x7f, 0x49, 0x49, 0x49, 0x41};
        case 'F': return {0x7f, 0x09, 0x09, 0x09, 0x01};
        case 'G': return {0x3e, 0x41, 0x49, 0x49, 0x7a};
        case 'H': return {0x7f, 0x08, 0x08, 0x08, 0x7f};
        case 'I': return {0x41, 0x41, 0x7f, 0x41, 0x41};
        case 'J': return {0x20, 0x40, 0x41, 0x3f, 0x01};
        case 'K': return {0x7f, 0x08, 0x14, 0x22, 0x41};
        case 'L': return {0x7f, 0x40, 0x40, 0x40, 0x40};
        case 'M': return {0x7f, 0x02, 0x0c, 0x02, 0x7f};
        case 'N': return {0x7f, 0x04, 0x08, 0x10, 0x7f};
        case 'O': return {0x3e, 0x41, 0x41, 0x41, 0x3e};
        case 'P': return {0x7f, 0x09, 0x09, 0x09, 0x06};
        case 'Q': return {0x3e, 0x41, 0x51, 0x21, 0x5e};
        case 'R': return {0x7f, 0x09, 0x19, 0x29, 0x46};
        case 'S': return {0x46, 0x49, 0x49, 0x49, 0x31};
        case 'T': return {0x01, 0x01, 0x7f, 0x01, 0x01};
        case 'U': return {0x3f, 0x40, 0x40, 0x40, 0x3f};
        case 'V': return {0x1f, 0x20, 0x40, 0x20, 0x1f};
        case 'W': return {0x7f, 0x20, 0x18, 0x20, 0x7f};
        case 'X': return {0x63, 0x14, 0x08, 0x14, 0x63};
        case 'Y': return {0x03, 0x04, 0x78, 0x04, 0x03};
        case 'Z': return {0x61, 0x51, 0x49, 0x45, 0x43};
        case '0': return {0x3e, 0x51, 0x49, 0x45, 0x3e};
        case '1': return {0x00, 0x42, 0x7f, 0x40, 0x00};
        case '2': return {0x62, 0x51, 0x49, 0x49, 0x46};
        case '3': return {0x22, 0x41, 0x49, 0x49, 0x36};
        case '4': return {0x18, 0x14, 0x12, 0x7f, 0x10};
        case '5': return {0x2f, 0x49, 0x49, 0x49, 0x31};
        case '6': return {0x3e, 0x49, 0x49, 0x49, 0x32};
        case '7': return {0x01, 0x71, 0x09, 0x05, 0x03};
        case '8': return {0x36, 0x49, 0x49, 0x49, 0x36};
        case '9': return {0x26, 0x49, 0x49, 0x49, 0x3e};
        case '#': return {0x14, 0x7f, 0x14, 0x7f, 0x14};
        case '-': return {0x08, 0x08, 0x08, 0x08, 0x08};
        case '+': return {0x08, 0x08, 0x3e, 0x08, 0x08};
        case '=': return {0x14, 0x14, 0x14, 0x14, 0x14};
        case '/': return {0x60, 0x10, 0x08, 0x04, 0x03};
        case '\\': return {0x03, 0x04, 0x08, 0x10, 0x60};
        case '.': return {0x00, 0x60, 0x60, 0x00, 0x00};
        case ',': return {0x00, 0x40, 0x30, 0x00, 0x00};
        case ':': return {0x00, 0x36, 0x36, 0x00, 0x00};
        case ';': return {0x00, 0x40, 0x36, 0x00, 0x00};
        case '!': return {0x00, 0x00, 0x5f, 0x00, 0x00};
        case '?': return {0x02, 0x01, 0x51, 0x09, 0x06};
        case '[': return {0x00, 0x7f, 0x41, 0x41, 0x00};
        case ']': return {0x00, 0x41, 0x41, 0x7f, 0x00};
        case '(': return {0x00, 0x1c, 0x22, 0x41, 0x00};
        case ')': return {0x00, 0x41, 0x22, 0x1c, 0x00};
        case '<': return {0x08, 0x14, 0x22, 0x41, 0x00};
        case '>': return {0x00, 0x41, 0x22, 0x14, 0x08};
        case '%': return {0x63, 0x13, 0x08, 0x64, 0x63};
        case '_': return {0x40, 0x40, 0x40, 0x40, 0x40};
        case '*': return {0x14, 0x08, 0x3e, 0x08, 0x14};
        case '|': return {0x00, 0x00, 0x7f, 0x00, 0x00};
        case '"': return {0x00, 0x07, 0x00, 0x07, 0x00};
        case '\'': return {0x00, 0x00, 0x07, 0x00, 0x00};
        default: return {0, 0, 0, 0, 0};
    }
}

int textWidth(std::string_view text, int scale) {
    if (text.empty()) return 0;
    return static_cast<int>(text.size()) * 6 * scale - scale;
}

void drawText(SDL_Renderer* renderer, int x, int y, std::string_view text, Color color,
              int scale = 2) {
    setColor(renderer, color);
    int cursor = x;
    for (char character : text) {
        const auto bits = glyph(character);
        for (int column = 0; column < 5; ++column) {
            for (int row = 0; row < 7; ++row) {
                if ((bits[static_cast<std::size_t>(column)] & (1u << row)) == 0u) continue;
                const SDL_Rect pixel {cursor + column * scale, y + row * scale, scale, scale};
                SDL_RenderFillRect(renderer, &pixel);
            }
        }
        cursor += 6 * scale;
    }
}

void drawTextRight(SDL_Renderer* renderer, int right, int y, std::string_view text, Color color,
                   int scale = 2) {
    drawText(renderer, right - textWidth(text, scale), y, text, color, scale);
}

void drawTextCentered(SDL_Renderer* renderer, int centerX, int y, std::string_view text,
                      Color color, int scale = 2) {
    drawText(renderer, centerX - textWidth(text, scale) / 2, y, text, color, scale);
}

std::string hexValue(int value, int digits = 2) {
    char buffer[16];
    if (digits == 1) std::snprintf(buffer, sizeof(buffer), "%X", value & 0xF);
    else if (digits == 3) std::snprintf(buffer, sizeof(buffer), "%03X", value & 0xFFF);
    else std::snprintf(buffer, sizeof(buffer), "%02X", value & 0xFF);
    return buffer;
}

std::string signedValue(int value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%+d", value);
    return buffer;
}

std::string decimalValue(int value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    return buffer;
}

std::string asciiOnly(std::string_view input) {
    std::string output;
    bool replaced = false;
    for (const unsigned char c : input) {
        if (c >= 32u && c <= 126u) {
            output.push_back(static_cast<char>(c));
            replaced = false;
        } else if ((c & 0xC0u) != 0x80u && !replaced) {
            output.push_back('-');
            replaced = true;
        }
    }
    return output;
}

int wrapIndex(int value, int count) {
    if (count <= 0) return 0;
    value %= count;
    return value < 0 ? value + count : value;
}

int clampInt(int value, int minimum, int maximum) {
    return std::max(minimum, std::min(maximum, value));
}

enum class View : int { Grid, Synth, Echo, Transpose, Mod, Scale, Data };

constexpr std::array<const char*, 7> kViewNames {
    "GRID", "SYNTH", "ECHO", "TRANSPOSE", "MOD", "SCALE", "DATA"
};

enum class Overlay : std::uint8_t { None, Palette, ControllerMap, BankName };
enum class EditScope : std::uint8_t { Selection, Track, All };
enum class DataLoadMode : std::uint8_t { InPlace, Reset };

enum class GridParam {
    Note,
    Level,
    Pan,
    Portamento,
    Condition,
    Microtime,
    Chord1,
    Chord2,
    Chord3,
    Echo,
    Transpose,
    Mode,
    Trigless,
    AmpAttack,
    AmpHold,
    AmpRelease,
    ModRatio,
    ModDepth,
    ModFeedback,
    ModAttack,
    ModRelease,
    ModEnd,
    SweepDepth,
    SweepRelease,
    NoiseRate,
    NoiseWidth,
};

struct ParamItem {
    GridParam id;
    const char* shortName;
    const char* fullName;
};

constexpr std::array<ParamItem, 24> kFmParams {{
    {GridParam::Note, "NOTE", "NOTE"},
    {GridParam::Level, "LEVL", "LEVEL"},
    {GridParam::Pan, "PAN", "PAN"},
    {GridParam::Portamento, "PORT", "PORTAMENTO"},
    {GridParam::Condition, "COND", "CONDITION"},
    {GridParam::Microtime, "WAIT", "MICROTIME"},
    {GridParam::Chord1, "CHD1", "CHORD 1"},
    {GridParam::Chord2, "CHD2", "CHORD 2"},
    {GridParam::Chord3, "CHD3", "CHORD 3"},
    {GridParam::Echo, "ECHO", "ECHO SEND"},
    {GridParam::Transpose, "TSP", "TRANSPOSE SEND"},
    {GridParam::Mode, "MODE", "SYNTH MODE"},
    {GridParam::Trigless, "TRIG", "TRIGLESS"},
    {GridParam::AmpAttack, "A.ATK", "AMP ATTACK"},
    {GridParam::AmpHold, "A.HLD", "AMP HOLD"},
    {GridParam::AmpRelease, "A.REL", "AMP RELEASE"},
    {GridParam::ModRatio, "RATIO", "MOD RATIO"},
    {GridParam::ModDepth, "M.DEP", "MOD DEPTH"},
    {GridParam::ModFeedback, "M.FBK", "MOD FEEDBACK"},
    {GridParam::ModAttack, "M.ATK", "MOD ATTACK"},
    {GridParam::ModRelease, "M.REL", "MOD RELEASE"},
    {GridParam::ModEnd, "M.END", "MOD END"},
    {GridParam::SweepDepth, "S.DEP", "SWEEP DEPTH"},
    {GridParam::SweepRelease, "S.REL", "SWEEP RELEASE"},
}};

constexpr std::array<ParamItem, 12> kNoiseParams {{
    {GridParam::Level, "LEVL", "LEVEL"},
    {GridParam::Pan, "PAN", "PAN"},
    {GridParam::Portamento, "PORT", "PORTAMENTO"},
    {GridParam::Condition, "COND", "CONDITION"},
    {GridParam::Microtime, "WAIT", "MICROTIME"},
    {GridParam::Echo, "ECHO", "ECHO SEND"},
    {GridParam::Trigless, "TRIG", "TRIGLESS"},
    {GridParam::AmpAttack, "A.ATK", "AMP ATTACK"},
    {GridParam::AmpHold, "A.HLD", "AMP HOLD"},
    {GridParam::AmpRelease, "A.REL", "AMP RELEASE"},
    {GridParam::NoiseRate, "RATE", "NOISE RATE"},
    {GridParam::NoiseWidth, "WIDTH", "NOISE WIDTH"},
}};

const ParamItem& gridParamItem(int track, int index) {
    if (track == kTrackCount - 1) {
        return kNoiseParams[static_cast<std::size_t>(clampInt(index, 0,
            static_cast<int>(kNoiseParams.size()) - 1))];
    }
    return kFmParams[static_cast<std::size_t>(clampInt(index, 0,
        static_cast<int>(kFmParams.size()) - 1))];
}

int gridParamCount(int track) {
    return track == kTrackCount - 1 ? static_cast<int>(kNoiseParams.size())
                                    : static_cast<int>(kFmParams.size());
}

int gridStepParamCount(int track) {
    return track == kTrackCount - 1 ? 7 : 13;
}

const char* panName(Pan pan) {
    switch (pan) {
        case Pan::Left: return "L";
        case Pan::Center: return "C";
        case Pan::Right: return "R";
    }
    return "C";
}

const char* echoPanName(EchoPan pan) {
    switch (pan) {
        case EchoPan::Original: return "ORIG";
        case EchoPan::Left: return "LEFT";
        case EchoPan::Right: return "RIGHT";
        case EchoPan::PingPong: return "PINGPONG";
    }
    return "ORIG";
}

const char* advanceName(TransposeAdvance advance) {
    switch (advance) {
        case TransposeAdvance::Pattern: return "PATTERN";
        case TransposeAdvance::Step: return "STEP";
        case TransposeAdvance::Trigger: return "TRIGGER";
    }
    return "PATTERN";
}

const char* waveName(ModWave wave) {
    switch (wave) {
        case ModWave::RampDown: return "RAMP DN";
        case ModWave::RampUp: return "RAMP UP";
        case ModWave::Triangle: return "TRIANGLE";
        case ModWave::Square: return "SQUARE";
        case ModWave::Random: return "RANDOM";
    }
    return "TRIANGLE";
}

const char* destinationName(ModDest destination) {
    switch (destination) {
        case ModDest::Level: return "LEVEL";
        case ModDest::Pan: return "PAN";
        case ModDest::Note: return "NOTE";
        case ModDest::ModDepth: return "MOD DEPTH";
        case ModDest::ModFeedback: return "MOD FBK";
        case ModDest::Sweep: return "SWEEP";
        case ModDest::NoiseRate: return "NOISE RATE";
    }
    return "LEVEL";
}

std::string gridValue(const Step& step, GridParam parameter, bool noiseTrack = false) {
    switch (parameter) {
        case GridParam::Note: return noteName(step.note);
        case GridParam::Level: return hexValue(step.level);
        case GridParam::Pan: return panName(step.pan);
        case GridParam::Portamento: return hexValue(step.portamento);
        case GridParam::Condition: return "1:" + decimalValue(step.condition);
        case GridParam::Microtime: return signedValue(step.microTicks);
        case GridParam::Chord1: return signedValue(step.chord[0]);
        case GridParam::Chord2: return signedValue(step.chord[1]);
        case GridParam::Chord3: return signedValue(step.chord[2]);
        case GridParam::Echo: return step.echo ? "ON" : "OFF";
        case GridParam::Transpose: return step.transpose ? "ON" : "OFF";
        case GridParam::Mode: return step.mode == SynthMode::FM ? "FM" : "PAR";
        case GridParam::Trigless: return step.trigless ? "YES" : "NO";
        case GridParam::AmpAttack:
            return hexValue(noiseTrack ? step.noise.ampAttack : step.fm.ampAttack);
        case GridParam::AmpHold:
            return hexValue(noiseTrack ? step.noise.ampHold : step.fm.ampHold);
        case GridParam::AmpRelease:
            return hexValue(noiseTrack ? step.noise.ampRelease : step.fm.ampRelease);
        case GridParam::ModRatio: {
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%.3g", fmRatio(step.fm.modRatio));
            return buffer;
        }
        case GridParam::ModDepth: return hexValue(step.fm.modDepth);
        case GridParam::ModFeedback: return hexValue(step.fm.modFeedback);
        case GridParam::ModAttack: return hexValue(step.fm.modAttack);
        case GridParam::ModRelease: return hexValue(step.fm.modRelease);
        case GridParam::ModEnd: return hexValue(step.fm.modEnd);
        case GridParam::SweepDepth: return signedValue(step.fm.sweepDepth);
        case GridParam::SweepRelease: return hexValue(step.fm.sweepRelease);
        case GridParam::NoiseRate: return hexValue(step.noise.rate);
        case GridParam::NoiseWidth: return step.noise.narrow ? "NAR" : "WIDE";
    }
    return "--";
}

float gridValueUnit(const Step& step, GridParam parameter, bool noiseTrack = false) {
    switch (parameter) {
        case GridParam::Note: return static_cast<float>(step.note - 12u) / 107.0f;
        case GridParam::Level: return static_cast<float>(step.level) / 127.0f;
        case GridParam::Pan: return static_cast<float>(static_cast<int>(step.pan)) / 2.0f;
        case GridParam::Portamento: return static_cast<float>(step.portamento) / 127.0f;
        case GridParam::Condition: return static_cast<float>(step.condition - 1u) / 7.0f;
        case GridParam::Microtime: return static_cast<float>(step.microTicks + 6) / 12.0f;
        case GridParam::Chord1: return static_cast<float>(step.chord[0]) / 24.0f;
        case GridParam::Chord2: return static_cast<float>(step.chord[1]) / 24.0f;
        case GridParam::Chord3: return static_cast<float>(step.chord[2]) / 24.0f;
        case GridParam::Echo: return step.echo ? 1.0f : 0.0f;
        case GridParam::Transpose: return step.transpose ? 1.0f : 0.0f;
        case GridParam::Mode: return step.mode == SynthMode::Parallel ? 1.0f : 0.0f;
        case GridParam::Trigless: return step.trigless ? 1.0f : 0.0f;
        case GridParam::AmpAttack:
            return static_cast<float>(noiseTrack ? step.noise.ampAttack : step.fm.ampAttack) / 127.0f;
        case GridParam::AmpHold:
            return static_cast<float>(noiseTrack ? step.noise.ampHold : step.fm.ampHold) / 127.0f;
        case GridParam::AmpRelease:
            return static_cast<float>(noiseTrack ? step.noise.ampRelease : step.fm.ampRelease) / 127.0f;
        case GridParam::ModRatio: return static_cast<float>(step.fm.modRatio) / 127.0f;
        case GridParam::ModDepth: return static_cast<float>(step.fm.modDepth) / 127.0f;
        case GridParam::ModFeedback: return static_cast<float>(step.fm.modFeedback) / 127.0f;
        case GridParam::ModAttack: return static_cast<float>(step.fm.modAttack) / 127.0f;
        case GridParam::ModRelease: return static_cast<float>(step.fm.modRelease) / 127.0f;
        case GridParam::ModEnd: return static_cast<float>(step.fm.modEnd) / 127.0f;
        case GridParam::SweepDepth: return static_cast<float>(step.fm.sweepDepth + 64) / 127.0f;
        case GridParam::SweepRelease: return static_cast<float>(step.fm.sweepRelease) / 127.0f;
        case GridParam::NoiseRate: return static_cast<float>(step.noise.rate) / 127.0f;
        case GridParam::NoiseWidth: return step.noise.narrow ? 1.0f : 0.0f;
    }
    return 0.0f;
}

void drawMiniBar(SDL_Renderer* renderer, int x, int y, int width, float unit,
                 const Palette& palette, bool active) {
    unit = std::max(0.0f, std::min(1.0f, unit));
    drawLine(renderer, x, y, x + width, y, palette.lineStrong);
    const int filled = static_cast<int>(std::lround(static_cast<double>(width) * unit));
    drawLine(renderer, x, y, x + filled, y, active ? palette.accent : palette.muted);
}

} // namespace

struct UiController::Impl {
    struct RangeClipboard {
        int tracks = 0;
        int steps = 0;
        std::array<Step, kTrackCount * kStepCount> data {};

        bool empty() const { return tracks <= 0 || steps <= 0; }
    };

    SharedState& shared;
    AudioEngine& audio;
    View view = View::Grid;
    Overlay overlay = Overlay::None;
    EditScope editScope = EditScope::Selection;
    DataLoadMode dataLoadMode = DataLoadMode::Reset;
    int selectedTrack = 0;
    int selectedStep = 0;
    int selectedParameter = 0;
    int editorIndex = 0;
    int dataBank = 0;
    int dataColumn = 0;
    int paletteCursor = 2;
    int controllerMapCursor = 0;
    bool controllerCapture = false;
    bool controllerCoarseHeld = false;
    bool controllerAlternateHeld = false;
    SDL_GameController* controller = nullptr;
    SDL_JoystickID controllerInstance = -1;
    int rangeAnchorTrack = 0;
    int rangeAnchorStep = 0;
    bool rangeActive = false;
    bool dataAllTracks = false;
    bool dataArmTempo = false;
    bool dataArmScale = false;
    int bankNameCursor = 0;
    std::array<char, 5> bankNameEdit {'B', 'A', 'N', 'K', '\0'};
    bool helpVisible = false;
    bool saveRequested = false;
    bool snapshotSide = false;
    bool snapshotAlternateReady = false;
    std::optional<PerformanceState> snapshot;
    std::optional<Step> stepClipboard;
    RangeClipboard rangeClipboard;
    std::array<int, kTrackCount> queuedPattern {-1, -1, -1, -1, -1};
    std::array<std::uint64_t, kTrackCount> queueGeneration {};
    int queuedColumnPattern = -1;
    std::uint64_t queuedColumnGeneration = 0;
    bool queuedColumnIncludesSettings = false;
    std::uint64_t queuedGlobalGeneration = 0;
    TransportStatus transport {};
    std::string toastMessage;
    bool toastIsError = false;
    double toastSeconds = 0.0;
    double shutterSeconds = 0.0;
    double elapsed = 0.0;
    int outputWidth = kLogicalWidth;
    int outputHeight = kLogicalHeight;
    int viewportX = 0;
    int viewportY = 0;
    float viewportScale = 1.0f;

    Impl(SharedState& sharedState, AudioEngine& audioEngine)
        : shared(sharedState), audio(audioEngine), transport(audioEngine.status()) {
        for (int index = 0; index < SDL_NumJoysticks(); ++index) {
            if (SDL_IsGameController(index) == SDL_TRUE) {
                openController(index);
                break;
            }
        }
    }

    ~Impl() {
        SDL_StopTextInput();
        if (controller) SDL_GameControllerClose(controller);
    }

    void openController(int deviceIndex) {
        if (controller || SDL_IsGameController(deviceIndex) != SDL_TRUE) return;
        controller = SDL_GameControllerOpen(deviceIndex);
        if (!controller) return;
        SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
        controllerInstance = joystick ? SDL_JoystickInstanceID(joystick) : -1;
        const char* name = SDL_GameControllerName(controller);
        toast(std::string("CONTROLLER ") + (name ? name : "CONNECTED"));
    }

    void closeController(SDL_JoystickID instance) {
        if (!controller || instance != controllerInstance) return;
        SDL_GameControllerClose(controller);
        controller = nullptr;
        controllerInstance = -1;
        controllerCoarseHeld = false;
        controllerAlternateHeld = false;
        toast("CONTROLLER DISCONNECTED", true);
    }

    AppState stateCopy() const {
        std::lock_guard<std::mutex> lock(shared.mutex);
        return shared.app;
    }

    void toast(std::string_view message, bool error = false) {
        toastMessage = asciiOnly(message);
        if (toastMessage.size() > 76u) toastMessage.resize(76u);
        toastIsError = error;
        toastSeconds = 2.4;
    }

    void bumpRevision(AppState& app) {
        ++app.editRevision;
    }

    int dataPattern() const {
        return dataBank * 16 + clampInt(dataColumn, 0, 15);
    }

    int selectionTrackFirst() const {
        return rangeActive ? std::min(rangeAnchorTrack, selectedTrack) : selectedTrack;
    }

    int selectionTrackLast() const {
        return rangeActive ? std::max(rangeAnchorTrack, selectedTrack) : selectedTrack;
    }

    int selectionStepFirst() const {
        return rangeActive ? std::min(rangeAnchorStep, selectedStep) : selectedStep;
    }

    int selectionStepLast() const {
        return rangeActive ? std::max(rangeAnchorStep, selectedStep) : selectedStep;
    }

    bool selectedCell(int track, int step) const {
        if (!rangeActive) return track == selectedTrack && step == selectedStep;
        return track >= selectionTrackFirst() && track <= selectionTrackLast() &&
               step >= selectionStepFirst() && step <= selectionStepLast();
    }

    template <typename Function>
    void forEditCells(Function&& function) {
        if (editScope == EditScope::All) {
            for (int track = 0; track < kTrackCount; ++track)
                for (int step = 0; step < kStepCount; ++step) function(track, step);
            return;
        }
        if (editScope == EditScope::Track) {
            for (int step = 0; step < kStepCount; ++step) function(selectedTrack, step);
            return;
        }
        for (int track = selectionTrackFirst(); track <= selectionTrackLast(); ++track)
            for (int step = selectionStepFirst(); step <= selectionStepLast(); ++step)
                function(track, step);
    }

    void beginOrExtendRange(int trackDelta, int stepDelta) {
        if (!rangeActive) {
            rangeActive = true;
            rangeAnchorTrack = selectedTrack;
            rangeAnchorStep = selectedStep;
        }
        selectedTrack = clampInt(selectedTrack + trackDelta, 0, kTrackCount - 1);
        selectedStep = clampInt(selectedStep + stepDelta, 0, kStepCount - 1);
        selectedParameter = clampInt(selectedParameter, 0, gridParamCount(selectedTrack) - 1);
    }

    void cancelRange() {
        rangeActive = false;
        rangeAnchorTrack = selectedTrack;
        rangeAnchorStep = selectedStep;
    }

    const char* scopeName() const {
        switch (editScope) {
            case EditScope::Selection: return rangeActive ? "RANGE" : "STEP";
            case EditScope::Track: return "TRACK";
            case EditScope::All: return "ALL";
        }
        return "STEP";
    }

    void cycleEditScope() {
        editScope = static_cast<EditScope>(wrapIndex(static_cast<int>(editScope) + 1, 3));
        toast(std::string("EDIT SCOPE ") + scopeName());
    }

    void selectTrack(int track) {
        selectedTrack = clampInt(track, 0, kTrackCount - 1);
        selectedParameter = clampInt(selectedParameter, 0, gridParamCount(selectedTrack) - 1);
    }

    void changeView(int delta) {
        view = static_cast<View>(wrapIndex(static_cast<int>(view) + delta,
                                           static_cast<int>(kViewNames.size())));
        editorIndex = 0;
    }

    void previewSelected() {
        Step step;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            step = shared.app.tracks[static_cast<std::size_t>(selectedTrack)]
                       .steps[static_cast<std::size_t>(selectedStep)];
        }
        if (step.active) audio.preview(selectedTrack, step);
    }

    void toggleStep() {
        Step preview;
        bool havePreview = false;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto& app = shared.app;
            const bool activate = !app.tracks[static_cast<std::size_t>(selectedTrack)]
                                      .steps[static_cast<std::size_t>(selectedStep)].active;
            forEditCells([&](int track, int stepIndex) {
                auto& step = app.tracks[static_cast<std::size_t>(track)]
                                 .steps[static_cast<std::size_t>(stepIndex)];
                step.active = activate;
                if (!step.active) step.trigless = false;
                if (track == selectedTrack && stepIndex == selectedStep) {
                    preview = step;
                    havePreview = true;
                }
            });
            bumpRevision(app);
        }
        if (havePreview && preview.active) audio.preview(selectedTrack, preview);
    }

    void toggleTrigless() {
        Step preview;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto& app = shared.app;
            auto& step = app.tracks[static_cast<std::size_t>(selectedTrack)]
                             .steps[static_cast<std::size_t>(selectedStep)];
            step.trigless = !step.trigless;
            if (step.trigless) step.active = true;
            preview = step;
            bumpRevision(app);
        }
        if (preview.active) audio.preview(selectedTrack, preview);
        toast(preview.trigless ? "TRIGLESS ON" : "TRIGLESS OFF");
    }

    void clearStep() {
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto& app = shared.app;
            forEditCells([&](int track, int stepIndex) {
                auto& step = app.tracks[static_cast<std::size_t>(track)]
                                 .steps[static_cast<std::size_t>(stepIndex)];
                const std::uint8_t note = step.note;
                step = Step {};
                step.note = note;
            });
            bumpRevision(app);
        }
        toast(rangeActive || editScope != EditScope::Selection ? "SELECTION CLEARED" : "STEP CLEARED");
    }

    void copyStep() {
        std::lock_guard<std::mutex> lock(shared.mutex);
        if (!rangeActive) {
            stepClipboard = shared.app.tracks[static_cast<std::size_t>(selectedTrack)]
                                .steps[static_cast<std::size_t>(selectedStep)];
            rangeClipboard = RangeClipboard {};
            toast("STEP COPIED");
            return;
        }
        const int firstTrack = selectionTrackFirst();
        const int firstStep = selectionStepFirst();
        rangeClipboard.tracks = selectionTrackLast() - firstTrack + 1;
        rangeClipboard.steps = selectionStepLast() - firstStep + 1;
        for (int track = 0; track < rangeClipboard.tracks; ++track) {
            for (int step = 0; step < rangeClipboard.steps; ++step) {
                rangeClipboard.data[static_cast<std::size_t>(track * kStepCount + step)] =
                    shared.app.tracks[static_cast<std::size_t>(firstTrack + track)]
                                     .steps[static_cast<std::size_t>(firstStep + step)];
            }
        }
        stepClipboard.reset();
        toast("RANGE COPIED");
    }

    void cutStep() {
        copyStep();
        clearStep();
        toast(rangeActive ? "RANGE CUT" : "STEP CUT");
    }

    void pasteStep() {
        if (!stepClipboard.has_value() && rangeClipboard.empty()) {
            toast("CLIPBOARD EMPTY", true);
            return;
        }
        Step preview;
        bool havePreview = false;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto& app = shared.app;
            if (stepClipboard.has_value()) {
                app.tracks[static_cast<std::size_t>(selectedTrack)]
                    .steps[static_cast<std::size_t>(selectedStep)] = *stepClipboard;
                preview = *stepClipboard;
                havePreview = true;
            } else {
                for (int track = 0; track < rangeClipboard.tracks; ++track) {
                    const int targetTrack = selectedTrack + track;
                    if (targetTrack >= kTrackCount) break;
                    for (int step = 0; step < rangeClipboard.steps; ++step) {
                        const int targetStep = selectedStep + step;
                        if (targetStep >= kStepCount) break;
                        const Step& source = rangeClipboard.data[
                            static_cast<std::size_t>(track * kStepCount + step)];
                        app.tracks[static_cast<std::size_t>(targetTrack)]
                                  .steps[static_cast<std::size_t>(targetStep)] = source;
                        if (targetTrack == selectedTrack && targetStep == selectedStep) {
                            preview = source;
                            havePreview = true;
                        }
                    }
                }
            }
            bumpRevision(app);
        }
        if (havePreview && preview.active) audio.preview(selectedTrack, preview);
        toast(stepClipboard.has_value() ? "STEP PASTED" : "RANGE PASTED");
    }

    bool gridParamSupported(int track, GridParam parameter) const {
        const bool noise = track == kTrackCount - 1;
        if (parameter == GridParam::NoiseRate || parameter == GridParam::NoiseWidth) return noise;
        if (!noise) return true;
        switch (parameter) {
            case GridParam::Level:
            case GridParam::Pan:
            case GridParam::Portamento:
            case GridParam::Condition:
            case GridParam::Microtime:
            case GridParam::Echo:
            case GridParam::Trigless:
            case GridParam::AmpAttack:
            case GridParam::AmpHold:
            case GridParam::AmpRelease: return true;
            default: return false;
        }
    }

    void adjustOneGridParameter(AppState& app, Step& step, int track, GridParam parameter,
                                int direction, bool coarse) {
        if (!gridParamSupported(track, parameter) || direction == 0) return;
        const int fine = direction > 0 ? 1 : -1;
        const int wide = fine * (coarse ? 8 : 1);
        const auto setByte = [](std::uint8_t& target, int value, int low = 0, int high = 127) {
            target = static_cast<std::uint8_t>(clampInt(value, low, high));
        };
        switch (parameter) {
            case GridParam::Note: {
                const int amount = fine * (coarse ? 12 : 1);
                const int raw = clampInt(static_cast<int>(step.note) + amount, 12, 119);
                step.note = static_cast<std::uint8_t>(quantizeNote(raw, app.scaleRoot, app.scaleMask));
                break;
            }
            case GridParam::Level: setByte(step.level, static_cast<int>(step.level) + wide); break;
            case GridParam::Pan:
                step.pan = static_cast<Pan>(wrapIndex(static_cast<int>(step.pan) + fine, 3));
                break;
            case GridParam::Portamento: setByte(step.portamento, static_cast<int>(step.portamento) + wide); break;
            case GridParam::Condition: setByte(step.condition, static_cast<int>(step.condition) + fine, 1, 8); break;
            case GridParam::Microtime:
                step.microTicks = static_cast<std::int8_t>(clampInt(
                    static_cast<int>(step.microTicks) + fine * (coarse ? 3 : 1), -6, 6));
                break;
            case GridParam::Chord1:
            case GridParam::Chord2:
            case GridParam::Chord3: {
                const int slot = parameter == GridParam::Chord1 ? 0
                               : parameter == GridParam::Chord2 ? 1 : 2;
                auto& chord = step.chord[static_cast<std::size_t>(slot)];
                chord = static_cast<std::int8_t>(clampInt(static_cast<int>(chord) +
                    fine * (coarse ? 12 : 1), 0, 24));
                break;
            }
            case GridParam::Echo: step.echo = direction > 0; break;
            case GridParam::Transpose: step.transpose = direction > 0; break;
            case GridParam::Mode: step.mode = direction > 0 ? SynthMode::Parallel : SynthMode::FM; break;
            case GridParam::Trigless:
                step.trigless = direction > 0;
                if (step.trigless) step.active = true;
                break;
            case GridParam::AmpAttack:
                if (track == kTrackCount - 1) setByte(step.noise.ampAttack, static_cast<int>(step.noise.ampAttack) + wide);
                else setByte(step.fm.ampAttack, static_cast<int>(step.fm.ampAttack) + wide);
                break;
            case GridParam::AmpHold:
                if (track == kTrackCount - 1) setByte(step.noise.ampHold, static_cast<int>(step.noise.ampHold) + wide);
                else setByte(step.fm.ampHold, static_cast<int>(step.fm.ampHold) + wide);
                break;
            case GridParam::AmpRelease:
                if (track == kTrackCount - 1) setByte(step.noise.ampRelease, static_cast<int>(step.noise.ampRelease) + wide);
                else setByte(step.fm.ampRelease, static_cast<int>(step.fm.ampRelease) + wide);
                break;
            case GridParam::ModRatio: setByte(step.fm.modRatio, static_cast<int>(step.fm.modRatio) + wide); break;
            case GridParam::ModDepth: setByte(step.fm.modDepth, static_cast<int>(step.fm.modDepth) + wide); break;
            case GridParam::ModFeedback: setByte(step.fm.modFeedback, static_cast<int>(step.fm.modFeedback) + wide); break;
            case GridParam::ModAttack: setByte(step.fm.modAttack, static_cast<int>(step.fm.modAttack) + wide); break;
            case GridParam::ModRelease: setByte(step.fm.modRelease, static_cast<int>(step.fm.modRelease) + wide); break;
            case GridParam::ModEnd: setByte(step.fm.modEnd, static_cast<int>(step.fm.modEnd) + wide); break;
            case GridParam::SweepDepth:
                step.fm.sweepDepth = static_cast<std::int8_t>(clampInt(
                    static_cast<int>(step.fm.sweepDepth) + wide, -64, 63));
                break;
            case GridParam::SweepRelease: setByte(step.fm.sweepRelease, static_cast<int>(step.fm.sweepRelease) + wide); break;
            case GridParam::NoiseRate: setByte(step.noise.rate, static_cast<int>(step.noise.rate) + wide); break;
            case GridParam::NoiseWidth: step.noise.narrow = direction > 0; break;
        }
    }

    void adjustGridParameter(int direction, bool coarse) {
        if (direction == 0) return;
        const GridParam parameter = gridParamItem(selectedTrack, selectedParameter).id;
        Step preview;
        bool havePreview = false;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto& app = shared.app;
            forEditCells([&](int track, int stepIndex) {
                auto& step = app.tracks[static_cast<std::size_t>(track)]
                                 .steps[static_cast<std::size_t>(stepIndex)];
                adjustOneGridParameter(app, step, track, parameter, direction, coarse);
                if (track == selectedTrack && stepIndex == selectedStep) {
                    preview = step;
                    havePreview = true;
                }
            });
            bumpRevision(app);
        }
        if (havePreview && preview.active) audio.preview(selectedTrack, preview);
    }

    void adjustBpm(int direction, bool coarse) {
        std::lock_guard<std::mutex> lock(shared.mutex);
        auto& app = shared.app;
        const int amount = direction * (coarse ? 10 : 1);
        app.bpm = static_cast<std::uint16_t>(clampInt(static_cast<int>(app.bpm) + amount, 30, 300));
        bumpRevision(app);
    }

    void toggleTransport() {
        if (!audio.available()) {
            toast("AUDIO OFFLINE", true);
            return;
        }
        audio.toggleRunning();
    }

    void adjustTrackRate(int direction) {
        std::lock_guard<std::mutex> lock(shared.mutex);
        auto& app = shared.app;
        auto& rate = app.tracks[static_cast<std::size_t>(selectedTrack)].rateIndex;
        rate = static_cast<std::uint8_t>(clampInt(static_cast<int>(rate) + direction, 0, 8));
        bumpRevision(app);
    }

    void adjustTrackShuffle(int direction, bool coarse, bool allTracks = false) {
        std::lock_guard<std::mutex> lock(shared.mutex);
        auto& app = shared.app;
        const int first = allTracks ? 0 : selectedTrack;
        const int last = allTracks ? kTrackCount - 1 : selectedTrack;
        for (int track = first; track <= last; ++track) {
            auto& shuffle = app.tracks[static_cast<std::size_t>(track)].shuffle;
            shuffle = static_cast<std::uint8_t>(clampInt(static_cast<int>(shuffle) +
                direction * (coarse ? 5 : 1), 0, 50));
        }
        bumpRevision(app);
    }

    void adjustTrackLength(int direction) {
        std::lock_guard<std::mutex> lock(shared.mutex);
        auto& app = shared.app;
        auto& length = app.tracks[static_cast<std::size_t>(selectedTrack)].length;
        length = static_cast<std::uint8_t>(clampInt(static_cast<int>(length) + direction, 1, 16));
        bumpRevision(app);
    }

    void cycleDirection(int direction, bool allTracks = false) {
        std::lock_guard<std::mutex> lock(shared.mutex);
        auto& app = shared.app;
        const int first = allTracks ? 0 : selectedTrack;
        const int last = allTracks ? kTrackCount - 1 : selectedTrack;
        for (int index = first; index <= last; ++index) {
            auto& track = app.tracks[static_cast<std::size_t>(index)];
            track.direction = static_cast<Direction>(wrapIndex(
                static_cast<int>(track.direction) + direction, 4));
        }
        bumpRevision(app);
    }

    void toggleMute() {
        bool muted = false;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto& app = shared.app;
            auto& track = app.tracks[static_cast<std::size_t>(selectedTrack)];
            track.muted = !track.muted;
            if (track.muted) track.solo = false;
            muted = track.muted;
            bumpRevision(app);
        }
        toast(muted ? "TRACK MUTED" : "TRACK LIVE");
    }

    void toggleSolo() {
        bool solo = false;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto& app = shared.app;
            auto& track = app.tracks[static_cast<std::size_t>(selectedTrack)];
            track.solo = !track.solo;
            if (track.solo) track.muted = false;
            solo = track.solo;
            bumpRevision(app);
        }
        toast(solo ? "TRACK SOLO" : "SOLO OFF");
    }

    void unmuteAll() {
        std::lock_guard<std::mutex> lock(shared.mutex);
        auto& app = shared.app;
        for (auto& track : app.tracks) {
            track.muted = false;
            track.solo = false;
        }
        bumpRevision(app);
        toast("ALL TRACKS LIVE");
    }

    void rotateSteps(int direction, bool allTracks) {
        if (direction == 0) return;
        std::lock_guard<std::mutex> lock(shared.mutex);
        auto& app = shared.app;
        const int firstTrack = allTracks ? 0 : selectionTrackFirst();
        const int lastTrack = allTracks ? kTrackCount - 1 : selectionTrackLast();
        const int firstStep = (allTracks || !rangeActive) ? 0 : selectionStepFirst();
        const int lastStep = (allTracks || !rangeActive) ? kStepCount - 1 : selectionStepLast();
        if (firstStep == lastStep) return;
        for (int track = firstTrack; track <= lastTrack; ++track) {
            auto& steps = app.tracks[static_cast<std::size_t>(track)].steps;
            if (direction > 0) {
                const Step tail = steps[static_cast<std::size_t>(lastStep)];
                for (int step = lastStep; step > firstStep; --step)
                    steps[static_cast<std::size_t>(step)] = steps[static_cast<std::size_t>(step - 1)];
                steps[static_cast<std::size_t>(firstStep)] = tail;
            } else {
                const Step head = steps[static_cast<std::size_t>(firstStep)];
                for (int step = firstStep; step < lastStep; ++step)
                    steps[static_cast<std::size_t>(step)] = steps[static_cast<std::size_t>(step + 1)];
                steps[static_cast<std::size_t>(lastStep)] = head;
            }
        }
        bumpRevision(app);
        toast(allTracks ? "ALL TRACKS ROTATED" : (rangeActive ? "RANGE ROTATED" : "TRACK ROTATED"));
    }

    void randomizeSelectedParameter() {
        if (view != View::Grid) return;
        const GridParam parameter = gridParamItem(selectedTrack, selectedParameter).id;
        Step preview;
        bool havePreview = false;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto& app = shared.app;
            std::uint32_t value = static_cast<std::uint32_t>(
                app.editRevision ^ (static_cast<std::uint64_t>(selectedParameter + 1) * 0x9E3779B9ull));
            forEditCells([&](int track, int stepIndex) {
                value ^= value << 13u;
                value ^= value >> 17u;
                value ^= value << 5u;
                const int direction = (value & 1u) != 0u ? 1 : -1;
                const bool coarse = (value & 0x18u) == 0x18u;
                auto& step = app.tracks[static_cast<std::size_t>(track)]
                                 .steps[static_cast<std::size_t>(stepIndex)];
                adjustOneGridParameter(app, step, track, parameter, direction, coarse);
                if (track == selectedTrack && stepIndex == selectedStep) {
                    preview = step;
                    havePreview = true;
                }
            });
            bumpRevision(app);
        }
        if (havePreview && preview.active) audio.preview(selectedTrack, preview);
        toast(std::string("NUDGE ") + scopeName());
    }

    void randomizeSelectedTrack() {
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto& app = shared.app;
            const std::uint32_t seed = static_cast<std::uint32_t>(
                app.editRevision ^ (static_cast<std::uint64_t>(selectedTrack + 1) * 0x9E3779B9ull));
            randomizeTrack(app.tracks[static_cast<std::size_t>(selectedTrack)], selectedTrack, seed);
            bumpRevision(app);
        }
        toast("TRACK RANDOMIZED");
    }

    void copySoundFields(const Step& source, Step& destination, int track) const {
        if (track == kTrackCount - 1) {
            destination.noise = source.noise;
        } else {
            destination.fm = source.fm;
            destination.mode = source.mode;
            destination.advancedFm = source.advancedFm;
        }
    }

    void clearSoundFields(Step& step, int track) const {
        if (track == kTrackCount - 1) {
            step.noise = NoisePatch {};
        } else {
            step.fm = FmPatch {};
            step.mode = SynthMode::FM;
            step.advancedFm = AdvancedFmPatch {};
        }
    }

    Step randomizedSound(int track, std::uint32_t seed) const {
        TrackData scratch;
        randomizeTrack(scratch, track, seed);
        return scratch.steps[0];
    }

    void openPalette() {
        if (view != View::Grid && view != View::Synth) {
            toast("PALETTE AVAILABLE IN GRID OR SYNTH", true);
            return;
        }
        overlay = overlay == Overlay::Palette ? Overlay::None : Overlay::Palette;
        paletteCursor = clampInt(paletteCursor, 0, kPaletteSize + 1);
    }

    void paletteAction(bool store, bool wholeTrack) {
        Step preview;
        bool havePreview = false;
        bool rejected = false;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto& app = shared.app;
            auto& palette = selectedTrack == kTrackCount - 1 ? app.noisePalette : app.fmPalette;
            if (store) {
                if (paletteCursor < 2) {
                    rejected = true;
                } else {
                    Step stored;
                    copySoundFields(app.tracks[static_cast<std::size_t>(selectedTrack)]
                                       .steps[static_cast<std::size_t>(selectedStep)], stored,
                                    selectedTrack);
                    stored.active = true;
                    palette[static_cast<std::size_t>(paletteCursor - 2)] = stored;
                    bumpRevision(app);
                }
            } else {
                const int sourceEngine = selectedTrack == kTrackCount - 1 ? 1 : 0;
                Step sound;
                bool useSound = false;
                if (paletteCursor == 1) {
                    sound = randomizedSound(selectedTrack, static_cast<std::uint32_t>(
                        app.editRevision ^ (static_cast<std::uint64_t>(paletteCursor + 1) * 0xA511E9B3ull)));
                    useSound = true;
                } else if (paletteCursor >= 2) {
                    sound = palette[static_cast<std::size_t>(paletteCursor - 2)];
                    useSound = sound.active;
                    rejected = !useSound;
                }
                const auto apply = [&](int track, int stepIndex) {
                    if ((track == kTrackCount - 1 ? 1 : 0) != sourceEngine) return;
                    auto& destination = app.tracks[static_cast<std::size_t>(track)]
                                           .steps[static_cast<std::size_t>(stepIndex)];
                    if (paletteCursor == 0) clearSoundFields(destination, track);
                    else if (useSound) copySoundFields(sound, destination, track);
                    if (track == selectedTrack && stepIndex == selectedStep) {
                        preview = destination;
                        havePreview = true;
                    }
                };
                if (!rejected) {
                    if (wholeTrack) {
                        for (int step = 0; step < kStepCount; ++step) apply(selectedTrack, step);
                    } else {
                        const int firstTrack = selectionTrackFirst();
                        const int lastTrack = selectionTrackLast();
                        const int firstStep = selectionStepFirst();
                        const int lastStep = selectionStepLast();
                        for (int track = firstTrack; track <= lastTrack; ++track)
                            for (int step = firstStep; step <= lastStep; ++step) apply(track, step);
                    }
                    bumpRevision(app);
                }
            }
        }
        if (rejected) {
            toast(store ? "SELECT A USER SOUND SLOT" : "SOUND SLOT EMPTY", true);
            return;
        }
        if (havePreview && preview.active) audio.preview(selectedTrack, preview);
        if (store) toast("SOUND " + hexValue(paletteCursor - 2, 1) + " STORED");
        else if (paletteCursor == 0) toast(wholeTrack ? "TRACK SOUND CLEARED" : "SOUND CLEARED");
        else if (paletteCursor == 1) toast(wholeTrack ? "TRACK SOUND RANDOMIZED" : "SOUND RANDOMIZED");
        else toast("SOUND " + hexValue(paletteCursor - 2, 1) +
                   (wholeTrack ? " APPLIED TO TRACK" : " RECALLED"));
    }

    void clearPaletteSlot() {
        if (paletteCursor < 2) {
            paletteAction(false, false);
            return;
        }
        std::lock_guard<std::mutex> lock(shared.mutex);
        auto& app = shared.app;
        auto& palette = selectedTrack == kTrackCount - 1 ? app.noisePalette : app.fmPalette;
        palette[static_cast<std::size_t>(paletteCursor - 2)] = Step {};
        bumpRevision(app);
        toast("SOUND SLOT CLEARED");
    }

    void toggleSnapshot() {
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto& app = shared.app;
            if (!snapshot.has_value()) {
                snapshot = capturePerformance(app);
                snapshotSide = false;
                snapshotAlternateReady = false;
            } else {
                PerformanceState live = capturePerformance(app);
                restorePerformance(app, *snapshot);
                snapshot = std::move(live);
                if (snapshotAlternateReady) snapshotSide = !snapshotSide;
                else snapshotAlternateReady = true;
            }
        }
        shutterSeconds = 0.18;
        toast(snapshotSide ? "SNAPSHOT B" : (snapshot.has_value() ? "SNAPSHOT A" : "SNAPSHOT"));
    }

    void toggleTheme() {
        bool light = false;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto& app = shared.app;
            app.lightTheme = !app.lightTheme;
            light = app.lightTheme;
            bumpRevision(app);
        }
        toast(light ? "LIGHT THEME" : "DARK THEME");
    }

    void cycleAccent() {
        int accent = 0;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto& app = shared.app;
            app.accent = static_cast<std::uint8_t>((app.accent + 1u) % 6u);
            accent = app.accent;
            bumpRevision(app);
        }
        toast("ACCENT " + decimalValue(accent + 1));
    }

    void adjustEcho(int direction, bool coarse) {
        if (direction == 0) return;
        if (selectedTrack == kTrackCount - 1 &&
            (editorIndex == 2 || editorIndex == 3 || editorIndex == 5 || editorIndex == 6)) return;
        std::lock_guard<std::mutex> lock(shared.mutex);
        auto& app = shared.app;
        auto& echo = app.tracks[static_cast<std::size_t>(selectedTrack)].echo;
        const int amount = direction * (coarse ? 4 : 1);
        switch (editorIndex) {
            case 0: echo.repeats = static_cast<std::uint8_t>(clampInt(static_cast<int>(echo.repeats) + direction, 0, 8)); break;
            case 1: echo.speedTicks = static_cast<std::uint8_t>(clampInt(static_cast<int>(echo.speedTicks) + amount, 1, 96)); break;
            case 2: echo.transpose = static_cast<std::int8_t>(clampInt(static_cast<int>(echo.transpose) + amount, -24, 24)); break;
            case 3: echo.transposeModulo = static_cast<std::uint8_t>(clampInt(static_cast<int>(echo.transposeModulo) + direction, 1, 8)); break;
            case 4: echo.volumeDelta = static_cast<std::int8_t>(clampInt(static_cast<int>(echo.volumeDelta) + amount, -64, 63)); break;
            case 5: echo.modDelta = static_cast<std::int8_t>(clampInt(static_cast<int>(echo.modDelta) + amount, -64, 63)); break;
            case 6: echo.feedbackDelta = static_cast<std::int8_t>(clampInt(static_cast<int>(echo.feedbackDelta) + amount, -64, 63)); break;
            case 7: echo.pan = static_cast<EchoPan>(wrapIndex(static_cast<int>(echo.pan) + direction, 4)); break;
            default: break;
        }
        bumpRevision(app);
    }

    void adjustTranspose(int direction, bool coarse) {
        if (direction == 0) return;
        if (selectedTrack == kTrackCount - 1) return;
        std::lock_guard<std::mutex> lock(shared.mutex);
        auto& app = shared.app;
        auto& transpose = app.tracks[static_cast<std::size_t>(selectedTrack)].transpose;
        if (editorIndex >= 0 && editorIndex < 8) {
            auto& value = transpose.values[static_cast<std::size_t>(editorIndex)];
            value = static_cast<std::int8_t>(clampInt(static_cast<int>(value) +
                direction * (coarse ? 12 : 1), -24, 24));
        } else if (editorIndex == 8) {
            transpose.length = static_cast<std::uint8_t>(clampInt(
                static_cast<int>(transpose.length) + direction, 1, 8));
        } else if (editorIndex == 9) {
            transpose.rate = static_cast<std::uint8_t>(clampInt(
                static_cast<int>(transpose.rate) + direction * (coarse ? 4 : 1), 1, 16));
        } else if (editorIndex == 10) {
            transpose.advance = static_cast<TransposeAdvance>(
                wrapIndex(static_cast<int>(transpose.advance) + direction, 3));
        }
        bumpRevision(app);
    }

    void adjustModulator(int direction, bool coarse) {
        if (direction == 0) return;
        std::lock_guard<std::mutex> lock(shared.mutex);
        auto& app = shared.app;
        auto& mod = app.tracks[static_cast<std::size_t>(selectedTrack)].modulator;
        const int amount = direction * (coarse ? 4 : 1);
        switch (editorIndex) {
            case 0: mod.targetTrack = static_cast<std::uint8_t>(wrapIndex(static_cast<int>(mod.targetTrack) + direction, kTrackCount)); break;
            case 1: mod.destination = static_cast<ModDest>(wrapIndex(static_cast<int>(mod.destination) + direction, 7)); break;
            case 2: mod.speed = static_cast<std::uint8_t>(clampInt(static_cast<int>(mod.speed) + amount, 1, 64)); break;
            case 3: mod.wave = static_cast<ModWave>(wrapIndex(static_cast<int>(mod.wave) + direction, 5)); break;
            case 4: mod.depth = static_cast<std::int8_t>(clampInt(static_cast<int>(mod.depth) + amount, -64, 63)); break;
            case 5: mod.offset = static_cast<std::uint8_t>(clampInt(static_cast<int>(mod.offset) + amount, 0, 63)); break;
            default: break;
        }
        bumpRevision(app);
    }

    void toggleScaleNote(int degree) {
        if (degree < 0 || degree >= 12) return;
        std::lock_guard<std::mutex> lock(shared.mutex);
        auto& app = shared.app;
        const std::uint16_t bit = static_cast<std::uint16_t>(1u << degree);
        const std::uint16_t changed = static_cast<std::uint16_t>(app.scaleMask ^ bit);
        if ((changed & 0x0FFFu) != 0u) app.scaleMask = changed;
        bumpRevision(app);
    }

    void applyScalePreset(int preset) {
        static constexpr std::array<std::uint16_t, 6> masks {
            0x0FFFu, 0x0AB5u, 0x05ADu, 0x06ADu, 0x06B5u, 0x0555u
        };
        if (preset < 0 || preset >= static_cast<int>(masks.size())) return;
        std::lock_guard<std::mutex> lock(shared.mutex);
        auto& app = shared.app;
        app.scaleMask = masks[static_cast<std::size_t>(preset)];
        bumpRevision(app);
        toast("SCALE PRESET LOADED");
    }

    void adjustScale(int direction) {
        if (editorIndex == 0) {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto& app = shared.app;
            app.scaleRoot = static_cast<std::uint8_t>(wrapIndex(static_cast<int>(app.scaleRoot) + direction, 12));
            bumpRevision(app);
        } else if (editorIndex >= 1 && editorIndex <= 12) {
            const int degree = editorIndex - 1;
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto& app = shared.app;
            const std::uint16_t bit = static_cast<std::uint16_t>(1u << degree);
            if (direction > 0) app.scaleMask = static_cast<std::uint16_t>(app.scaleMask | bit);
            else if ((app.scaleMask & static_cast<std::uint16_t>(~bit) & 0x0FFFu) != 0u)
                app.scaleMask = static_cast<std::uint16_t>(app.scaleMask & static_cast<std::uint16_t>(~bit));
            bumpRevision(app);
        } else if (editorIndex >= 13 && editorIndex < 19) {
            applyScalePreset(editorIndex - 13);
        }
    }

    bool trackIsEmpty(const TrackData& track) const {
        return std::none_of(track.steps.begin(), track.steps.end(),
            [](const Step& step) { return step.active || step.trigless; });
    }

    TimedGlobalSettings armedBankSettings(const AppState& app) const {
        TimedGlobalSettings settings;
        const auto& bank = app.banks[static_cast<std::size_t>(dataBank)];
        settings.applyTempo = dataArmTempo && bank.hasTempo;
        settings.bpm = bank.tempo;
        settings.applyScale = dataArmScale && bank.hasScale;
        settings.scaleRoot = bank.scaleRoot;
        settings.scaleMask = bank.scaleMask;
        return settings;
    }

    bool queueArmedBankSettings(const TimedGlobalSettings& settings) {
        if (!settings.applyTempo && !settings.applyScale) return true;
        if (!audio.queueGlobalSettings(settings)) return false;
        queuedGlobalGeneration = audio.status().submittedGlobalSettingsGeneration;
        return true;
    }

    void savePattern(int pattern) {
        bool locked = false;
        bool allCleared = true;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto& app = shared.app;
            auto& bank = app.banks[static_cast<std::size_t>(dataBank)];
            locked = bank.locked;
            if (!locked) {
                const int first = dataAllTracks ? 0 : selectedTrack;
                const int last = dataAllTracks ? kTrackCount - 1 : selectedTrack;
                for (int track = first; track <= last; ++track) {
                    auto& slot = app.patterns[static_cast<std::size_t>(track)]
                                             [static_cast<std::size_t>(pattern)];
                    const auto& source = app.tracks[static_cast<std::size_t>(track)];
                    if (trackIsEmpty(source)) slot = StoredPattern {};
                    else {
                        slot.track = source;
                        slot.occupied = true;
                        allCleared = false;
                    }
                }
                bumpRevision(app);
            }
        }
        if (locked) {
            toast("BANK LOCKED - SAVE REJECTED", true);
            return;
        }
        toast(std::string(dataAllTracks ? "COLUMN " : "PATTERN ") + hexValue(pattern) +
              (allCleared ? " CLEARED" : " SAVED"));
    }

    void loadPattern(int pattern, bool cue) {
        std::array<TrackData, kTrackCount> patterns {};
        std::array<bool, kTrackCount> occupied {};
        TimedGlobalSettings settings;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            const auto& app = shared.app;
            settings = armedBankSettings(app);
            for (int track = 0; track < kTrackCount; ++track) {
                const auto& slot = app.patterns[static_cast<std::size_t>(track)]
                                               [static_cast<std::size_t>(pattern)];
                occupied[static_cast<std::size_t>(track)] = slot.occupied;
                patterns[static_cast<std::size_t>(track)] = slot.occupied
                    ? slot.track : app.tracks[static_cast<std::size_t>(track)];
            }
        }
        const int first = dataAllTracks ? 0 : selectedTrack;
        const int last = dataAllTracks ? kTrackCount - 1 : selectedTrack;
        for (int track = first; track <= last; ++track) {
            if (!occupied[static_cast<std::size_t>(track)]) {
                toast("EMPTY PATTERN ON TRACK " + decimalValue(track + 1), true);
                return;
            }
        }

        if (cue) {
            const bool needColumnBoundary = dataAllTracks || settings.applyTempo || settings.applyScale;
            if (needColumnBoundary) {
                const std::uint8_t trackMask = dataAllTracks ? 0x1Fu
                    : static_cast<std::uint8_t>(1u << selectedTrack);
                if (!audio.queuePatternColumn(patterns, trackMask, settings)) {
                    toast("COLUMN CUE FULL OR AUDIO OFFLINE", true);
                    return;
                }
                const TransportStatus queued = audio.status();
                queuedColumnPattern = pattern;
                queuedColumnGeneration = queued.submittedColumnGeneration;
                queuedColumnIncludesSettings = settings.applyTempo || settings.applyScale;
                for (int track = 0; track < kTrackCount; ++track)
                    if (dataAllTracks || track == selectedTrack) queuedPattern[static_cast<std::size_t>(track)] = pattern;
                toast(std::string(dataAllTracks ? "COLUMN " : "PATTERN ") + hexValue(pattern) +
                      " CUE" + ((settings.applyTempo || settings.applyScale) ? " + BANK SETTINGS" : ""));
                return;
            }
            if (!audio.queuePattern(selectedTrack, patterns[static_cast<std::size_t>(selectedTrack)])) {
                toast("PATTERN QUEUE FULL OR AUDIO OFFLINE", true);
                return;
            }
            const TransportStatus queued = audio.status();
            queuedPattern[static_cast<std::size_t>(selectedTrack)] = pattern;
            queueGeneration[static_cast<std::size_t>(selectedTrack)] =
                queued.submittedPatternGenerations[static_cast<std::size_t>(selectedTrack)];
            toast("PATTERN " + hexValue(pattern) + " CUED");
            return;
        }

        if (!audio.available()) {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto& app = shared.app;
            for (int track = first; track <= last; ++track)
                app.tracks[static_cast<std::size_t>(track)] = patterns[static_cast<std::size_t>(track)];
            if (settings.applyTempo) app.bpm = settings.bpm;
            if (settings.applyScale) {
                app.scaleRoot = settings.scaleRoot;
                app.scaleMask = settings.scaleMask;
            }
            bumpRevision(app);
        } else {
            const TrackLoadMode mode = dataLoadMode == DataLoadMode::Reset
                ? TrackLoadMode::Reset : TrackLoadMode::InPlace;
            const std::uint8_t trackMask = dataAllTracks ? 0x1Fu
                : static_cast<std::uint8_t>(1u << selectedTrack);
            if (!audio.loadPatternColumnImmediate(patterns, mode, trackMask, settings)) {
                toast("PATTERN LOAD QUEUE FULL", true);
                return;
            }
            if (settings.applyTempo || settings.applyScale) {
                queuedColumnPattern = pattern;
                queuedColumnGeneration = audio.status().submittedColumnGeneration;
                queuedColumnIncludesSettings = true;
            }
        }
        queuedPattern.fill(-1);
        if (!queuedColumnIncludesSettings) queuedColumnPattern = -1;
        toast(std::string(dataAllTracks ? "COLUMN " : "PATTERN ") + hexValue(pattern) +
              (dataLoadMode == DataLoadMode::Reset ? " LOAD RESET" : " LOAD IN PLACE"));
    }

    void runDataOperation(bool randomize) {
        const int first = dataAllTracks ? 0 : selectedTrack;
        const int last = dataAllTracks ? kTrackCount - 1 : selectedTrack;
        std::array<TrackData, kTrackCount> replacements {};
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto& app = shared.app;
            for (int track = first; track <= last; ++track) {
                replacements[static_cast<std::size_t>(track)] = randomize
                    ? app.tracks[static_cast<std::size_t>(track)] : TrackData {};
                if (randomize) randomizeTrack(replacements[static_cast<std::size_t>(track)], track,
                    static_cast<std::uint32_t>(app.editRevision + static_cast<std::uint64_t>(track + 1) * 7919u));
                if (!audio.available()) app.tracks[static_cast<std::size_t>(track)] = replacements[static_cast<std::size_t>(track)];
            }
            if (!audio.available()) bumpRevision(app);
        }
        if (audio.available()) {
            const TrackLoadMode mode = dataLoadMode == DataLoadMode::Reset
                ? TrackLoadMode::Reset : TrackLoadMode::InPlace;
            const std::uint8_t trackMask = dataAllTracks ? 0x1Fu
                : static_cast<std::uint8_t>(1u << selectedTrack);
            if (!audio.loadPatternColumnImmediate(replacements, mode, trackMask)) {
                toast("TRACK OPERATION QUEUE FULL", true);
                return;
            }
        }
        toast(std::string(dataAllTracks ? "ALL TRACKS " : "TRACK ") +
              (randomize ? "RANDOMIZED" : "CLEARED"));
    }

    void activateData(bool save, bool cue = false) {
        if (dataColumn == -2) {
            if (!save) runDataOperation(false);
            return;
        }
        if (dataColumn == -1) {
            if (!save) runDataOperation(true);
            return;
        }
        const int pattern = dataPattern();
        if (save) savePattern(pattern);
        else loadPattern(pattern, cue);
    }

    void toggleCurrentBankLock() {
        bool locked = false;
        const int bank = dataBank;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto& app = shared.app;
            auto& settings = app.banks[static_cast<std::size_t>(bank)];
            settings.locked = !settings.locked;
            locked = settings.locked;
            bumpRevision(app);
        }
        toast("BANK " + decimalValue(bank + 1) + (locked ? " LOCKED" : " UNLOCKED"));
    }

    void beginBankNameEdit() {
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            bankNameEdit = shared.app.banks[static_cast<std::size_t>(dataBank)].name;
        }
        bankNameCursor = 0;
        overlay = Overlay::BankName;
        SDL_StartTextInput();
    }

    void commitBankNameEdit() {
        bool locked = false;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto& app = shared.app;
            locked = app.banks[static_cast<std::size_t>(dataBank)].locked;
            if (!locked) {
                app.banks[static_cast<std::size_t>(dataBank)].name = bankNameEdit;
                bumpRevision(app);
            }
        }
        overlay = Overlay::None;
        SDL_StopTextInput();
        toast(locked ? "BANK LOCKED - NAME REJECTED" : "BANK NAME SAVED", locked);
    }

    void storeRecallBankSetting(bool tempo, bool store, bool timed) {
        TimedGlobalSettings settings;
        bool available = true;
        bool locked = false;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto& app = shared.app;
            auto& bank = app.banks[static_cast<std::size_t>(dataBank)];
            if (store) {
                locked = bank.locked;
                if (!locked && tempo) {
                    bank.tempo = app.bpm;
                    bank.hasTempo = true;
                } else if (!locked) {
                    bank.scaleRoot = app.scaleRoot;
                    bank.scaleMask = app.scaleMask;
                    bank.hasScale = true;
                }
                if (!locked) bumpRevision(app);
            } else if (tempo) {
                available = bank.hasTempo;
                settings.applyTempo = available;
                settings.bpm = bank.tempo;
                if (available && !timed) {
                    app.bpm = bank.tempo;
                    bumpRevision(app);
                }
            } else {
                available = bank.hasScale;
                settings.applyScale = available;
                settings.scaleRoot = bank.scaleRoot;
                settings.scaleMask = bank.scaleMask;
                if (available && !timed) {
                    app.scaleRoot = bank.scaleRoot;
                    app.scaleMask = bank.scaleMask;
                    bumpRevision(app);
                }
            }
        }
        if (locked) {
            toast("BANK LOCKED - STORE REJECTED", true);
            return;
        }
        if (!available) {
            toast(tempo ? "BANK HAS NO BPM" : "BANK HAS NO SCALE", true);
            return;
        }
        if (!store && timed) {
            if (!queueArmedBankSettings(settings)) {
                toast("TIMED SETTINGS QUEUE FULL", true);
                return;
            }
        }
        toast(std::string(tempo ? "BPM " : "SCALE ") +
              (store ? "STORED" : (timed ? "CUED" : "RECALLED")));
    }

    static constexpr int kSynthParameterCount = 50;

    const char* oscillatorName(AdvancedOscShape value) const {
        switch (value) {
            case AdvancedOscShape::Sine: return "SINE";
            case AdvancedOscShape::Triangle: return "TRI";
            case AdvancedOscShape::Saw: return "SAW";
            case AdvancedOscShape::Square: return "SQUARE";
            case AdvancedOscShape::Pulse: return "PULSE";
            case AdvancedOscShape::Noise: return "NOISE";
        }
        return "SINE";
    }

    const char* filterName(AdvancedFilterMode value) const {
        switch (value) {
            case AdvancedFilterMode::Off: return "OFF";
            case AdvancedFilterMode::LowPass: return "LOW PASS";
            case AdvancedFilterMode::HighPass: return "HIGH PASS";
            case AdvancedFilterMode::BandPass: return "BAND PASS";
            case AdvancedFilterMode::Notch: return "NOTCH";
        }
        return "OFF";
    }

    const char* driveName(AdvancedDriveMode value) const {
        switch (value) {
            case AdvancedDriveMode::Off: return "OFF";
            case AdvancedDriveMode::SoftClip: return "SOFT";
            case AdvancedDriveMode::HardClip: return "HARD";
            case AdvancedDriveMode::Wavefold: return "FOLD";
        }
        return "OFF";
    }

    const char* modSourceName(AdvancedModSource value) const {
        switch (value) {
            case AdvancedModSource::Off: return "OFF";
            case AdvancedModSource::SineLfo: return "SINE LFO";
            case AdvancedModSource::TriangleLfo: return "TRI LFO";
            case AdvancedModSource::SawLfo: return "SAW LFO";
            case AdvancedModSource::SquareLfo: return "SQR LFO";
            case AdvancedModSource::SampleAndHold: return "S+H";
            case AdvancedModSource::AmpEnvelope: return "AMP ENV";
        }
        return "OFF";
    }

    const char* modDestinationName(AdvancedModDestination value) const {
        static constexpr std::array<const char*, 15> names {{
            "NONE", "PITCH", "LEVEL", "PAN", "CUTOFF", "RESONANCE", "DRIVE",
            "OP1 LEVEL", "OP2 LEVEL", "OP3 LEVEL", "OP4 LEVEL",
            "OP1 RATIO", "OP2 RATIO", "OP3 RATIO", "OP4 RATIO"
        }};
        return names[static_cast<std::size_t>(clampInt(static_cast<int>(value), 0, 14))];
    }

    std::string synthParameterLabel(int index) const {
        if (index == 0) return "ENGINE";
        if (index == 1) return "ALGORITHM";
        if (index >= 2 && index < 22) {
            static constexpr std::array<const char*, 5> names {{"WAVE", "RATIO", "LEVEL", "FEEDBACK", "DETUNE"}};
            const int local = index - 2;
            return "OP" + decimalValue(local / 5 + 1) + " " + names[static_cast<std::size_t>(local % 5)];
        }
        static constexpr std::array<const char*, 12> globalNames {{
            "AMP ATTACK", "AMP DECAY", "AMP SUSTAIN", "AMP RELEASE",
            "FILTER MODE", "FILTER CUTOFF", "RESONANCE", "DRIVE MODE",
            "DRIVE AMOUNT", "UNISON VOICES", "UNISON DETUNE", "UNISON WIDTH"
        }};
        if (index < 34) return globalNames[static_cast<std::size_t>(index - 22)];
        static constexpr std::array<const char*, 4> modNames {{"SOURCE", "RATE", "DEPTH", "DEST"}};
        const int local = index - 34;
        return "MOD" + decimalValue(local / 4 + 1) + " " + modNames[static_cast<std::size_t>(local % 4)];
    }

    std::string synthParameterValue(const AdvancedFmPatch& patch, int index) const {
        if (index == 0) return patch.enabled ? "4-OP ON" : "LEGACY";
        if (index == 1) return decimalValue(static_cast<int>(patch.algorithm) + 1);
        if (index >= 2 && index < 22) {
            const int local = index - 2;
            const auto& op = patch.operators[static_cast<std::size_t>(local / 5)];
            switch (local % 5) {
                case 0: return oscillatorName(op.shape);
                case 1: {
                    char buffer[16];
                    std::snprintf(buffer, sizeof(buffer), "%.3g", fmRatio(op.ratio));
                    return buffer;
                }
                case 2: return hexValue(op.level);
                case 3: return hexValue(op.feedback);
                case 4: return signedValue(op.detune);
            }
        }
        switch (index) {
            case 22: return hexValue(patch.ampEnvelope.attack);
            case 23: return hexValue(patch.ampEnvelope.decay);
            case 24: return hexValue(patch.ampEnvelope.sustain);
            case 25: return hexValue(patch.ampEnvelope.release);
            case 26: return filterName(patch.filterMode);
            case 27: return hexValue(patch.filterCutoff);
            case 28: return hexValue(patch.resonance);
            case 29: return driveName(patch.driveMode);
            case 30: return hexValue(patch.driveAmount);
            case 31: return decimalValue(patch.unisonVoices);
            case 32: return hexValue(patch.unisonDetune);
            case 33: return hexValue(patch.unisonWidth);
            default: break;
        }
        const int local = index - 34;
        const auto& mod = patch.modulation[static_cast<std::size_t>(clampInt(local / 4, 0, 3))];
        switch (local % 4) {
            case 0: return modSourceName(mod.source);
            case 1: return hexValue(mod.rate);
            case 2: return signedValue(mod.depth);
            case 3: return modDestinationName(mod.destination);
        }
        return "--";
    }

    void adjustOneSynthParameter(AdvancedFmPatch& patch, int index, int direction, bool coarse) {
        const int sign = direction > 0 ? 1 : -1;
        const int amount = sign * (coarse ? 8 : 1);
        const auto byte = [](std::uint8_t& target, int value, int low = 0, int high = 127) {
            target = static_cast<std::uint8_t>(clampInt(value, low, high));
        };
        if (index == 0) {
            patch.enabled = direction > 0;
            return;
        }
        patch.enabled = true;
        if (index == 1) {
            patch.algorithm = static_cast<AdvancedFmAlgorithm>(
                wrapIndex(static_cast<int>(patch.algorithm) + sign, 12));
            return;
        }
        if (index >= 2 && index < 22) {
            const int local = index - 2;
            auto& op = patch.operators[static_cast<std::size_t>(local / 5)];
            switch (local % 5) {
                case 0: op.shape = static_cast<AdvancedOscShape>(wrapIndex(static_cast<int>(op.shape) + sign, 6)); break;
                case 1: byte(op.ratio, static_cast<int>(op.ratio) + amount); break;
                case 2: byte(op.level, static_cast<int>(op.level) + amount); break;
                case 3: byte(op.feedback, static_cast<int>(op.feedback) + amount); break;
                case 4: op.detune = static_cast<std::int8_t>(clampInt(static_cast<int>(op.detune) + amount, -64, 63)); break;
            }
            return;
        }
        switch (index) {
            case 22: byte(patch.ampEnvelope.attack, static_cast<int>(patch.ampEnvelope.attack) + amount); return;
            case 23: byte(patch.ampEnvelope.decay, static_cast<int>(patch.ampEnvelope.decay) + amount); return;
            case 24: byte(patch.ampEnvelope.sustain, static_cast<int>(patch.ampEnvelope.sustain) + amount); return;
            case 25: byte(patch.ampEnvelope.release, static_cast<int>(patch.ampEnvelope.release) + amount); return;
            case 26: patch.filterMode = static_cast<AdvancedFilterMode>(wrapIndex(static_cast<int>(patch.filterMode) + sign, 5)); return;
            case 27: byte(patch.filterCutoff, static_cast<int>(patch.filterCutoff) + amount); return;
            case 28: byte(patch.resonance, static_cast<int>(patch.resonance) + amount); return;
            case 29: patch.driveMode = static_cast<AdvancedDriveMode>(wrapIndex(static_cast<int>(patch.driveMode) + sign, 4)); return;
            case 30: byte(patch.driveAmount, static_cast<int>(patch.driveAmount) + amount); return;
            case 31: byte(patch.unisonVoices, static_cast<int>(patch.unisonVoices) + sign, 1, 4); return;
            case 32: byte(patch.unisonDetune, static_cast<int>(patch.unisonDetune) + amount); return;
            case 33: byte(patch.unisonWidth, static_cast<int>(patch.unisonWidth) + amount); return;
            default: break;
        }
        const int local = index - 34;
        auto& mod = patch.modulation[static_cast<std::size_t>(clampInt(local / 4, 0, 3))];
        switch (local % 4) {
            case 0: mod.source = static_cast<AdvancedModSource>(wrapIndex(static_cast<int>(mod.source) + sign, 7)); break;
            case 1: byte(mod.rate, static_cast<int>(mod.rate) + amount); break;
            case 2: mod.depth = static_cast<std::int8_t>(clampInt(static_cast<int>(mod.depth) + amount, -127, 127)); break;
            case 3: mod.destination = static_cast<AdvancedModDestination>(wrapIndex(static_cast<int>(mod.destination) + sign, 15)); break;
        }
    }

    void adjustSynth(int direction, bool coarse) {
        if (selectedTrack == kTrackCount - 1 || direction == 0) return;
        Step preview;
        bool havePreview = false;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto& app = shared.app;
            forEditCells([&](int track, int stepIndex) {
                if (track == kTrackCount - 1) return;
                auto& step = app.tracks[static_cast<std::size_t>(track)]
                                 .steps[static_cast<std::size_t>(stepIndex)];
                adjustOneSynthParameter(step.advancedFm, editorIndex, direction, coarse);
                if (track == selectedTrack && stepIndex == selectedStep) {
                    preview = step;
                    havePreview = true;
                }
            });
            bumpRevision(app);
        }
        if (havePreview && preview.active) audio.preview(selectedTrack, preview);
    }

    void randomizeSynthParameter() {
        if (selectedTrack == kTrackCount - 1) return;
        Step preview;
        bool havePreview = false;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto& app = shared.app;
            std::uint32_t value = static_cast<std::uint32_t>(
                app.editRevision ^ (static_cast<std::uint64_t>(editorIndex + 17) * 0x85EBCA6Bull));
            forEditCells([&](int track, int stepIndex) {
                if (track == kTrackCount - 1) return;
                value ^= value << 13u;
                value ^= value >> 17u;
                value ^= value << 5u;
                auto& step = app.tracks[static_cast<std::size_t>(track)]
                                 .steps[static_cast<std::size_t>(stepIndex)];
                adjustOneSynthParameter(step.advancedFm, editorIndex,
                    (value & 1u) != 0u ? 1 : -1, (value & 0x1Cu) == 0x1Cu);
                if (track == selectedTrack && stepIndex == selectedStep) {
                    preview = step;
                    havePreview = true;
                }
            });
            bumpRevision(app);
        }
        if (havePreview && preview.active) audio.preview(selectedTrack, preview);
        toast(std::string("NUDGE ") + scopeName());
    }

    void adjustEditor(int direction, bool coarse) {
        switch (view) {
            case View::Grid: adjustGridParameter(direction, coarse); break;
            case View::Synth: adjustSynth(direction, coarse); break;
            case View::Echo: adjustEcho(direction, coarse); break;
            case View::Transpose: adjustTranspose(direction, coarse); break;
            case View::Mod: adjustModulator(direction, coarse); break;
            case View::Scale: adjustScale(direction); break;
            case View::Data: break;
        }
    }

    int editorCount() const {
        switch (view) {
            case View::Grid: return gridParamCount(selectedTrack);
            case View::Synth: return kSynthParameterCount;
            case View::Echo: return 8;
            case View::Transpose: return 11;
            case View::Mod: return 6;
            case View::Scale: return 19;
            case View::Data: return 18;
        }
        return 1;
    }

    void activateEditor(bool save) {
        switch (view) {
            case View::Grid: toggleStep(); break;
            case View::Synth: adjustSynth(1, false); break;
            case View::Echo: adjustEcho(1, false); break;
            case View::Transpose: adjustTranspose(1, false); break;
            case View::Mod: adjustModulator(1, false); break;
            case View::Scale:
                if (editorIndex >= 1 && editorIndex <= 12) toggleScaleNote(editorIndex - 1);
                else if (editorIndex >= 13) applyScalePreset(editorIndex - 13);
                break;
            case View::Data: activateData(save); break;
        }
    }

    const char* controllerActionName(int index) const {
        static constexpr std::array<const char*, kControllerActionCount> names {{
            "NAV UP", "NAV DOWN", "NAV LEFT", "NAV RIGHT", "CONFIRM", "CLEAR",
            "PARAM PREV", "PARAM NEXT", "VALUE DOWN", "VALUE UP", "COARSE MOD",
            "ALT MOD", "TRANSPORT", "COPY", "PASTE", "RANDOMIZE", "PALETTE", "CYCLE VIEW"
        }};
        return names[static_cast<std::size_t>(clampInt(index, 0,
            static_cast<int>(kControllerActionCount) - 1))];
    }

    std::string controllerButtonName(std::uint8_t button) const {
        if (button == kControllerButtonUnbound) return "--";
        const char* name = SDL_GameControllerGetStringForButton(
            static_cast<SDL_GameControllerButton>(button));
        return name ? asciiOnly(name) : decimalValue(button);
    }

    bool controllerButtonMatches(const ControllerSettings& settings, ControllerAction action,
                                 std::uint8_t button) const {
        return settings.buttons[static_cast<std::size_t>(action)] == button;
    }

    bool sendMappedKey(SDL_Scancode code, bool shift = false, bool control = false,
                       bool alt = false) {
        SDL_KeyboardEvent event {};
        event.type = SDL_KEYDOWN;
        event.state = SDL_PRESSED;
        event.repeat = 0;
        event.keysym.scancode = code;
        event.keysym.mod = static_cast<SDL_Keymod>((shift ? KMOD_SHIFT : KMOD_NONE) |
            (control ? KMOD_CTRL : KMOD_NONE) | (alt ? KMOD_ALT : KMOD_NONE));
        return handleKey(event);
    }

    void resetControllerBindings() {
        std::lock_guard<std::mutex> lock(shared.mutex);
        auto& app = shared.app;
        app.controller = ControllerSettings {};
        bumpRevision(app);
        toast("CONTROLLER DEFAULTS RESTORED");
    }

    void unbindControllerAction() {
        std::lock_guard<std::mutex> lock(shared.mutex);
        auto& app = shared.app;
        app.controller.buttons[static_cast<std::size_t>(controllerMapCursor)] = kControllerButtonUnbound;
        bumpRevision(app);
        controllerCapture = false;
        toast("ACTION UNBOUND");
    }

    bool handleControllerButton(const SDL_ControllerButtonEvent& event, bool pressed) {
        const std::uint8_t button = event.button;
        ControllerSettings settings;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            settings = shared.app.controller;
        }
        if (overlay == Overlay::ControllerMap && pressed && controllerCapture) {
            if (button > kControllerButtonMax) {
                toast("BUTTON NUMBER OUT OF RANGE", true);
                return true;
            }
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto& app = shared.app;
                app.controller.buttons[static_cast<std::size_t>(controllerMapCursor)] = button;
                bumpRevision(app);
            }
            controllerCapture = false;
            toast(std::string(controllerActionName(controllerMapCursor)) + " = " +
                  controllerButtonName(button));
            return true;
        }
        if (!settings.enabled) return true;
        if (controllerButtonMatches(settings, ControllerAction::CoarseModifier, button)) {
            controllerCoarseHeld = pressed;
            if (!pressed) return true;
        }
        if (controllerButtonMatches(settings, ControllerAction::AlternateModifier, button)) {
            controllerAlternateHeld = pressed;
            if (!pressed) return true;
        }
        if (!pressed) return true;

        if (overlay == Overlay::ControllerMap) {
            if (controllerButtonMatches(settings, ControllerAction::NavigateUp, button))
                controllerMapCursor = wrapIndex(controllerMapCursor - 1, static_cast<int>(kControllerActionCount));
            else if (controllerButtonMatches(settings, ControllerAction::NavigateDown, button))
                controllerMapCursor = wrapIndex(controllerMapCursor + 1, static_cast<int>(kControllerActionCount));
            else if (controllerButtonMatches(settings, ControllerAction::Confirm, button))
                controllerCapture = true;
            else if (controllerButtonMatches(settings, ControllerAction::Clear, button))
                unbindControllerAction();
            return true;
        }

        const bool valueMode = controllerAlternateHeld || controllerCoarseHeld;
        if (valueMode && controllerButtonMatches(settings, ControllerAction::ValueDecrease, button))
            return sendMappedKey(SDL_SCANCODE_MINUS, controllerCoarseHeld);
        if (valueMode && controllerButtonMatches(settings, ControllerAction::ValueIncrease, button))
            return sendMappedKey(SDL_SCANCODE_EQUALS, controllerCoarseHeld);
        if (controllerButtonMatches(settings, ControllerAction::NavigateUp, button))
            return sendMappedKey(SDL_SCANCODE_UP);
        if (controllerButtonMatches(settings, ControllerAction::NavigateDown, button))
            return sendMappedKey(SDL_SCANCODE_DOWN);
        if (controllerButtonMatches(settings, ControllerAction::NavigateLeft, button))
            return sendMappedKey(SDL_SCANCODE_LEFT);
        if (controllerButtonMatches(settings, ControllerAction::NavigateRight, button))
            return sendMappedKey(SDL_SCANCODE_RIGHT);
        if (controllerButtonMatches(settings, ControllerAction::Confirm, button))
            return sendMappedKey(SDL_SCANCODE_RETURN, controllerAlternateHeld);
        if (controllerButtonMatches(settings, ControllerAction::Clear, button))
            return sendMappedKey(SDL_SCANCODE_DELETE);
        if (controllerButtonMatches(settings, ControllerAction::ParameterPrevious, button))
            return sendMappedKey(SDL_SCANCODE_LEFTBRACKET);
        if (controllerButtonMatches(settings, ControllerAction::ParameterNext, button))
            return sendMappedKey(SDL_SCANCODE_RIGHTBRACKET);
        if (controllerButtonMatches(settings, ControllerAction::Transport, button))
            return sendMappedKey(SDL_SCANCODE_SPACE);
        if (controllerButtonMatches(settings, ControllerAction::Copy, button))
            return sendMappedKey(controllerAlternateHeld ? SDL_SCANCODE_X : SDL_SCANCODE_C);
        if (controllerButtonMatches(settings, ControllerAction::Paste, button))
            return sendMappedKey(SDL_SCANCODE_V);
        if (controllerButtonMatches(settings, ControllerAction::Randomize, button))
            return sendMappedKey(SDL_SCANCODE_R, controllerAlternateHeld);
        if (controllerButtonMatches(settings, ControllerAction::Palette, button))
            return sendMappedKey(SDL_SCANCODE_P);
        if (controllerButtonMatches(settings, ControllerAction::CycleView, button))
            return sendMappedKey(SDL_SCANCODE_TAB, controllerAlternateHeld);
        return true;
    }

    bool handleKey(const SDL_KeyboardEvent& key) {
        const SDL_Scancode code = key.keysym.scancode;
        const bool shift = (key.keysym.mod & KMOD_SHIFT) != 0;
        const bool control = (key.keysym.mod & KMOD_CTRL) != 0;
        const bool alt = (key.keysym.mod & KMOD_ALT) != 0;

        if (overlay == Overlay::BankName) {
            if (code == SDL_SCANCODE_ESCAPE) {
                overlay = Overlay::None;
                SDL_StopTextInput();
            } else if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER) {
                commitBankNameEdit();
            } else if (code == SDL_SCANCODE_LEFT) {
                bankNameCursor = wrapIndex(bankNameCursor - 1, 4);
            } else if (code == SDL_SCANCODE_RIGHT) {
                bankNameCursor = wrapIndex(bankNameCursor + 1, 4);
            } else if (code == SDL_SCANCODE_BACKSPACE || code == SDL_SCANCODE_DELETE) {
                bankNameEdit[static_cast<std::size_t>(bankNameCursor)] = ' ';
                bankNameCursor = wrapIndex(bankNameCursor - 1, 4);
            }
            return true;
        }
        if (overlay == Overlay::Palette) {
            if (code == SDL_SCANCODE_ESCAPE || code == SDL_SCANCODE_P) overlay = Overlay::None;
            else if (code == SDL_SCANCODE_LEFT || code == SDL_SCANCODE_UP)
                paletteCursor = wrapIndex(paletteCursor - 1, kPaletteSize + 2);
            else if (code == SDL_SCANCODE_RIGHT || code == SDL_SCANCODE_DOWN)
                paletteCursor = wrapIndex(paletteCursor + 1, kPaletteSize + 2);
            else if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER)
                paletteAction(shift, control);
            else if (code == SDL_SCANCODE_DELETE || code == SDL_SCANCODE_BACKSPACE)
                clearPaletteSlot();
            else if (code == SDL_SCANCODE_R) {
                paletteCursor = 1;
                paletteAction(false, control);
            }
            return true;
        }
        if (overlay == Overlay::ControllerMap) {
            if (code == SDL_SCANCODE_ESCAPE || code == SDL_SCANCODE_F4) {
                overlay = Overlay::None;
                controllerCapture = false;
            } else if (!controllerCapture && (code == SDL_SCANCODE_UP || code == SDL_SCANCODE_LEFT)) {
                controllerMapCursor = wrapIndex(controllerMapCursor - 1,
                    static_cast<int>(kControllerActionCount));
            } else if (!controllerCapture && (code == SDL_SCANCODE_DOWN || code == SDL_SCANCODE_RIGHT)) {
                controllerMapCursor = wrapIndex(controllerMapCursor + 1,
                    static_cast<int>(kControllerActionCount));
            } else if (!controllerCapture && (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER)) {
                controllerCapture = true;
                toast("PRESS A CONTROLLER BUTTON");
            } else if (code == SDL_SCANCODE_DELETE || code == SDL_SCANCODE_BACKSPACE) {
                unbindControllerAction();
            } else if (!controllerCapture && code == SDL_SCANCODE_D) {
                resetControllerBindings();
            } else if (!controllerCapture && code == SDL_SCANCODE_E) {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto& app = shared.app;
                app.controller.enabled = !app.controller.enabled;
                bumpRevision(app);
            }
            return true;
        }

        if (code == SDL_SCANCODE_ESCAPE) {
            if (helpVisible) helpVisible = false;
            else if (rangeActive) cancelRange();
            return true;
        }
        if (code == SDL_SCANCODE_F1 || (code == SDL_SCANCODE_SLASH && shift)) {
            if (key.repeat == 0) helpVisible = !helpVisible;
            return true;
        }
        if (code == SDL_SCANCODE_F2) {
            if (key.repeat == 0) toggleTheme();
            return true;
        }
        if (code == SDL_SCANCODE_F3) {
            if (key.repeat == 0) cycleAccent();
            return true;
        }
        if (code == SDL_SCANCODE_F4) {
            if (key.repeat == 0) overlay = Overlay::ControllerMap;
            return true;
        }
        if (code == SDL_SCANCODE_F5) {
            if (key.repeat == 0) cycleEditScope();
            return true;
        }
        if (helpVisible) return true;
        if (control && code == SDL_SCANCODE_S) {
            if (key.repeat == 0) {
                saveRequested = true;
                toast("SAVE REQUESTED");
            }
            return true;
        }
        if (code == SDL_SCANCODE_SPACE) {
            if (key.repeat == 0) toggleTransport();
            return true;
        }
        if (code == SDL_SCANCODE_TAB) {
            if (key.repeat == 0) changeView(shift ? -1 : 1);
            return true;
        }
        if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_5) {
            if (key.repeat == 0) {
                cancelRange();
                selectTrack(static_cast<int>(code - SDL_SCANCODE_1));
            }
            return true;
        }
        if (code == SDL_SCANCODE_PAGEUP || code == SDL_SCANCODE_PAGEDOWN) {
            selectedStep = wrapIndex(selectedStep + (code == SDL_SCANCODE_PAGEDOWN ? 1 : -1), kStepCount);
            return true;
        }
        if (code == SDL_SCANCODE_COMMA || code == SDL_SCANCODE_PERIOD) {
            if (alt) rotateSteps(code == SDL_SCANCODE_PERIOD ? 1 : -1, control);
            else adjustBpm(code == SDL_SCANCODE_PERIOD ? 1 : -1, shift);
            return true;
        }
        if (alt && (code == SDL_SCANCODE_LEFT || code == SDL_SCANCODE_RIGHT)) {
            adjustTrackRate(code == SDL_SCANCODE_RIGHT ? 1 : -1);
            return true;
        }
        if (alt && (code == SDL_SCANCODE_UP || code == SDL_SCANCODE_DOWN)) {
            adjustTrackShuffle(code == SDL_SCANCODE_UP ? 1 : -1, shift, control);
            return true;
        }

        if (code == SDL_SCANCODE_LEFT || code == SDL_SCANCODE_RIGHT ||
            code == SDL_SCANCODE_UP || code == SDL_SCANCODE_DOWN) {
            if (view == View::Grid) {
                if (shift) {
                    beginOrExtendRange(code == SDL_SCANCODE_LEFT ? -1 : (code == SDL_SCANCODE_RIGHT ? 1 : 0),
                                       code == SDL_SCANCODE_UP ? -1 : (code == SDL_SCANCODE_DOWN ? 1 : 0));
                } else {
                    cancelRange();
                    if (code == SDL_SCANCODE_LEFT) selectTrack(selectedTrack - 1);
                    else if (code == SDL_SCANCODE_RIGHT) selectTrack(selectedTrack + 1);
                    else selectedStep = wrapIndex(selectedStep +
                        (code == SDL_SCANCODE_DOWN ? 1 : -1), kStepCount);
                }
            } else if (view == View::Data) {
                if (code == SDL_SCANCODE_LEFT) dataColumn = dataColumn <= -2 ? 15 : dataColumn - 1;
                else if (code == SDL_SCANCODE_RIGHT) dataColumn = dataColumn >= 15 ? -2 : dataColumn + 1;
                else dataBank = wrapIndex(dataBank + (code == SDL_SCANCODE_DOWN ? 1 : -1), 8);
            } else if (code == SDL_SCANCODE_LEFT || code == SDL_SCANCODE_RIGHT) {
                editorIndex = wrapIndex(editorIndex + (code == SDL_SCANCODE_RIGHT ? 1 : -1), editorCount());
            } else {
                adjustEditor(code == SDL_SCANCODE_UP ? 1 : -1, shift);
            }
            return true;
        }

        if (code == SDL_SCANCODE_LEFTBRACKET || code == SDL_SCANCODE_RIGHTBRACKET) {
            const int delta = code == SDL_SCANCODE_RIGHTBRACKET ? 1 : -1;
            if (view == View::Grid)
                selectedParameter = wrapIndex(selectedParameter + delta, gridParamCount(selectedTrack));
            else if (view != View::Data) editorIndex = wrapIndex(editorIndex + delta, editorCount());
            return true;
        }
        if (code == SDL_SCANCODE_MINUS || code == SDL_SCANCODE_EQUALS ||
            code == SDL_SCANCODE_KP_MINUS || code == SDL_SCANCODE_KP_PLUS) {
            const bool increase = code == SDL_SCANCODE_EQUALS || code == SDL_SCANCODE_KP_PLUS;
            adjustEditor(increase ? 1 : -1, shift);
            return true;
        }
        if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER) {
            if (key.repeat == 0) activateEditor(shift);
            return true;
        }
        if (code == SDL_SCANCODE_DELETE || code == SDL_SCANCODE_BACKSPACE) {
            if (key.repeat == 0 && view == View::Grid) clearStep();
            return true;
        }
        if (key.repeat != 0) return true;

        if (view == View::Data) {
            if (code == SDL_SCANCODE_A) {
                dataAllTracks = !dataAllTracks;
                toast(dataAllTracks ? "DATA WHOLE COLUMN" : "DATA CURRENT TRACK");
            } else if (code == SDL_SCANCODE_I) {
                dataLoadMode = dataLoadMode == DataLoadMode::Reset
                    ? DataLoadMode::InPlace : DataLoadMode::Reset;
                toast(dataLoadMode == DataLoadMode::Reset ? "LOAD MODE RESET" : "LOAD MODE IN PLACE");
            } else if (code == SDL_SCANCODE_Q) {
                activateData(false, true);
            } else if (code == SDL_SCANCODE_N) {
                beginBankNameEdit();
            } else if (code == SDL_SCANCODE_K) {
                toggleCurrentBankLock();
            } else if (code == SDL_SCANCODE_B) {
                if (control) {
                    dataArmTempo = !dataArmTempo;
                    toast(dataArmTempo ? "BPM ARMED WITH NEXT CUE" : "BPM CUE DISARMED");
                } else storeRecallBankSetting(true, shift, alt);
            } else if (code == SDL_SCANCODE_G) {
                if (control) {
                    dataArmScale = !dataArmScale;
                    toast(dataArmScale ? "SCALE ARMED WITH NEXT CUE" : "SCALE CUE DISARMED");
                } else storeRecallBankSetting(false, shift, alt);
            }
            return true;
        }

        if (code == SDL_SCANCODE_L) {
            adjustTrackLength(shift ? -1 : 1);
        } else if (code == SDL_SCANCODE_D) {
            cycleDirection(1, shift || control);
        } else if (code == SDL_SCANCODE_M) {
            shift ? toggleSolo() : toggleMute();
        } else if (code == SDL_SCANCODE_U) {
            unmuteAll();
        } else if (code == SDL_SCANCODE_S) {
            toggleSnapshot();
        } else if (code == SDL_SCANCODE_R) {
            if (shift) randomizeSelectedTrack();
            else if (view == View::Grid) randomizeSelectedParameter();
            else if (view == View::Synth) randomizeSynthParameter();
        } else if (code == SDL_SCANCODE_C) {
            copyStep();
        } else if (code == SDL_SCANCODE_X && view == View::Grid) {
            cutStep();
        } else if (code == SDL_SCANCODE_V) {
            pasteStep();
        } else if (code == SDL_SCANCODE_T && view == View::Grid) {
            toggleTrigless();
        } else if (code == SDL_SCANCODE_P) {
            openPalette();
        } else if (code == SDL_SCANCODE_O) {
            rotateSteps(shift ? -1 : 1, control);
        }
        return true;
    }

    bool logicalMouse(int mouseX, int mouseY, std::uint32_t windowId, float& x, float& y) const {
        double pixelX = static_cast<double>(mouseX);
        double pixelY = static_cast<double>(mouseY);
        if (SDL_Window* window = SDL_GetWindowFromID(windowId)) {
            int windowWidth = 0;
            int windowHeight = 0;
            SDL_GetWindowSize(window, &windowWidth, &windowHeight);
            if (windowWidth > 0 && windowHeight > 0) {
                pixelX *= static_cast<double>(outputWidth) / static_cast<double>(windowWidth);
                pixelY *= static_cast<double>(outputHeight) / static_cast<double>(windowHeight);
            }
        }
        x = static_cast<float>((pixelX - static_cast<double>(viewportX)) /
                               static_cast<double>(viewportScale));
        y = static_cast<float>((pixelY - static_cast<double>(viewportY)) /
                               static_cast<double>(viewportScale));
        return x >= 0.0f && x < static_cast<float>(kLogicalWidth) &&
               y >= 0.0f && y < static_cast<float>(kLogicalHeight);
    }

    void handleTrackSelectorClick(float x, float y) {
        if (y < 119.0f || y >= 153.0f || x < 868.0f || x >= 1253.0f) return;
        const int track = static_cast<int>((x - 868.0f) / 77.0f);
        selectTrack(track);
    }

    void clickGrid(float x, float y, std::uint8_t button, bool shift) {
        constexpr int left = 28;
        constexpr int columnWidth = 238;
        constexpr int gap = 8;
        constexpr int headerTop = 118;
        constexpr int gridTop = 202;
        constexpr int rowHeight = 25;
        if (x >= static_cast<float>(left) && x < 1252.0f) {
            const int stride = columnWidth + gap;
            const int column = static_cast<int>(x - static_cast<float>(left)) / stride;
            const int inside = static_cast<int>(x - static_cast<float>(left)) % stride;
            if (column >= 0 && column < kTrackCount && inside < columnWidth) {
                if (y >= static_cast<float>(gridTop) && y < static_cast<float>(gridTop + rowHeight * 16)) {
                    const int step = clampInt(static_cast<int>(y - static_cast<float>(gridTop)) / rowHeight, 0, 15);
                    if (shift) {
                        if (!rangeActive) {
                            rangeActive = true;
                            rangeAnchorTrack = selectedTrack;
                            rangeAnchorStep = selectedStep;
                        }
                        selectedTrack = column;
                        selectedStep = step;
                    } else {
                        cancelRange();
                        selectTrack(column);
                        selectedStep = step;
                    }
                    if (button == SDL_BUTTON_RIGHT) toggleTrigless();
                    else toggleStep();
                    return;
                }
                selectTrack(column);
                if (y >= static_cast<float>(headerTop) && y < 198.0f) {
                    const float localX = x - static_cast<float>(left + column * stride);
                    if (y >= 151.0f && y < 174.0f)
                        localX < 119.0f ? adjustTrackRate(button == SDL_BUTTON_RIGHT ? -1 : 1)
                                        : adjustTrackLength(button == SDL_BUTTON_RIGHT ? -1 : 1);
                    else if (y >= 174.0f)
                        localX < 119.0f ? cycleDirection(button == SDL_BUTTON_RIGHT ? -1 : 1)
                                        : adjustTrackShuffle(button == SDL_BUTTON_RIGHT ? -1 : 1, false);
                    return;
                }
            }
        }

        if (y >= 622.0f && y < 738.0f) {
            const bool synthRow = y >= 681.0f;
            const int stepCount = gridStepParamCount(selectedTrack);
            const int count = synthRow ? gridParamCount(selectedTrack) - stepCount : stepCount;
            if (count <= 0 || x < 112.0f || x >= 1252.0f) return;
            const int width = 1140 / count;
            const int item = clampInt(static_cast<int>(x - 112.0f) / width, 0, count - 1);
            selectedParameter = synthRow ? stepCount + item : item;
        }
    }

    void clickSynth(float x, float y, std::uint8_t button) {
        if (x < 36.0f || x >= 1244.0f || y < 176.0f || y >= 626.0f) return;
        const int column = x >= 640.0f ? 1 : 0;
        const int row = clampInt(static_cast<int>((y - 176.0f) / 18.0f), 0, 24);
        editorIndex = column * 25 + row;
        adjustSynth(button == SDL_BUTTON_RIGHT ? -1 : 1, false);
    }

    void clickEcho(float x, float y, std::uint8_t button) {
        if (y < 335.0f || y >= 583.0f) return;
        const int column = x >= 640.0f ? 1 : 0;
        if (x < 35.0f || x >= 1245.0f) return;
        const int row = static_cast<int>(y - 335.0f) / 62;
        if (row < 0 || row >= 4) return;
        editorIndex = column * 4 + row;
        adjustEcho(button == SDL_BUTTON_RIGHT ? -1 : 1, false);
    }

    void clickTranspose(float x, float y, std::uint8_t button) {
        if (selectedTrack == kTrackCount - 1) return;
        if (x >= 58.0f && x < 1226.0f && y >= 190.0f && y < 450.0f) {
            const int index = clampInt(static_cast<int>((x - 58.0f) / 146.0f), 0, 7);
            editorIndex = index;
            if (button == SDL_BUTTON_RIGHT) {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto& app = shared.app;
                app.tracks[static_cast<std::size_t>(selectedTrack)].transpose
                    .values[static_cast<std::size_t>(index)] = 0;
                bumpRevision(app);
            } else {
                const int value = clampInt(static_cast<int>(std::lround((320.0f - y) / 5.0f)), -24, 24);
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto& app = shared.app;
                app.tracks[static_cast<std::size_t>(selectedTrack)].transpose
                    .values[static_cast<std::size_t>(index)] = static_cast<std::int8_t>(value);
                bumpRevision(app);
            }
            return;
        }
        if (y >= 494.0f && y < 576.0f && x >= 96.0f && x < 1184.0f) {
            editorIndex = 8 + clampInt(static_cast<int>((x - 96.0f) / 362.0f), 0, 2);
            adjustTranspose(button == SDL_BUTTON_RIGHT ? -1 : 1, false);
        }
    }

    void clickMod(float x, float y, std::uint8_t button) {
        if (x < 822.0f || x >= 1248.0f || y < 174.0f || y >= 570.0f) return;
        editorIndex = clampInt(static_cast<int>(y - 174.0f) / 66, 0, 5);
        adjustModulator(button == SDL_BUTTON_RIGHT ? -1 : 1, false);
    }

    void clickScale(float x, float y, std::uint8_t button) {
        if (x >= 46.0f && x < 1234.0f && y >= 242.0f && y < 420.0f) {
            const int degree = clampInt(static_cast<int>((x - 46.0f) / 99.0f), 0, 11);
            editorIndex = degree + 1;
            toggleScaleNote(degree);
            return;
        }
        if (x >= 46.0f && x < 318.0f && y >= 166.0f && y < 218.0f) {
            editorIndex = 0;
            adjustScale(button == SDL_BUTTON_RIGHT ? -1 : 1);
            return;
        }
        if (x >= 46.0f && x < 1234.0f && y >= 476.0f && y < 548.0f) {
            const int preset = clampInt(static_cast<int>((x - 46.0f) / 198.0f), 0, 5);
            editorIndex = 13 + preset;
            applyScalePreset(preset);
        }
    }

    void clickData(float x, float y, bool shift) {
        if (x < 72.0f || x >= 1206.0f || y < 188.0f || y >= 556.0f) return;
        const int visualColumn = clampInt(static_cast<int>((x - 72.0f) / 63.0f), 0, 17);
        dataColumn = visualColumn - 2;
        dataBank = clampInt(static_cast<int>((y - 188.0f) / 46.0f), 0, 7);
        activateData(shift);
    }

    void handleClick(float x, float y, std::uint8_t button, bool shift) {
        if (helpVisible) {
            helpVisible = false;
            return;
        }
        if (overlay == Overlay::Palette) {
            if (x >= 166.0f && x < 1110.0f && y >= 326.0f && y < 390.0f) {
                paletteCursor = clampInt(static_cast<int>((x - 166.0f) / 59.0f), 0, kPaletteSize + 1);
                paletteAction(shift, button == SDL_BUTTON_RIGHT);
            } else overlay = Overlay::None;
            return;
        }
        if (overlay == Overlay::ControllerMap) {
            if (x >= 168.0f && x < 1112.0f && y >= 190.0f && y < 514.0f) {
                const int column = x >= 640.0f ? 1 : 0;
                const int row = clampInt(static_cast<int>((y - 190.0f) / 36.0f), 0, 8);
                controllerMapCursor = column * 9 + row;
                if (button == SDL_BUTTON_RIGHT) unbindControllerAction();
                else controllerCapture = true;
            } else overlay = Overlay::None;
            return;
        }
        if (overlay == Overlay::BankName) {
            if (x >= 490.0f && x < 790.0f && y >= 390.0f && y < 450.0f) commitBankNameEdit();
            return;
        }
        if (y >= 15.0f && y < 57.0f && x >= 606.0f && x < 790.0f) {
            toggleTransport();
            return;
        }
        if (y >= 15.0f && y < 57.0f && x >= 800.0f && x < 925.0f) {
            adjustBpm(button == SDL_BUTTON_RIGHT ? -1 : 1, shift);
            return;
        }
        if (y >= 68.0f && y < 108.0f && x >= 28.0f && x < 812.0f) {
            const int tab = static_cast<int>((x - 28.0f) / 112.0f);
            if (tab >= 0 && tab < static_cast<int>(kViewNames.size())) {
                view = static_cast<View>(tab);
                editorIndex = 0;
                return;
            }
        }
        if (view != View::Grid) handleTrackSelectorClick(x, y);
        switch (view) {
            case View::Grid: clickGrid(x, y, button, shift); break;
            case View::Synth: clickSynth(x, y, button); break;
            case View::Echo: clickEcho(x, y, button); break;
            case View::Transpose: clickTranspose(x, y, button); break;
            case View::Mod: clickMod(x, y, button); break;
            case View::Scale: clickScale(x, y, button); break;
            case View::Data: clickData(x, y, shift); break;
        }
    }

    bool handleEvent(const SDL_Event& event) {
        if (event.type == SDL_KEYDOWN) return handleKey(event.key);
        if (event.type == SDL_TEXTINPUT && overlay == Overlay::BankName) {
            for (const unsigned char raw : std::string_view(event.text.text)) {
                if (raw < 32u || raw > 126u) continue;
                char value = static_cast<char>(raw);
                if (value >= 'a' && value <= 'z') value = static_cast<char>(value - 'a' + 'A');
                bankNameEdit[static_cast<std::size_t>(bankNameCursor)] = value;
                bankNameCursor = wrapIndex(bankNameCursor + 1, 4);
            }
            bankNameEdit[4] = '\0';
            return true;
        }
        if (event.type == SDL_CONTROLLERDEVICEADDED) {
            openController(event.cdevice.which);
            return true;
        }
        if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
            closeController(event.cdevice.which);
            return true;
        }
        if (event.type == SDL_CONTROLLERBUTTONDOWN)
            return handleControllerButton(event.cbutton, true);
        if (event.type == SDL_CONTROLLERBUTTONUP)
            return handleControllerButton(event.cbutton, false);
        if (event.type == SDL_MOUSEBUTTONDOWN) {
            float x = 0.0f;
            float y = 0.0f;
            if (logicalMouse(event.button.x, event.button.y, event.button.windowID, x, y)) {
                handleClick(x, y, event.button.button, (SDL_GetModState() & KMOD_SHIFT) != 0);
            }
        } else if (event.type == SDL_MOUSEWHEEL) {
            int mouseX = 0;
            int mouseY = 0;
            SDL_GetMouseState(&mouseX, &mouseY);
            float x = 0.0f;
            float y = 0.0f;
            const int raw = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
            if (logicalMouse(mouseX, mouseY, event.wheel.windowID, x, y) && raw != 0) {
                const int direction = raw > 0 ? 1 : -1;
                const bool coarse = (SDL_GetModState() & KMOD_SHIFT) != 0;
                if (y >= 15.0f && y < 57.0f && x >= 800.0f && x < 925.0f)
                    adjustBpm(direction, coarse);
                else if (view == View::Grid && y >= 118.0f && y < 198.0f) {
                    constexpr int stride = 246;
                    const int track = clampInt(static_cast<int>(x - 28.0f) / stride, 0, 4);
                    selectTrack(track);
                    const float localX = x - static_cast<float>(28 + track * stride);
                    if (y >= 151.0f && y < 174.0f) {
                        if (localX < 119.0f) adjustTrackRate(direction);
                        else adjustTrackLength(direction);
                    } else if (y >= 174.0f) {
                        if (localX < 119.0f) cycleDirection(direction);
                        else adjustTrackShuffle(direction, coarse);
                    }
                } else {
                    adjustEditor(direction, coarse);
                }
            }
        }
        return true;
    }

    void update(double deltaSeconds) {
        elapsed += deltaSeconds;
        toastSeconds = std::max(0.0, toastSeconds - deltaSeconds);
        shutterSeconds = std::max(0.0, shutterSeconds - deltaSeconds);
        transport = audio.status();
        if (queuedColumnPattern >= 0 && queuedColumnGeneration != 0 &&
            transport.appliedColumnGeneration == queuedColumnGeneration) {
            const int pattern = queuedColumnPattern;
            queuedColumnPattern = -1;
            queuedColumnGeneration = 0;
            if (queuedColumnIncludesSettings) {
                dataArmTempo = false;
                dataArmScale = false;
                queuedColumnIncludesSettings = false;
            }
            for (int track = 0; track < kTrackCount; ++track)
                if (queuedPattern[static_cast<std::size_t>(track)] == pattern)
                    queuedPattern[static_cast<std::size_t>(track)] = -1;
            toast("COLUMN " + hexValue(pattern) + " LOADED ON BOUNDARY");
        }
        if (queuedGlobalGeneration != 0 &&
            transport.appliedGlobalSettingsGeneration == queuedGlobalGeneration) {
            queuedGlobalGeneration = 0;
            dataArmTempo = false;
            dataArmScale = false;
        }
        for (int track = 0; track < kTrackCount; ++track) {
            const std::size_t index = static_cast<std::size_t>(track);
            const int pattern = queuedPattern[index];
            if (pattern < 0) continue;
            if (queueGeneration[index] != 0 &&
                transport.appliedPatternGenerations[index] == queueGeneration[index]) {
                queuedPattern[index] = -1;
                queueGeneration[index] = 0;
                toast("PATTERN " + hexValue(pattern) + " LOADED ON LOOP");
            }
        }
    }

    void drawHeader(SDL_Renderer* renderer, const AppState& app, const Palette& palette) const {
        fillRect(renderer, 0, 0, kLogicalWidth, 108, palette.surface);
        fillRect(renderer, 28, 17, 3, 34, palette.accent);
        drawText(renderer, 42, 15, "FMS", palette.text, 4);
        drawText(renderer, 125, 18, "NATIVE FM STEP SEQUENCER", palette.muted, 2);
        drawText(renderer, 125, 39, "4 X HYBRID 2/4-OP FM  /  PSG NOISE  /  16 STEPS", palette.faint, 1);

        const bool running = transport.running;
        fillRect(renderer, 606, 15, 184, 42, running ? palette.accentDim : palette.raised);
        strokeRect(renderer, 606, 15, 184, 42, running ? palette.accent : palette.lineStrong);
        if (running) {
            fillRect(renderer, 621, 27, 4, 18, palette.accent);
            fillRect(renderer, 630, 27, 4, 18, palette.accent);
        } else {
            const SDL_Point points[4] {{621, 26}, {621, 46}, {637, 36}, {621, 26}};
            setColor(renderer, palette.text);
            SDL_RenderDrawLines(renderer, points, 4);
        }
        drawText(renderer, 650, 28, running ? "RUNNING" : "STOPPED", running ? palette.accent : palette.text, 2);

        fillRect(renderer, 800, 15, 125, 42, palette.raised);
        drawText(renderer, 812, 21, "BPM", palette.muted, 1);
        drawTextRight(renderer, 912, 25, decimalValue(app.bpm), palette.text, 3);

        drawText(renderer, 946, 18, snapshot.has_value() ? (snapshotSide ? "SNAP B" : "SNAP A") : "SNAP --", snapshot.has_value() ? palette.accent : palette.muted, 1);
        drawText(renderer, 946, 38, audio.available() ? "AUDIO OK" : "AUDIO OFF", audio.available() ? palette.text : palette.muted, 1);

        drawText(renderer, 1052, 16, "L", palette.muted, 1);
        drawText(renderer, 1052, 36, "R", palette.muted, 1);
        drawLine(renderer, 1068, 21, 1248, 21, palette.line);
        drawLine(renderer, 1068, 41, 1248, 41, palette.line);
        fillRect(renderer, 1068, 18, static_cast<int>(std::lround(180.0f * std::clamp(transport.peakLeft, 0.0f, 1.0f))), 7, palette.accent);
        fillRect(renderer, 1068, 38, static_cast<int>(std::lround(180.0f * std::clamp(transport.peakRight, 0.0f, 1.0f))), 7, palette.accent);

        for (int i = 0; i < static_cast<int>(kViewNames.size()); ++i) {
            const int x = 28 + i * 112;
            const bool active = static_cast<int>(view) == i;
            if (active) fillRect(renderer, x, 68, 108, 40, palette.raised);
            drawTextCentered(renderer, x + 54, 81, kViewNames[static_cast<std::size_t>(i)],
                             active ? palette.accent : palette.muted, 2);
            if (active) fillRect(renderer, x, 105, 108, 3, palette.accent);
        }
        drawTextRight(renderer, 1252, 80, "TAB VIEW   F4 PAD   F1 HELP", palette.muted, 1);
        drawLine(renderer, 0, 107, kLogicalWidth, 107, palette.lineStrong);
    }

    void drawTrackSelector(SDL_Renderer* renderer, const Palette& palette) const {
        drawText(renderer, 735, 129, "EDIT TRACK", palette.muted, 1);
        for (int track = 0; track < kTrackCount; ++track) {
            const int x = 868 + track * 77;
            const bool selected = track == selectedTrack;
            drawTextCentered(renderer, x + 34, 126, track == 4 ? "N" : decimalValue(track + 1),
                             selected ? palette.accent : palette.muted, 2);
            drawLine(renderer, x + 4, 150, x + 65, 150, selected ? palette.accent : palette.line);
        }
    }

    void drawSectionTitle(SDL_Renderer* renderer, const Palette& palette, std::string_view title,
                          std::string_view description, bool showTracks = true) const {
        drawText(renderer, 28, 124, title, palette.text, 3);
        drawText(renderer, 29, 153, description, palette.muted, 1);
        if (showTracks) drawTrackSelector(renderer, palette);
    }

    void drawGrid(SDL_Renderer* renderer, const AppState& app, const Palette& palette) const {
        constexpr int left = 28;
        constexpr int columnWidth = 238;
        constexpr int gap = 8;
        constexpr int gridTop = 202;
        constexpr int rowHeight = 25;
        const GridParam parameter = gridParamItem(selectedTrack, selectedParameter).id;

        for (int trackIndex = 0; trackIndex < kTrackCount; ++trackIndex) {
            const int x = left + trackIndex * (columnWidth + gap);
            const auto& track = app.tracks[static_cast<std::size_t>(trackIndex)];
            const bool selected = trackIndex == selectedTrack;
            if (trackIndex > 0) drawLine(renderer, x - gap / 2, 118, x - gap / 2, 606, palette.line);
            if (selected) fillRect(renderer, x, 118, columnWidth, 3, palette.accent);
            drawText(renderer, x + 6, 128, trackIndex == 4 ? "PSG NOISE" : "FM " + decimalValue(trackIndex + 1),
                     selected ? palette.text : palette.muted, 2);
            if (track.muted) drawTextRight(renderer, x + columnWidth - 6, 128, "MUTE", palette.accent, 1);
            else if (track.solo) drawTextRight(renderer, x + columnWidth - 6, 128, "SOLO", palette.accent, 1);

            drawText(renderer, x + 6, 154, "RATE", palette.faint, 1);
            drawText(renderer, x + 43, 154, rateName(track.rateIndex), palette.text, 1);
            drawText(renderer, x + 126, 154, "LEN", palette.faint, 1);
            drawTextRight(renderer, x + columnWidth - 7, 154, decimalValue(track.length), palette.text, 1);
            drawText(renderer, x + 6, 177, "DIR", palette.faint, 1);
            drawText(renderer, x + 43, 177, directionName(track.direction), palette.text, 1);
            drawText(renderer, x + 126, 177, "SHF", palette.faint, 1);
            drawTextRight(renderer, x + columnWidth - 7, 177, decimalValue(track.shuffle), palette.text, 1);
            drawLine(renderer, x, 196, x + columnWidth, 196, selected ? palette.lineStrong : palette.line);

            for (int stepIndex = 0; stepIndex < kStepCount; ++stepIndex) {
                const int y = gridTop + stepIndex * rowHeight;
                const auto& step = track.steps[static_cast<std::size_t>(stepIndex)];
                const bool cursor = selected && stepIndex == selectedStep;
                const bool inSelection = selectedCell(trackIndex, stepIndex);
                const bool playhead = transport.playheads[static_cast<std::size_t>(trackIndex)] == stepIndex;
                const bool inLength = stepIndex < static_cast<int>(track.length);
                if (playhead) fillRect(renderer, x, y, columnWidth, rowHeight - 1, palette.accentDim);
                if (inSelection) fillRect(renderer, x, y, columnWidth, rowHeight - 1,
                                          rangeActive ? palette.accentDim : palette.raised);
                if (!inLength) fillRect(renderer, x, y, columnWidth, rowHeight - 1,
                                        {palette.background.r, palette.background.g, palette.background.b, 170});
                if ((stepIndex + 1) % 4 == 0)
                    drawLine(renderer, x, y + rowHeight - 1, x + columnWidth, y + rowHeight - 1, palette.lineStrong);
                else drawLine(renderer, x + 4, y + rowHeight - 1, x + columnWidth, y + rowHeight - 1, palette.line);

                if (playhead) fillRect(renderer, x, y + 2, 3, rowHeight - 5, palette.accent);
                drawText(renderer, x + 8, y + 8, hexValue(stepIndex, 1), inLength ? palette.faint : palette.lineStrong, 1);
                if (step.active) {
                    if (step.trigless) strokeRect(renderer, x + 30, y + 8, 8, 8, palette.accent);
                    else fillRect(renderer, x + 31, y + 9, 7, 7, palette.accent);
                } else {
                    drawLine(renderer, x + 31, y + 12, x + 37, y + 12, palette.faint);
                }

                const GridParam shown = selected ? parameter
                                      : (trackIndex == kTrackCount - 1
                                             ? GridParam::NoiseRate
                                             : GridParam::Note);
                const Color valueColor = step.active && inLength ? palette.text : palette.muted;
                const char* shownLabel = selected
                    ? gridParamItem(trackIndex, clampInt(selectedParameter, 0,
                                                        gridParamCount(trackIndex) - 1)).shortName
                    : (trackIndex == kTrackCount - 1 ? "RATE" : "NOTE");
                drawText(renderer, x + 49, y + 8, shownLabel,
                         cursor ? palette.accent : palette.faint, 1);
                drawText(renderer, x + 94, y + 6, gridValue(step, shown, trackIndex == 4), valueColor, 2);
                drawMiniBar(renderer, x + 174, y + 13, 53,
                            gridValueUnit(step, shown, trackIndex == 4), palette, step.active);
                if (inSelection && (!rangeActive || std::fmod(elapsed, 0.6) < 0.42))
                    strokeRect(renderer, x, y, columnWidth, rowHeight - 1, palette.accent);
            }
        }

        drawGridParameterStrip(renderer, app, palette);
    }

    void drawGridParameterStrip(SDL_Renderer* renderer, const AppState& app,
                                const Palette& palette) const {
        const auto& step = app.tracks[static_cast<std::size_t>(selectedTrack)]
                              .steps[static_cast<std::size_t>(selectedStep)];
        const int stepCount = gridStepParamCount(selectedTrack);
        const int totalCount = gridParamCount(selectedTrack);
        fillRect(renderer, 0, 614, kLogicalWidth, 146, palette.surface);
        drawLine(renderer, 0, 614, kLogicalWidth, 614, palette.lineStrong);
        drawText(renderer, 28, 630, "STEP", palette.muted, 1);
        drawText(renderer, 28, 645, hexValue(selectedStep, 1), palette.text, 3);
        drawText(renderer, 70, 646, selectedTrack == 4 ? "NOISE" : "FM" + decimalValue(selectedTrack + 1), palette.faint, 1);

        const auto drawRow = [&](int first, int count, int y, const char* label) {
            drawText(renderer, 28, y + 13, label, palette.muted, 1);
            const int itemWidth = 1140 / count;
            for (int i = 0; i < count; ++i) {
                const int parameterIndex = first + i;
                const int x = 112 + i * itemWidth;
                const bool selected = parameterIndex == selectedParameter;
                const auto& item = gridParamItem(selectedTrack, parameterIndex);
                if (selected) fillRect(renderer, x, y, itemWidth - 3, 48, palette.accentDim);
                drawText(renderer, x + 5, y + 7, item.shortName,
                         selected ? palette.accent : palette.muted, 1);
                drawText(renderer, x + 5, y + 24, gridValue(step, item.id, selectedTrack == 4),
                         selected ? palette.text : palette.faint, 1);
                if (selected) fillRect(renderer, x, y + 46, itemWidth - 3, 2, palette.accent);
            }
        };
        drawRow(0, stepCount, 622, "STEP");
        drawRow(stepCount, totalCount - stepCount, 681, selectedTrack == 4 ? "PSG" : "SYNTH");
        drawText(renderer, 28, 748, std::string("F5 SCOPE ") + scopeName(), palette.accent, 1);
        drawTextRight(renderer, 1252, 748,
                      "SHIFT+ARROWS RANGE   C/X/V COPY CUT PASTE   P PALETTE   R NUDGE",
                      palette.muted, 1);
    }

    void drawSynth(SDL_Renderer* renderer, const AppState& app, const Palette& palette) const {
        drawSectionTitle(renderer, palette, "ADVANCED SYNTH", "PER-STEP HYBRID 4-OP ENGINE - ALL 50 FIELDS");
        if (selectedTrack == kTrackCount - 1) {
            drawTextCentered(renderer, 640, 286, "ADVANCED 4-OP ENGINE IS AVAILABLE ON FM TRACKS 1-4",
                             palette.muted, 2);
            drawTextCentered(renderer, 640, 332, "SELECT TRACK 1-4  /  PSG SOUND REMAINS IN GRID",
                             palette.faint, 1);
            drawEditorFooter(renderer, palette, "P OPENS THE NOISE SOUND PALETTE   PAGE UP/DOWN SELECTS STEP");
            return;
        }
        const auto& step = app.tracks[static_cast<std::size_t>(selectedTrack)]
                              .steps[static_cast<std::size_t>(selectedStep)];
        drawText(renderer, 28, 160,
                 "STEP " + hexValue(selectedStep, 1) + "  FM" + decimalValue(selectedTrack + 1) +
                 "  SCOPE " + scopeName(), palette.accent, 1);
        drawTextRight(renderer, 1250, 160,
                      step.advancedFm.enabled ? "4-OP ACTIVE" : "LEGACY 2-OP ACTIVE", palette.text, 1);
        drawLine(renderer, 628, 174, 628, 628, palette.lineStrong);
        for (int index = 0; index < kSynthParameterCount; ++index) {
            const int column = index / 25;
            const int row = index % 25;
            const int x = 36 + column * 612;
            const int y = 176 + row * 18;
            const bool selected = editorIndex == index;
            if (selected) fillRect(renderer, x, y, 596, 17, palette.accentDim);
            if (selected) fillRect(renderer, x, y, 3, 17, palette.accent);
            drawText(renderer, x + 10, y + 5, synthParameterLabel(index),
                     selected ? palette.accent : palette.muted, 1);
            drawTextRight(renderer, x + 584, y + 5, synthParameterValue(step.advancedFm, index),
                          selected ? palette.text : palette.faint, 1);
            drawLine(renderer, x + 6, y + 17, x + 590, y + 17, palette.line);
        }
        drawEditorFooter(renderer, palette,
            "ALL FIELDS VISIBLE   [ ] OR LEFT/RIGHT SELECT   -/= EDIT   PAGE UP/DOWN STEP   P PALETTE");
    }

    std::string echoValue(const EchoSettings& echo, int index) const {
        if (selectedTrack == kTrackCount - 1 &&
            (index == 2 || index == 3 || index == 5 || index == 6)) return "N/A";
        switch (index) {
            case 0: return decimalValue(echo.repeats);
            case 1: return decimalValue(echo.speedTicks) + " TICKS";
            case 2: return signedValue(echo.transpose) + " ST";
            case 3: return "EVERY " + decimalValue(echo.transposeModulo);
            case 4: return signedValue(echo.volumeDelta);
            case 5: return signedValue(echo.modDelta);
            case 6: return signedValue(echo.feedbackDelta);
            case 7: return echoPanName(echo.pan);
            default: return "--";
        }
    }

    float echoUnit(const EchoSettings& echo, int index) const {
        if (selectedTrack == kTrackCount - 1 &&
            (index == 2 || index == 3 || index == 5 || index == 6)) return 0.0f;
        switch (index) {
            case 0: return static_cast<float>(echo.repeats) / 8.0f;
            case 1: return static_cast<float>(echo.speedTicks - 1u) / 95.0f;
            case 2: return static_cast<float>(echo.transpose + 24) / 48.0f;
            case 3: return static_cast<float>(echo.transposeModulo - 1u) / 7.0f;
            case 4: return static_cast<float>(echo.volumeDelta + 64) / 127.0f;
            case 5: return static_cast<float>(echo.modDelta + 64) / 127.0f;
            case 6: return static_cast<float>(echo.feedbackDelta + 64) / 127.0f;
            case 7: return static_cast<float>(static_cast<int>(echo.pan)) / 3.0f;
            default: return 0.0f;
        }
    }

    void drawEcho(SDL_Renderer* renderer, const AppState& app, const Palette& palette) const {
        drawSectionTitle(renderer, palette, "NOTE ECHO", "ALGORITHMIC REPEATS PER TRACK");
        const auto& echo = app.tracks[static_cast<std::size_t>(selectedTrack)].echo;
        drawLine(renderer, 54, 269, 1226, 269, palette.lineStrong);
        const int count = static_cast<int>(echo.repeats) + 1;
        for (int i = 0; i < count; ++i) {
            const float decay = std::max(0.12f, 1.0f + static_cast<float>(echo.volumeDelta) *
                static_cast<float>(i) / 64.0f);
            const int x = count == 1 ? 640 : 80 + i * (1080 / (count - 1));
            const int height = static_cast<int>(std::lround(78.0f * decay));
            drawLine(renderer, x, 269, x, 269 - height, i == 0 ? palette.text : palette.accent);
            fillRect(renderer, x - 3, 266 - height, 7, 7, i == 0 ? palette.text : palette.accent);
            drawTextCentered(renderer, x, 286, i == 0 ? "DRY" : decimalValue(i), palette.faint, 1);
        }
        if (echo.repeats == 0) drawTextCentered(renderer, 640, 225, "NO REPEATS", palette.muted, 2);

        static constexpr std::array<const char*, 8> labels {
            "REPEATS", "SPEED", "TRANSPOSE", "TSP MODULO",
            "VOLUME DELTA", "MOD DELTA", "FEEDBACK DELTA", "PAN"
        };
        for (int index = 0; index < 8; ++index) {
            const int column = index / 4;
            const int row = index % 4;
            const int x = 35 + column * 605;
            const int y = 335 + row * 62;
            const bool selected = editorIndex == index;
            const bool available = selectedTrack != kTrackCount - 1 ||
                                   (index != 2 && index != 3 && index != 5 && index != 6);
            if (column == 1) drawLine(renderer, 622, 330, 622, 583, palette.line);
            drawText(renderer, x + 8, y + 10, labels[static_cast<std::size_t>(index)],
                     available ? (selected ? palette.accent : palette.muted) : palette.faint, 1);
            drawTextRight(renderer, x + 572, y + 8, echoValue(echo, index),
                          available ? palette.text : palette.faint, 2);
            drawMiniBar(renderer, x + 8, y + 42, 564, echoUnit(echo, index), palette, selected);
            drawLine(renderer, x, y + 60, x + 582, y + 60, palette.line);
            if (selected) fillRect(renderer, x, y + 3, 3, 48, palette.accent);
        }
        drawEditorFooter(renderer, palette, "LEFT/RIGHT SELECT   UP/DOWN OR -/= EDIT   RIGHT CLICK DECREASE");
    }

    void drawTranspose(SDL_Renderer* renderer, const AppState& app, const Palette& palette) const {
        drawSectionTitle(renderer, palette, "TRANSPOSE", "8-STEP SEMITONE SEQUENCER");
        if (selectedTrack == kTrackCount - 1) {
            drawLine(renderer, 180, 320, 1100, 320, palette.lineStrong);
            drawTextCentered(renderer, 640, 272, "FM TRACKS ONLY", palette.muted, 3);
            drawTextCentered(renderer, 640, 345,
                             "NOISE PITCH IS EDITED WITH PER-STEP RATE", palette.faint, 1);
            drawEditorFooter(renderer, palette,
                             "SELECT FM TRACK 1-4   NOISE RATE LIVES IN THE GRID SYNTH ROW");
            return;
        }
        const auto& transpose = app.tracks[static_cast<std::size_t>(selectedTrack)].transpose;
        drawLine(renderer, 58, 320, 1226, 320, palette.lineStrong);
        drawText(renderer, 30, 313, "0", palette.faint, 1);
        for (int i = 0; i < 8; ++i) {
            const int x = 58 + i * 146;
            const int value = transpose.values[static_cast<std::size_t>(i)];
            const bool selected = editorIndex == i;
            const bool inLength = i < static_cast<int>(transpose.length);
            if (selected) fillRect(renderer, x + 4, 185, 136, 270, palette.accentDim);
            drawLine(renderer, x + 72, 190, x + 72, 446, palette.line);
            const int bar = value * 5;
            const int top = std::min(320, 320 - bar);
            const int height = std::max(2, std::abs(bar));
            fillRect(renderer, x + 48, top, 48, height, inLength ? palette.accent : palette.faint);
            drawTextCentered(renderer, x + 72, 212, signedValue(value), inLength ? palette.text : palette.muted, 2);
            drawTextCentered(renderer, x + 72, 428, decimalValue(i + 1), selected ? palette.accent : palette.faint, 1);
            if (!inLength) drawLine(renderer, x + 19, 401, x + 125, 401, palette.lineStrong);
            if (selected) fillRect(renderer, x + 16, 451, 112, 3, palette.accent);
        }

        static constexpr std::array<const char*, 3> labels {"LENGTH", "RATE", "ADVANCE"};
        const std::array<std::string, 3> values {
            decimalValue(transpose.length), decimalValue(transpose.rate), advanceName(transpose.advance)
        };
        for (int i = 0; i < 3; ++i) {
            const int x = 96 + i * 362;
            const bool selected = editorIndex == 8 + i;
            drawText(renderer, x + 8, 505, labels[static_cast<std::size_t>(i)],
                     selected ? palette.accent : palette.muted, 1);
            drawText(renderer, x + 8, 530, values[static_cast<std::size_t>(i)], palette.text, 2);
            drawLine(renderer, x, 570, x + 330, 570, selected ? palette.accent : palette.lineStrong);
        }
        drawEditorFooter(renderer, palette, "CLICK/DRAG HEIGHT TO SET   RIGHT CLICK ZERO   ARROWS EDIT");
    }

    float modWaveSample(ModWave wave, float phase, std::uint32_t seed) const {
        phase -= std::floor(phase);
        switch (wave) {
            case ModWave::RampDown: return 1.0f - phase * 2.0f;
            case ModWave::RampUp: return phase * 2.0f - 1.0f;
            case ModWave::Triangle: return 1.0f - 4.0f * std::abs(phase - 0.5f);
            case ModWave::Square: return phase < 0.5f ? 1.0f : -1.0f;
            case ModWave::Random: {
                std::uint32_t value = seed + static_cast<std::uint32_t>(phase * 8.0f) * 0x9E3779B9u;
                value ^= value >> 16u;
                value *= 0x7FEB352Du;
                value ^= value >> 15u;
                return static_cast<float>(value & 0xFFFFu) / 32767.5f - 1.0f;
            }
        }
        return 0.0f;
    }

    std::string modValue(const ModulatorSettings& mod, int index) const {
        switch (index) {
            case 0: return mod.targetTrack == 4 ? "NOISE" : "FM " + decimalValue(mod.targetTrack + 1);
            case 1: return destinationName(mod.destination);
            case 2: return decimalValue(mod.speed) + " STEPS";
            case 3: return waveName(mod.wave);
            case 4: return signedValue(mod.depth);
            case 5: return decimalValue(mod.offset);
            default: return "--";
        }
    }

    void drawMod(SDL_Renderer* renderer, const AppState& app, const Palette& palette) const {
        drawSectionTitle(renderer, palette, "STEP MODULATOR", "PER-STEP SAMPLE AND HOLD MODULATION");
        const auto& mod = app.tracks[static_cast<std::size_t>(selectedTrack)].modulator;
        drawText(renderer, 52, 190, waveName(mod.wave), palette.muted, 1);
        drawTextRight(renderer, 774, 190, "DEPTH " + signedValue(mod.depth), palette.muted, 1);
        drawLine(renderer, 52, 362, 778, 362, palette.lineStrong);
        drawLine(renderer, 52, 208, 52, 516, palette.line);
        drawLine(renderer, 778, 208, 778, 516, palette.line);
        const float amplitude = static_cast<float>(std::abs(static_cast<int>(mod.depth))) / 64.0f;
        int previousX = 52;
        int previousY = 362;
        for (int i = 1; i <= 128; ++i) {
            const float phase = static_cast<float>(i) / 32.0f + static_cast<float>(mod.offset) / 64.0f;
            float sample = modWaveSample(mod.wave, phase, static_cast<std::uint32_t>(selectedTrack + 1));
            if (mod.depth < 0) sample = -sample;
            const int x = 52 + i * 726 / 128;
            const int y = 362 - static_cast<int>(std::lround(sample * amplitude * 135.0f));
            drawLine(renderer, previousX, previousY, x, y, palette.accent);
            previousX = x;
            previousY = y;
        }
        for (int i = 0; i < 9; ++i) {
            const int x = 52 + i * 726 / 8;
            drawLine(renderer, x, 354, x, 370, palette.lineStrong);
        }

        static constexpr std::array<const char*, 6> labels {
            "TARGET TRACK", "DESTINATION", "SPEED", "WAVE", "DEPTH", "OFFSET"
        };
        drawLine(renderer, 810, 174, 810, 570, palette.lineStrong);
        for (int i = 0; i < 6; ++i) {
            const int y = 174 + i * 66;
            const bool selected = editorIndex == i;
            if (selected) fillRect(renderer, 822, y, 426, 64, palette.accentDim);
            drawText(renderer, 838, y + 10, labels[static_cast<std::size_t>(i)],
                     selected ? palette.accent : palette.muted, 1);
            drawTextRight(renderer, 1232, y + 27, modValue(mod, i), palette.text, 2);
            drawLine(renderer, 822, y + 64, 1248, y + 64, palette.line);
            if (selected) fillRect(renderer, 822, y + 5, 3, 52, palette.accent);
        }
        drawEditorFooter(renderer, palette, "MODULATOR LIVES WITH ORIGIN TRACK   TARGET CAN BE ANY TRACK");
    }

    void drawScale(SDL_Renderer* renderer, const AppState& app, const Palette& palette) const {
        drawSectionTitle(renderer, palette, "SCALE", "GLOBAL INPUT AND PLAYBACK QUANTIZATION", false);
        static constexpr std::array<const char*, 12> notes {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };
        drawText(renderer, 46, 173, "ROOT", editorIndex == 0 ? palette.accent : palette.muted, 1);
        drawText(renderer, 124, 166, notes[app.scaleRoot], palette.text, 3);
        drawText(renderer, 204, 173, "CLICK / RIGHT CLICK", palette.faint, 1);
        drawLine(renderer, 46, 217, 318, 217, editorIndex == 0 ? palette.accent : palette.lineStrong);

        for (int degree = 0; degree < 12; ++degree) {
            const int x = 46 + degree * 99;
            const bool enabled = (app.scaleMask & (1u << degree)) != 0u;
            const bool selected = editorIndex == degree + 1;
            if (enabled) fillRect(renderer, x + 3, 242, 91, 178, palette.accentDim);
            if (selected) strokeRect(renderer, x + 2, 242, 92, 178, palette.accent);
            drawTextCentered(renderer, x + 48, 273, notes[(degree + app.scaleRoot) % 12],
                             enabled ? palette.text : palette.muted, 2);
            drawTextCentered(renderer, x + 48, 344, enabled ? "ON" : "OFF",
                             enabled ? palette.accent : palette.faint, 2);
            drawLine(renderer, x + 3, 419, x + 94, 419, enabled ? palette.accent : palette.line);
        }
        static constexpr std::array<const char*, 6> presets {
            "CHROMATIC", "MAJOR", "MINOR", "DORIAN", "MIXOLYD", "WHOLE"
        };
        drawText(renderer, 46, 454, "PRESETS", palette.muted, 1);
        for (int i = 0; i < 6; ++i) {
            const int x = 46 + i * 198;
            const bool selected = editorIndex == 13 + i;
            if (selected) fillRect(renderer, x, 476, 190, 72, palette.accentDim);
            drawTextCentered(renderer, x + 95, 503, presets[static_cast<std::size_t>(i)],
                             selected ? palette.accent : palette.text, 1);
            drawLine(renderer, x, 547, x + 190, 547, selected ? palette.accent : palette.lineStrong);
        }
        drawEditorFooter(renderer, palette, "ENTER/CLICK TO TOGGLE NOTES   ROOT SHIFTS NOTE NAMES, MASK STAYS RELATIVE");
    }

    void drawData(SDL_Renderer* renderer, const AppState& app, const Palette& palette) const {
        drawSectionTitle(renderer, palette, "PATTERN DATA", "8 BANKS X 16 SLOTS PER TRACK");
        const auto& currentBank = app.banks[static_cast<std::size_t>(dataBank)];
        const std::string bankName(currentBank.name.data(), 4);
        drawText(renderer, 360, 128, "BANK " + decimalValue(dataBank + 1) + "  " + bankName,
                 palette.text, 2);
        drawText(renderer, 360, 151, "N RENAME   K LOCK", palette.faint, 1);
        for (int visual = 0; visual < 18; ++visual) {
            const int x = 72 + visual * 63;
            const int column = visual - 2;
            const std::string label = column == -2 ? "X" : (column == -1 ? "?" : hexValue(column, 1));
            drawTextCentered(renderer, x + 29, 169, label,
                             column < 0 ? palette.accent : palette.faint, 1);
        }
        const bool blink = std::fmod(elapsed, 0.5) < 0.25;
        for (int bank = 0; bank < 8; ++bank) {
            const int y = 188 + bank * 46;
            drawText(renderer, 34, y + 18, decimalValue(bank + 1), palette.muted, 1);
            if (app.banks[static_cast<std::size_t>(bank)].locked)
                drawText(renderer, 50, y + 18, "L", palette.accent, 1);
            for (int visual = 0; visual < 18; ++visual) {
                const int column = visual - 2;
                const int x = 72 + visual * 63;
                const bool selected = dataBank == bank && dataColumn == column;
                if (selected) fillRect(renderer, x, y, 58, 41, palette.raised);
                if (column < 0) {
                    drawTextCentered(renderer, x + 29, y + 14, column == -2 ? "X" : "?",
                                     selected ? palette.accent : palette.muted, 2);
                } else {
                    const int pattern = bank * 16 + column;
                    bool occupied = true;
                    const int first = dataAllTracks ? 0 : selectedTrack;
                    const int last = dataAllTracks ? kTrackCount - 1 : selectedTrack;
                    bool queued = false;
                    for (int track = first; track <= last; ++track) {
                        occupied = occupied && app.patterns[static_cast<std::size_t>(track)]
                                                           [static_cast<std::size_t>(pattern)].occupied;
                        queued = queued || queuedPattern[static_cast<std::size_t>(track)] == pattern;
                    }
                    if (occupied) fillRect(renderer, x + 24, y + 16, 10, 10,
                                           queued && blink ? palette.text : palette.accent);
                    else drawLine(renderer, x + 24, y + 21, x + 34, y + 21, palette.faint);
                    if (queued && blink) {
                        strokeRect(renderer, x + 4, y + 5, 50, 31, palette.accent);
                        drawText(renderer, x + 7, y + 8, "Q", palette.accent, 1);
                    }
                }
                if (selected) strokeRect(renderer, x, y, 58, 41, palette.accent);
                drawLine(renderer, x, y + 43, x + 58, y + 43, palette.line);
            }
        }
        drawText(renderer, 72, 576, "ENTER LOAD   SHIFT+ENTER SAVE   Q CUE", palette.text, 1);
        drawText(renderer, 72, 598,
                 std::string("A TARGET ") + (dataAllTracks ? "ALL 5 TRACKS" : "CURRENT TRACK") +
                 "   I MODE " + (dataLoadMode == DataLoadMode::Reset ? "RESET" : "IN PLACE"),
                 palette.accent, 1);
        drawText(renderer, 650, 576,
                 std::string("B BPM ") + (currentBank.hasTempo ? decimalValue(currentBank.tempo) : "--") +
                 (dataArmTempo ? " ARMED" : "") + "   G SCALE " +
                 (currentBank.hasScale ? (hexValue(currentBank.scaleMask, 3) + " R" +
                    decimalValue(currentBank.scaleRoot)) : "--") + (dataArmScale ? " ARMED" : ""),
                 palette.muted, 1);
        drawText(renderer, 650, 598, "SHIFT STORE   CTRL ARM WITH CUE   ALT TIMED RECALL", palette.faint, 1);
        drawTextRight(renderer, 1208, 620,
                      dataColumn < 0 ? (dataColumn == -2 ? "CLEAR TRACK" : "RANDOM TRACK")
                                     : "SLOT " + hexValue(dataPattern()),
                      palette.accent, 1);
        drawEditorFooter(renderer, palette,
                         "X / ? ARE SELECTABLE OPERATIONS   RESET IS TRACK-LOCAL   CUES BLINK UNTIL APPLIED");
    }

    void drawEditorFooter(SDL_Renderer* renderer, const Palette& palette,
                          std::string_view hint) const {
        fillRect(renderer, 0, 650, kLogicalWidth, 110, palette.surface);
        drawLine(renderer, 0, 650, kLogicalWidth, 650, palette.lineStrong);
        drawText(renderer, 28, 672, "1-5 TRACK   [ ] FIELD   - = VALUE   SHIFT COARSE",
                 palette.text, 1);
        drawTextRight(renderer, 1252, 672, "SPACE PLAY   S SNAPSHOT   CTRL+S SAVE", palette.text, 1);
        drawText(renderer, 28, 708, hint, palette.muted, 1);
        drawTextRight(renderer, 1252, 738, "F2 THEME   F3 ACCENT   F4 PAD MAP   F5 SCOPE", palette.faint, 1);
    }

    void drawPaletteOverlay(SDL_Renderer* renderer, const AppState& app,
                            const Palette& palette) const {
        fillRect(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {0, 0, 0, 190});
        fillRect(renderer, 132, 236, 1016, 264, palette.surface);
        strokeRect(renderer, 132, 236, 1016, 264, palette.lineStrong);
        fillRect(renderer, 132, 236, 1016, 4, palette.accent);
        drawText(renderer, 166, 270,
                 selectedTrack == kTrackCount - 1 ? "NOISE SOUND PALETTE" : "FM SOUND PALETTE",
                 palette.text, 3);
        drawTextRight(renderer, 1110, 278, "14 GLOBAL USER SLOTS  0-D", palette.muted, 1);
        const auto& sounds = selectedTrack == kTrackCount - 1 ? app.noisePalette : app.fmPalette;
        for (int index = 0; index < kPaletteSize + 2; ++index) {
            const int x = 166 + index * 59;
            const bool selected = paletteCursor == index;
            if (selected) fillRect(renderer, x, 326, 54, 64, palette.accentDim);
            strokeRect(renderer, x, 326, 54, 64, selected ? palette.accent : palette.lineStrong);
            const std::string label = index == 0 ? "X" : (index == 1 ? "?" : hexValue(index - 2, 1));
            drawTextCentered(renderer, x + 27, 339, label,
                             selected ? palette.accent : palette.text, 2);
            if (index >= 2) {
                const Step& sound = sounds[static_cast<std::size_t>(index - 2)];
                if (sound.active) fillRect(renderer, x + 23, 369, 8, 8, palette.accent);
                else drawLine(renderer, x + 22, 373, x + 32, 373, palette.faint);
            } else {
                drawTextCentered(renderer, x + 27, 370, index == 0 ? "CLR" : "RND", palette.faint, 1);
            }
        }
        drawText(renderer, 166, 420, "ENTER RECALL   SHIFT+ENTER STORE   CTRL+ENTER APPLY WHOLE TRACK",
                 palette.text, 1);
        drawText(renderer, 166, 446, "DELETE CLEAR SLOT   R RANDOM SOUND   RIGHT CLICK APPLY TRACK   P/ESC CLOSE",
                 palette.muted, 1);
        drawText(renderer, 166, 472, "SOUND ONLY: SEQUENCE NOTE/TRIG/LEVEL/PAN/TIMING STAY UNCHANGED",
                 palette.accent, 1);
    }

    void drawControllerOverlay(SDL_Renderer* renderer, const AppState& app,
                               const Palette& palette) const {
        fillRect(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {0, 0, 0, 200});
        fillRect(renderer, 132, 92, 1016, 568, palette.surface);
        strokeRect(renderer, 132, 92, 1016, 568, palette.lineStrong);
        fillRect(renderer, 132, 92, 1016, 4, palette.accent);
        drawText(renderer, 168, 122, "CONTROLLER MAPPING", palette.text, 3);
        const char* rawName = controller ? SDL_GameControllerName(controller) : nullptr;
        const std::string status = controller
            ? std::string("CONNECTED  ") + (rawName ? asciiOnly(rawName) : "GAMEPAD")
            : "NO CONTROLLER - HOTPLUG READY";
        drawText(renderer, 168, 157, status, controller ? palette.accent : palette.muted, 1);
        drawTextRight(renderer, 1112, 157, app.controller.enabled ? "INPUT ENABLED" : "INPUT DISABLED",
                      app.controller.enabled ? palette.text : palette.accent, 1);
        drawLine(renderer, 640, 184, 640, 526, palette.lineStrong);
        for (int index = 0; index < static_cast<int>(kControllerActionCount); ++index) {
            const int column = index / 9;
            const int row = index % 9;
            const int x = 168 + column * 490;
            const int y = 190 + row * 36;
            const bool selected = controllerMapCursor == index;
            if (selected) fillRect(renderer, x, y, 452, 32, palette.accentDim);
            if (selected) fillRect(renderer, x, y, 3, 32, palette.accent);
            drawText(renderer, x + 12, y + 10, controllerActionName(index),
                     selected ? palette.accent : palette.muted, 1);
            drawTextRight(renderer, x + 438, y + 10,
                          controllerButtonName(app.controller.buttons[static_cast<std::size_t>(index)]),
                          selected ? palette.text : palette.faint, 1);
            drawLine(renderer, x, y + 33, x + 452, y + 33, palette.line);
        }
        drawText(renderer, 168, 548,
                 controllerCapture ? "CAPTURE ACTIVE - PRESS THE NEXT CONTROLLER BUTTON"
                                   : "ENTER/CLICK CAPTURE   DELETE/RIGHT CLICK UNBIND",
                 controllerCapture ? palette.accent : palette.text, 1);
        drawText(renderer, 168, 578, "D DEFAULTS   E ENABLE/DISABLE   F4/ESC CLOSE", palette.muted, 1);
        drawText(renderer, 168, 608,
                 "HOLD ALT OR COARSE MODIFIER WITH DPAD FOR VALUE EDIT; COARSE CHANGES BY 8",
                 palette.faint, 1);
    }

    void drawBankNameOverlay(SDL_Renderer* renderer, const Palette& palette) const {
        fillRect(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {0, 0, 0, 200});
        fillRect(renderer, 410, 248, 460, 254, palette.surface);
        strokeRect(renderer, 410, 248, 460, 254, palette.lineStrong);
        fillRect(renderer, 410, 248, 460, 4, palette.accent);
        drawTextCentered(renderer, 640, 282, "EDIT BANK NAME", palette.text, 3);
        for (int index = 0; index < 4; ++index) {
            const int x = 494 + index * 76;
            const bool selected = bankNameCursor == index;
            if (selected) fillRect(renderer, x, 338, 62, 62, palette.accentDim);
            strokeRect(renderer, x, 338, 62, 62, selected ? palette.accent : palette.lineStrong);
            const std::string value(1, bankNameEdit[static_cast<std::size_t>(index)]);
            drawTextCentered(renderer, x + 31, 354, value, selected ? palette.accent : palette.text, 4);
        }
        drawTextCentered(renderer, 640, 424, "TYPE 4 CHARACTERS   ARROWS MOVE   ENTER SAVE", palette.muted, 1);
        drawTextCentered(renderer, 640, 454, "ESC CANCEL   BANK LOCK BLOCKS THE SAVE", palette.faint, 1);
    }

    void drawToast(SDL_Renderer* renderer, const Palette& palette) const {
        if (toastSeconds <= 0.0 || toastMessage.empty()) return;
        const double fade = std::min(1.0, toastSeconds * 2.0);
        const std::uint8_t alpha = static_cast<std::uint8_t>(std::lround(235.0 * fade));
        const int width = std::min(760, textWidth(toastMessage, 2) + 54);
        const int x = (kLogicalWidth - width) / 2;
        const Color background {palette.raised.r, palette.raised.g, palette.raised.b, alpha};
        const Color border = toastIsError ? palette.text : palette.accent;
        fillRect(renderer, x, 574, width, 44, background);
        fillRect(renderer, x, 574, 4, 44, border);
        drawTextCentered(renderer, kLogicalWidth / 2, 589,
                         (toastIsError ? "! " : "") + toastMessage, palette.text, 2);
    }

    void drawHelp(SDL_Renderer* renderer, const Palette& palette) const {
        fillRect(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {0, 0, 0, 205});
        fillRect(renderer, 128, 78, 1024, 604, palette.surface);
        strokeRect(renderer, 128, 78, 1024, 604, palette.lineStrong);
        fillRect(renderer, 128, 78, 1024, 4, palette.accent);
        drawText(renderer, 164, 112, "FMS CONTROL MAP", palette.text, 3);
        drawTextRight(renderer, 1116, 119, "F1 / ? / CLICK TO CLOSE", palette.muted, 1);
        drawLine(renderer, 164, 154, 1116, 154, palette.lineStrong);

        static constexpr std::array<std::array<const char*, 2>, 14> controls {{
            {{"SPACE", "START / STOP TRANSPORT"}},
            {{"ARROWS", "MOVE CURSOR / SELECT FIELD"}},
            {{"SHIFT+ARROWS", "EXTEND RECTANGULAR RANGE"}},
            {{"ENTER", "TOGGLE STEP / ACTIVATE / LOAD"}},
            {{"[  ]", "SELECT PARAMETER"}},
            {{"-  =", "ADJUST VALUE  /  SHIFT COARSE"}},
            {{"1 - 5", "SELECT TRACK"}},
            {{"TAB", "NEXT VIEW  /  SHIFT PREVIOUS"}},
            {{"F5", "EDIT SCOPE STEP/RANGE/TRACK/ALL"}},
            {{"C / X / V", "COPY / CUT / PASTE RANGE"}},
            {{"R / SHIFT+R", "NUDGE PARAM / RANDOMIZE TRACK"}},
            {{"P", "SOUND PALETTE 14 FM + 14 NOISE"}},
            {{"O / CTRL+O", "ROTATE SELECTED / ALL TRACKS"}},
            {{"S", "CAPTURE / SWAP SNAPSHOT"}},
        }};
        static constexpr std::array<std::array<const char*, 2>, 10> controlsRight {{
            {{",  .", "BPM DOWN / UP"}},
            {{"D / SHIFT+D", "DIRECTION TRACK / ALL"}},
            {{"ALT UP/DOWN", "SHUFFLE; CTRL APPLIES ALL"}},
            {{"M / SHIFT+M / U", "MUTE / SOLO / UNMUTE ALL"}},
            {{"DATA A / I / Q", "ALL-COLUMN / LOAD MODE / CUE"}},
            {{"DATA X / ?", "CLEAR / RANDOM WORKING TRACKS"}},
            {{"DATA N / K", "RENAME / LOCK BANK"}},
            {{"DATA B / G", "BPM / SCALE; SHIFT STORE"}},
            {{"F4", "CONTROLLER MAP / HOTPLUG STATUS"}},
            {{"F2 / F3", "THEME / PHOSPHOR ACCENT"}},
        }};
        for (int i = 0; i < static_cast<int>(controls.size()); ++i) {
            const int y = 180 + i * 31;
            drawText(renderer, 164, y, controls[static_cast<std::size_t>(i)][0], palette.accent, 1);
            drawText(renderer, 306, y, controls[static_cast<std::size_t>(i)][1], palette.text, 1);
        }
        drawLine(renderer, 628, 176, 628, 625, palette.line);
        for (int i = 0; i < static_cast<int>(controlsRight.size()); ++i) {
            const int y = 180 + i * 39;
            drawText(renderer, 660, y, controlsRight[static_cast<std::size_t>(i)][0], palette.accent, 1);
            drawText(renderer, 660, y + 17, controlsRight[static_cast<std::size_t>(i)][1], palette.text, 1);
        }
        drawText(renderer, 660, 584, "GRID MOUSE", palette.muted, 1);
        drawText(renderer, 660, 605, "LEFT TOGGLE  RIGHT TRIGLESS", palette.text, 1);
        drawText(renderer, 164, 647, "ALL EDITS ARE LIVE AND AUDIO-PREVIEWED WHERE APPLICABLE", palette.muted, 1);
    }

    void render(SDL_Renderer* renderer, int width, int height) {
        outputWidth = std::max(1, width);
        outputHeight = std::max(1, height);
        viewportScale = std::min(static_cast<float>(outputWidth) / static_cast<float>(kLogicalWidth),
                                 static_cast<float>(outputHeight) / static_cast<float>(kLogicalHeight));
        const int scaledWidth = static_cast<int>(std::lround(static_cast<double>(kLogicalWidth) * viewportScale));
        const int scaledHeight = static_cast<int>(std::lround(static_cast<double>(kLogicalHeight) * viewportScale));
        viewportX = (outputWidth - scaledWidth) / 2;
        viewportY = (outputHeight - scaledHeight) / 2;

        SDL_RenderSetViewport(renderer, nullptr);
        SDL_RenderSetScale(renderer, 1.0f, 1.0f);
        const AppState app = stateCopy();
        const Palette palette = makePalette(app.lightTheme, app.accent);
        fillRect(renderer, 0, 0, outputWidth, outputHeight, palette.outside);

        const SDL_Rect viewport {viewportX, viewportY, scaledWidth, scaledHeight};
        SDL_RenderSetViewport(renderer, &viewport);
        SDL_RenderSetScale(renderer, viewportScale, viewportScale);
        fillRect(renderer, 0, 0, kLogicalWidth, kLogicalHeight, palette.background);
        drawHeader(renderer, app, palette);
        switch (view) {
            case View::Grid: drawGrid(renderer, app, palette); break;
            case View::Synth: drawSynth(renderer, app, palette); break;
            case View::Echo: drawEcho(renderer, app, palette); break;
            case View::Transpose: drawTranspose(renderer, app, palette); break;
            case View::Mod: drawMod(renderer, app, palette); break;
            case View::Scale: drawScale(renderer, app, palette); break;
            case View::Data: drawData(renderer, app, palette); break;
        }
        if (overlay == Overlay::Palette) drawPaletteOverlay(renderer, app, palette);
        else if (overlay == Overlay::ControllerMap) drawControllerOverlay(renderer, app, palette);
        else if (overlay == Overlay::BankName) drawBankNameOverlay(renderer, palette);
        drawToast(renderer, palette);
        if (shutterSeconds > 0.0) {
            const std::uint8_t alpha = static_cast<std::uint8_t>(
                std::lround(150.0 * std::min(1.0, shutterSeconds / 0.10)));
            fillRect(renderer, 0, 0, kLogicalWidth, kLogicalHeight,
                     {palette.text.r, palette.text.g, palette.text.b, alpha});
        }
        if (helpVisible) drawHelp(renderer, palette);

        SDL_RenderSetScale(renderer, 1.0f, 1.0f);
        SDL_RenderSetViewport(renderer, nullptr);
    }
};

UiController::UiController(SharedState& state, AudioEngine& audio)
    : impl_(new Impl(state, audio)) {}

UiController::~UiController() {
    delete impl_;
}

bool UiController::handleEvent(const SDL_Event& event) {
    return impl_->handleEvent(event);
}

void UiController::update(double deltaSeconds) {
    impl_->update(deltaSeconds);
}

void UiController::render(SDL_Renderer* renderer, int width, int height) {
    impl_->render(renderer, width, height);
}

bool UiController::consumeSaveRequest() {
    const bool requested = impl_->saveRequested;
    impl_->saveRequested = false;
    return requested;
}

void UiController::showToast(const std::string& message, bool error) {
    impl_->toast(message, error);
}

} // namespace fms
