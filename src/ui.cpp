#include "ui.hpp"

#include "persistence.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace fms
{
    namespace
    {

        constexpr int kLogicalWidth = 1280;
        constexpr int kLogicalHeight = 760;
        constexpr int kMargin = 28;

        struct Color
        {
            std::uint8_t r;
            std::uint8_t g;
            std::uint8_t b;
            std::uint8_t a = 255;
        };

        struct Palette
        {
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

        constexpr std::array<Color, 6> kDarkAccents{{
            {142, 244, 139, 255},
            {100, 226, 220, 255},
            {246, 201, 102, 255},
            {192, 154, 247, 255},
            {242, 139, 119, 255},
            {119, 174, 247, 255},
        }};

        constexpr std::array<Color, 6> kLightAccents{{
            {27, 129, 65, 255},
            {0, 125, 129, 255},
            {153, 99, 0, 255},
            {112, 70, 169, 255},
            {174, 66, 50, 255},
            {42, 95, 169, 255},
        }};

        Palette makePalette(bool light, std::uint8_t accentIndex)
        {
            const std::size_t index = static_cast<std::size_t>(accentIndex % 6u);
            if (light)
            {
                const Color accent = kLightAccents[index];
                return {{221, 222, 213, 255}, {239, 239, 231, 255}, {234, 235, 226, 255}, {226, 228, 218, 255}, {190, 194, 183, 255}, {146, 153, 143, 255}, {25, 31, 29, 255}, {86, 96, 90, 255}, {132, 140, 133, 255}, accent, {accent.r, accent.g, accent.b, 36}};
            }
            const Color accent = kDarkAccents[index];
            return {{2, 5, 8, 255}, {7, 12, 17, 255}, {10, 17, 23, 255}, {14, 23, 30, 255}, {34, 47, 53, 255}, {55, 70, 75, 255}, {232, 233, 220, 255}, {119, 132, 127, 255}, {67, 80, 80, 255}, accent, {accent.r, accent.g, accent.b, 34}};
        }

        void setColor(SDL_Renderer *renderer, Color color)
        {
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        }

        void fillRect(SDL_Renderer *renderer, int x, int y, int width, int height, Color color)
        {
            if (width <= 0 || height <= 0)
                return;
            const SDL_Rect rect{x, y, width, height};
            setColor(renderer, color);
            SDL_RenderFillRect(renderer, &rect);
        }

        void strokeRect(SDL_Renderer *renderer, int x, int y, int width, int height, Color color)
        {
            if (width <= 0 || height <= 0)
                return;
            const SDL_Rect rect{x, y, width, height};
            setColor(renderer, color);
            SDL_RenderDrawRect(renderer, &rect);
        }

        void drawLine(SDL_Renderer *renderer, int x1, int y1, int x2, int y2, Color color)
        {
            setColor(renderer, color);
            SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
        }

        std::array<std::uint8_t, 5> glyph(char input)
        {
            char c = input;
            if (c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 'a' + 'A');
            switch (c)
            {
            case 'A':
                return {0x7e, 0x11, 0x11, 0x11, 0x7e};
            case 'B':
                return {0x7f, 0x49, 0x49, 0x49, 0x36};
            case 'C':
                return {0x3e, 0x41, 0x41, 0x41, 0x22};
            case 'D':
                return {0x7f, 0x41, 0x41, 0x22, 0x1c};
            case 'E':
                return {0x7f, 0x49, 0x49, 0x49, 0x41};
            case 'F':
                return {0x7f, 0x09, 0x09, 0x09, 0x01};
            case 'G':
                return {0x3e, 0x41, 0x49, 0x49, 0x7a};
            case 'H':
                return {0x7f, 0x08, 0x08, 0x08, 0x7f};
            case 'I':
                return {0x41, 0x41, 0x7f, 0x41, 0x41};
            case 'J':
                return {0x20, 0x40, 0x41, 0x3f, 0x01};
            case 'K':
                return {0x7f, 0x08, 0x14, 0x22, 0x41};
            case 'L':
                return {0x7f, 0x40, 0x40, 0x40, 0x40};
            case 'M':
                return {0x7f, 0x02, 0x0c, 0x02, 0x7f};
            case 'N':
                return {0x7f, 0x04, 0x08, 0x10, 0x7f};
            case 'O':
                return {0x3e, 0x41, 0x41, 0x41, 0x3e};
            case 'P':
                return {0x7f, 0x09, 0x09, 0x09, 0x06};
            case 'Q':
                return {0x3e, 0x41, 0x51, 0x21, 0x5e};
            case 'R':
                return {0x7f, 0x09, 0x19, 0x29, 0x46};
            case 'S':
                return {0x46, 0x49, 0x49, 0x49, 0x31};
            case 'T':
                return {0x01, 0x01, 0x7f, 0x01, 0x01};
            case 'U':
                return {0x3f, 0x40, 0x40, 0x40, 0x3f};
            case 'V':
                return {0x1f, 0x20, 0x40, 0x20, 0x1f};
            case 'W':
                return {0x7f, 0x20, 0x18, 0x20, 0x7f};
            case 'X':
                return {0x63, 0x14, 0x08, 0x14, 0x63};
            case 'Y':
                return {0x03, 0x04, 0x78, 0x04, 0x03};
            case 'Z':
                return {0x61, 0x51, 0x49, 0x45, 0x43};
            case '0':
                return {0x3e, 0x51, 0x49, 0x45, 0x3e};
            case '1':
                return {0x00, 0x42, 0x7f, 0x40, 0x00};
            case '2':
                return {0x62, 0x51, 0x49, 0x49, 0x46};
            case '3':
                return {0x22, 0x41, 0x49, 0x49, 0x36};
            case '4':
                return {0x18, 0x14, 0x12, 0x7f, 0x10};
            case '5':
                return {0x2f, 0x49, 0x49, 0x49, 0x31};
            case '6':
                return {0x3e, 0x49, 0x49, 0x49, 0x32};
            case '7':
                return {0x01, 0x71, 0x09, 0x05, 0x03};
            case '8':
                return {0x36, 0x49, 0x49, 0x49, 0x36};
            case '9':
                return {0x26, 0x49, 0x49, 0x49, 0x3e};
            case '#':
                return {0x14, 0x7f, 0x14, 0x7f, 0x14};
            case '-':
                return {0x08, 0x08, 0x08, 0x08, 0x08};
            case '+':
                return {0x08, 0x08, 0x3e, 0x08, 0x08};
            case '=':
                return {0x14, 0x14, 0x14, 0x14, 0x14};
            case '/':
                return {0x60, 0x10, 0x08, 0x04, 0x03};
            case '\\':
                return {0x03, 0x04, 0x08, 0x10, 0x60};
            case '.':
                return {0x00, 0x60, 0x60, 0x00, 0x00};
            case ',':
                return {0x00, 0x40, 0x30, 0x00, 0x00};
            case ':':
                return {0x00, 0x36, 0x36, 0x00, 0x00};
            case ';':
                return {0x00, 0x40, 0x36, 0x00, 0x00};
            case '!':
                return {0x00, 0x00, 0x5f, 0x00, 0x00};
            case '?':
                return {0x02, 0x01, 0x51, 0x09, 0x06};
            case '[':
                return {0x00, 0x7f, 0x41, 0x41, 0x00};
            case ']':
                return {0x00, 0x41, 0x41, 0x7f, 0x00};
            case '(':
                return {0x00, 0x1c, 0x22, 0x41, 0x00};
            case ')':
                return {0x00, 0x41, 0x22, 0x1c, 0x00};
            case '<':
                return {0x08, 0x14, 0x22, 0x41, 0x00};
            case '>':
                return {0x00, 0x41, 0x22, 0x14, 0x08};
            case '%':
                return {0x63, 0x13, 0x08, 0x64, 0x63};
            case '_':
                return {0x40, 0x40, 0x40, 0x40, 0x40};
            case '*':
                return {0x14, 0x08, 0x3e, 0x08, 0x14};
            case '|':
                return {0x00, 0x00, 0x7f, 0x00, 0x00};
            case '"':
                return {0x00, 0x07, 0x00, 0x07, 0x00};
            case '\'':
                return {0x00, 0x00, 0x07, 0x00, 0x00};
            default:
                return {0, 0, 0, 0, 0};
            }
        }

        int textWidth(std::string_view text, int scale)
        {
            if (text.empty())
                return 0;
            return static_cast<int>(text.size()) * 6 * scale - scale;
        }

        void drawText(SDL_Renderer *renderer, int x, int y, std::string_view text, Color color,
                      int scale = 2)
        {
            setColor(renderer, color);
            int cursor = x;
            for (char character : text)
            {
                const auto bits = glyph(character);
                for (int column = 0; column < 5; ++column)
                {
                    for (int row = 0; row < 7; ++row)
                    {
                        if ((bits[static_cast<std::size_t>(column)] & (1u << row)) == 0u)
                            continue;
                        const SDL_Rect pixel{cursor + column * scale, y + row * scale, scale, scale};
                        SDL_RenderFillRect(renderer, &pixel);
                    }
                }
                cursor += 6 * scale;
            }
        }

        void drawTextRight(SDL_Renderer *renderer, int right, int y, std::string_view text, Color color,
                           int scale = 2)
        {
            drawText(renderer, right - textWidth(text, scale), y, text, color, scale);
        }

        void drawTextCentered(SDL_Renderer *renderer, int centerX, int y, std::string_view text,
                              Color color, int scale = 2)
        {
            drawText(renderer, centerX - textWidth(text, scale) / 2, y, text, color, scale);
        }

        std::string hexValue(int value, int digits = 2)
        {
            char buffer[16];
            if (digits == 1)
                std::snprintf(buffer, sizeof(buffer), "%X", value & 0xF);
            else if (digits == 3)
                std::snprintf(buffer, sizeof(buffer), "%03X", value & 0xFFF);
            else
                std::snprintf(buffer, sizeof(buffer), "%02X", value & 0xFF);
            return buffer;
        }

        std::string signedValue(int value)
        {
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%+d", value);
            return buffer;
        }

        std::string decimalValue(int value)
        {
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%d", value);
            return buffer;
        }

        std::string paddedDecimal(int value)
        {
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%02d", value);
            return buffer;
        }

        std::string asciiOnly(std::string_view input)
        {
            std::string output;
            bool replaced = false;
            for (const unsigned char c : input)
            {
                if (c >= 32u && c <= 126u)
                {
                    output.push_back(static_cast<char>(c));
                    replaced = false;
                }
                else if ((c & 0xC0u) != 0x80u && !replaced)
                {
                    output.push_back('-');
                    replaced = true;
                }
            }
            return output;
        }

        int wrapIndex(int value, int count)
        {
            if (count <= 0)
                return 0;
            value %= count;
            return value < 0 ? value + count : value;
        }

        int clampInt(int value, int minimum, int maximum)
        {
            return std::max(minimum, std::min(maximum, value));
        }

        enum class View : int
        {
            Grid,
            Synth,
            Echo,
            Transpose,
            Mod,
            Scale,
            Data
        };

        constexpr std::array<const char *, 7> kViewNames{
            "GRID", "SYNTH", "ECHO", "TRANSPOSE", "MOD", "SCALE", "DATA"};

        enum class Overlay : std::uint8_t
        {
            None,
            Palette,
            ControllerMap,
            BankName,
            PatternName,
            ProjectMenu,
            ProjectName,
            ProjectBrowser,
            CommandPalette
        };
        enum class EditScope : std::uint8_t
        {
            Selection,
            Track,
            All
        };
        enum class DataLoadMode : std::uint8_t
        {
            InPlace,
            Reset
        };
        enum class DataWorkspace : std::uint8_t
        {
            Perform,
            Manage
        };

        enum class GridParam
        {
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

        struct ParamItem
        {
            GridParam id;
            const char *shortName;
            const char *fullName;
        };

        constexpr std::array<ParamItem, 24> kFmParams{{
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

        constexpr std::array<ParamItem, 12> kNoiseParams{{
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

        const ParamItem &gridParamItem(int track, int index)
        {
            if (track == kTrackCount - 1)
            {
                return kNoiseParams[static_cast<std::size_t>(clampInt(index, 0,
                                                                      static_cast<int>(kNoiseParams.size()) - 1))];
            }
            return kFmParams[static_cast<std::size_t>(clampInt(index, 0,
                                                               static_cast<int>(kFmParams.size()) - 1))];
        }

        int gridParamCount(int track)
        {
            return track == kTrackCount - 1 ? static_cast<int>(kNoiseParams.size())
                                            : static_cast<int>(kFmParams.size());
        }

        int gridStepParamCount(int track)
        {
            return track == kTrackCount - 1 ? 7 : 13;
        }

        int gridParamIndex(int track, GridParam parameter)
        {
            const int count = gridParamCount(track);
            for (int index = 0; index < count; ++index)
                if (gridParamItem(track, index).id == parameter)
                    return index;
            return -1;
        }

        const char *panName(Pan pan)
        {
            switch (pan)
            {
            case Pan::Left:
                return "L";
            case Pan::Center:
                return "C";
            case Pan::Right:
                return "R";
            }
            return "C";
        }

        const char *echoPanName(EchoPan pan)
        {
            switch (pan)
            {
            case EchoPan::Original:
                return "ORIG";
            case EchoPan::Left:
                return "LEFT";
            case EchoPan::Right:
                return "RIGHT";
            case EchoPan::PingPong:
                return "PINGPONG";
            }
            return "ORIG";
        }

        const char *advanceName(TransposeAdvance advance)
        {
            switch (advance)
            {
            case TransposeAdvance::Pattern:
                return "PATTERN";
            case TransposeAdvance::Step:
                return "STEP";
            case TransposeAdvance::Trigger:
                return "TRIGGER";
            }
            return "PATTERN";
        }

        const char *waveName(ModWave wave)
        {
            switch (wave)
            {
            case ModWave::RampDown:
                return "RAMP DN";
            case ModWave::RampUp:
                return "RAMP UP";
            case ModWave::Triangle:
                return "TRIANGLE";
            case ModWave::Square:
                return "SQUARE";
            case ModWave::Random:
                return "RANDOM";
            }
            return "TRIANGLE";
        }

        const char *destinationName(ModDest destination)
        {
            switch (destination)
            {
            case ModDest::Level:
                return "LEVEL";
            case ModDest::Pan:
                return "PAN";
            case ModDest::Note:
                return "NOTE";
            case ModDest::ModDepth:
                return "MOD DEPTH";
            case ModDest::ModFeedback:
                return "MOD FBK";
            case ModDest::Sweep:
                return "SWEEP";
            case ModDest::NoiseRate:
                return "NOISE RATE";
            }
            return "LEVEL";
        }

        std::string gridValue(const Step &step, GridParam parameter, bool noiseTrack = false)
        {
            switch (parameter)
            {
            case GridParam::Note:
                return noteName(step.note);
            case GridParam::Level:
                return hexValue(step.level);
            case GridParam::Pan:
                return panName(step.pan);
            case GridParam::Portamento:
                return hexValue(step.portamento);
            case GridParam::Condition:
                return "1:" + decimalValue(step.condition);
            case GridParam::Microtime:
                return signedValue(step.microTicks);
            case GridParam::Chord1:
                return signedValue(step.chord[0]);
            case GridParam::Chord2:
                return signedValue(step.chord[1]);
            case GridParam::Chord3:
                return signedValue(step.chord[2]);
            case GridParam::Echo:
                return step.echo ? "ON" : "OFF";
            case GridParam::Transpose:
                return step.transpose ? "ON" : "OFF";
            case GridParam::Mode:
                return step.mode == SynthMode::FM ? "FM" : "PAR";
            case GridParam::Trigless:
                return step.trigless ? "YES" : "NO";
            case GridParam::AmpAttack:
                return hexValue(noiseTrack ? step.noise.ampAttack : step.fm.ampAttack);
            case GridParam::AmpHold:
                return hexValue(noiseTrack ? step.noise.ampHold : step.fm.ampHold);
            case GridParam::AmpRelease:
                return hexValue(noiseTrack ? step.noise.ampRelease : step.fm.ampRelease);
            case GridParam::ModRatio:
            {
                char buffer[16];
                std::snprintf(buffer, sizeof(buffer), "%.3g", fmRatio(step.fm.modRatio));
                return buffer;
            }
            case GridParam::ModDepth:
                return hexValue(step.fm.modDepth);
            case GridParam::ModFeedback:
                return hexValue(step.fm.modFeedback);
            case GridParam::ModAttack:
                return hexValue(step.fm.modAttack);
            case GridParam::ModRelease:
                return hexValue(step.fm.modRelease);
            case GridParam::ModEnd:
                return hexValue(step.fm.modEnd);
            case GridParam::SweepDepth:
                return signedValue(step.fm.sweepDepth);
            case GridParam::SweepRelease:
                return hexValue(step.fm.sweepRelease);
            case GridParam::NoiseRate:
                return hexValue(step.noise.rate);
            case GridParam::NoiseWidth:
                return step.noise.narrow ? "NAR" : "WIDE";
            }
            return "--";
        }

        float gridValueUnit(const Step &step, GridParam parameter, bool noiseTrack = false)
        {
            switch (parameter)
            {
            case GridParam::Note:
                return static_cast<float>(step.note - 12u) / 107.0f;
            case GridParam::Level:
                return static_cast<float>(step.level) / 127.0f;
            case GridParam::Pan:
                return static_cast<float>(static_cast<int>(step.pan)) / 2.0f;
            case GridParam::Portamento:
                return static_cast<float>(step.portamento) / 127.0f;
            case GridParam::Condition:
                return static_cast<float>(step.condition - 1u) / 7.0f;
            case GridParam::Microtime:
                return static_cast<float>(step.microTicks + 6) / 12.0f;
            case GridParam::Chord1:
                return static_cast<float>(step.chord[0]) / 24.0f;
            case GridParam::Chord2:
                return static_cast<float>(step.chord[1]) / 24.0f;
            case GridParam::Chord3:
                return static_cast<float>(step.chord[2]) / 24.0f;
            case GridParam::Echo:
                return step.echo ? 1.0f : 0.0f;
            case GridParam::Transpose:
                return step.transpose ? 1.0f : 0.0f;
            case GridParam::Mode:
                return step.mode == SynthMode::Parallel ? 1.0f : 0.0f;
            case GridParam::Trigless:
                return step.trigless ? 1.0f : 0.0f;
            case GridParam::AmpAttack:
                return static_cast<float>(noiseTrack ? step.noise.ampAttack : step.fm.ampAttack) / 127.0f;
            case GridParam::AmpHold:
                return static_cast<float>(noiseTrack ? step.noise.ampHold : step.fm.ampHold) / 127.0f;
            case GridParam::AmpRelease:
                return static_cast<float>(noiseTrack ? step.noise.ampRelease : step.fm.ampRelease) / 127.0f;
            case GridParam::ModRatio:
                return static_cast<float>(step.fm.modRatio) / 127.0f;
            case GridParam::ModDepth:
                return static_cast<float>(step.fm.modDepth) / 127.0f;
            case GridParam::ModFeedback:
                return static_cast<float>(step.fm.modFeedback) / 127.0f;
            case GridParam::ModAttack:
                return static_cast<float>(step.fm.modAttack) / 127.0f;
            case GridParam::ModRelease:
                return static_cast<float>(step.fm.modRelease) / 127.0f;
            case GridParam::ModEnd:
                return static_cast<float>(step.fm.modEnd) / 127.0f;
            case GridParam::SweepDepth:
                return static_cast<float>(step.fm.sweepDepth + 64) / 127.0f;
            case GridParam::SweepRelease:
                return static_cast<float>(step.fm.sweepRelease) / 127.0f;
            case GridParam::NoiseRate:
                return static_cast<float>(step.noise.rate) / 127.0f;
            case GridParam::NoiseWidth:
                return step.noise.narrow ? 1.0f : 0.0f;
            }
            return 0.0f;
        }

        void drawMiniBar(SDL_Renderer *renderer, int x, int y, int width, float unit,
                         const Palette &palette, bool active)
        {
            unit = std::max(0.0f, std::min(1.0f, unit));
            drawLine(renderer, x, y, x + width, y, palette.lineStrong);
            const int filled = static_cast<int>(std::lround(static_cast<double>(width) * unit));
            drawLine(renderer, x, y, x + filled, y, active ? palette.accent : palette.muted);
        }

    } // namespace

    const AdvancedFmAlgorithmTopology &
    advancedFmAlgorithmTopology(AdvancedFmAlgorithm algorithm)
    {
        static constexpr std::array<AdvancedFmAlgorithmTopology, 12> topologies{{
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
        const int index = clampInt(static_cast<int>(algorithm), 0,
                                   static_cast<int>(topologies.size()) - 1);
        return topologies[static_cast<std::size_t>(index)];
    }

    struct UiController::Impl
    {
        struct RangeClipboard
        {
            int tracks = 0;
            int steps = 0;
            std::array<Step, kTrackCount * kStepCount> data{};

            bool empty() const { return tracks <= 0 || steps <= 0; }
        };

        struct HistoryEntry
        {
            std::shared_ptr<AppState> state;
            std::uint64_t generation = 0;
            std::uint64_t actionSequence = 0;
            std::uint8_t trackMask = 0;
            bool tempo = false;
            bool scale = false;
            bool scoped = false;
        };

        enum class AsyncHistoryKind : std::uint8_t
        {
            Track,
            Column,
            Global
        };

        struct AsyncHistoryTransaction
        {
            std::shared_ptr<AppState> before;
            AsyncHistoryKind kind = AsyncHistoryKind::Column;
            std::uint64_t token = 0;
            std::uint8_t trackMask = 0;
            bool tempo = false;
            bool scale = false;
            std::uint64_t actionSequence = 0;
            bool cancelRequested = false;
        };

        enum class AsyncHistoryOutcome : std::uint8_t
        {
            Pending,
            Applied,
            Cancelled
        };

        struct AsyncHistorySettlement
        {
            AsyncHistoryOutcome outcome = AsyncHistoryOutcome::Pending;
            std::uint64_t sequence = 0;
            std::uint8_t trackMask = 0;
            bool tempo = false;
            bool scale = false;
        };

        struct UiMutationGuard
        {
            SharedState &shared;
            bool active = false;

            UiMutationGuard(SharedState &state, bool shouldActivate)
                : shared(state), active(shouldActivate)
            {
                if (!active)
                    return;
                shared.uiMutationInProgress.store(true, std::memory_order_release);
                // Any callback that already passed its gate owns this mutex.
                // Crossing it here guarantees its state and settlement are
                // visible before the UI snapshots the next mutation.
                std::lock_guard<std::mutex> fence(shared.mutex);
            }

            ~UiMutationGuard()
            {
                if (active)
                    shared.uiMutationInProgress.store(false, std::memory_order_release);
            }

            UiMutationGuard(const UiMutationGuard &) = delete;
            UiMutationGuard &operator=(const UiMutationGuard &) = delete;
        };

        SharedState &shared;
        AudioEngine &audio;
        View view = View::Grid;
        Overlay overlay = Overlay::None;
        EditScope editScope = EditScope::Selection;
        DataLoadMode dataLoadMode = DataLoadMode::Reset;
        DataWorkspace dataWorkspace = DataWorkspace::Perform;
        int selectedTrack = 0;
        int selectedStep = 0;
        int selectedParameter = 0;
        int editorIndex = 0;
        int synthMacroCursor = 0;
        int dataBank = 0;
        int dataColumn = 0;
        int paletteCursor = 2;
        bool lastPaletteNoise = false;
        int controllerMapCursor = 0;
        int projectActionCursor = 0;
        int projectBrowserCursor = 0;
        int commandCursor = 0;
        bool controllerCapture = false;
        bool controllerCoarseHeld = false;
        bool controllerAlternateHeld = false;
        SDL_GameController *controller = nullptr;
        SDL_JoystickID controllerInstance = -1;
        int rangeAnchorTrack = 0;
        int rangeAnchorStep = 0;
        bool rangeActive = false;
        bool gridCompareValues = false;
        bool synthPerformanceMode = true;
        bool destructiveEditArmed = false;
        int destructiveEditTargetCount = 0;
        std::string destructiveEditAction;
        double destructiveEditDeadline = 0.0;
        bool dataAllTracks = false;
        bool dataArmTempo = false;
        bool dataArmScale = false;
        int bankNameCursor = 0;
        std::array<char, 5> bankNameEdit{'B', 'A', 'N', 'K', '\0'};
        bool helpVisible = false;
        bool hintPanelVisible = false;
        bool saveRequested = false;
        bool projectActionArmed = false;
        bool projectBrowserArmed = false;
        bool historyRestoring = false;
        bool historyRecordedThisEvent = false;
        bool snapshotSide = false;
        bool snapshotAlternateReady = false;
        std::optional<PerformanceState> snapshot;
        std::optional<Step> stepClipboard;
        std::optional<ProjectRequest> projectRequest;
        RangeClipboard rangeClipboard;
        std::deque<HistoryEntry> undoHistory;
        std::deque<HistoryEntry> redoHistory;
        std::deque<AsyncHistoryTransaction> pendingAsyncHistory;
        std::uint64_t historyGeneration = 1;
        std::uint64_t nextHistoryGeneration = 2;
        std::uint64_t savedHistoryGeneration = 1;
        std::uint64_t nextActionSequence = 1;
        std::deque<std::uint64_t> autoUndoAppliedCancellations;
        std::deque<std::uint64_t> unavailableAsyncRedos;
        std::string activeProjectPath;
        std::string projectNameEdit = "untitled";
        std::string patternNameEdit;
        std::vector<std::string> projectPaths;
        std::array<int, kTrackCount> queuedPattern{-1, -1, -1, -1, -1};
        std::array<std::uint64_t, kTrackCount> queueGeneration{};
        std::array<std::uint64_t, kTrackCount> queueColumnGeneration{};
        std::array<bool, kTrackCount> queuedByColumn{};
        int queuedColumnPattern = -1;
        std::uint64_t queuedColumnGeneration = 0;
        bool queuedColumnAppliesTempo = false;
        bool queuedColumnAppliesScale = false;
        std::uint64_t queuedGlobalGeneration = 0;
        TransportStatus transport{};
        std::string toastMessage;
        bool toastIsError = false;
        double toastSeconds = 0.0;
        double shutterSeconds = 0.0;
        double elapsed = 0.0;
        double lastSavedElapsed = 0.0;
        bool savedMomentKnown = false;
        float hoverX = 0.0f;
        float hoverY = 0.0f;
        double hoverSeconds = 0.0;
        bool hoverActive = false;
        bool wideHintInspector = false;
        bool onboardingVisible = false;
        bool onboardingStartedTransport = false;
        bool onboardingPlacedStep = false;
        bool onboardingChangedSound = false;
        bool onboardingSavedPattern = false;
        int outputWidth = kLogicalWidth;
        int outputHeight = kLogicalHeight;
        int viewportX = 0;
        int viewportY = 0;
        float viewportScale = 1.0f;

        Impl(SharedState &sharedState, AudioEngine &audioEngine)
            : shared(sharedState), audio(audioEngine), transport(audioEngine.status())
        {
            onboardingVisible = !stateCopy().onboardingDismissed;
            for (int index = 0; index < SDL_NumJoysticks(); ++index)
            {
                if (SDL_IsGameController(index) == SDL_TRUE)
                {
                    openController(index);
                    break;
                }
            }
        }

        ~Impl()
        {
            SDL_StopTextInput();
            if (controller)
                SDL_GameControllerClose(controller);
        }

        void openController(int deviceIndex)
        {
            if (controller || SDL_IsGameController(deviceIndex) != SDL_TRUE)
                return;
            controller = SDL_GameControllerOpen(deviceIndex);
            if (!controller)
                return;
            SDL_Joystick *joystick = SDL_GameControllerGetJoystick(controller);
            controllerInstance = joystick ? SDL_JoystickInstanceID(joystick) : -1;
            const char *name = SDL_GameControllerName(controller);
            toast(std::string("CONTROLLER ") + (name ? name : "CONNECTED"));
        }

        void closeController(SDL_JoystickID instance)
        {
            if (!controller || instance != controllerInstance)
                return;
            SDL_GameControllerClose(controller);
            controller = nullptr;
            controllerInstance = -1;
            controllerCoarseHeld = false;
            controllerAlternateHeld = false;
            toast("CONTROLLER DISCONNECTED", true);
            for (int index = 0; index < SDL_NumJoysticks(); ++index)
            {
                if (SDL_IsGameController(index) == SDL_TRUE)
                {
                    openController(index);
                    break;
                }
            }
        }

        AppState stateCopy() const
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            return shared.app;
        }

        std::shared_ptr<AppState> stateSnapshot() const
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            return std::make_shared<AppState>(shared.app);
        }

        void toast(std::string_view message, bool error = false)
        {
            toastMessage = asciiOnly(message);
            if (toastMessage.size() > 76u)
                toastMessage.resize(76u);
            toastIsError = error;
            toastSeconds = 2.4;
        }

        void bumpRevision(AppState &app)
        {
            ++app.editRevision;
        }

        bool isDirty() const
        {
            return historyGeneration != savedHistoryGeneration || !pendingAsyncHistory.empty();
        }

        void trimHistory(std::deque<HistoryEntry> &history)
        {
            constexpr std::size_t kHistoryLimit = 24;
            while (history.size() > kHistoryLimit)
                history.pop_front();
        }

        void recordHistory(const AppState &before)
        {
            if (historyRestoring)
                return;
            undoHistory.push_back({std::make_shared<AppState>(before), historyGeneration,
                                   nextActionSequence++, 0, false, false, false});
            trimHistory(undoHistory);
            redoHistory.clear();
            unavailableAsyncRedos.clear();
            historyGeneration = nextHistoryGeneration++;
        }

        void recordHistory(std::shared_ptr<AppState> before)
        {
            if (historyRestoring || !before)
                return;
            undoHistory.push_back({std::move(before), historyGeneration,
                                   nextActionSequence++, 0, false, false, false});
            trimHistory(undoHistory);
            redoHistory.clear();
            unavailableAsyncRedos.clear();
            historyGeneration = nextHistoryGeneration++;
        }

        std::uint64_t recordScopedHistory(std::shared_ptr<AppState> before,
                                          std::uint8_t trackMask, bool tempo, bool scale)
        {
            if (historyRestoring || !before)
                return 0;
            const std::uint64_t actionSequence = nextActionSequence++;
            undoHistory.push_back({std::move(before), historyGeneration,
                                   actionSequence, trackMask, tempo, scale, true});
            trimHistory(undoHistory);
            redoHistory.clear();
            unavailableAsyncRedos.clear();
            historyGeneration = nextHistoryGeneration++;
            return actionSequence;
        }

        void recordAcceptedAsyncHistory(std::shared_ptr<AppState> before, AsyncHistoryKind kind,
                                        std::uint64_t token, std::uint8_t trackMask = 0,
                                        bool tempo = false, bool scale = false)
        {
            pendingAsyncHistory.push_back({std::move(before), kind, token, trackMask,
                                           tempo, scale, nextActionSequence++, false});
            redoHistory.clear();
            unavailableAsyncRedos.clear();
            historyRecordedThisEvent = true;
        }

        void markSaved()
        {
            savedHistoryGeneration = historyGeneration;
            lastSavedElapsed = elapsed;
            savedMomentKnown = true;
            toast("SAVED");
        }

        void resetHistory(bool saved)
        {
            undoHistory.clear();
            redoHistory.clear();
            pendingAsyncHistory.clear();
            historyGeneration = 1;
            nextHistoryGeneration = 2;
            savedHistoryGeneration = saved ? historyGeneration : 0;
            nextActionSequence = 1;
            autoUndoAppliedCancellations.clear();
            unavailableAsyncRedos.clear();
            if (saved)
            {
                lastSavedElapsed = elapsed;
                savedMomentKnown = true;
            }
        }

        std::string projectPathLabel(std::string_view path) const
        {
            if (path.empty())
                return "SESSION";
            const std::size_t slash = path.find_last_of('/');
            std::string label(path.substr(slash == std::string_view::npos ? 0 : slash + 1));
            const std::size_t extension = label.find_last_of('.');
            if (extension != std::string::npos)
                label.resize(extension);
            return asciiOnly(label.empty() ? "SESSION" : label);
        }

        std::string projectLabel() const { return projectPathLabel(activeProjectPath); }

        void setProjectPath(std::string path)
        {
            activeProjectPath = std::move(path);
        }

        void projectLoaded(std::string path)
        {
            UiMutationGuard mutationGuard(shared, true);
            setProjectPath(std::move(path));
            // The host has already installed the opened AppState. Publish a
            // fresh reset after that assignment so the callback cannot retain
            // cursor/runtime metadata from the prior project.
            audio.setRunning(false);
            audio.reset();
            resetProjectWorkspace();
            resetHistory(true);
            onboardingVisible = !stateCopy().onboardingDismissed;
            onboardingStartedTransport = false;
            onboardingPlacedStep = false;
            onboardingChangedSound = false;
            onboardingSavedPattern = false;
            overlay = Overlay::None;
            toast("PROJECT LOADED " + projectLabel());
        }

        void projectStarted(std::string path)
        {
            UiMutationGuard mutationGuard(shared, true);
            setProjectPath(std::move(path));
            audio.setRunning(false);
            audio.reset();
            resetProjectWorkspace();
            resetHistory(false);
            onboardingVisible = !stateCopy().onboardingDismissed;
            onboardingStartedTransport = false;
            onboardingPlacedStep = false;
            onboardingChangedSound = false;
            onboardingSavedPattern = false;
            overlay = Overlay::None;
            toast("NEW PROJECT " + projectLabel());
        }

        void beginProjectNameEdit()
        {
            projectNameEdit = "untitled";
            overlay = Overlay::ProjectName;
            SDL_StartTextInput();
        }

        void submitProjectName()
        {
            const std::string path = projectPathForName(projectNameEdit);
            projectRequest = ProjectRequest{ProjectRequestKind::SaveAs, path};
            overlay = Overlay::None;
            SDL_StopTextInput();
            toast("SAVE AS " + projectPathLabel(path));
        }

        void openProjectBrowser()
        {
            projectPaths = recentProjectPaths();
            projectBrowserCursor = 0;
            projectBrowserArmed = false;
            overlay = Overlay::ProjectBrowser;
        }

        void submitProjectOpen()
        {
            if (projectPaths.empty())
            {
                toast("NO RECENT PROJECTS", true);
                return;
            }
            const std::string &path = projectPaths[static_cast<std::size_t>(projectBrowserCursor)];
            projectRequest = ProjectRequest{ProjectRequestKind::Open, path};
            overlay = Overlay::None;
            projectBrowserArmed = false;
            toast("OPENING " + projectPathLabel(path));
        }

        void dismissOnboarding()
        {
            if (!onboardingVisible)
                return;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                if (!app.onboardingDismissed)
                {
                    app.onboardingDismissed = true;
                    bumpRevision(app);
                }
            }
            onboardingVisible = false;
            toast("QUICK START DISMISSED - REOPEN FROM CTRL+K");
        }

        void reopenOnboarding()
        {
            onboardingVisible = true;
            toast("QUICK START OPEN");
        }

        static constexpr int kCommandCount = 12;

        const char *commandName(int command) const
        {
            static constexpr std::array<const char *, kCommandCount> names{{"START / STOP", "OPEN GRID", "SYNTH BASIC", "SYNTH DEEP",
                                                                            "DATA PERFORM", "DATA MANAGE", "SOUND PALETTE", "GRID COMPARE",
                                                                            "PROJECT ACTIONS", "SAVE PROJECT", "CONTEXT HINTS", "QUICK START GUIDE"}};
            return names[static_cast<std::size_t>(clampInt(command, 0, kCommandCount - 1))];
        }

        const char *commandDescription(int command) const
        {
            static constexpr std::array<const char *, kCommandCount> descriptions{{"SPACE - TOGGLE THE SEQUENCER", "RETURN TO STEP EDITING", "AMBIENT MACROS AND ROUTING",
                                                                                   "GROUPED ACCESS TO ALL 50 FIELDS", "LOAD AND QUEUE PATTERNS", "NAME, COLOR, LOCK, AND CURATE",
                                                                                   "RECALL OR STORE FM / NOISE SOUNDS", "SHOW ONE FIELD ACROSS TRACKS",
                                                                                   "NEW, CLEAR, SAVE AS, OR OPEN", "WRITE THE ACTIVE PROJECT", "TOGGLE THE LEARNING INSPECTOR",
                                                                                   "REOPEN THE FOUR-STEP LOOP WALKTHROUGH"}};
            return descriptions[static_cast<std::size_t>(clampInt(command, 0, kCommandCount - 1))];
        }

        void openCommandPalette()
        {
            commandCursor = 0;
            overlay = Overlay::CommandPalette;
        }

        void executeCommand(int command)
        {
            overlay = Overlay::None;
            switch (clampInt(command, 0, kCommandCount - 1))
            {
            case 0:
                toggleTransport();
                break;
            case 1:
                view = View::Grid;
                editorIndex = 0;
                break;
            case 2:
                view = View::Synth;
                synthPerformanceMode = true;
                editorIndex = 0;
                break;
            case 3:
                view = View::Synth;
                synthPerformanceMode = false;
                editorIndex = 0;
                break;
            case 4:
                view = View::Data;
                dataWorkspace = DataWorkspace::Perform;
                dataColumn = std::max(0, dataColumn);
                break;
            case 5:
                view = View::Data;
                dataWorkspace = DataWorkspace::Manage;
                break;
            case 6:
                if (view != View::Grid && view != View::Synth)
                    view = View::Grid;
                openPalette();
                break;
            case 7:
                view = View::Grid;
                gridCompareValues = !gridCompareValues;
                toast(gridCompareValues ? "GRID COMPARE ON" : "GRID COMPARE OFF");
                break;
            case 8:
                openProjectMenu();
                break;
            case 9:
                requestSave();
                break;
            case 10:
                hintPanelVisible = !hintPanelVisible;
                toast(hintPanelVisible ? "CONTEXT HINTS ON" : "CONTEXT HINTS OFF");
                break;
            case 11:
                reopenOnboarding();
                break;
            }
        }

        int asyncTrack(const AsyncHistoryTransaction &transaction) const
        {
            for (int track = 0; track < kTrackCount; ++track)
                if ((transaction.trackMask & static_cast<std::uint8_t>(1u << track)) != 0u)
                    return track;
            return -1;
        }

        TransportCommandFamily asyncFamily(const AsyncHistoryTransaction &transaction) const
        {
            switch (transaction.kind)
            {
            case AsyncHistoryKind::Track:
                return TransportCommandFamily::Track;
            case AsyncHistoryKind::Column:
                return TransportCommandFamily::Column;
            case AsyncHistoryKind::Global:
                return TransportCommandFamily::GlobalSettings;
            }
            return TransportCommandFamily::Column;
        }

        AsyncHistorySettlement asyncSettlement(const AsyncHistoryTransaction &transaction,
                                               const TransportStatus &status) const
        {
            AsyncHistorySettlement result;
            const TransportCommandFamily family = asyncFamily(transaction);
            const int track = asyncTrack(transaction);
            for (const auto &settlement : status.settlements)
            {
                if (settlement.sequence == 0 || settlement.family != family ||
                    settlement.token != transaction.token ||
                    (family == TransportCommandFamily::Track && settlement.track != track) ||
                    settlement.sequence < result.sequence)
                    continue;
                result.sequence = settlement.sequence;
                result.outcome = settlement.outcome == TransportSettlementOutcome::Applied
                                     ? AsyncHistoryOutcome::Applied
                                     : AsyncHistoryOutcome::Cancelled;
                result.trackMask = family == TransportCommandFamily::Track
                                       ? transaction.trackMask
                                       : static_cast<std::uint8_t>(settlement.appliedTrackMask &
                                                                   transaction.trackMask);
                result.tempo = settlement.appliedTempo && transaction.tempo;
                result.scale = settlement.appliedScale && transaction.scale;
            }
            return result;
        }

        void clearAsyncQueueMarker(const AsyncHistoryTransaction &transaction)
        {
            if (transaction.kind == AsyncHistoryKind::Track)
            {
                const int track = asyncTrack(transaction);
                if (track >= 0 && queueGeneration[static_cast<std::size_t>(track)] == transaction.token)
                {
                    queueGeneration[static_cast<std::size_t>(track)] = 0;
                    queuedPattern[static_cast<std::size_t>(track)] = -1;
                }
                return;
            }
            if (transaction.kind == AsyncHistoryKind::Column)
            {
                for (int track = 0; track < kTrackCount; ++track)
                {
                    const std::size_t index = static_cast<std::size_t>(track);
                    if (queueColumnGeneration[index] != transaction.token)
                        continue;
                    queueColumnGeneration[index] = 0;
                    queuedByColumn[index] = false;
                    queuedPattern[index] = -1;
                }
                if (queuedColumnGeneration == transaction.token)
                {
                    queuedColumnGeneration = 0;
                    queuedColumnPattern = -1;
                    queuedColumnAppliesTempo = false;
                    queuedColumnAppliesScale = false;
                }
                return;
            }
            if (transaction.kind == AsyncHistoryKind::Global &&
                queuedGlobalGeneration == transaction.token)
                queuedGlobalGeneration = 0;
        }

        bool applyHistoryEntry(bool redo)
        {
            auto &source = redo ? redoHistory : undoHistory;
            auto &destination = redo ? undoHistory : redoHistory;
            if (source.empty())
            {
                toast(redo ? "REDO HISTORY EMPTY" : "UNDO HISTORY EMPTY", true);
                return true;
            }
            const HistoryEntry target = source.back();
            historyRestoring = true;
            const bool preservePending = !pendingAsyncHistory.empty();
            std::shared_ptr<AppState> current;
            if (preservePending)
            {
                std::unique_lock<std::mutex> lock(shared.mutex);
                const TransportStatus lockedStatus = audio.status();
                bool settledWhileWaiting = false;
                for (const auto &transaction : pendingAsyncHistory)
                    if (asyncSettlement(transaction, lockedStatus).outcome !=
                        AsyncHistoryOutcome::Pending)
                    {
                        settledWhileWaiting = true;
                        break;
                    }
                if (settledWhileWaiting)
                {
                    lock.unlock();
                    historyRestoring = false;
                    transport = lockedStatus;
                    finalizeAsyncHistory();
                    return false;
                }
                current = std::make_shared<AppState>(shared.app);
                source.pop_back();
                destination.push_back({current, historyGeneration,
                                       target.actionSequence, target.trackMask,
                                       target.tempo, target.scale, target.scoped});
                trimHistory(destination);
                if (!target.scoped)
                {
                    shared.app = *target.state;
                }
                else
                {
                    for (int track = 0; track < kTrackCount; ++track)
                    {
                        if ((target.trackMask & static_cast<std::uint8_t>(1u << track)) != 0u)
                            shared.app.tracks[static_cast<std::size_t>(track)] =
                                target.state->tracks[static_cast<std::size_t>(track)];
                    }
                    if (target.tempo)
                        shared.app.bpm = target.state->bpm;
                    if (target.scale)
                    {
                        shared.app.scaleRoot = target.state->scaleRoot;
                        shared.app.scaleMask = target.state->scaleMask;
                    }
                    ++shared.app.editRevision;
                }
            }
            else
            {
                current = stateSnapshot();
                source.pop_back();
                destination.push_back({current, historyGeneration,
                                       target.actionSequence, target.trackMask,
                                       target.tempo, target.scale, target.scoped});
                trimHistory(destination);
                audio.reset();
                std::lock_guard<std::mutex> lock(shared.mutex);
                if (!target.scoped)
                {
                    shared.app = *target.state;
                }
                else
                {
                    for (int track = 0; track < kTrackCount; ++track)
                        if ((target.trackMask & static_cast<std::uint8_t>(1u << track)) != 0u)
                            shared.app.tracks[static_cast<std::size_t>(track)] =
                                target.state->tracks[static_cast<std::size_t>(track)];
                    if (target.tempo)
                        shared.app.bpm = target.state->bpm;
                    if (target.scale)
                    {
                        shared.app.scaleRoot = target.state->scaleRoot;
                        shared.app.scaleMask = target.state->scaleMask;
                    }
                    ++shared.app.editRevision;
                }
            }
            historyGeneration = target.generation;
            historyRestoring = false;
            if (!preservePending)
                clearProjectQueues();
            snapshot.reset();
            snapshotSide = false;
            snapshotAlternateReady = false;
            toast(redo ? "REDO APPLIED" : "UNDO APPLIED");
            return true;
        }

        bool hasSettledAsyncHistory(const TransportStatus &status) const
        {
            return std::any_of(
                pendingAsyncHistory.begin(), pendingAsyncHistory.end(),
                [this, &status](const AsyncHistoryTransaction &transaction)
                { return asyncSettlement(transaction, status).outcome !=
                         AsyncHistoryOutcome::Pending; });
        }

        bool undoSpecificScopedHistory(std::uint64_t actionSequence)
        {
            for (;;)
            {
                const auto targetIterator = std::find_if(
                    undoHistory.begin(), undoHistory.end(),
                    [actionSequence](const HistoryEntry &entry)
                    { return entry.actionSequence == actionSequence; });
                if (targetIterator == undoHistory.end() || !targetIterator->scoped)
                    return false;
                const HistoryEntry target = *targetIterator;
                const bool newest = std::next(targetIterator) == undoHistory.end();
                historyRestoring = true;
                std::shared_ptr<AppState> current;
                std::unique_lock<std::mutex> lock(shared.mutex, std::defer_lock);
                if (pendingAsyncHistory.empty())
                {
                    audio.reset();
                    lock.lock();
                }
                else
                {
                    lock.lock();
                    const TransportStatus lockedStatus = audio.status();
                    if (hasSettledAsyncHistory(lockedStatus))
                    {
                        lock.unlock();
                        historyRestoring = false;
                        transport = lockedStatus;
                        finalizeAsyncHistory();
                        continue;
                    }
                }
                current = std::make_shared<AppState>(shared.app);
                for (int track = 0; track < kTrackCount; ++track)
                    if ((target.trackMask & static_cast<std::uint8_t>(1u << track)) != 0u)
                        shared.app.tracks[static_cast<std::size_t>(track)] =
                            target.state->tracks[static_cast<std::size_t>(track)];
                if (target.tempo)
                    shared.app.bpm = target.state->bpm;
                if (target.scale)
                {
                    shared.app.scaleRoot = target.state->scaleRoot;
                    shared.app.scaleMask = target.state->scaleMask;
                }
                ++shared.app.editRevision;
                lock.unlock();
                redoHistory.push_back({std::move(current), historyGeneration,
                                       target.actionSequence, target.trackMask,
                                       target.tempo, target.scale, true});
                trimHistory(redoHistory);
                undoHistory.erase(targetIterator);
                historyGeneration = newest ? target.generation : nextHistoryGeneration++;
                historyRestoring = false;
                toast("CUE APPLIED DURING CANCEL - SCOPED UNDO");
                return true;
            }
        }

        void drainAppliedCancellationUndos()
        {
            while (!autoUndoAppliedCancellations.empty())
            {
                const std::uint64_t actionSequence = autoUndoAppliedCancellations.front();
                autoUndoAppliedCancellations.pop_front();
                undoSpecificScopedHistory(actionSequence);
            }
        }

        void requestPendingUndo(AsyncHistoryTransaction &transaction)
        {
            if (transaction.cancelRequested)
            {
                toast("WAITING FOR CUE CANCELLATION");
                return;
            }
            if (!audio.cancelTransportCommand(asyncFamily(transaction), transaction.token,
                                              asyncTrack(transaction)))
            {
                const std::uint64_t actionSequence = transaction.actionSequence;
                transport = audio.status();
                const AsyncHistoryOutcome outcome =
                    asyncSettlement(transaction, transport).outcome;
                if (outcome != AsyncHistoryOutcome::Pending)
                    transaction.cancelRequested = true;
                finalizeAsyncHistory();
                drainAppliedCancellationUndos();
                const bool stillPending = std::any_of(
                    pendingAsyncHistory.begin(), pendingAsyncHistory.end(),
                    [actionSequence](const AsyncHistoryTransaction &pending)
                    { return pending.actionSequence == actionSequence; });
                if (stillPending)
                    toast("CUE CANCELLATION UNAVAILABLE - REQUEUE FROM DATA", true);
                else if (outcome == AsyncHistoryOutcome::Cancelled)
                    toast("PENDING CHANGE CANCELED - REQUEUE FROM DATA");
                return;
            }
            transaction.cancelRequested = true;
            toast("CANCELING PENDING PATTERN CHANGE");
        }

        void restoreHistory(bool redo)
        {
            transport = audio.status();
            finalizeAsyncHistory();
            drainAppliedCancellationUndos();
            if (redo && !unavailableAsyncRedos.empty() &&
                (redoHistory.empty() ||
                 unavailableAsyncRedos.back() < redoHistory.back().actionSequence))
            {
                unavailableAsyncRedos.pop_back();
                toast("CUE REDO UNAVAILABLE - REQUEUE FROM DATA", true);
                return;
            }
            if (!redo && !pendingAsyncHistory.empty())
            {
                auto newest = pendingAsyncHistory.begin();
                for (auto transaction = std::next(pendingAsyncHistory.begin());
                     transaction != pendingAsyncHistory.end(); ++transaction)
                    if (transaction->actionSequence > newest->actionSequence)
                        newest = transaction;
                if (newest->cancelRequested)
                {
                    toast("WAITING FOR CUE CANCELLATION");
                    return;
                }
                const std::uint64_t historySequence = undoHistory.empty()
                                                          ? 0
                                                          : undoHistory.back().actionSequence;
                if (newest->actionSequence > historySequence)
                {
                    requestPendingUndo(*newest);
                    return;
                }
            }
            if (!applyHistoryEntry(redo))
                restoreHistory(redo);
        }

        void finalizeAsyncHistory()
        {
            if (pendingAsyncHistory.empty())
                return;
            for (;;)
            {
                auto transaction = pendingAsyncHistory.end();
                AsyncHistorySettlement settlement;
                for (auto candidate = pendingAsyncHistory.begin();
                     candidate != pendingAsyncHistory.end(); ++candidate)
                {
                    const AsyncHistorySettlement candidateSettlement =
                        asyncSettlement(*candidate, transport);
                    if (candidateSettlement.outcome == AsyncHistoryOutcome::Pending)
                        continue;
                    if (transaction == pendingAsyncHistory.end() ||
                        candidateSettlement.sequence < settlement.sequence)
                    {
                        transaction = candidate;
                        settlement = candidateSettlement;
                    }
                }
                if (transaction == pendingAsyncHistory.end())
                    break;
                const bool cancellationUndo = transaction->cancelRequested;
                if (settlement.outcome == AsyncHistoryOutcome::Applied)
                {
                    const std::uint64_t actionSequence = recordScopedHistory(
                        transaction->before, settlement.trackMask,
                        settlement.tempo, settlement.scale);
                    if (cancellationUndo)
                        autoUndoAppliedCancellations.push_back(actionSequence);
                    if (settlement.tempo)
                        dataArmTempo = false;
                    if (settlement.scale)
                        dataArmScale = false;
                }
                clearAsyncQueueMarker(*transaction);
                if (cancellationUndo && settlement.outcome == AsyncHistoryOutcome::Cancelled)
                    unavailableAsyncRedos.push_back(transaction->actionSequence);
                pendingAsyncHistory.erase(transaction);
                if (cancellationUndo && settlement.outcome == AsyncHistoryOutcome::Cancelled)
                    toast("PENDING CHANGE CANCELED - REQUEUE FROM DATA");
                else if (settlement.outcome == AsyncHistoryOutcome::Cancelled)
                    toast("OLDER PATTERN CHANGE SUPERSEDED");
            }
        }

        bool shouldCaptureHistory(const SDL_Event &event) const
        {
            if (historyRestoring)
                return false;
            if (event.type == SDL_KEYDOWN)
            {
                if (event.key.repeat != 0)
                    return false;
                const SDL_Scancode code = event.key.keysym.scancode;
                const bool control = (event.key.keysym.mod & KMOD_CTRL) != 0;
                return !(control && (code == SDL_SCANCODE_Z || code == SDL_SCANCODE_Y));
            }
            return event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEWHEEL ||
                   event.type == SDL_CONTROLLERBUTTONDOWN;
        }

        void clearProjectQueues()
        {
            queuedPattern.fill(-1);
            queueGeneration.fill(0);
            queueColumnGeneration.fill(0);
            queuedByColumn.fill(false);
            queuedColumnPattern = -1;
            queuedColumnGeneration = 0;
            queuedColumnAppliesTempo = false;
            queuedColumnAppliesScale = false;
            queuedGlobalGeneration = 0;
        }

        void resetProjectWorkspace()
        {
            view = View::Grid;
            selectedTrack = 0;
            selectedStep = 0;
            selectedParameter = 0;
            editorIndex = 0;
            dataBank = 0;
            dataColumn = 0;
            dataAllTracks = false;
            dataArmTempo = false;
            dataArmScale = false;
            cancelRange();
            snapshot.reset();
            snapshotSide = false;
            snapshotAlternateReady = false;
            stepClipboard.reset();
            rangeClipboard = {};
            clearProjectQueues();
        }

        void requestSave()
        {
            if (activeProjectPath.empty())
            {
                beginProjectNameEdit();
                toast("NAME THIS UNTITLED PROJECT");
                return;
            }
            saveRequested = true;
            toast("SAVE REQUESTED");
        }

        void openProjectMenu(int action = 0)
        {
            overlay = Overlay::ProjectMenu;
            projectActionCursor = clampInt(action, 0, 4);
            projectActionArmed = false;
        }

        void clearWorkingTracks()
        {
            audio.reset();
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                for (int track = 0; track < kTrackCount; ++track)
                {
                    TrackData cleared;
                    cleared.modulator.targetTrack = static_cast<std::uint8_t>(track);
                    app.tracks[static_cast<std::size_t>(track)] = cleared;
                }
                bumpRevision(app);
            }
            resetProjectWorkspace();
            toast("WORKING TRACKS CLEARED");
        }

        void startNewSession()
        {
            projectRequest = ProjectRequest{ProjectRequestKind::New, {}};
            toast("STARTING NEW PROJECT - PRESERVING CURRENT WORK");
        }

        void activateProjectAction()
        {
            if (projectActionCursor == 2)
            {
                overlay = Overlay::None;
                requestSave();
                return;
            }
            if (projectActionCursor == 3)
            {
                beginProjectNameEdit();
                return;
            }
            if (projectActionCursor == 4)
            {
                openProjectBrowser();
                return;
            }
            if (!projectActionArmed)
            {
                projectActionArmed = true;
                toast(projectActionCursor == 0 ? "ENTER CONFIRMS NEW SESSION"
                                               : "ENTER CONFIRMS CLEAR TRACKS");
                return;
            }
            if (projectActionCursor == 0)
                startNewSession();
            else
                clearWorkingTracks();
            overlay = Overlay::None;
            projectActionArmed = false;
        }

        int dataPattern() const
        {
            return dataBank * 16 + clampInt(dataColumn, 0, 15);
        }

        int selectionTrackFirst() const
        {
            return rangeActive ? std::min(rangeAnchorTrack, selectedTrack) : selectedTrack;
        }

        int selectionTrackLast() const
        {
            return rangeActive ? std::max(rangeAnchorTrack, selectedTrack) : selectedTrack;
        }

        int selectionStepFirst() const
        {
            return rangeActive ? std::min(rangeAnchorStep, selectedStep) : selectedStep;
        }

        int selectionStepLast() const
        {
            return rangeActive ? std::max(rangeAnchorStep, selectedStep) : selectedStep;
        }

        bool selectedCell(int track, int step) const
        {
            if (!rangeActive)
                return track == selectedTrack && step == selectedStep;
            return track >= selectionTrackFirst() && track <= selectionTrackLast() &&
                   step >= selectionStepFirst() && step <= selectionStepLast();
        }

        template <typename Function>
        void forEditCells(Function &&function)
        {
            if (editScope == EditScope::All)
            {
                for (int track = 0; track < kTrackCount; ++track)
                    for (int step = 0; step < kStepCount; ++step)
                        function(track, step);
                return;
            }
            if (editScope == EditScope::Track)
            {
                for (int step = 0; step < kStepCount; ++step)
                    function(selectedTrack, step);
                return;
            }
            for (int track = selectionTrackFirst(); track <= selectionTrackLast(); ++track)
                for (int step = selectionStepFirst(); step <= selectionStepLast(); ++step)
                    function(track, step);
        }

        void beginOrExtendRange(int trackDelta, int stepDelta)
        {
            if (!rangeActive)
            {
                rangeActive = true;
                rangeAnchorTrack = selectedTrack;
                rangeAnchorStep = selectedStep;
            }
            selectTrack(clampInt(selectedTrack + trackDelta, 0, kTrackCount - 1));
            selectedStep = clampInt(selectedStep + stepDelta, 0, kStepCount - 1);
        }

        void cancelRange()
        {
            rangeActive = false;
            rangeAnchorTrack = selectedTrack;
            rangeAnchorStep = selectedStep;
        }

        const char *scopeName() const
        {
            switch (editScope)
            {
            case EditScope::Selection:
                return rangeActive ? "RANGE" : "STEP";
            case EditScope::Track:
                return "TRACK";
            case EditScope::All:
                return "ALL";
            }
            return "STEP";
        }

        int editCellCount() const
        {
            if (editScope == EditScope::All)
                return kTrackCount * kStepCount;
            if (editScope == EditScope::Track)
                return kStepCount;
            return (selectionTrackLast() - selectionTrackFirst() + 1) *
                   (selectionStepLast() - selectionStepFirst() + 1);
        }

        bool confirmDestructiveEdit(int targetCount, std::string_view action)
        {
            if (targetCount <= 1)
                return true;
            const std::string label(action);
            if (destructiveEditArmed && elapsed <= destructiveEditDeadline &&
                destructiveEditTargetCount == targetCount && destructiveEditAction == label)
            {
                destructiveEditArmed = false;
                destructiveEditAction.clear();
                return true;
            }
            destructiveEditArmed = true;
            destructiveEditTargetCount = targetCount;
            destructiveEditAction = label;
            destructiveEditDeadline = elapsed + 2.5;
            toast("PRESS AGAIN TO " + label + " " + decimalValue(targetCount) + " STEPS");
            return false;
        }

        void cycleEditScope()
        {
            editScope = static_cast<EditScope>(wrapIndex(static_cast<int>(editScope) + 1, 3));
            toast(std::string("EDIT SCOPE ") + scopeName());
        }

        void selectTrack(int track)
        {
            const GridParam prior = gridParamItem(selectedTrack, selectedParameter).id;
            selectedTrack = clampInt(track, 0, kTrackCount - 1);
            const int mapped = gridParamIndex(selectedTrack, prior);
            selectedParameter = mapped >= 0 ? mapped
                                            : clampInt(selectedParameter, 0, gridParamCount(selectedTrack) - 1);
        }

        void changeView(int delta)
        {
            view = static_cast<View>(wrapIndex(static_cast<int>(view) + delta,
                                               static_cast<int>(kViewNames.size())));
            editorIndex = view == View::Synth && synthPerformanceMode ? 0 : 0;
            synthMacroCursor = 0;
        }

        void previewSelected()
        {
            Step step;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                step = shared.app.tracks[static_cast<std::size_t>(selectedTrack)]
                           .steps[static_cast<std::size_t>(selectedStep)];
            }
            if (step.active)
                audio.preview(selectedTrack, step);
        }

        void toggleStep()
        {
            if (editScope == EditScope::All)
            {
                const AppState app = stateCopy();
                const bool willDisable = app.tracks[static_cast<std::size_t>(selectedTrack)]
                                             .steps[static_cast<std::size_t>(selectedStep)]
                                             .active;
                if (willDisable && !confirmDestructiveEdit(kTrackCount * kStepCount,
                                                           "DISABLE ALL"))
                    return;
            }
            Step preview;
            bool havePreview = false;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                const bool activate = !app.tracks[static_cast<std::size_t>(selectedTrack)]
                                           .steps[static_cast<std::size_t>(selectedStep)]
                                           .active;
                forEditCells([&](int track, int stepIndex)
                             {
                auto& step = app.tracks[static_cast<std::size_t>(track)]
                                 .steps[static_cast<std::size_t>(stepIndex)];
                step.active = activate;
                if (!step.active) step.trigless = false;
                if (track == selectedTrack && stepIndex == selectedStep) {
                    preview = step;
                    havePreview = true;
                } });
                bumpRevision(app);
            }
            if (havePreview && preview.active)
                audio.preview(selectedTrack, preview);
            onboardingPlacedStep = true;
        }

        void toggleTrigless()
        {
            Step preview;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                auto &step = app.tracks[static_cast<std::size_t>(selectedTrack)]
                                 .steps[static_cast<std::size_t>(selectedStep)];
                step.trigless = !step.trigless;
                if (step.trigless)
                    step.active = true;
                preview = step;
                bumpRevision(app);
            }
            if (preview.active)
                audio.preview(selectedTrack, preview);
            toast(preview.trigless ? "TRIGLESS ON" : "TRIGLESS OFF");
        }

        void clearStep()
        {
            if (!confirmDestructiveEdit(editCellCount(), "CLEAR GRID"))
                return;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                forEditCells([&](int track, int stepIndex)
                             {
                auto& step = app.tracks[static_cast<std::size_t>(track)]
                                 .steps[static_cast<std::size_t>(stepIndex)];
                const std::uint8_t note = step.note;
                step = Step {};
                step.note = note; });
                bumpRevision(app);
            }
            toast(rangeActive || editScope != EditScope::Selection ? "SELECTION CLEARED" : "STEP CLEARED");
        }

        void copyStep()
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            if (!rangeActive)
            {
                stepClipboard = shared.app.tracks[static_cast<std::size_t>(selectedTrack)]
                                    .steps[static_cast<std::size_t>(selectedStep)];
                rangeClipboard = RangeClipboard{};
                toast("STEP COPIED");
                return;
            }
            const int firstTrack = selectionTrackFirst();
            const int firstStep = selectionStepFirst();
            rangeClipboard.tracks = selectionTrackLast() - firstTrack + 1;
            rangeClipboard.steps = selectionStepLast() - firstStep + 1;
            for (int track = 0; track < rangeClipboard.tracks; ++track)
            {
                for (int step = 0; step < rangeClipboard.steps; ++step)
                {
                    rangeClipboard.data[static_cast<std::size_t>(track * kStepCount + step)] =
                        shared.app.tracks[static_cast<std::size_t>(firstTrack + track)]
                            .steps[static_cast<std::size_t>(firstStep + step)];
                }
            }
            stepClipboard.reset();
            toast("RANGE COPIED");
        }

        void cutStep()
        {
            const int targetCount = (selectionTrackLast() - selectionTrackFirst() + 1) *
                                    (selectionStepLast() - selectionStepFirst() + 1);
            if (!confirmDestructiveEdit(targetCount, "CUT GRID"))
                return;
            copyStep();
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                for (int track = selectionTrackFirst(); track <= selectionTrackLast(); ++track)
                {
                    for (int stepIndex = selectionStepFirst(); stepIndex <= selectionStepLast(); ++stepIndex)
                    {
                        auto &step = app.tracks[static_cast<std::size_t>(track)]
                                         .steps[static_cast<std::size_t>(stepIndex)];
                        const std::uint8_t note = step.note;
                        step = Step{};
                        step.note = note;
                    }
                }
                bumpRevision(app);
            }
            toast(rangeActive ? "RANGE CUT" : "STEP CUT");
        }

        void pasteStep()
        {
            if (!stepClipboard.has_value() && rangeClipboard.empty())
            {
                toast("CLIPBOARD EMPTY", true);
                return;
            }
            Step preview;
            bool havePreview = false;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                if (stepClipboard.has_value())
                {
                    app.tracks[static_cast<std::size_t>(selectedTrack)]
                        .steps[static_cast<std::size_t>(selectedStep)] = *stepClipboard;
                    preview = *stepClipboard;
                    havePreview = true;
                }
                else
                {
                    for (int track = 0; track < rangeClipboard.tracks; ++track)
                    {
                        const int targetTrack = selectedTrack + track;
                        if (targetTrack >= kTrackCount)
                            break;
                        for (int step = 0; step < rangeClipboard.steps; ++step)
                        {
                            const int targetStep = selectedStep + step;
                            if (targetStep >= kStepCount)
                                break;
                            const Step &source = rangeClipboard.data[static_cast<std::size_t>(track * kStepCount + step)];
                            app.tracks[static_cast<std::size_t>(targetTrack)]
                                .steps[static_cast<std::size_t>(targetStep)] = source;
                            if (targetTrack == selectedTrack && targetStep == selectedStep)
                            {
                                preview = source;
                                havePreview = true;
                            }
                        }
                    }
                }
                bumpRevision(app);
            }
            if (havePreview && preview.active)
                audio.preview(selectedTrack, preview);
            toast(stepClipboard.has_value() ? "STEP PASTED" : "RANGE PASTED");
        }

        bool gridParamSupported(int track, GridParam parameter) const
        {
            const bool noise = track == kTrackCount - 1;
            if (parameter == GridParam::NoiseRate || parameter == GridParam::NoiseWidth)
                return noise;
            if (!noise)
                return true;
            switch (parameter)
            {
            case GridParam::Level:
            case GridParam::Pan:
            case GridParam::Portamento:
            case GridParam::Condition:
            case GridParam::Microtime:
            case GridParam::Echo:
            case GridParam::Trigless:
            case GridParam::AmpAttack:
            case GridParam::AmpHold:
            case GridParam::AmpRelease:
                return true;
            default:
                return false;
            }
        }

        void adjustOneGridParameter(AppState &app, Step &step, int track, GridParam parameter,
                                    int direction, bool coarse)
        {
            if (!gridParamSupported(track, parameter) || direction == 0)
                return;
            const int fine = direction > 0 ? 1 : -1;
            const int wide = fine * (coarse ? 8 : 1);
            const auto setByte = [](std::uint8_t &target, int value, int low = 0, int high = 127)
            {
                target = static_cast<std::uint8_t>(clampInt(value, low, high));
            };
            switch (parameter)
            {
            case GridParam::Note:
            {
                const int amount = fine * (coarse ? 12 : 1);
                const int raw = clampInt(static_cast<int>(step.note) + amount, 12, 119);
                step.note = static_cast<std::uint8_t>(quantizeNote(raw, app.scaleRoot, app.scaleMask));
                break;
            }
            case GridParam::Level:
                setByte(step.level, static_cast<int>(step.level) + wide);
                break;
            case GridParam::Pan:
                step.pan = static_cast<Pan>(wrapIndex(static_cast<int>(step.pan) + fine, 3));
                break;
            case GridParam::Portamento:
                setByte(step.portamento, static_cast<int>(step.portamento) + wide);
                break;
            case GridParam::Condition:
                setByte(step.condition, static_cast<int>(step.condition) + fine, 1, 8);
                break;
            case GridParam::Microtime:
                step.microTicks = static_cast<std::int8_t>(clampInt(
                    static_cast<int>(step.microTicks) + fine * (coarse ? 3 : 1), -6, 6));
                break;
            case GridParam::Chord1:
            case GridParam::Chord2:
            case GridParam::Chord3:
            {
                const int slot = parameter == GridParam::Chord1   ? 0
                                 : parameter == GridParam::Chord2 ? 1
                                                                  : 2;
                auto &chord = step.chord[static_cast<std::size_t>(slot)];
                chord = static_cast<std::int8_t>(clampInt(static_cast<int>(chord) +
                                                              fine * (coarse ? 12 : 1),
                                                          0, 24));
                break;
            }
            case GridParam::Echo:
                step.echo = direction > 0;
                break;
            case GridParam::Transpose:
                step.transpose = direction > 0;
                break;
            case GridParam::Mode:
                step.mode = direction > 0 ? SynthMode::Parallel : SynthMode::FM;
                break;
            case GridParam::Trigless:
                step.trigless = direction > 0;
                if (step.trigless)
                    step.active = true;
                break;
            case GridParam::AmpAttack:
                if (track == kTrackCount - 1)
                    setByte(step.noise.ampAttack, static_cast<int>(step.noise.ampAttack) + wide);
                else
                    setByte(step.fm.ampAttack, static_cast<int>(step.fm.ampAttack) + wide);
                break;
            case GridParam::AmpHold:
                if (track == kTrackCount - 1)
                    setByte(step.noise.ampHold, static_cast<int>(step.noise.ampHold) + wide);
                else
                    setByte(step.fm.ampHold, static_cast<int>(step.fm.ampHold) + wide);
                break;
            case GridParam::AmpRelease:
                if (track == kTrackCount - 1)
                    setByte(step.noise.ampRelease, static_cast<int>(step.noise.ampRelease) + wide);
                else
                    setByte(step.fm.ampRelease, static_cast<int>(step.fm.ampRelease) + wide);
                break;
            case GridParam::ModRatio:
                setByte(step.fm.modRatio, static_cast<int>(step.fm.modRatio) + wide);
                break;
            case GridParam::ModDepth:
                setByte(step.fm.modDepth, static_cast<int>(step.fm.modDepth) + wide);
                break;
            case GridParam::ModFeedback:
                setByte(step.fm.modFeedback, static_cast<int>(step.fm.modFeedback) + wide);
                break;
            case GridParam::ModAttack:
                setByte(step.fm.modAttack, static_cast<int>(step.fm.modAttack) + wide);
                break;
            case GridParam::ModRelease:
                setByte(step.fm.modRelease, static_cast<int>(step.fm.modRelease) + wide);
                break;
            case GridParam::ModEnd:
                setByte(step.fm.modEnd, static_cast<int>(step.fm.modEnd) + wide);
                break;
            case GridParam::SweepDepth:
                step.fm.sweepDepth = static_cast<std::int8_t>(clampInt(
                    static_cast<int>(step.fm.sweepDepth) + wide, -64, 63));
                break;
            case GridParam::SweepRelease:
                setByte(step.fm.sweepRelease, static_cast<int>(step.fm.sweepRelease) + wide);
                break;
            case GridParam::NoiseRate:
                setByte(step.noise.rate, static_cast<int>(step.noise.rate) + wide);
                break;
            case GridParam::NoiseWidth:
                step.noise.narrow = direction > 0;
                break;
            }
        }

        void adjustGridParameter(int direction, bool coarse)
        {
            if (direction == 0)
                return;
            const GridParam parameter = gridParamItem(selectedTrack, selectedParameter).id;
            Step preview;
            bool havePreview = false;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                forEditCells([&](int track, int stepIndex)
                             {
                auto& step = app.tracks[static_cast<std::size_t>(track)]
                                 .steps[static_cast<std::size_t>(stepIndex)];
                adjustOneGridParameter(app, step, track, parameter, direction, coarse);
                if (track == selectedTrack && stepIndex == selectedStep) {
                    preview = step;
                    havePreview = true;
                } });
                bumpRevision(app);
            }
            if (havePreview && preview.active)
                audio.preview(selectedTrack, preview);
            onboardingChangedSound = true;
        }

        void adjustBpm(int direction, bool coarse)
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto &app = shared.app;
            const int amount = direction * (coarse ? 10 : 1);
            app.bpm = static_cast<std::uint16_t>(clampInt(static_cast<int>(app.bpm) + amount, 30, 300));
            bumpRevision(app);
        }

        void toggleTransport()
        {
            if (!audio.available())
            {
                toast("AUDIO OFFLINE", true);
                return;
            }
            if (!transport.running)
                onboardingStartedTransport = true;
            audio.toggleRunning();
        }

        void adjustTrackRate(int direction)
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto &app = shared.app;
            auto &rate = app.tracks[static_cast<std::size_t>(selectedTrack)].rateIndex;
            rate = static_cast<std::uint8_t>(clampInt(static_cast<int>(rate) + direction, 0, 8));
            bumpRevision(app);
        }

        void adjustTrackShuffle(int direction, bool coarse, bool allTracks = false)
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto &app = shared.app;
            const int first = allTracks ? 0 : selectedTrack;
            const int last = allTracks ? kTrackCount - 1 : selectedTrack;
            for (int track = first; track <= last; ++track)
            {
                auto &shuffle = app.tracks[static_cast<std::size_t>(track)].shuffle;
                shuffle = static_cast<std::uint8_t>(clampInt(static_cast<int>(shuffle) +
                                                                 direction * (coarse ? 5 : 1),
                                                             0, 50));
            }
            bumpRevision(app);
        }

        void adjustTrackLength(int direction)
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto &app = shared.app;
            auto &length = app.tracks[static_cast<std::size_t>(selectedTrack)].length;
            length = static_cast<std::uint8_t>(clampInt(static_cast<int>(length) + direction, 1, 16));
            bumpRevision(app);
        }

        void cycleDirection(int direction, bool allTracks = false)
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto &app = shared.app;
            const int first = allTracks ? 0 : selectedTrack;
            const int last = allTracks ? kTrackCount - 1 : selectedTrack;
            for (int index = first; index <= last; ++index)
            {
                auto &track = app.tracks[static_cast<std::size_t>(index)];
                track.direction = static_cast<Direction>(wrapIndex(
                    static_cast<int>(track.direction) + direction, 4));
            }
            bumpRevision(app);
        }

        void toggleMute()
        {
            bool muted = false;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                auto &track = app.tracks[static_cast<std::size_t>(selectedTrack)];
                track.muted = !track.muted;
                if (track.muted)
                    track.solo = false;
                muted = track.muted;
                bumpRevision(app);
            }
            toast(muted ? "TRACK MUTED" : "TRACK LIVE");
        }

        void toggleSolo()
        {
            bool solo = false;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                auto &track = app.tracks[static_cast<std::size_t>(selectedTrack)];
                track.solo = !track.solo;
                if (track.solo)
                    track.muted = false;
                solo = track.solo;
                bumpRevision(app);
            }
            toast(solo ? "TRACK SOLO" : "SOLO OFF");
        }

        void unmuteAll()
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto &app = shared.app;
            for (auto &track : app.tracks)
            {
                track.muted = false;
                track.solo = false;
            }
            bumpRevision(app);
            toast("ALL TRACKS LIVE");
        }

        void rotateSteps(int direction, bool allTracks)
        {
            if (direction == 0)
                return;
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto &app = shared.app;
            const int firstTrack = allTracks ? 0 : selectionTrackFirst();
            const int lastTrack = allTracks ? kTrackCount - 1 : selectionTrackLast();
            const int firstStep = (allTracks || !rangeActive) ? 0 : selectionStepFirst();
            const int lastStep = (allTracks || !rangeActive) ? kStepCount - 1 : selectionStepLast();
            if (firstStep == lastStep)
                return;
            for (int track = firstTrack; track <= lastTrack; ++track)
            {
                auto &steps = app.tracks[static_cast<std::size_t>(track)].steps;
                if (direction > 0)
                {
                    const Step tail = steps[static_cast<std::size_t>(lastStep)];
                    for (int step = lastStep; step > firstStep; --step)
                        steps[static_cast<std::size_t>(step)] = steps[static_cast<std::size_t>(step - 1)];
                    steps[static_cast<std::size_t>(firstStep)] = tail;
                }
                else
                {
                    const Step head = steps[static_cast<std::size_t>(firstStep)];
                    for (int step = firstStep; step < lastStep; ++step)
                        steps[static_cast<std::size_t>(step)] = steps[static_cast<std::size_t>(step + 1)];
                    steps[static_cast<std::size_t>(lastStep)] = head;
                }
            }
            bumpRevision(app);
            toast(allTracks ? "ALL TRACKS ROTATED" : (rangeActive ? "RANGE ROTATED" : "TRACK ROTATED"));
        }

        void randomizeSelectedParameter()
        {
            if (view != View::Grid)
                return;
            if (editScope == EditScope::All &&
                !confirmDestructiveEdit(editCellCount(), "RANDOMIZE PARAM"))
                return;
            const GridParam parameter = gridParamItem(selectedTrack, selectedParameter).id;
            Step preview;
            bool havePreview = false;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                std::uint32_t value = static_cast<std::uint32_t>(
                    app.editRevision ^ (static_cast<std::uint64_t>(selectedParameter + 1) * 0x9E3779B9ull));
                forEditCells([&](int track, int stepIndex)
                             {
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
                } });
                bumpRevision(app);
            }
            if (havePreview && preview.active)
                audio.preview(selectedTrack, preview);
            toast(std::string("NUDGE ") + scopeName());
        }

        void randomizeSelectedTrack()
        {
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                const std::uint32_t seed = static_cast<std::uint32_t>(
                    app.editRevision ^ (static_cast<std::uint64_t>(selectedTrack + 1) * 0x9E3779B9ull));
                randomizeTrack(app.tracks[static_cast<std::size_t>(selectedTrack)], selectedTrack, seed);
                bumpRevision(app);
            }
            toast("TRACK RANDOMIZED");
        }

        void copySoundFields(const Step &source, Step &destination, int track) const
        {
            if (track == kTrackCount - 1)
            {
                destination.noise = source.noise;
            }
            else
            {
                destination.fm = source.fm;
                destination.mode = source.mode;
                destination.advancedFm = source.advancedFm;
            }
        }

        void clearSoundFields(Step &step, int track) const
        {
            if (track == kTrackCount - 1)
            {
                step.noise = NoisePatch{};
            }
            else
            {
                step.fm = FmPatch{};
                step.mode = SynthMode::FM;
                step.advancedFm = AdvancedFmPatch{};
            }
        }

        Step randomizedSound(int track, std::uint32_t seed) const
        {
            TrackData scratch;
            randomizeTrack(scratch, track, seed);
            return scratch.steps[0];
        }

        void openPalette()
        {
            if (view != View::Grid && view != View::Synth)
            {
                toast("PALETTE AVAILABLE IN GRID OR SYNTH", true);
                return;
            }
            const bool opening = overlay != Overlay::Palette;
            overlay = opening ? Overlay::Palette : Overlay::None;
            if (opening)
                lastPaletteNoise = selectedTrack == kTrackCount - 1;
            paletteCursor = clampInt(paletteCursor, 0, kPaletteSize + 1);
        }

        void paletteAction(bool store, bool wholeTrack)
        {
            if (!store && wholeTrack && paletteCursor == 0 &&
                !confirmDestructiveEdit(kStepCount, "CLEAR TRACK SOUND"))
                return;
            Step preview;
            bool havePreview = false;
            bool rejected = false;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                auto &palette = selectedTrack == kTrackCount - 1 ? app.noisePalette : app.fmPalette;
                if (store)
                {
                    if (paletteCursor < 2)
                    {
                        rejected = true;
                    }
                    else
                    {
                        Step stored;
                        copySoundFields(app.tracks[static_cast<std::size_t>(selectedTrack)]
                                            .steps[static_cast<std::size_t>(selectedStep)],
                                        stored,
                                        selectedTrack);
                        stored.active = true;
                        palette[static_cast<std::size_t>(paletteCursor - 2)] = stored;
                        bumpRevision(app);
                    }
                }
                else
                {
                    const int sourceEngine = selectedTrack == kTrackCount - 1 ? 1 : 0;
                    Step sound;
                    bool useSound = false;
                    if (paletteCursor == 1)
                    {
                        sound = randomizedSound(selectedTrack, static_cast<std::uint32_t>(
                                                                   app.editRevision ^ (static_cast<std::uint64_t>(paletteCursor + 1) * 0xA511E9B3ull)));
                        useSound = true;
                    }
                    else if (paletteCursor >= 2)
                    {
                        sound = palette[static_cast<std::size_t>(paletteCursor - 2)];
                        useSound = sound.active;
                        rejected = !useSound;
                    }
                    const auto apply = [&](int track, int stepIndex)
                    {
                        if ((track == kTrackCount - 1 ? 1 : 0) != sourceEngine)
                            return;
                        auto &destination = app.tracks[static_cast<std::size_t>(track)]
                                                .steps[static_cast<std::size_t>(stepIndex)];
                        if (paletteCursor == 0)
                            clearSoundFields(destination, track);
                        else if (useSound)
                            copySoundFields(sound, destination, track);
                        if (track == selectedTrack && stepIndex == selectedStep)
                        {
                            preview = destination;
                            havePreview = true;
                        }
                    };
                    if (!rejected)
                    {
                        if (wholeTrack)
                        {
                            for (int step = 0; step < kStepCount; ++step)
                                apply(selectedTrack, step);
                        }
                        else
                        {
                            const int firstTrack = selectionTrackFirst();
                            const int lastTrack = selectionTrackLast();
                            const int firstStep = selectionStepFirst();
                            const int lastStep = selectionStepLast();
                            for (int track = firstTrack; track <= lastTrack; ++track)
                                for (int step = firstStep; step <= lastStep; ++step)
                                    apply(track, step);
                        }
                        bumpRevision(app);
                    }
                }
            }
            if (rejected)
            {
                toast(store ? "SELECT A USER SOUND SLOT" : "SOUND SLOT EMPTY", true);
                return;
            }
            if (havePreview && preview.active)
                audio.preview(selectedTrack, preview);
            if (!store && !rejected)
                onboardingChangedSound = true;
            if (store)
                toast("SOUND " + hexValue(paletteCursor - 2, 1) + " STORED");
            else if (paletteCursor == 0)
                toast(wholeTrack ? "TRACK SOUND CLEARED" : "SOUND CLEARED");
            else if (paletteCursor == 1)
                toast(wholeTrack ? "TRACK SOUND RANDOMIZED" : "SOUND RANDOMIZED");
            else
                toast("SOUND " + hexValue(paletteCursor - 2, 1) +
                      (wholeTrack ? " APPLIED TO TRACK" : " RECALLED"));
        }

        void clearPaletteSlot()
        {
            if (paletteCursor < 2)
            {
                paletteAction(false, false);
                return;
            }
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto &app = shared.app;
            auto &palette = selectedTrack == kTrackCount - 1 ? app.noisePalette : app.fmPalette;
            palette[static_cast<std::size_t>(paletteCursor - 2)] = Step{};
            bumpRevision(app);
            toast("SOUND SLOT CLEARED");
        }

        void toggleSnapshot()
        {
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                if (!snapshot.has_value())
                {
                    snapshot = capturePerformance(app);
                    snapshotSide = false;
                    snapshotAlternateReady = false;
                }
                else
                {
                    PerformanceState live = capturePerformance(app);
                    restorePerformance(app, *snapshot);
                    snapshot = std::move(live);
                    if (snapshotAlternateReady)
                        snapshotSide = !snapshotSide;
                    else
                        snapshotAlternateReady = true;
                }
            }
            shutterSeconds = 0.18;
            toast(snapshotSide ? "SNAPSHOT B" : (snapshot.has_value() ? "SNAPSHOT A" : "SNAPSHOT"));
        }

        void toggleTheme()
        {
            bool light = false;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                app.lightTheme = !app.lightTheme;
                light = app.lightTheme;
                bumpRevision(app);
            }
            toast(light ? "LIGHT THEME" : "DARK THEME");
        }

        void cycleAccent()
        {
            int accent = 0;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                app.accent = static_cast<std::uint8_t>((app.accent + 1u) % 6u);
                accent = app.accent;
                bumpRevision(app);
            }
            toast("ACCENT " + decimalValue(accent + 1));
        }

        void adjustEcho(int direction, bool coarse)
        {
            if (direction == 0)
                return;
            if (selectedTrack == kTrackCount - 1 &&
                (editorIndex == 2 || editorIndex == 3 || editorIndex == 5 || editorIndex == 6))
                return;
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto &app = shared.app;
            auto &echo = app.tracks[static_cast<std::size_t>(selectedTrack)].echo;
            const int amount = direction * (coarse ? 4 : 1);
            switch (editorIndex)
            {
            case 0:
                echo.repeats = static_cast<std::uint8_t>(clampInt(static_cast<int>(echo.repeats) + direction, 0, 8));
                break;
            case 1:
                echo.speedTicks = static_cast<std::uint8_t>(clampInt(static_cast<int>(echo.speedTicks) + amount, 1, 96));
                break;
            case 2:
                echo.transpose = static_cast<std::int8_t>(clampInt(static_cast<int>(echo.transpose) + amount, -24, 24));
                break;
            case 3:
                echo.transposeModulo = static_cast<std::uint8_t>(clampInt(static_cast<int>(echo.transposeModulo) + direction, 1, 8));
                break;
            case 4:
                echo.volumeDelta = static_cast<std::int8_t>(clampInt(static_cast<int>(echo.volumeDelta) + amount, -64, 63));
                break;
            case 5:
                echo.modDelta = static_cast<std::int8_t>(clampInt(static_cast<int>(echo.modDelta) + amount, -64, 63));
                break;
            case 6:
                echo.feedbackDelta = static_cast<std::int8_t>(clampInt(static_cast<int>(echo.feedbackDelta) + amount, -64, 63));
                break;
            case 7:
                echo.pan = static_cast<EchoPan>(wrapIndex(static_cast<int>(echo.pan) + direction, 4));
                break;
            default:
                break;
            }
            bumpRevision(app);
        }

        void adjustTranspose(int direction, bool coarse)
        {
            if (direction == 0)
                return;
            if (selectedTrack == kTrackCount - 1)
                return;
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto &app = shared.app;
            auto &transpose = app.tracks[static_cast<std::size_t>(selectedTrack)].transpose;
            if (editorIndex >= 0 && editorIndex < 8)
            {
                auto &value = transpose.values[static_cast<std::size_t>(editorIndex)];
                value = static_cast<std::int8_t>(clampInt(static_cast<int>(value) +
                                                              direction * (coarse ? 12 : 1),
                                                          -24, 24));
            }
            else if (editorIndex == 8)
            {
                transpose.length = static_cast<std::uint8_t>(clampInt(
                    static_cast<int>(transpose.length) + direction, 1, 8));
            }
            else if (editorIndex == 9)
            {
                transpose.rate = static_cast<std::uint8_t>(clampInt(
                    static_cast<int>(transpose.rate) + direction * (coarse ? 4 : 1), 1, 16));
            }
            else if (editorIndex == 10)
            {
                transpose.advance = static_cast<TransposeAdvance>(
                    wrapIndex(static_cast<int>(transpose.advance) + direction, 3));
            }
            bumpRevision(app);
        }

        void adjustModulator(int direction, bool coarse)
        {
            if (direction == 0)
                return;
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto &app = shared.app;
            auto &mod = app.tracks[static_cast<std::size_t>(selectedTrack)].modulator;
            const int amount = direction * (coarse ? 4 : 1);
            switch (editorIndex)
            {
            case 0:
                mod.targetTrack = static_cast<std::uint8_t>(wrapIndex(static_cast<int>(mod.targetTrack) + direction, kTrackCount));
                break;
            case 1:
                mod.destination = static_cast<ModDest>(wrapIndex(static_cast<int>(mod.destination) + direction, 7));
                break;
            case 2:
                mod.speed = static_cast<std::uint8_t>(clampInt(static_cast<int>(mod.speed) + amount, 1, 64));
                break;
            case 3:
                mod.wave = static_cast<ModWave>(wrapIndex(static_cast<int>(mod.wave) + direction, 5));
                break;
            case 4:
                mod.depth = static_cast<std::int8_t>(clampInt(static_cast<int>(mod.depth) + amount, -64, 63));
                break;
            case 5:
                mod.offset = static_cast<std::uint8_t>(clampInt(static_cast<int>(mod.offset) + amount, 0, 63));
                break;
            default:
                break;
            }
            bumpRevision(app);
        }

        void toggleScaleNote(int degree)
        {
            if (degree < 0 || degree >= 12)
                return;
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto &app = shared.app;
            const std::uint16_t bit = static_cast<std::uint16_t>(1u << degree);
            const std::uint16_t changed = static_cast<std::uint16_t>(app.scaleMask ^ bit);
            if ((changed & 0x0FFFu) != 0u)
                app.scaleMask = changed;
            bumpRevision(app);
        }

        void applyScalePreset(int preset)
        {
            static constexpr std::array<std::uint16_t, 6> masks{
                0x0FFFu, 0x0AB5u, 0x05ADu, 0x06ADu, 0x06B5u, 0x0555u};
            if (preset < 0 || preset >= static_cast<int>(masks.size()))
                return;
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto &app = shared.app;
            app.scaleMask = masks[static_cast<std::size_t>(preset)];
            bumpRevision(app);
            toast("SCALE PRESET LOADED");
        }

        void adjustScale(int direction)
        {
            if (editorIndex == 0)
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                app.scaleRoot = static_cast<std::uint8_t>(wrapIndex(static_cast<int>(app.scaleRoot) + direction, 12));
                bumpRevision(app);
            }
            else if (editorIndex >= 1 && editorIndex <= 12)
            {
                const int degree = editorIndex - 1;
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                const std::uint16_t bit = static_cast<std::uint16_t>(1u << degree);
                if (direction > 0)
                    app.scaleMask = static_cast<std::uint16_t>(app.scaleMask | bit);
                else if ((app.scaleMask & static_cast<std::uint16_t>(~bit) & 0x0FFFu) != 0u)
                    app.scaleMask = static_cast<std::uint16_t>(app.scaleMask & static_cast<std::uint16_t>(~bit));
                bumpRevision(app);
            }
            else if (editorIndex >= 13 && editorIndex < 19)
            {
                applyScalePreset(editorIndex - 13);
            }
        }

        bool trackIsEmpty(const TrackData &track) const
        {
            return std::none_of(track.steps.begin(), track.steps.end(),
                                [](const Step &step)
                                { return step.active || step.trigless; });
        }

        TimedGlobalSettings armedBankSettings(const AppState &app) const
        {
            TimedGlobalSettings settings;
            const auto &bank = app.banks[static_cast<std::size_t>(dataBank)];
            settings.applyTempo = dataArmTempo && bank.hasTempo;
            settings.bpm = bank.tempo;
            settings.applyScale = dataArmScale && bank.hasScale;
            settings.scaleRoot = bank.scaleRoot;
            settings.scaleMask = bank.scaleMask;
            return settings;
        }

        bool queueArmedBankSettings(const TimedGlobalSettings &settings)
        {
            if (!settings.applyTempo && !settings.applyScale)
                return true;
            if (!audio.queueGlobalSettings(settings))
                return false;
            queuedGlobalGeneration = audio.status().submittedGlobalSettingsGeneration;
            return true;
        }

        void savePattern(int pattern)
        {
            bool lockedBeforeSave = false;
            bool overwritesColumn = false;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                const auto &app = shared.app;
                lockedBeforeSave = app.banks[static_cast<std::size_t>(dataBank)].locked;
                if (dataAllTracks && !lockedBeforeSave)
                {
                    for (int track = 0; track < kTrackCount; ++track)
                    {
                        if (app.patterns[static_cast<std::size_t>(track)]
                                        [static_cast<std::size_t>(pattern)]
                                            .occupied)
                        {
                            overwritesColumn = true;
                            break;
                        }
                    }
                }
            }
            if (lockedBeforeSave)
            {
                toast("BANK LOCKED - SAVE REJECTED", true);
                return;
            }
            if (overwritesColumn &&
                !confirmDestructiveEdit(kTrackCount, "OVERWRITE COLUMN"))
                return;
            bool locked = false;
            bool allCleared = true;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                auto &bank = app.banks[static_cast<std::size_t>(dataBank)];
                locked = bank.locked;
                if (!locked)
                {
                    const int first = dataAllTracks ? 0 : selectedTrack;
                    const int last = dataAllTracks ? kTrackCount - 1 : selectedTrack;
                    for (int track = first; track <= last; ++track)
                    {
                        auto &slot = app.patterns[static_cast<std::size_t>(track)]
                                                 [static_cast<std::size_t>(pattern)];
                        const auto &source = app.tracks[static_cast<std::size_t>(track)];
                        if (trackIsEmpty(source))
                            slot = StoredPattern{};
                        else
                        {
                            slot.track = source;
                            slot.occupied = true;
                            allCleared = false;
                        }
                    }
                    bumpRevision(app);
                }
            }
            if (locked)
            {
                toast("BANK LOCKED - SAVE REJECTED", true);
                return;
            }
            toast(std::string(dataAllTracks ? "COLUMN " : "PATTERN ") + hexValue(pattern) +
                  (allCleared ? " CLEARED" : " SAVED"));
            if (!locked)
                onboardingSavedPattern = true;
        }

        void loadPattern(int pattern, bool cue)
        {
            const auto historyBefore = stateSnapshot();
            std::array<TrackData, kTrackCount> patterns{};
            std::array<bool, kTrackCount> occupied{};
            TimedGlobalSettings settings;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                const auto &app = shared.app;
                settings = armedBankSettings(app);
                for (int track = 0; track < kTrackCount; ++track)
                {
                    const auto &slot = app.patterns[static_cast<std::size_t>(track)]
                                                   [static_cast<std::size_t>(pattern)];
                    occupied[static_cast<std::size_t>(track)] = slot.occupied;
                    patterns[static_cast<std::size_t>(track)] = slot.occupied
                                                                    ? slot.track
                                                                    : app.tracks[static_cast<std::size_t>(track)];
                }
            }
            const int first = dataAllTracks ? 0 : selectedTrack;
            const int last = dataAllTracks ? kTrackCount - 1 : selectedTrack;
            for (int track = first; track <= last; ++track)
            {
                if (!occupied[static_cast<std::size_t>(track)])
                {
                    toast("EMPTY PATTERN ON TRACK " + decimalValue(track + 1), true);
                    return;
                }
            }

            if (cue)
            {
                const bool needColumnBoundary = dataAllTracks || settings.applyTempo || settings.applyScale;
                if (needColumnBoundary)
                {
                    const std::uint8_t trackMask = dataAllTracks ? static_cast<std::uint8_t>(0x1Fu)
                                                                 : static_cast<std::uint8_t>(1u << selectedTrack);
                    if (!audio.queuePatternColumn(patterns, trackMask, settings))
                    {
                        toast("COLUMN CUE FULL OR AUDIO OFFLINE", true);
                        return;
                    }
                    const TransportStatus queued = audio.status();
                    queuedColumnPattern = pattern;
                    queuedColumnGeneration = queued.submittedColumnGeneration;
                    queuedColumnAppliesTempo = queuedColumnAppliesTempo || settings.applyTempo;
                    queuedColumnAppliesScale = queuedColumnAppliesScale || settings.applyScale;
                    for (int track = 0; track < kTrackCount; ++track)
                    {
                        if (!dataAllTracks && track != selectedTrack)
                            continue;
                        const std::size_t index = static_cast<std::size_t>(track);
                        queuedPattern[index] = pattern;
                        queuedByColumn[index] = true;
                        queueColumnGeneration[index] = queued.submittedColumnGeneration;
                        queueGeneration[index] = 0;
                    }
                    recordAcceptedAsyncHistory(historyBefore, AsyncHistoryKind::Column,
                                               queued.submittedColumnGeneration, trackMask,
                                               settings.applyTempo, settings.applyScale);
                    toast(std::string(dataAllTracks ? "COLUMN " : "PATTERN ") + hexValue(pattern) +
                          " CUE" + ((settings.applyTempo || settings.applyScale) ? " + BANK SETTINGS" : ""));
                    return;
                }
                if (!audio.queuePattern(selectedTrack, patterns[static_cast<std::size_t>(selectedTrack)]))
                {
                    toast("PATTERN QUEUE FULL OR AUDIO OFFLINE", true);
                    return;
                }
                const TransportStatus queued = audio.status();
                queuedPattern[static_cast<std::size_t>(selectedTrack)] = pattern;
                queuedByColumn[static_cast<std::size_t>(selectedTrack)] = false;
                queueColumnGeneration[static_cast<std::size_t>(selectedTrack)] = 0;
                queueGeneration[static_cast<std::size_t>(selectedTrack)] =
                    queued.submittedPatternGenerations[static_cast<std::size_t>(selectedTrack)];
                recordAcceptedAsyncHistory(
                    historyBefore, AsyncHistoryKind::Track,
                    queued.submittedPatternGenerations[static_cast<std::size_t>(selectedTrack)],
                    static_cast<std::uint8_t>(1u << selectedTrack));
                toast("PATTERN " + hexValue(pattern) + " CUED");
                return;
            }

            if (dataAllTracks &&
                !confirmDestructiveEdit(kTrackCount, "LOAD ALL TRACKS"))
                return;

            if (!audio.available())
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                for (int track = first; track <= last; ++track)
                    app.tracks[static_cast<std::size_t>(track)] = patterns[static_cast<std::size_t>(track)];
                if (settings.applyTempo)
                    app.bpm = settings.bpm;
                if (settings.applyScale)
                {
                    app.scaleRoot = settings.scaleRoot;
                    app.scaleMask = settings.scaleMask;
                }
                if (settings.applyTempo)
                    dataArmTempo = false;
                if (settings.applyScale)
                    dataArmScale = false;
                bumpRevision(app);
            }
            else
            {
                const TrackLoadMode mode = dataLoadMode == DataLoadMode::Reset
                                               ? TrackLoadMode::Reset
                                               : TrackLoadMode::InPlace;
                const std::uint8_t trackMask = dataAllTracks ? static_cast<std::uint8_t>(0x1Fu)
                                                             : static_cast<std::uint8_t>(1u << selectedTrack);
                if (!audio.loadPatternColumnImmediate(patterns, mode, trackMask, settings))
                {
                    toast("PATTERN LOAD QUEUE FULL", true);
                    return;
                }
                recordAcceptedAsyncHistory(historyBefore, AsyncHistoryKind::Column,
                                           audio.status().submittedColumnGeneration,
                                           trackMask, settings.applyTempo, settings.applyScale);
                if (settings.applyTempo || settings.applyScale)
                {
                    queuedColumnPattern = pattern;
                    queuedColumnGeneration = audio.status().submittedColumnGeneration;
                    queuedColumnAppliesTempo = queuedColumnAppliesTempo || settings.applyTempo;
                    queuedColumnAppliesScale = queuedColumnAppliesScale || settings.applyScale;
                }
            }
            for (int track = first; track <= last; ++track)
            {
                const std::size_t index = static_cast<std::size_t>(track);
                queuedPattern[index] = -1;
                queuedByColumn[index] = false;
                queueGeneration[index] = 0;
                queueColumnGeneration[index] = 0;
            }
            if (!queuedColumnAppliesTempo && !queuedColumnAppliesScale)
                queuedColumnPattern = -1;
            toast(std::string(dataAllTracks ? "COLUMN " : "PATTERN ") + hexValue(pattern) +
                  (dataLoadMode == DataLoadMode::Reset ? " LOAD RESET" : " LOAD IN PLACE"));
        }

        void runDataOperation(bool randomize)
        {
            const auto historyBefore = stateSnapshot();
            const int first = dataAllTracks ? 0 : selectedTrack;
            const int last = dataAllTracks ? kTrackCount - 1 : selectedTrack;
            if ((!randomize || dataAllTracks) &&
                !confirmDestructiveEdit((last - first + 1) * kStepCount,
                                        randomize ? "RANDOMIZE DATA" : "CLEAR DATA"))
                return;
            std::array<TrackData, kTrackCount> replacements{};
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                for (int track = first; track <= last; ++track)
                {
                    replacements[static_cast<std::size_t>(track)] = randomize
                                                                        ? app.tracks[static_cast<std::size_t>(track)]
                                                                        : TrackData{};
                    if (randomize)
                        randomizeTrack(replacements[static_cast<std::size_t>(track)], track,
                                       static_cast<std::uint32_t>(app.editRevision + static_cast<std::uint64_t>(track + 1) * 7919u));
                    if (!audio.available())
                        app.tracks[static_cast<std::size_t>(track)] = replacements[static_cast<std::size_t>(track)];
                }
                if (!audio.available())
                    bumpRevision(app);
            }
            if (audio.available())
            {
                const TrackLoadMode mode = dataLoadMode == DataLoadMode::Reset
                                               ? TrackLoadMode::Reset
                                               : TrackLoadMode::InPlace;
                const std::uint8_t trackMask = dataAllTracks ? static_cast<std::uint8_t>(0x1Fu)
                                                             : static_cast<std::uint8_t>(1u << selectedTrack);
                if (!audio.loadPatternColumnImmediate(replacements, mode, trackMask))
                {
                    toast("TRACK OPERATION QUEUE FULL", true);
                    return;
                }
                recordAcceptedAsyncHistory(historyBefore, AsyncHistoryKind::Column,
                                           audio.status().submittedColumnGeneration,
                                           trackMask);
            }
            toast(std::string(dataAllTracks ? "ALL TRACKS " : "TRACK ") +
                  (randomize ? "RANDOMIZED" : "CLEARED"));
        }

        void activateData(bool save, bool cue = false)
        {
            if (dataColumn == -2)
            {
                if (!save)
                    runDataOperation(false);
                return;
            }
            if (dataColumn == -1)
            {
                if (!save)
                    runDataOperation(true);
                return;
            }
            const int pattern = dataPattern();
            if (save)
                savePattern(pattern);
            else
                loadPattern(pattern, cue);
        }

        void toggleDataWorkspace()
        {
            dataWorkspace = dataWorkspace == DataWorkspace::Perform
                                ? DataWorkspace::Manage
                                : DataWorkspace::Perform;
            if (dataWorkspace == DataWorkspace::Perform && dataColumn < 0)
                dataColumn = 0;
            toast(dataWorkspace == DataWorkspace::Perform
                      ? "DATA PERFORM - LOAD AND QUEUE"
                      : "DATA MANAGE - ORGANIZE AND PROTECT");
        }

        void activateDataRibbon(int action)
        {
            switch (clampInt(action, 0, 6))
            {
            case 0:
                if (dataColumn < 0)
                    dataColumn = 0;
                activateData(false, false);
                break;
            case 1:
                if (dataColumn < 0)
                    dataColumn = 0;
                activateData(false, true);
                break;
            case 2:
                if (dataColumn < 0)
                    dataColumn = 0;
                activateData(true, false);
                break;
            case 3:
                runDataOperation(false);
                break;
            case 4:
                runDataOperation(true);
                break;
            case 5:
                dataAllTracks = !dataAllTracks;
                toast(dataAllTracks ? "TARGET ALL 5 TRACKS" : "TARGET CURRENT TRACK");
                break;
            case 6:
                dataLoadMode = dataLoadMode == DataLoadMode::Reset
                                   ? DataLoadMode::InPlace
                                   : DataLoadMode::Reset;
                toast(dataLoadMode == DataLoadMode::Reset ? "LOAD MODE RESET" : "LOAD MODE IN PLACE");
                break;
            }
        }

        void toggleCurrentBankLock()
        {
            bool locked = false;
            const int bank = dataBank;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                auto &settings = app.banks[static_cast<std::size_t>(bank)];
                settings.locked = !settings.locked;
                locked = settings.locked;
                bumpRevision(app);
            }
            toast("BANK " + decimalValue(bank + 1) + (locked ? " LOCKED" : " UNLOCKED"));
        }

        void beginBankNameEdit()
        {
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                bankNameEdit = shared.app.banks[static_cast<std::size_t>(dataBank)].name;
            }
            bankNameCursor = 0;
            overlay = Overlay::BankName;
            SDL_StartTextInput();
        }

        void commitBankNameEdit()
        {
            bool locked = false;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                locked = app.banks[static_cast<std::size_t>(dataBank)].locked;
                if (!locked)
                {
                    app.banks[static_cast<std::size_t>(dataBank)].name = bankNameEdit;
                    bumpRevision(app);
                }
            }
            overlay = Overlay::None;
            SDL_StopTextInput();
            toast(locked ? "BANK LOCKED - NAME REJECTED" : "BANK NAME SAVED", locked);
        }

        void beginPatternNameEdit()
        {
            if (dataColumn < 0)
            {
                toast("SELECT A PATTERN SLOT TO NAME", true);
                return;
            }
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                if (shared.app.banks[static_cast<std::size_t>(dataBank)].locked)
                {
                    toast("BANK LOCKED - PATTERN NAME REJECTED", true);
                    return;
                }
                patternNameEdit = shared.app.patternMetadata[static_cast<std::size_t>(dataPattern())].name.data();
            }
            overlay = Overlay::PatternName;
            SDL_StartTextInput();
        }

        void commitPatternNameEdit()
        {
            if (dataColumn < 0)
            {
                overlay = Overlay::None;
                SDL_StopTextInput();
                return;
            }
            bool locked = false;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                locked = app.banks[static_cast<std::size_t>(dataBank)].locked;
                if (!locked)
                {
                    auto &name = app.patternMetadata[static_cast<std::size_t>(dataPattern())].name;
                    name.fill('\0');
                    const std::size_t count = std::min(patternNameEdit.size(), kPatternMetadataNameLength);
                    std::copy_n(patternNameEdit.begin(), count, name.begin());
                    bumpRevision(app);
                }
            }
            overlay = Overlay::None;
            SDL_StopTextInput();
            toast(locked ? "BANK LOCKED - PATTERN NAME REJECTED" : "PATTERN NAME SAVED", locked);
        }

        void cyclePatternColor(int direction = 1)
        {
            if (dataColumn < 0)
            {
                toast("SELECT A PATTERN SLOT TO COLOR", true);
                return;
            }
            bool locked = false;
            int color = 0;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                locked = app.banks[static_cast<std::size_t>(dataBank)].locked;
                if (!locked)
                {
                    auto &value = app.patternMetadata[static_cast<std::size_t>(dataPattern())].color;
                    value = static_cast<std::uint8_t>(wrapIndex(static_cast<int>(value) + direction, 6));
                    color = value;
                    bumpRevision(app);
                }
            }
            toast(locked ? "BANK LOCKED - COLOR REJECTED"
                         : "PATTERN COLOR " + decimalValue(color),
                  locked);
        }

        void storeRecallBankSetting(bool tempo, bool store, bool timed)
        {
            const auto historyBefore = stateSnapshot();
            TimedGlobalSettings settings;
            bool available = true;
            bool locked = false;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                auto &bank = app.banks[static_cast<std::size_t>(dataBank)];
                if (store)
                {
                    locked = bank.locked;
                    if (!locked && tempo)
                    {
                        bank.tempo = app.bpm;
                        bank.hasTempo = true;
                    }
                    else if (!locked)
                    {
                        bank.scaleRoot = app.scaleRoot;
                        bank.scaleMask = app.scaleMask;
                        bank.hasScale = true;
                    }
                    if (!locked)
                        bumpRevision(app);
                }
                else if (tempo)
                {
                    available = bank.hasTempo;
                    settings.applyTempo = available;
                    settings.bpm = bank.tempo;
                    if (available && !timed)
                    {
                        app.bpm = bank.tempo;
                        bumpRevision(app);
                    }
                }
                else
                {
                    available = bank.hasScale;
                    settings.applyScale = available;
                    settings.scaleRoot = bank.scaleRoot;
                    settings.scaleMask = bank.scaleMask;
                    if (available && !timed)
                    {
                        app.scaleRoot = bank.scaleRoot;
                        app.scaleMask = bank.scaleMask;
                        bumpRevision(app);
                    }
                }
            }
            if (locked)
            {
                toast("BANK LOCKED - STORE REJECTED", true);
                return;
            }
            if (!available)
            {
                toast(tempo ? "BANK HAS NO BPM" : "BANK HAS NO SCALE", true);
                return;
            }
            if (!store && timed)
            {
                if (!queueArmedBankSettings(settings))
                {
                    toast("TIMED SETTINGS QUEUE FULL", true);
                    return;
                }
                recordAcceptedAsyncHistory(historyBefore, AsyncHistoryKind::Global,
                                           queuedGlobalGeneration, 0,
                                           settings.applyTempo, settings.applyScale);
            }
            toast(std::string(tempo ? "BPM " : "SCALE ") +
                  (store ? "STORED" : (timed ? "CUED" : "RECALLED")));
        }

        static constexpr int kSynthParameterCount = 50;
        static constexpr int kSynthMacroCount = 12;
        static constexpr std::array<const char *, kSynthMacroCount> kSynthMacroNames{{"ENGINE", "ALGORITHM", "TIMBRE", "ATTACK", "RELEASE", "CUTOFF",
                                                                                      "RESONANCE", "MOTION RATE", "MOTION DEPTH", "DRIVE", "DETUNE", "SPACE"}};
        static constexpr std::array<const char *, kSynthMacroCount> kSynthMacroDescriptions{{
            "SWITCHES BETWEEN CLASSIC 2-OP AND RICH 4-OP FM",
            "CHANGES WHICH OPERATORS MODULATE AND REACH OUTPUT",
            "RAISE FOR A BRIGHTER, MORE HARMONIC TONE",
            "RAISE FOR A SLOWER FADE-IN AND SOFTER ENTRANCE",
            "RAISE FOR A LONGER TAIL AND MORE OVERLAP",
            "LOWER DARKENS; RAISE OPENS THE FILTER",
            "RAISE TO EMPHASIZE THE FILTER EDGE",
            "SETS HOW QUICKLY THE SOUND EVOLVES",
            "SETS HOW FAR THE TIMBRE MOVES",
            "RAISE FOR DENSITY, EDGE, AND SATURATION",
            "SPREADS VOICES FOR CHORUS-LIKE DRIFT",
            "WIDENS UNISON ACROSS THE STEREO FIELD",
        }};

        int synthMacroParameter() const
        {
            static constexpr std::array<int, kSynthMacroCount> primaryParameters{{0, 1, 8, 22, 25, 27, 28, 35, 36, 30, 32, 33}};
            return primaryParameters[static_cast<std::size_t>(
                clampInt(synthMacroCursor, 0, kSynthMacroCount - 1))];
        }

        float synthMacroUnit(const AdvancedFmPatch &patch, int macro) const
        {
            if (macro == 0)
                return patch.enabled ? 1.0f : 0.0f;
            if (macro == 1)
                return static_cast<float>(static_cast<int>(patch.algorithm)) / 11.0f;
            if (macro == 2)
            {
                const int sum = static_cast<int>(patch.operators[1].level) +
                                static_cast<int>(patch.operators[2].level) +
                                static_cast<int>(patch.operators[3].level);
                return static_cast<float>(sum) / (3.0f * 127.0f);
            }
            switch (macro)
            {
            case 3:
                return static_cast<float>(patch.ampEnvelope.attack) / 127.0f;
            case 4:
                return static_cast<float>(patch.ampEnvelope.release) / 127.0f;
            case 5:
                return static_cast<float>(patch.filterCutoff) / 127.0f;
            case 6:
                return static_cast<float>(patch.resonance) / 127.0f;
            case 7:
                return static_cast<float>(patch.modulation[0].rate) / 127.0f;
            case 8:
                return static_cast<float>(static_cast<int>(patch.modulation[0].depth) + 127) / 254.0f;
            case 9:
                return static_cast<float>(patch.driveAmount) / 127.0f;
            case 10:
                return static_cast<float>(patch.unisonDetune) / 127.0f;
            case 11:
                return static_cast<float>(patch.unisonWidth) / 127.0f;
            default:
                return 0.0f;
            }
        }

        std::string synthMacroValue(const AdvancedFmPatch &patch, int macro) const
        {
            switch (macro)
            {
            case 0:
                return patch.enabled ? "4-OP" : "LEGACY";
            case 1:
                return paddedDecimal(static_cast<int>(patch.algorithm) + 1);
            case 2:
                return hexValue(static_cast<int>(std::lround(synthMacroUnit(patch, macro) * 127.0f)));
            case 3:
                return hexValue(patch.ampEnvelope.attack);
            case 4:
                return hexValue(patch.ampEnvelope.release);
            case 5:
                return hexValue(patch.filterCutoff);
            case 6:
                return hexValue(patch.resonance);
            case 7:
                return hexValue(patch.modulation[0].rate);
            case 8:
                return signedValue(patch.modulation[0].depth);
            case 9:
                return hexValue(patch.driveAmount);
            case 10:
                return hexValue(patch.unisonDetune);
            case 11:
                return decimalValue(patch.unisonVoices) + "V / " + hexValue(patch.unisonWidth);
            default:
                return "--";
            }
        }

        void adjustOneSynthMacro(AdvancedFmPatch &patch, int macro, int direction, bool coarse)
        {
            const int sign = direction > 0 ? 1 : -1;
            const int amount = sign * (coarse ? 8 : 1);
            const auto byte = [](std::uint8_t &target, int value)
            {
                target = static_cast<std::uint8_t>(clampInt(value, 0, 127));
            };
            if (macro == 0)
            {
                patch.enabled = direction > 0;
                return;
            }
            patch.enabled = true;
            switch (macro)
            {
            case 1:
                patch.algorithm = static_cast<AdvancedFmAlgorithm>(
                    wrapIndex(static_cast<int>(patch.algorithm) + sign, 12));
                break;
            case 2:
                for (std::size_t op = 1; op < patch.operators.size(); ++op)
                    byte(patch.operators[op].level,
                         static_cast<int>(patch.operators[op].level) + amount);
                break;
            case 3:
                byte(patch.ampEnvelope.attack, static_cast<int>(patch.ampEnvelope.attack) + amount);
                break;
            case 4:
                byte(patch.ampEnvelope.release, static_cast<int>(patch.ampEnvelope.release) + amount);
                break;
            case 5:
                if (patch.filterMode == AdvancedFilterMode::Off && direction > 0)
                    patch.filterMode = AdvancedFilterMode::LowPass;
                byte(patch.filterCutoff, static_cast<int>(patch.filterCutoff) + amount);
                break;
            case 6:
                if (patch.filterMode == AdvancedFilterMode::Off && direction > 0)
                    patch.filterMode = AdvancedFilterMode::LowPass;
                byte(patch.resonance, static_cast<int>(patch.resonance) + amount);
                break;
            case 7:
                if (patch.modulation[0].source == AdvancedModSource::Off)
                {
                    patch.modulation[0].source = AdvancedModSource::TriangleLfo;
                    patch.modulation[0].destination = AdvancedModDestination::FilterCutoff;
                }
                byte(patch.modulation[0].rate,
                     static_cast<int>(patch.modulation[0].rate) + amount);
                break;
            case 8:
                if (patch.modulation[0].source == AdvancedModSource::Off)
                {
                    patch.modulation[0].source = AdvancedModSource::TriangleLfo;
                    patch.modulation[0].destination = AdvancedModDestination::FilterCutoff;
                    patch.modulation[0].rate = 32;
                }
                patch.modulation[0].depth = static_cast<std::int8_t>(clampInt(
                    static_cast<int>(patch.modulation[0].depth) + amount, -127, 127));
                break;
            case 9:
                if (patch.driveMode == AdvancedDriveMode::Off && direction > 0)
                    patch.driveMode = AdvancedDriveMode::SoftClip;
                byte(patch.driveAmount, static_cast<int>(patch.driveAmount) + amount);
                break;
            case 10:
                byte(patch.unisonDetune, static_cast<int>(patch.unisonDetune) + amount);
                if (direction > 0 && patch.unisonVoices < 2u)
                    patch.unisonVoices = 2;
                break;
            case 11:
                byte(patch.unisonWidth, static_cast<int>(patch.unisonWidth) + amount);
                if (direction > 0)
                    patch.unisonVoices = static_cast<std::uint8_t>(
                        std::max(2, clampInt(1 + static_cast<int>(patch.unisonWidth) / 32, 1, 4)));
                else if (patch.unisonWidth == 0u)
                    patch.unisonVoices = 1;
                break;
            default:
                break;
            }
        }

        void toggleSynthPerformanceMode()
        {
            if (view != View::Synth || selectedTrack == kTrackCount - 1)
                return;
            synthPerformanceMode = !synthPerformanceMode;
            synthMacroCursor = 0;
            editorIndex = 0;
            toast(synthPerformanceMode ? "SYNTH BASIC MACROS" : "SYNTH DEEP EDITOR");
        }

        const char *oscillatorName(AdvancedOscShape value) const
        {
            switch (value)
            {
            case AdvancedOscShape::Sine:
                return "SINE";
            case AdvancedOscShape::Triangle:
                return "TRI";
            case AdvancedOscShape::Saw:
                return "SAW";
            case AdvancedOscShape::Square:
                return "SQUARE";
            case AdvancedOscShape::Pulse:
                return "PULSE";
            case AdvancedOscShape::Noise:
                return "NOISE";
            }
            return "SINE";
        }

        const char *filterName(AdvancedFilterMode value) const
        {
            switch (value)
            {
            case AdvancedFilterMode::Off:
                return "OFF";
            case AdvancedFilterMode::LowPass:
                return "LOW PASS";
            case AdvancedFilterMode::HighPass:
                return "HIGH PASS";
            case AdvancedFilterMode::BandPass:
                return "BAND PASS";
            case AdvancedFilterMode::Notch:
                return "NOTCH";
            }
            return "OFF";
        }

        const char *driveName(AdvancedDriveMode value) const
        {
            switch (value)
            {
            case AdvancedDriveMode::Off:
                return "OFF";
            case AdvancedDriveMode::SoftClip:
                return "SOFT";
            case AdvancedDriveMode::HardClip:
                return "HARD";
            case AdvancedDriveMode::Wavefold:
                return "FOLD";
            }
            return "OFF";
        }

        const char *modSourceName(AdvancedModSource value) const
        {
            switch (value)
            {
            case AdvancedModSource::Off:
                return "OFF";
            case AdvancedModSource::SineLfo:
                return "SINE LFO";
            case AdvancedModSource::TriangleLfo:
                return "TRI LFO";
            case AdvancedModSource::SawLfo:
                return "SAW LFO";
            case AdvancedModSource::SquareLfo:
                return "SQR LFO";
            case AdvancedModSource::SampleAndHold:
                return "S+H";
            case AdvancedModSource::AmpEnvelope:
                return "AMP ENV";
            }
            return "OFF";
        }

        const char *modDestinationName(AdvancedModDestination value) const
        {
            static constexpr std::array<const char *, 15> names{{"NONE", "PITCH", "LEVEL", "PAN", "CUTOFF", "RESONANCE", "DRIVE",
                                                                 "OP1 LEVEL", "OP2 LEVEL", "OP3 LEVEL", "OP4 LEVEL",
                                                                 "OP1 RATIO", "OP2 RATIO", "OP3 RATIO", "OP4 RATIO"}};
            return names[static_cast<std::size_t>(clampInt(static_cast<int>(value), 0, 14))];
        }

        std::string synthParameterLabel(int index) const
        {
            if (index == 0)
                return "ENGINE";
            if (index == 1)
                return "ALGORITHM";
            if (index >= 2 && index < 22)
            {
                static constexpr std::array<const char *, 5> names{{"WAVE", "RATIO", "LEVEL", "FEEDBACK", "DETUNE"}};
                const int local = index - 2;
                return "OP" + decimalValue(local / 5 + 1) + " " + names[static_cast<std::size_t>(local % 5)];
            }
            static constexpr std::array<const char *, 12> globalNames{{"AMP ATTACK", "AMP DECAY", "AMP SUSTAIN", "AMP RELEASE",
                                                                       "FILTER MODE", "FILTER CUTOFF", "RESONANCE", "DRIVE MODE",
                                                                       "DRIVE AMOUNT", "UNISON VOICES", "UNISON DETUNE", "UNISON WIDTH"}};
            if (index < 34)
                return globalNames[static_cast<std::size_t>(index - 22)];
            static constexpr std::array<const char *, 4> modNames{{"SOURCE", "RATE", "DEPTH", "DEST"}};
            const int local = index - 34;
            return "MOD" + decimalValue(local / 4 + 1) + " " + modNames[static_cast<std::size_t>(local % 4)];
        }

        std::string synthParameterValue(const AdvancedFmPatch &patch, int index) const
        {
            if (index == 0)
                return patch.enabled ? "4-OP ON" : "LEGACY";
            if (index == 1)
                return paddedDecimal(static_cast<int>(patch.algorithm) + 1);
            if (index >= 2 && index < 22)
            {
                const int local = index - 2;
                const auto &op = patch.operators[static_cast<std::size_t>(local / 5)];
                switch (local % 5)
                {
                case 0:
                    return oscillatorName(op.shape);
                case 1:
                {
                    char buffer[16];
                    std::snprintf(buffer, sizeof(buffer), "%.3g", fmRatio(op.ratio));
                    return buffer;
                }
                case 2:
                    return hexValue(op.level);
                case 3:
                    return hexValue(op.feedback);
                case 4:
                    return signedValue(op.detune);
                }
            }
            switch (index)
            {
            case 22:
                return hexValue(patch.ampEnvelope.attack);
            case 23:
                return hexValue(patch.ampEnvelope.decay);
            case 24:
                return hexValue(patch.ampEnvelope.sustain);
            case 25:
                return hexValue(patch.ampEnvelope.release);
            case 26:
                return filterName(patch.filterMode);
            case 27:
                return hexValue(patch.filterCutoff);
            case 28:
                return hexValue(patch.resonance);
            case 29:
                return driveName(patch.driveMode);
            case 30:
                return hexValue(patch.driveAmount);
            case 31:
                return decimalValue(patch.unisonVoices);
            case 32:
                return hexValue(patch.unisonDetune);
            case 33:
                return hexValue(patch.unisonWidth);
            default:
                break;
            }
            const int local = index - 34;
            const auto &mod = patch.modulation[static_cast<std::size_t>(clampInt(local / 4, 0, 3))];
            switch (local % 4)
            {
            case 0:
                return modSourceName(mod.source);
            case 1:
                return hexValue(mod.rate);
            case 2:
                return signedValue(mod.depth);
            case 3:
                return modDestinationName(mod.destination);
            }
            return "--";
        }

        void adjustOneSynthParameter(AdvancedFmPatch &patch, int index, int direction, bool coarse)
        {
            const int sign = direction > 0 ? 1 : -1;
            const int amount = sign * (coarse ? 8 : 1);
            const auto byte = [](std::uint8_t &target, int value, int low = 0, int high = 127)
            {
                target = static_cast<std::uint8_t>(clampInt(value, low, high));
            };
            if (index == 0)
            {
                patch.enabled = direction > 0;
                return;
            }
            patch.enabled = true;
            if (index == 1)
            {
                patch.algorithm = static_cast<AdvancedFmAlgorithm>(
                    wrapIndex(static_cast<int>(patch.algorithm) + sign, 12));
                return;
            }
            if (index >= 2 && index < 22)
            {
                const int local = index - 2;
                auto &op = patch.operators[static_cast<std::size_t>(local / 5)];
                switch (local % 5)
                {
                case 0:
                    op.shape = static_cast<AdvancedOscShape>(wrapIndex(static_cast<int>(op.shape) + sign, 6));
                    break;
                case 1:
                    byte(op.ratio, static_cast<int>(op.ratio) + amount);
                    break;
                case 2:
                    byte(op.level, static_cast<int>(op.level) + amount);
                    break;
                case 3:
                    byte(op.feedback, static_cast<int>(op.feedback) + amount);
                    break;
                case 4:
                    op.detune = static_cast<std::int8_t>(clampInt(static_cast<int>(op.detune) + amount, -64, 63));
                    break;
                }
                return;
            }
            switch (index)
            {
            case 22:
                byte(patch.ampEnvelope.attack, static_cast<int>(patch.ampEnvelope.attack) + amount);
                return;
            case 23:
                byte(patch.ampEnvelope.decay, static_cast<int>(patch.ampEnvelope.decay) + amount);
                return;
            case 24:
                byte(patch.ampEnvelope.sustain, static_cast<int>(patch.ampEnvelope.sustain) + amount);
                return;
            case 25:
                byte(patch.ampEnvelope.release, static_cast<int>(patch.ampEnvelope.release) + amount);
                return;
            case 26:
                patch.filterMode = static_cast<AdvancedFilterMode>(wrapIndex(static_cast<int>(patch.filterMode) + sign, 5));
                return;
            case 27:
                byte(patch.filterCutoff, static_cast<int>(patch.filterCutoff) + amount);
                return;
            case 28:
                byte(patch.resonance, static_cast<int>(patch.resonance) + amount);
                return;
            case 29:
                patch.driveMode = static_cast<AdvancedDriveMode>(wrapIndex(static_cast<int>(patch.driveMode) + sign, 4));
                return;
            case 30:
                byte(patch.driveAmount, static_cast<int>(patch.driveAmount) + amount);
                return;
            case 31:
                byte(patch.unisonVoices, static_cast<int>(patch.unisonVoices) + sign, 1, 4);
                return;
            case 32:
                byte(patch.unisonDetune, static_cast<int>(patch.unisonDetune) + amount);
                return;
            case 33:
                byte(patch.unisonWidth, static_cast<int>(patch.unisonWidth) + amount);
                return;
            default:
                break;
            }
            const int local = index - 34;
            auto &mod = patch.modulation[static_cast<std::size_t>(clampInt(local / 4, 0, 3))];
            switch (local % 4)
            {
            case 0:
                mod.source = static_cast<AdvancedModSource>(wrapIndex(static_cast<int>(mod.source) + sign, 7));
                break;
            case 1:
                byte(mod.rate, static_cast<int>(mod.rate) + amount);
                break;
            case 2:
                mod.depth = static_cast<std::int8_t>(clampInt(static_cast<int>(mod.depth) + amount, -127, 127));
                break;
            case 3:
                mod.destination = static_cast<AdvancedModDestination>(wrapIndex(static_cast<int>(mod.destination) + sign, 15));
                break;
            }
        }

        void adjustSynth(int direction, bool coarse)
        {
            if (selectedTrack == kTrackCount - 1 || direction == 0)
                return;
            Step preview;
            bool havePreview = false;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                forEditCells([&](int track, int stepIndex)
                             {
                if (track == kTrackCount - 1) return;
                auto& step = app.tracks[static_cast<std::size_t>(track)]
                                 .steps[static_cast<std::size_t>(stepIndex)];
                if (synthPerformanceMode)
                    adjustOneSynthMacro(step.advancedFm, editorIndex, direction, coarse);
                else
                    adjustOneSynthParameter(step.advancedFm, editorIndex, direction, coarse);
                if (track == selectedTrack && stepIndex == selectedStep) {
                    preview = step;
                    havePreview = true;
                } });
                bumpRevision(app);
            }
            if (havePreview && preview.active)
                audio.preview(selectedTrack, preview);
            onboardingChangedSound = true;
        }

        void randomizeSynthParameter()
        {
            if (selectedTrack == kTrackCount - 1)
                return;
            Step preview;
            bool havePreview = false;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                auto &app = shared.app;
                std::uint32_t value = static_cast<std::uint32_t>(
                    app.editRevision ^ (static_cast<std::uint64_t>(editorIndex + 17) * 0x85EBCA6Bull));
                forEditCells([&](int track, int stepIndex)
                             {
                if (track == kTrackCount - 1) return;
                value ^= value << 13u;
                value ^= value >> 17u;
                value ^= value << 5u;
                auto& step = app.tracks[static_cast<std::size_t>(track)]
                                 .steps[static_cast<std::size_t>(stepIndex)];
                if (synthPerformanceMode)
                    adjustOneSynthMacro(step.advancedFm, editorIndex,
                        (value & 1u) != 0u ? 1 : -1, (value & 0x1Cu) == 0x1Cu);
                else
                    adjustOneSynthParameter(step.advancedFm, editorIndex,
                        (value & 1u) != 0u ? 1 : -1, (value & 0x1Cu) == 0x1Cu);
                if (track == selectedTrack && stepIndex == selectedStep) {
                    preview = step;
                    havePreview = true;
                } });
                bumpRevision(app);
            }
            if (havePreview && preview.active)
                audio.preview(selectedTrack, preview);
            toast(std::string("NUDGE ") + scopeName());
        }

        void adjustEditor(int direction, bool coarse)
        {
            switch (view)
            {
            case View::Grid:
                adjustGridParameter(direction, coarse);
                break;
            case View::Synth:
                adjustSynth(direction, coarse);
                break;
            case View::Echo:
                adjustEcho(direction, coarse);
                break;
            case View::Transpose:
                adjustTranspose(direction, coarse);
                break;
            case View::Mod:
                adjustModulator(direction, coarse);
                break;
            case View::Scale:
                adjustScale(direction);
                break;
            case View::Data:
                break;
            }
        }

        int editorCount() const
        {
            switch (view)
            {
            case View::Grid:
                return gridParamCount(selectedTrack);
            case View::Synth:
                return synthPerformanceMode ? kSynthMacroCount : kSynthParameterCount;
            case View::Echo:
                return 8;
            case View::Transpose:
                return 11;
            case View::Mod:
                return 6;
            case View::Scale:
                return 19;
            case View::Data:
                return 18;
            }
            return 1;
        }

        void activateEditor(bool save)
        {
            switch (view)
            {
            case View::Grid:
                toggleStep();
                break;
            case View::Synth:
                adjustSynth(1, false);
                break;
            case View::Echo:
                adjustEcho(1, false);
                break;
            case View::Transpose:
                adjustTranspose(1, false);
                break;
            case View::Mod:
                adjustModulator(1, false);
                break;
            case View::Scale:
                if (editorIndex >= 1 && editorIndex <= 12)
                    toggleScaleNote(editorIndex - 1);
                else if (editorIndex >= 13)
                    applyScalePreset(editorIndex - 13);
                break;
            case View::Data:
                activateData(save);
                break;
            }
        }

        const char *controllerActionName(int index) const
        {
            static constexpr std::array<const char *, kControllerActionCount> names{{"NAV UP", "NAV DOWN", "NAV LEFT", "NAV RIGHT", "CONFIRM", "CLEAR",
                                                                                     "PARAM PREV", "PARAM NEXT", "VALUE DOWN", "VALUE UP", "COARSE MOD",
                                                                                     "ALT MOD", "TRANSPORT", "COPY", "PASTE", "RANDOMIZE", "PALETTE", "CYCLE VIEW"}};
            return names[static_cast<std::size_t>(clampInt(index, 0,
                                                           static_cast<int>(kControllerActionCount) - 1))];
        }

        std::string controllerButtonName(std::uint8_t button) const
        {
            if (button == kControllerButtonUnbound)
                return "--";
            const char *name = SDL_GameControllerGetStringForButton(
                static_cast<SDL_GameControllerButton>(button));
            return name ? asciiOnly(name) : decimalValue(button);
        }

        bool controllerButtonMatches(const ControllerSettings &settings, ControllerAction action,
                                     std::uint8_t button) const
        {
            return settings.buttons[static_cast<std::size_t>(action)] == button;
        }

        bool sendMappedKey(SDL_Scancode code, bool shift = false, bool control = false,
                           bool alt = false)
        {
            SDL_KeyboardEvent event{};
            event.type = SDL_KEYDOWN;
            event.state = SDL_PRESSED;
            event.repeat = 0;
            event.keysym.scancode = code;
            event.keysym.mod = static_cast<SDL_Keymod>((shift ? KMOD_SHIFT : KMOD_NONE) |
                                                       (control ? KMOD_CTRL : KMOD_NONE) | (alt ? KMOD_ALT : KMOD_NONE));
            return handleKey(event);
        }

        void resetControllerBindings()
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto &app = shared.app;
            app.controller = ControllerSettings{};
            bumpRevision(app);
            controllerCoarseHeld = false;
            controllerAlternateHeld = false;
            toast("CONTROLLER DEFAULTS RESTORED");
        }

        void unbindControllerAction()
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto &app = shared.app;
            app.controller.buttons[static_cast<std::size_t>(controllerMapCursor)] = kControllerButtonUnbound;
            bumpRevision(app);
            controllerCapture = false;
            controllerCoarseHeld = false;
            controllerAlternateHeld = false;
            toast("ACTION UNBOUND");
        }

        bool handleControllerButton(const SDL_ControllerButtonEvent &event, bool pressed)
        {
            const std::uint8_t button = event.button;
            ControllerSettings settings;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                settings = shared.app.controller;
            }
            if (overlay == Overlay::ControllerMap && pressed && controllerCapture)
            {
                if (button > kControllerButtonMax)
                {
                    toast("BUTTON NUMBER OUT OF RANGE", true);
                    return true;
                }
                {
                    std::lock_guard<std::mutex> lock(shared.mutex);
                    auto &app = shared.app;
                    app.controller.buttons[static_cast<std::size_t>(controllerMapCursor)] = button;
                    bumpRevision(app);
                }
                controllerCapture = false;
                controllerCoarseHeld = false;
                controllerAlternateHeld = false;
                toast(std::string(controllerActionName(controllerMapCursor)) + " = " +
                      controllerButtonName(button));
                return true;
            }
            if (controllerButtonMatches(settings, ControllerAction::CoarseModifier, button))
            {
                controllerCoarseHeld = pressed;
                if (!pressed)
                    return true;
            }
            if (controllerButtonMatches(settings, ControllerAction::AlternateModifier, button))
            {
                controllerAlternateHeld = pressed;
                if (!pressed)
                    return true;
            }
            if (!pressed)
                return true;
            if (!settings.enabled)
                return true;

            if (overlay == Overlay::ControllerMap)
            {
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

        bool handleKey(const SDL_KeyboardEvent &key)
        {
            const SDL_Scancode code = key.keysym.scancode;
            const bool shift = (key.keysym.mod & KMOD_SHIFT) != 0;
            const bool control = (key.keysym.mod & KMOD_CTRL) != 0;
            const bool alt = (key.keysym.mod & KMOD_ALT) != 0;

            if (control && code == SDL_SCANCODE_K)
            {
                if (key.repeat == 0)
                {
                    if (overlay == Overlay::CommandPalette)
                        overlay = Overlay::None;
                    else
                        openCommandPalette();
                }
                return true;
            }
            if (code == SDL_SCANCODE_F6)
            {
                if (key.repeat == 0)
                {
                    hintPanelVisible = !hintPanelVisible;
                    toast(hintPanelVisible ? "CONTEXT HINTS ON" : "CONTEXT HINTS OFF");
                }
                return true;
            }
            if (code == SDL_SCANCODE_F7)
            {
                if (key.repeat == 0)
                {
                    if (onboardingVisible)
                        dismissOnboarding();
                    else
                        reopenOnboarding();
                }
                return true;
            }
            if (control && code == SDL_SCANCODE_Z)
            {
                if (key.repeat == 0)
                    restoreHistory(shift);
                return true;
            }
            if (control && code == SDL_SCANCODE_Y)
            {
                if (key.repeat == 0)
                    restoreHistory(true);
                return true;
            }
            if (code == SDL_SCANCODE_F9 && view == View::Grid)
            {
                if (key.repeat == 0)
                {
                    gridCompareValues = !gridCompareValues;
                    toast(gridCompareValues ? "GRID COMPARE ON" : "GRID COMPARE OFF");
                }
                return true;
            }
            if (code == SDL_SCANCODE_F8 && view == View::Data)
            {
                if (key.repeat == 0)
                    toggleDataWorkspace();
                return true;
            }

            if (overlay == Overlay::CommandPalette)
            {
                if (code == SDL_SCANCODE_ESCAPE)
                    overlay = Overlay::None;
                else if (code == SDL_SCANCODE_UP || code == SDL_SCANCODE_LEFT)
                    commandCursor = wrapIndex(commandCursor - 1, kCommandCount);
                else if (code == SDL_SCANCODE_DOWN || code == SDL_SCANCODE_RIGHT)
                    commandCursor = wrapIndex(commandCursor + 1, kCommandCount);
                else if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER)
                    executeCommand(commandCursor);
                return true;
            }

            if (overlay == Overlay::ProjectName)
            {
                if (code == SDL_SCANCODE_ESCAPE)
                {
                    overlay = Overlay::None;
                    SDL_StopTextInput();
                }
                else if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER)
                {
                    submitProjectName();
                }
                else if (code == SDL_SCANCODE_BACKSPACE || code == SDL_SCANCODE_DELETE)
                {
                    if (!projectNameEdit.empty())
                        projectNameEdit.pop_back();
                }
                return true;
            }
            if (overlay == Overlay::ProjectBrowser)
            {
                if (code == SDL_SCANCODE_ESCAPE)
                {
                    overlay = Overlay::None;
                    projectBrowserArmed = false;
                }
                else if (!projectPaths.empty() && (code == SDL_SCANCODE_UP || code == SDL_SCANCODE_LEFT))
                {
                    projectBrowserCursor = wrapIndex(projectBrowserCursor - 1,
                                                     static_cast<int>(projectPaths.size()));
                    projectBrowserArmed = false;
                }
                else if (!projectPaths.empty() && (code == SDL_SCANCODE_DOWN || code == SDL_SCANCODE_RIGHT))
                {
                    projectBrowserCursor = wrapIndex(projectBrowserCursor + 1,
                                                     static_cast<int>(projectPaths.size()));
                    projectBrowserArmed = false;
                }
                else if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER)
                {
                    if (projectBrowserArmed)
                        submitProjectOpen();
                    else if (!projectPaths.empty())
                    {
                        projectBrowserArmed = true;
                        toast("ENTER CONFIRMS OPEN");
                    }
                }
                return true;
            }
            if (overlay == Overlay::ProjectMenu)
            {
                if (code == SDL_SCANCODE_ESCAPE)
                {
                    overlay = Overlay::None;
                    projectActionArmed = false;
                }
                else if (code == SDL_SCANCODE_UP || code == SDL_SCANCODE_LEFT)
                {
                    projectActionCursor = wrapIndex(projectActionCursor - 1, 5);
                    projectActionArmed = false;
                }
                else if (code == SDL_SCANCODE_DOWN || code == SDL_SCANCODE_RIGHT)
                {
                    projectActionCursor = wrapIndex(projectActionCursor + 1, 5);
                    projectActionArmed = false;
                }
                else if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER)
                {
                    activateProjectAction();
                }
                return true;
            }
            if (overlay == Overlay::BankName)
            {
                if (code == SDL_SCANCODE_ESCAPE)
                {
                    overlay = Overlay::None;
                    SDL_StopTextInput();
                }
                else if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER)
                {
                    commitBankNameEdit();
                }
                else if (code == SDL_SCANCODE_LEFT)
                {
                    bankNameCursor = wrapIndex(bankNameCursor - 1, 4);
                }
                else if (code == SDL_SCANCODE_RIGHT)
                {
                    bankNameCursor = wrapIndex(bankNameCursor + 1, 4);
                }
                else if (code == SDL_SCANCODE_BACKSPACE || code == SDL_SCANCODE_DELETE)
                {
                    bankNameEdit[static_cast<std::size_t>(bankNameCursor)] = ' ';
                    bankNameCursor = wrapIndex(bankNameCursor - 1, 4);
                }
                return true;
            }
            if (overlay == Overlay::PatternName)
            {
                if (code == SDL_SCANCODE_ESCAPE)
                {
                    overlay = Overlay::None;
                    SDL_StopTextInput();
                }
                else if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER)
                {
                    commitPatternNameEdit();
                }
                else if (code == SDL_SCANCODE_BACKSPACE || code == SDL_SCANCODE_DELETE)
                {
                    if (!patternNameEdit.empty())
                        patternNameEdit.pop_back();
                }
                return true;
            }
            if (overlay == Overlay::Palette)
            {
                if (code == SDL_SCANCODE_ESCAPE || code == SDL_SCANCODE_P)
                    overlay = Overlay::None;
                else if (code == SDL_SCANCODE_LEFT || code == SDL_SCANCODE_UP)
                    paletteCursor = wrapIndex(paletteCursor - 1, kPaletteSize + 2);
                else if (code == SDL_SCANCODE_RIGHT || code == SDL_SCANCODE_DOWN)
                    paletteCursor = wrapIndex(paletteCursor + 1, kPaletteSize + 2);
                else if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER)
                    paletteAction(shift, control);
                else if (code == SDL_SCANCODE_DELETE || code == SDL_SCANCODE_BACKSPACE)
                    clearPaletteSlot();
                else if (code == SDL_SCANCODE_R)
                {
                    paletteCursor = 1;
                    paletteAction(false, control);
                }
                return true;
            }
            if (overlay == Overlay::ControllerMap)
            {
                if (code == SDL_SCANCODE_ESCAPE || code == SDL_SCANCODE_F4)
                {
                    overlay = Overlay::None;
                    controllerCapture = false;
                }
                else if (!controllerCapture && (code == SDL_SCANCODE_UP || code == SDL_SCANCODE_LEFT))
                {
                    controllerMapCursor = wrapIndex(controllerMapCursor - 1,
                                                    static_cast<int>(kControllerActionCount));
                }
                else if (!controllerCapture && (code == SDL_SCANCODE_DOWN || code == SDL_SCANCODE_RIGHT))
                {
                    controllerMapCursor = wrapIndex(controllerMapCursor + 1,
                                                    static_cast<int>(kControllerActionCount));
                }
                else if (!controllerCapture && (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER))
                {
                    controllerCapture = true;
                    toast("PRESS A CONTROLLER BUTTON");
                }
                else if (code == SDL_SCANCODE_DELETE || code == SDL_SCANCODE_BACKSPACE)
                {
                    unbindControllerAction();
                }
                else if (!controllerCapture && code == SDL_SCANCODE_D)
                {
                    resetControllerBindings();
                }
                else if (!controllerCapture && code == SDL_SCANCODE_E)
                {
                    std::lock_guard<std::mutex> lock(shared.mutex);
                    auto &app = shared.app;
                    app.controller.enabled = !app.controller.enabled;
                    bumpRevision(app);
                    controllerCoarseHeld = false;
                    controllerAlternateHeld = false;
                }
                return true;
            }

            if (code == SDL_SCANCODE_ESCAPE)
            {
                if (helpVisible)
                    helpVisible = false;
                else if (hintPanelVisible)
                    hintPanelVisible = false;
                else if (rangeActive)
                    cancelRange();
                return true;
            }
            if (code == SDL_SCANCODE_F1 || (code == SDL_SCANCODE_SLASH && shift))
            {
                if (key.repeat == 0)
                    helpVisible = !helpVisible;
                return true;
            }
            if (code == SDL_SCANCODE_F2)
            {
                if (key.repeat == 0)
                    toggleTheme();
                return true;
            }
            if (code == SDL_SCANCODE_F3)
            {
                if (key.repeat == 0)
                    cycleAccent();
                return true;
            }
            if (code == SDL_SCANCODE_F4)
            {
                if (key.repeat == 0)
                    overlay = Overlay::ControllerMap;
                return true;
            }
            if (code == SDL_SCANCODE_F5)
            {
                if (key.repeat == 0)
                    cycleEditScope();
                return true;
            }
            if (helpVisible)
                return true;
            if (control && shift && code == SDL_SCANCODE_S)
            {
                if (key.repeat == 0)
                    beginProjectNameEdit();
                return true;
            }
            if (control && code == SDL_SCANCODE_N)
            {
                if (key.repeat == 0)
                    openProjectMenu(0);
                return true;
            }
            if (control && shift &&
                (code == SDL_SCANCODE_BACKSPACE || code == SDL_SCANCODE_DELETE))
            {
                if (key.repeat == 0)
                    openProjectMenu(1);
                return true;
            }
            if (control && code == SDL_SCANCODE_S)
            {
                if (key.repeat == 0)
                    requestSave();
                return true;
            }
            if (code == SDL_SCANCODE_SPACE)
            {
                if (key.repeat == 0)
                    toggleTransport();
                return true;
            }
            if (code == SDL_SCANCODE_TAB)
            {
                if (key.repeat == 0)
                    changeView(shift ? -1 : 1);
                return true;
            }
            if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_5)
            {
                if (key.repeat == 0)
                {
                    cancelRange();
                    selectTrack(static_cast<int>(code - SDL_SCANCODE_1));
                }
                return true;
            }
            if (code == SDL_SCANCODE_PAGEUP || code == SDL_SCANCODE_PAGEDOWN)
            {
                selectedStep = wrapIndex(selectedStep + (code == SDL_SCANCODE_PAGEDOWN ? 1 : -1), kStepCount);
                return true;
            }
            if (code == SDL_SCANCODE_COMMA || code == SDL_SCANCODE_PERIOD)
            {
                if (alt)
                    rotateSteps(code == SDL_SCANCODE_PERIOD ? 1 : -1, control);
                else
                    adjustBpm(code == SDL_SCANCODE_PERIOD ? 1 : -1, shift);
                return true;
            }
            if (alt && (code == SDL_SCANCODE_LEFT || code == SDL_SCANCODE_RIGHT))
            {
                adjustTrackRate(code == SDL_SCANCODE_RIGHT ? 1 : -1);
                return true;
            }
            if (alt && (code == SDL_SCANCODE_UP || code == SDL_SCANCODE_DOWN))
            {
                adjustTrackShuffle(code == SDL_SCANCODE_UP ? 1 : -1, shift, control);
                return true;
            }

            if (code == SDL_SCANCODE_LEFT || code == SDL_SCANCODE_RIGHT ||
                code == SDL_SCANCODE_UP || code == SDL_SCANCODE_DOWN)
            {
                if (view == View::Grid)
                {
                    if (shift)
                    {
                        beginOrExtendRange(code == SDL_SCANCODE_LEFT ? -1 : (code == SDL_SCANCODE_RIGHT ? 1 : 0),
                                           code == SDL_SCANCODE_UP ? -1 : (code == SDL_SCANCODE_DOWN ? 1 : 0));
                    }
                    else
                    {
                        cancelRange();
                        if (code == SDL_SCANCODE_LEFT)
                            selectTrack(selectedTrack - 1);
                        else if (code == SDL_SCANCODE_RIGHT)
                            selectTrack(selectedTrack + 1);
                        else
                            selectedStep = wrapIndex(selectedStep +
                                                         (code == SDL_SCANCODE_DOWN ? 1 : -1),
                                                     kStepCount);
                    }
                }
                else if (view == View::Data)
                {
                    if (code == SDL_SCANCODE_LEFT || code == SDL_SCANCODE_RIGHT)
                    {
                        const int delta = code == SDL_SCANCODE_RIGHT ? 1 : -1;
                        if (dataWorkspace == DataWorkspace::Perform)
                            dataColumn = wrapIndex(std::max(0, dataColumn) + delta, 16);
                        else
                            dataColumn = wrapIndex(dataColumn + 2 + delta, 18) - 2;
                    }
                    else
                        dataBank = wrapIndex(dataBank + (code == SDL_SCANCODE_DOWN ? 1 : -1), 8);
                }
                else if (code == SDL_SCANCODE_LEFT || code == SDL_SCANCODE_RIGHT)
                {
                    editorIndex = wrapIndex(editorIndex + (code == SDL_SCANCODE_RIGHT ? 1 : -1), editorCount());
                }
                else
                {
                    adjustEditor(code == SDL_SCANCODE_UP ? 1 : -1, shift);
                }
                return true;
            }

            if (code == SDL_SCANCODE_LEFTBRACKET || code == SDL_SCANCODE_RIGHTBRACKET)
            {
                const int delta = code == SDL_SCANCODE_RIGHTBRACKET ? 1 : -1;
                if (view == View::Grid)
                    selectedParameter = wrapIndex(selectedParameter + delta, gridParamCount(selectedTrack));
                else if (view != View::Data)
                    editorIndex = wrapIndex(editorIndex + delta, editorCount());
                return true;
            }
            if (code == SDL_SCANCODE_MINUS || code == SDL_SCANCODE_EQUALS ||
                code == SDL_SCANCODE_KP_MINUS || code == SDL_SCANCODE_KP_PLUS)
            {
                const bool increase = code == SDL_SCANCODE_EQUALS || code == SDL_SCANCODE_KP_PLUS;
                adjustEditor(increase ? 1 : -1, shift);
                return true;
            }
            if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER)
            {
                if (key.repeat == 0)
                    activateEditor(shift);
                return true;
            }
            if (code == SDL_SCANCODE_DELETE || code == SDL_SCANCODE_BACKSPACE)
            {
                if (key.repeat == 0 && view == View::Grid)
                    clearStep();
                return true;
            }
            if (key.repeat != 0)
                return true;

            if (view == View::Synth && code == SDL_SCANCODE_B)
            {
                toggleSynthPerformanceMode();
                return true;
            }

            if (view == View::Data)
            {
                if (code == SDL_SCANCODE_V)
                {
                    toggleDataWorkspace();
                }
                else if (code == SDL_SCANCODE_X)
                {
                    runDataOperation(false);
                }
                else if (code == SDL_SCANCODE_R)
                {
                    runDataOperation(true);
                }
                else if (code == SDL_SCANCODE_A)
                {
                    dataAllTracks = !dataAllTracks;
                    toast(dataAllTracks ? "DATA WHOLE COLUMN" : "DATA CURRENT TRACK");
                }
                else if (code == SDL_SCANCODE_I)
                {
                    dataLoadMode = dataLoadMode == DataLoadMode::Reset
                                       ? DataLoadMode::InPlace
                                       : DataLoadMode::Reset;
                    toast(dataLoadMode == DataLoadMode::Reset ? "LOAD MODE RESET" : "LOAD MODE IN PLACE");
                }
                else if (code == SDL_SCANCODE_Q)
                {
                    activateData(false, true);
                }
                else if (code == SDL_SCANCODE_N)
                {
                    beginBankNameEdit();
                }
                else if (code == SDL_SCANCODE_E && dataWorkspace == DataWorkspace::Manage)
                {
                    beginPatternNameEdit();
                }
                else if (code == SDL_SCANCODE_C && dataWorkspace == DataWorkspace::Manage)
                {
                    cyclePatternColor(shift ? -1 : 1);
                }
                else if (code == SDL_SCANCODE_K)
                {
                    toggleCurrentBankLock();
                }
                else if (code == SDL_SCANCODE_B)
                {
                    if (control)
                    {
                        dataArmTempo = !dataArmTempo;
                        toast(dataArmTempo ? "BPM ARMED WITH NEXT CUE" : "BPM CUE DISARMED");
                    }
                    else
                        storeRecallBankSetting(true, shift, alt);
                }
                else if (code == SDL_SCANCODE_G)
                {
                    if (control)
                    {
                        dataArmScale = !dataArmScale;
                        toast(dataArmScale ? "SCALE ARMED WITH NEXT CUE" : "SCALE CUE DISARMED");
                    }
                    else
                        storeRecallBankSetting(false, shift, alt);
                }
                return true;
            }

            if (code == SDL_SCANCODE_L)
            {
                adjustTrackLength(shift ? -1 : 1);
            }
            else if (code == SDL_SCANCODE_D)
            {
                cycleDirection(1, shift || control);
            }
            else if (code == SDL_SCANCODE_M)
            {
                shift ? toggleSolo() : toggleMute();
            }
            else if (code == SDL_SCANCODE_U)
            {
                unmuteAll();
            }
            else if (code == SDL_SCANCODE_S)
            {
                toggleSnapshot();
            }
            else if (code == SDL_SCANCODE_R)
            {
                if (shift)
                    randomizeSelectedTrack();
                else if (view == View::Grid)
                    randomizeSelectedParameter();
                else if (view == View::Synth)
                    randomizeSynthParameter();
            }
            else if (code == SDL_SCANCODE_C)
            {
                copyStep();
            }
            else if (code == SDL_SCANCODE_X && view == View::Grid)
            {
                cutStep();
            }
            else if (code == SDL_SCANCODE_V)
            {
                pasteStep();
            }
            else if (code == SDL_SCANCODE_T && view == View::Grid)
            {
                toggleTrigless();
            }
            else if (code == SDL_SCANCODE_P)
            {
                openPalette();
            }
            else if (code == SDL_SCANCODE_O)
            {
                rotateSteps(shift ? -1 : 1, control);
            }
            return true;
        }

        bool logicalMouse(int mouseX, int mouseY, std::uint32_t windowId, float &x, float &y) const
        {
            double pixelX = static_cast<double>(mouseX);
            double pixelY = static_cast<double>(mouseY);
            if (SDL_Window *window = SDL_GetWindowFromID(windowId))
            {
                int windowWidth = 0;
                int windowHeight = 0;
                SDL_GetWindowSize(window, &windowWidth, &windowHeight);
                if (windowWidth > 0 && windowHeight > 0)
                {
                    pixelX *= static_cast<double>(outputWidth) / static_cast<double>(windowWidth);
                    pixelY *= static_cast<double>(outputHeight) / static_cast<double>(windowHeight);
                }
            }
            x = static_cast<float>((pixelX - static_cast<double>(viewportX)) /
                                   static_cast<double>(viewportScale));
            y = static_cast<float>((pixelY - static_cast<double>(viewportY)) /
                                   static_cast<double>(viewportScale));
            const float logicalWidth = static_cast<float>(
                kLogicalWidth + (wideHintInspector ? 342 : 0));
            return x >= 0.0f && x < logicalWidth &&
                   y >= 0.0f && y < static_cast<float>(kLogicalHeight);
        }

        void handleTrackSelectorClick(float x, float y)
        {
            if (y < 119.0f || y >= 153.0f || x < 868.0f || x >= 1253.0f)
                return;
            const int track = static_cast<int>((x - 868.0f) / 77.0f);
            selectTrack(track);
        }

        void clickGrid(float x, float y, std::uint8_t button, bool shift)
        {
            constexpr int left = 28;
            constexpr int columnWidth = 238;
            constexpr int gap = 8;
            constexpr int headerTop = 118;
            constexpr int gridTop = 202;
            constexpr int rowHeight = 25;
            if (x >= static_cast<float>(left) && x < 1252.0f)
            {
                const int stride = columnWidth + gap;
                const int column = static_cast<int>(x - static_cast<float>(left)) / stride;
                const int inside = static_cast<int>(x - static_cast<float>(left)) % stride;
                if (column >= 0 && column < kTrackCount && inside < columnWidth)
                {
                    if (y >= static_cast<float>(gridTop) && y < static_cast<float>(gridTop + rowHeight * 16))
                    {
                        const int step = clampInt(static_cast<int>(y - static_cast<float>(gridTop)) / rowHeight, 0, 15);
                        if (shift)
                        {
                            if (!rangeActive)
                            {
                                rangeActive = true;
                                rangeAnchorTrack = selectedTrack;
                                rangeAnchorStep = selectedStep;
                            }
                            selectTrack(column);
                            selectedStep = step;
                        }
                        else
                        {
                            cancelRange();
                            selectTrack(column);
                            selectedStep = step;
                        }
                        if (button == SDL_BUTTON_RIGHT)
                            toggleTrigless();
                        else
                            toggleStep();
                        return;
                    }
                    selectTrack(column);
                    if (y >= static_cast<float>(headerTop) && y < 198.0f)
                    {
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

            if (y >= 622.0f && y < 738.0f)
            {
                const bool synthRow = y >= 681.0f;
                const int stepCount = gridStepParamCount(selectedTrack);
                const int count = synthRow ? gridParamCount(selectedTrack) - stepCount : stepCount;
                if (count <= 0 || x < 112.0f || x >= 1252.0f)
                    return;
                const int width = 1140 / count;
                const int item = clampInt(static_cast<int>(x - 112.0f) / width, 0, count - 1);
                selectedParameter = synthRow ? stepCount + item : item;
            }
        }

        void clickSynth(float x, float y, std::uint8_t button)
        {
            if (y >= 121.0f && y < 154.0f && x >= 520.0f && x < 698.0f)
            {
                const bool wantsBasic = x < 609.0f;
                if (wantsBasic != synthPerformanceMode)
                    toggleSynthPerformanceMode();
                return;
            }
            if (selectedTrack == kTrackCount - 1)
                return;
            if (synthPerformanceMode)
            {
                if (x < 500.0f || x >= 1248.0f || y < 194.0f || y >= 626.0f)
                    return;
                const int column = static_cast<int>((x - 500.0f) / 188.0f);
                const int row = static_cast<int>((y - 194.0f) / 144.0f);
                if (column < 0 || column >= 4 || row < 0 || row >= 3)
                    return;
                editorIndex = row * 4 + column;
                synthMacroCursor = editorIndex;
                adjustSynth(button == SDL_BUTTON_RIGHT ? -1 : 1, false);
                return;
            }

            int parameter = -1;
            if (y >= 214.0f && y < 234.0f)
            {
                if (x >= 28.0f && x < 318.0f)
                    parameter = 0;
                else if (x >= 326.0f && x < 616.0f)
                    parameter = 1;
            }
            for (int op = 0; op < 4 && parameter < 0; ++op)
            {
                const int column = op % 2;
                const int row = op / 2;
                const float left = static_cast<float>(28 + column * 298);
                const float top = static_cast<float>(274 + row * 184);
                if (x >= left && x < left + 290.0f && y >= top && y < top + 5.0f * 27.0f)
                    parameter = 2 + op * 5 + clampInt(static_cast<int>((y - top) / 27.0f), 0, 4);
            }
            if (parameter < 0 && x >= 646.0f && x < 928.0f && y >= 216.0f && y < 324.0f)
                parameter = 22 + clampInt(static_cast<int>((y - 216.0f) / 27.0f), 0, 3);
            if (parameter < 0 && x >= 946.0f && x < 1252.0f && y >= 216.0f && y < 351.0f)
                parameter = 26 + clampInt(static_cast<int>((y - 216.0f) / 27.0f), 0, 4);
            if (parameter < 0 && x >= 646.0f && x < 928.0f && y >= 366.0f && y < 447.0f)
                parameter = 31 + clampInt(static_cast<int>((y - 366.0f) / 27.0f), 0, 2);
            if (parameter < 0 && x >= 646.0f && x < 1252.0f && y >= 501.0f && y < 609.0f)
            {
                const int slot = clampInt(static_cast<int>((x - 646.0f) / 151.0f), 0, 3);
                const int field = clampInt(static_cast<int>((y - 501.0f) / 27.0f), 0, 3);
                parameter = 34 + slot * 4 + field;
            }
            if (parameter < 0 || parameter >= kSynthParameterCount)
                return;
            editorIndex = parameter;
            adjustSynth(button == SDL_BUTTON_RIGHT ? -1 : 1, false);
        }

        void clickEcho(float x, float y, std::uint8_t button)
        {
            if (y < 335.0f || y >= 583.0f)
                return;
            const int column = x >= 640.0f ? 1 : 0;
            if (x < 35.0f || x >= 1245.0f)
                return;
            const int row = static_cast<int>(y - 335.0f) / 62;
            if (row < 0 || row >= 4)
                return;
            editorIndex = column * 4 + row;
            adjustEcho(button == SDL_BUTTON_RIGHT ? -1 : 1, false);
        }

        void clickTranspose(float x, float y, std::uint8_t button)
        {
            if (selectedTrack == kTrackCount - 1)
                return;
            if (x >= 58.0f && x < 1226.0f && y >= 190.0f && y < 450.0f)
            {
                const int index = clampInt(static_cast<int>((x - 58.0f) / 146.0f), 0, 7);
                editorIndex = index;
                if (button == SDL_BUTTON_RIGHT)
                {
                    std::lock_guard<std::mutex> lock(shared.mutex);
                    auto &app = shared.app;
                    app.tracks[static_cast<std::size_t>(selectedTrack)].transpose.values[static_cast<std::size_t>(index)] = 0;
                    bumpRevision(app);
                }
                else
                {
                    const int value = clampInt(static_cast<int>(std::lround((320.0f - y) / 5.0f)), -24, 24);
                    std::lock_guard<std::mutex> lock(shared.mutex);
                    auto &app = shared.app;
                    app.tracks[static_cast<std::size_t>(selectedTrack)].transpose.values[static_cast<std::size_t>(index)] = static_cast<std::int8_t>(value);
                    bumpRevision(app);
                }
                return;
            }
            if (y >= 494.0f && y < 576.0f && x >= 96.0f && x < 1184.0f)
            {
                editorIndex = 8 + clampInt(static_cast<int>((x - 96.0f) / 362.0f), 0, 2);
                adjustTranspose(button == SDL_BUTTON_RIGHT ? -1 : 1, false);
            }
        }

        void clickMod(float x, float y, std::uint8_t button)
        {
            if (x < 822.0f || x >= 1248.0f || y < 174.0f || y >= 570.0f)
                return;
            editorIndex = clampInt(static_cast<int>(y - 174.0f) / 66, 0, 5);
            adjustModulator(button == SDL_BUTTON_RIGHT ? -1 : 1, false);
        }

        void clickScale(float x, float y, std::uint8_t button)
        {
            if (x >= 46.0f && x < 1234.0f && y >= 242.0f && y < 420.0f)
            {
                const int degree = clampInt(static_cast<int>((x - 46.0f) / 99.0f), 0, 11);
                editorIndex = degree + 1;
                toggleScaleNote(degree);
                return;
            }
            if (x >= 46.0f && x < 318.0f && y >= 166.0f && y < 218.0f)
            {
                editorIndex = 0;
                adjustScale(button == SDL_BUTTON_RIGHT ? -1 : 1);
                return;
            }
            if (x >= 46.0f && x < 1234.0f && y >= 476.0f && y < 548.0f)
            {
                const int preset = clampInt(static_cast<int>((x - 46.0f) / 198.0f), 0, 5);
                editorIndex = 13 + preset;
                applyScalePreset(preset);
            }
        }

        void clickData(float x, float y, bool shift, bool cue)
        {
            if (y >= 121.0f && y < 154.0f && x >= 520.0f && x < 698.0f)
            {
                const bool wantsPerform = x < 609.0f;
                if (wantsPerform != (dataWorkspace == DataWorkspace::Perform))
                    toggleDataWorkspace();
                return;
            }
            if (x >= 28.0f && x < 1246.0f && y >= 180.0f && y < 234.0f)
            {
                activateDataRibbon(clampInt(static_cast<int>((x - 28.0f) / 174.0f), 0, 6));
                return;
            }
            if (dataWorkspace == DataWorkspace::Manage && y >= 576.0f && y < 628.0f)
            {
                if (x < 220.0f)
                    beginPatternNameEdit();
                else if (x < 430.0f)
                    cyclePatternColor(cue ? -1 : 1);
                return;
            }
            if (y < 260.0f || y >= 572.0f)
                return;
            if (dataWorkspace == DataWorkspace::Perform)
            {
                if (x < 188.0f || x >= 1228.0f)
                    return;
                dataColumn = clampInt(static_cast<int>((x - 188.0f) / 65.0f), 0, 15);
            }
            else
            {
                if (x < 72.0f || x >= 1206.0f)
                    return;
                const int visualColumn = clampInt(static_cast<int>((x - 72.0f) / 63.0f), 0, 17);
                dataColumn = visualColumn - 2;
            }
            dataBank = clampInt(static_cast<int>((y - 260.0f) / 39.0f), 0, 7);
            activateData(shift, cue);
        }

        void handleClick(float x, float y, std::uint8_t button, bool shift)
        {
            if (helpVisible)
            {
                helpVisible = false;
                return;
            }
            if (overlay == Overlay::None && onboardingVisible)
            {
                const bool compactDismiss =
                    x >= 1124.0f && x < 1234.0f && y >= 596.0f && y < 620.0f;
                const bool inspectorDismiss = wideHintInspector && !hintPanelVisible &&
                                              x >= 1494.0f && x < 1604.0f && y >= 286.0f && y < 310.0f;
                if (compactDismiss || inspectorDismiss)
                {
                    dismissOnboarding();
                    return;
                }
            }
            if (overlay == Overlay::CommandPalette)
            {
                if (x >= 318.0f && x < 962.0f && y >= 174.0f && y < 630.0f)
                {
                    commandCursor = clampInt(static_cast<int>((y - 174.0f) / 38.0f), 0,
                                             kCommandCount - 1);
                    executeCommand(commandCursor);
                }
                else
                    overlay = Overlay::None;
                return;
            }
            if (overlay == Overlay::Palette)
            {
                if (x >= 166.0f && x < 1110.0f && y >= 326.0f && y < 390.0f)
                {
                    paletteCursor = clampInt(static_cast<int>((x - 166.0f) / 59.0f), 0, kPaletteSize + 1);
                    paletteAction(shift, button == SDL_BUTTON_RIGHT);
                }
                else
                    overlay = Overlay::None;
                return;
            }
            if (overlay == Overlay::ControllerMap)
            {
                if (x >= 168.0f && x < 1112.0f && y >= 190.0f && y < 514.0f)
                {
                    const int column = x >= 640.0f ? 1 : 0;
                    const int row = clampInt(static_cast<int>((y - 190.0f) / 36.0f), 0, 8);
                    controllerMapCursor = column * 9 + row;
                    if (button == SDL_BUTTON_RIGHT)
                        unbindControllerAction();
                    else
                        controllerCapture = true;
                }
                else
                    overlay = Overlay::None;
                return;
            }
            if (overlay == Overlay::ProjectName)
            {
                if (x >= 414.0f && x < 866.0f && y >= 462.0f && y < 506.0f)
                    submitProjectName();
                else if (x >= 414.0f && x < 866.0f && y >= 512.0f && y < 556.0f)
                {
                    overlay = Overlay::None;
                    SDL_StopTextInput();
                }
                return;
            }
            if (overlay == Overlay::PatternName)
            {
                if (x >= 414.0f && x < 866.0f && y >= 462.0f && y < 506.0f)
                    commitPatternNameEdit();
                else if (x >= 414.0f && x < 866.0f && y >= 512.0f && y < 556.0f)
                {
                    overlay = Overlay::None;
                    SDL_StopTextInput();
                }
                return;
            }
            if (overlay == Overlay::ProjectBrowser)
            {
                if (x >= 344.0f && x < 936.0f && y >= 218.0f && y < 554.0f && !projectPaths.empty())
                {
                    const int index = clampInt(static_cast<int>((y - 218.0f) / 42.0f), 0,
                                               static_cast<int>(projectPaths.size()) - 1);
                    const bool changed = index != projectBrowserCursor;
                    projectBrowserCursor = index;
                    if (button == SDL_BUTTON_RIGHT)
                        projectBrowserArmed = false;
                    else if (!changed && projectBrowserArmed)
                        submitProjectOpen();
                    else
                    {
                        projectBrowserArmed = true;
                        toast("CLICK AGAIN TO CONFIRM OPEN");
                    }
                }
                else
                {
                    overlay = Overlay::None;
                    projectBrowserArmed = false;
                }
                return;
            }
            if (overlay == Overlay::ProjectMenu)
            {
                if (x >= 346.0f && x < 934.0f && y >= 202.0f && y < 552.0f)
                {
                    const int action = clampInt(static_cast<int>((y - 202.0f) / 70.0f), 0, 4);
                    const bool actionChanged = action != projectActionCursor;
                    if (button == SDL_BUTTON_RIGHT)
                    {
                        projectActionCursor = action;
                        projectActionArmed = false;
                    }
                    else if (action >= 2)
                    {
                        projectActionCursor = action;
                        activateProjectAction();
                    }
                    else if (!actionChanged && projectActionArmed)
                    {
                        activateProjectAction();
                    }
                    else
                    {
                        projectActionCursor = action;
                        projectActionArmed = true;
                        toast(action == 0 ? "CLICK AGAIN TO CONFIRM NEW SESSION"
                                          : "CLICK AGAIN TO CONFIRM CLEAR TRACKS");
                    }
                }
                else
                {
                    overlay = Overlay::None;
                    projectActionArmed = false;
                }
                return;
            }
            if (overlay == Overlay::BankName)
            {
                if (x >= 490.0f && x < 790.0f && y >= 390.0f && y < 450.0f)
                    commitBankNameEdit();
                return;
            }
            if (y >= 15.0f && y < 57.0f && x >= 468.0f && x < 590.0f)
            {
                openProjectMenu();
                return;
            }
            if (y >= 15.0f && y < 57.0f && x >= 606.0f && x < 790.0f)
            {
                toggleTransport();
                return;
            }
            if (y >= 15.0f && y < 57.0f && x >= 800.0f && x < 925.0f)
            {
                adjustBpm(button == SDL_BUTTON_RIGHT ? -1 : 1, shift);
                return;
            }
            if (y >= 68.0f && y < 108.0f && x >= 1080.0f && x < 1252.0f)
            {
                hintPanelVisible = !hintPanelVisible;
                toast(hintPanelVisible ? "CONTEXT HINTS ON" : "CONTEXT HINTS OFF");
                return;
            }
            if (y >= 68.0f && y < 108.0f && x >= 28.0f && x < 812.0f)
            {
                const int tab = static_cast<int>((x - 28.0f) / 112.0f);
                if (tab >= 0 && tab < static_cast<int>(kViewNames.size()))
                {
                    view = static_cast<View>(tab);
                    editorIndex = 0;
                    return;
                }
            }
            if (view != View::Grid)
                handleTrackSelectorClick(x, y);
            switch (view)
            {
            case View::Grid:
                clickGrid(x, y, button, shift);
                break;
            case View::Synth:
                clickSynth(x, y, button);
                break;
            case View::Echo:
                clickEcho(x, y, button);
                break;
            case View::Transpose:
                clickTranspose(x, y, button);
                break;
            case View::Mod:
                clickMod(x, y, button);
                break;
            case View::Scale:
                clickScale(x, y, button);
                break;
            case View::Data:
                clickData(x, y, shift, button == SDL_BUTTON_RIGHT);
                break;
            }
        }

        bool dispatchEvent(const SDL_Event &event)
        {
            if (event.type == SDL_DROPFILE)
            {
                const char *rawPath = event.drop.file;
                const std::string path = rawPath ? std::string(rawPath) : std::string{};
                if (event.drop.file)
                    SDL_free(event.drop.file);
                if (!path.empty())
                {
                    projectRequest = ProjectRequest{ProjectRequestKind::Open, path};
                    overlay = Overlay::None;
                    projectActionArmed = false;
                    projectBrowserArmed = false;
                    toast("OPENING DROPPED PROJECT " + projectPathLabel(path));
                }
                else
                {
                    toast("DROPPED PROJECT PATH IS EMPTY", true);
                }
                return true;
            }
            if (event.type == SDL_KEYDOWN)
                return handleKey(event.key);
            if (event.type == SDL_TEXTINPUT && overlay == Overlay::ProjectName)
            {
                for (const unsigned char raw : std::string_view(event.text.text))
                {
                    if (!((raw >= 'A' && raw <= 'Z') || (raw >= 'a' && raw <= 'z') ||
                          (raw >= '0' && raw <= '9') || raw == ' ' || raw == '_' || raw == '-'))
                        continue;
                    if (projectNameEdit.size() >= 40u)
                        break;
                    projectNameEdit.push_back(static_cast<char>(raw));
                }
                return true;
            }
            if (event.type == SDL_TEXTINPUT && overlay == Overlay::PatternName)
            {
                for (const unsigned char raw : std::string_view(event.text.text))
                {
                    if (raw < 32u || raw > 126u || patternNameEdit.size() >= kPatternMetadataNameLength)
                        continue;
                    patternNameEdit.push_back(static_cast<char>(raw));
                }
                return true;
            }
            if (event.type == SDL_TEXTINPUT && overlay == Overlay::BankName)
            {
                for (const unsigned char raw : std::string_view(event.text.text))
                {
                    if (raw < 32u || raw > 126u)
                        continue;
                    char value = static_cast<char>(raw);
                    if (value >= 'a' && value <= 'z')
                        value = static_cast<char>(value - 'a' + 'A');
                    bankNameEdit[static_cast<std::size_t>(bankNameCursor)] = value;
                    bankNameCursor = wrapIndex(bankNameCursor + 1, 4);
                }
                bankNameEdit[4] = '\0';
                return true;
            }
            if (event.type == SDL_CONTROLLERDEVICEADDED)
            {
                openController(event.cdevice.which);
                return true;
            }
            if (event.type == SDL_CONTROLLERDEVICEREMOVED)
            {
                closeController(event.cdevice.which);
                return true;
            }
            if (event.type == SDL_CONTROLLERBUTTONDOWN)
                return handleControllerButton(event.cbutton, true);
            if (event.type == SDL_CONTROLLERBUTTONUP)
                return handleControllerButton(event.cbutton, false);
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_LEAVE)
            {
                hoverActive = false;
                hoverSeconds = 0.0;
                return true;
            }
            if (event.type == SDL_MOUSEMOTION)
            {
                float x = 0.0f;
                float y = 0.0f;
                if (logicalMouse(event.motion.x, event.motion.y, event.motion.windowID, x, y))
                {
                    if (!hoverActive || std::abs(x - hoverX) > 2.0f || std::abs(y - hoverY) > 2.0f)
                        hoverSeconds = 0.0;
                    hoverX = x;
                    hoverY = y;
                    hoverActive = true;
                }
                else
                {
                    hoverActive = false;
                    hoverSeconds = 0.0;
                }
                return true;
            }
            if (event.type == SDL_MOUSEBUTTONDOWN)
            {
                hoverSeconds = 0.0;
                float x = 0.0f;
                float y = 0.0f;
                if (logicalMouse(event.button.x, event.button.y, event.button.windowID, x, y))
                {
                    handleClick(x, y, event.button.button, (SDL_GetModState() & KMOD_SHIFT) != 0);
                }
            }
            else if (event.type == SDL_MOUSEWHEEL)
            {
                int mouseX = 0;
                int mouseY = 0;
                SDL_GetMouseState(&mouseX, &mouseY);
                float x = 0.0f;
                float y = 0.0f;
                const int raw = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
                if (logicalMouse(mouseX, mouseY, event.wheel.windowID, x, y) && raw != 0)
                {
                    const int direction = raw > 0 ? 1 : -1;
                    const bool coarse = (SDL_GetModState() & KMOD_SHIFT) != 0;
                    if (y >= 15.0f && y < 57.0f && x >= 800.0f && x < 925.0f)
                        adjustBpm(direction, coarse);
                    else if (view == View::Grid && y >= 118.0f && y < 198.0f)
                    {
                        constexpr int stride = 246;
                        const int track = clampInt(static_cast<int>(x - 28.0f) / stride, 0, 4);
                        selectTrack(track);
                        const float localX = x - static_cast<float>(28 + track * stride);
                        if (y >= 151.0f && y < 174.0f)
                        {
                            if (localX < 119.0f)
                                adjustTrackRate(direction);
                            else
                                adjustTrackLength(direction);
                        }
                        else if (y >= 174.0f)
                        {
                            if (localX < 119.0f)
                                cycleDirection(direction);
                            else
                                adjustTrackShuffle(direction, coarse);
                        }
                    }
                    else if (view == View::Synth && synthPerformanceMode &&
                             x >= 500.0f && x < 1248.0f &&
                             y >= 194.0f && y < 626.0f)
                    {
                        const int column = clampInt(static_cast<int>((x - 500.0f) / 188.0f), 0, 3);
                        const int row = clampInt(static_cast<int>((y - 194.0f) / 144.0f), 0, 2);
                        editorIndex = row * 4 + column;
                        synthMacroCursor = editorIndex;
                        adjustSynth(direction, coarse);
                    }
                    else
                    {
                        adjustEditor(direction, coarse);
                    }
                }
            }
            return true;
        }

        bool handleEvent(const SDL_Event &event)
        {
            const bool mayMutateState =
                event.type == SDL_KEYDOWN || event.type == SDL_MOUSEBUTTONDOWN ||
                event.type == SDL_MOUSEWHEEL || event.type == SDL_CONTROLLERBUTTONDOWN;
            UiMutationGuard mutationGuard(shared, mayMutateState);
            historyRecordedThisEvent = false;
            if (mayMutateState)
            {
                // The gate plus mutex fence makes every settlement either
                // wholly older or wholly newer than this UI action. Commit
                // older receipts to history before taking the action's own
                // inverse snapshot.
                transport = audio.status();
                finalizeAsyncHistory();
                drainAppliedCancellationUndos();
            }
            std::shared_ptr<AppState> before;
            if (shouldCaptureHistory(event))
                before = stateSnapshot();
            const bool keepRunning = dispatchEvent(event);
            if (before)
            {
                const auto after = stateSnapshot();
                if (after->editRevision != before->editRevision)
                {
                    if (!historyRecordedThisEvent)
                        recordHistory(std::move(before));
                }
            }
            historyRecordedThisEvent = false;
            return keepRunning;
        }

        void update(double deltaSeconds)
        {
            elapsed += deltaSeconds;
            toastSeconds = std::max(0.0, toastSeconds - deltaSeconds);
            shutterSeconds = std::max(0.0, shutterSeconds - deltaSeconds);
            if (hoverActive)
                hoverSeconds += deltaSeconds;
            if (destructiveEditArmed && elapsed > destructiveEditDeadline)
            {
                destructiveEditArmed = false;
                destructiveEditAction.clear();
            }
            {
                const bool reconcileAsync = !pendingAsyncHistory.empty() ||
                                            !autoUndoAppliedCancellations.empty();
                UiMutationGuard mutationGuard(shared, reconcileAsync);
                transport = audio.status();
                finalizeAsyncHistory();
                drainAppliedCancellationUndos();
            }
            if (queuedGlobalGeneration != 0 &&
                transport.appliedGlobalSettingsGeneration == queuedGlobalGeneration)
            {
                queuedGlobalGeneration = 0;
            }
            for (int track = 0; track < kTrackCount; ++track)
            {
                const std::size_t index = static_cast<std::size_t>(track);
                const int pattern = queuedPattern[index];
                if (pattern < 0)
                    continue;
                if (queueGeneration[index] != 0 &&
                    transport.appliedPatternGenerations[index] == queueGeneration[index])
                {
                    queuedPattern[index] = -1;
                    queueGeneration[index] = 0;
                    toast("PATTERN " + hexValue(pattern) + " LOADED ON LOOP");
                }
            }
        }

        void drawHeader(SDL_Renderer *renderer, const AppState &app, const Palette &palette) const
        {
            fillRect(renderer, 0, 0, kLogicalWidth, 108, palette.surface);
            fillRect(renderer, 28, 17, 3, 34, palette.accent);
            drawText(renderer, 42, 15, "FMS", palette.text, 4);
            drawText(renderer, 125, 18, "NATIVE FM STEP SEQUENCER", palette.muted, 2);
            drawText(renderer, 125, 39, "HYBRID 2/4-OP FM / NOISE / 16 STEPS", palette.faint, 1);

            const int scopedCells = editCellCount();
            const bool broadScope = editScope != EditScope::Selection || rangeActive;
            fillRect(renderer, 330, 15, 122, 42, broadScope ? palette.accentDim : palette.raised);
            strokeRect(renderer, 330, 15, 122, 42, broadScope ? palette.accent : palette.lineStrong);
            drawText(renderer, 342, 22, "EDIT SCOPE", palette.muted, 1);
            drawTextRight(renderer, 440, 36,
                          std::string(scopeName()) + " / " + decimalValue(scopedCells),
                          broadScope ? palette.accent : palette.text, 1);

            fillRect(renderer, 468, 15, 122, 42, palette.raised);
            strokeRect(renderer, 468, 15, 122, 42, palette.lineStrong);
            drawTextCentered(renderer, 529, 21, "PROJECT", palette.muted, 1);
            std::string compactProject = projectLabel();
            if (compactProject.size() > 13u)
                compactProject.resize(13u);
            drawTextCentered(renderer, 529, 38, compactProject,
                             isDirty() ? palette.accent : palette.text, 1);

            const bool running = transport.running;
            fillRect(renderer, 606, 15, 184, 42, running ? palette.accentDim : palette.raised);
            strokeRect(renderer, 606, 15, 184, 42, running ? palette.accent : palette.lineStrong);
            if (running)
            {
                fillRect(renderer, 621, 27, 4, 18, palette.accent);
                fillRect(renderer, 630, 27, 4, 18, palette.accent);
            }
            else
            {
                const SDL_Point points[4]{{621, 26}, {621, 46}, {637, 36}, {621, 26}};
                setColor(renderer, palette.text);
                SDL_RenderDrawLines(renderer, points, 4);
            }
            drawText(renderer, 650, 28, running ? "RUNNING" : "STOPPED", running ? palette.accent : palette.text, 2);

            fillRect(renderer, 800, 15, 125, 42, palette.raised);
            drawText(renderer, 812, 21, "BPM", palette.muted, 1);
            drawTextRight(renderer, 912, 25, decimalValue(app.bpm), palette.text, 3);

            const auto &sounds = lastPaletteNoise ? app.noisePalette : app.fmPalette;
            const int paletteReady = static_cast<int>(std::count_if(
                sounds.begin(), sounds.end(), [](const Step &sound)
                { return sound.active; }));
            const std::string snapshotBadge = snapshot.has_value()
                                                  ? (snapshotSide ? "S-B" : "S-A")
                                                  : "S--";
            const std::string paletteSlot = paletteCursor == 0
                                                ? "C"
                                                : (paletteCursor == 1
                                                       ? "R"
                                                       : hexValue(paletteCursor - 2, 1));
            const std::string paletteBadge =
                std::string("P") + (lastPaletteNoise ? "N" : "F") + paletteSlot;
            drawText(renderer, 938, 18,
                     snapshotBadge + " " + paletteBadge + " " + paddedDecimal(paletteReady),
                     snapshot.has_value() || paletteReady > 0 ? palette.accent : palette.muted, 1);
            std::string clipboardBadge = "C--";
            if (stepClipboard.has_value())
                clipboardBadge = "C1";
            else if (!rangeClipboard.empty())
                clipboardBadge = "C" + decimalValue(rangeClipboard.tracks) + "X" +
                                 decimalValue(rangeClipboard.steps);
            drawText(renderer, 938, 38,
                     clipboardBadge + (audio.available() ? " AOK" : " AOFF"),
                     audio.available() ? palette.text : palette.muted, 1);

            drawText(renderer, 1052, 16, "L", palette.muted, 1);
            drawText(renderer, 1052, 36, "R", palette.muted, 1);
            drawLine(renderer, 1068, 21, 1248, 21, palette.line);
            drawLine(renderer, 1068, 41, 1248, 41, palette.line);
            fillRect(renderer, 1068, 18, static_cast<int>(std::lround(180.0f * std::clamp(transport.peakLeft, 0.0f, 1.0f))), 7, palette.accent);
            fillRect(renderer, 1068, 38, static_cast<int>(std::lround(180.0f * std::clamp(transport.peakRight, 0.0f, 1.0f))), 7, palette.accent);

            for (int i = 0; i < static_cast<int>(kViewNames.size()); ++i)
            {
                const int x = 28 + i * 112;
                const bool active = static_cast<int>(view) == i;
                if (active)
                    fillRect(renderer, x, 68, 108, 40, palette.raised);
                drawTextCentered(renderer, x + 54, 81, kViewNames[static_cast<std::size_t>(i)],
                                 active ? palette.accent : palette.muted, 2);
                if (active)
                    fillRect(renderer, x, 105, 108, 3, palette.accent);
            }
            drawTextRight(renderer, 1064, 80, "TAB VIEW   CTRL+K COMMANDS", palette.muted, 1);
            fillRect(renderer, 1080, 73, 172, 28, hintPanelVisible ? palette.accentDim : palette.raised);
            strokeRect(renderer, 1080, 73, 172, 28, hintPanelVisible ? palette.accent : palette.lineStrong);
            drawTextCentered(renderer, 1166, 83, hintPanelVisible ? "F6 HINTS ON" : "F6 HINTS",
                             hintPanelVisible ? palette.accent : palette.muted, 1);
            const auto &selectedStepData = app.tracks[static_cast<std::size_t>(selectedTrack)]
                                               .steps[static_cast<std::size_t>(selectedStep)];
            int firstQueued = -1;
            for (int track = 0; track < kTrackCount; ++track)
                if (queuedPattern[static_cast<std::size_t>(track)] >= 0)
                {
                    firstQueued = queuedPattern[static_cast<std::size_t>(track)];
                    break;
                }
            std::string muteState = "M- S-";
            for (int track = 0; track < kTrackCount; ++track)
            {
                if (app.tracks[static_cast<std::size_t>(track)].muted)
                    muteState[1] = track == kTrackCount - 1 ? 'N' : static_cast<char>('1' + track);
                if (app.tracks[static_cast<std::size_t>(track)].solo)
                    muteState[4] = track == kTrackCount - 1 ? 'N' : static_cast<char>('1' + track);
            }
            const std::string flowState = "TRK " +
                                          (selectedTrack == kTrackCount - 1 ? std::string("NOISE")
                                                                            : "FM " + decimalValue(selectedTrack + 1)) +
                                          "  STEP " + hexValue(selectedStep, 1) + "  " +
                                          (selectedTrack == kTrackCount - 1 ? "PSG" : (selectedStepData.advancedFm.enabled ? "4-OP" : "LEGACY")) +
                                          "  Q " + (firstQueued >= 0 ? hexValue(firstQueued) : "--") +
                                          "  " + muteState + (gridCompareValues ? "  COMPARE" : "");
            drawText(renderer, 28, 59, flowState, palette.text, 1);
            std::string saveState;
            if (isDirty())
                saveState = "DIRTY";
            else if (!savedMomentKnown)
                saveState = "SAVED START";
            else
            {
                const int age = std::max(0, static_cast<int>(elapsed - lastSavedElapsed));
                saveState = age < 60 ? "SAVED " + decimalValue(age) + "S AGO"
                                     : "SAVED " + decimalValue(age / 60) + "M AGO";
            }
            const std::string historyState = "UNDO " + decimalValue(static_cast<int>(undoHistory.size())) +
                                             "  REDO " + decimalValue(static_cast<int>(redoHistory.size())) +
                                             "  " + saveState;
            drawTextRight(renderer, 1252, 59, historyState, isDirty() ? palette.accent : palette.muted, 1);
            drawLine(renderer, 0, 107, kLogicalWidth, 107, palette.lineStrong);
        }

        void drawTrackSelector(SDL_Renderer *renderer, const Palette &palette) const
        {
            drawText(renderer, 735, 129, "EDIT TRACK", palette.muted, 1);
            for (int track = 0; track < kTrackCount; ++track)
            {
                const int x = 868 + track * 77;
                const bool selected = track == selectedTrack;
                drawTextCentered(renderer, x + 34, 126, track == 4 ? "N" : decimalValue(track + 1),
                                 selected ? palette.accent : palette.muted, 2);
                drawLine(renderer, x + 4, 150, x + 65, 150, selected ? palette.accent : palette.line);
            }
        }

        void drawSectionTitle(SDL_Renderer *renderer, const Palette &palette, std::string_view title,
                              std::string_view description, bool showTracks = true) const
        {
            drawText(renderer, 28, 124, title, palette.text, 3);
            drawText(renderer, 29, 153, description, palette.muted, 1);
            if (showTracks)
                drawTrackSelector(renderer, palette);
        }

        void drawGrid(SDL_Renderer *renderer, const AppState &app, const Palette &palette) const
        {
            constexpr int left = 28;
            constexpr int columnWidth = 238;
            constexpr int gap = 8;
            constexpr int gridTop = 202;
            constexpr int rowHeight = 25;
            const GridParam parameter = gridParamItem(selectedTrack, selectedParameter).id;

            for (int trackIndex = 0; trackIndex < kTrackCount; ++trackIndex)
            {
                const int x = left + trackIndex * (columnWidth + gap);
                const auto &track = app.tracks[static_cast<std::size_t>(trackIndex)];
                const bool selected = trackIndex == selectedTrack;
                if (trackIndex > 0)
                    drawLine(renderer, x - gap / 2, 118, x - gap / 2, 606, palette.line);
                if (selected)
                    fillRect(renderer, x, 118, columnWidth, 3, palette.accent);
                drawText(renderer, x + 6, 128, trackIndex == 4 ? "PSG NOISE" : "FM " + decimalValue(trackIndex + 1),
                         selected ? palette.text : palette.muted, 2);
                if (track.muted)
                    drawTextRight(renderer, x + columnWidth - 6, 128, "MUTE", palette.accent, 1);
                else if (track.solo)
                    drawTextRight(renderer, x + columnWidth - 6, 128, "SOLO", palette.accent, 1);

                drawText(renderer, x + 6, 154, "RATE", palette.faint, 1);
                drawText(renderer, x + 43, 154, rateName(track.rateIndex), palette.text, 1);
                drawText(renderer, x + 126, 154, "LEN", palette.faint, 1);
                drawTextRight(renderer, x + columnWidth - 7, 154, decimalValue(track.length), palette.text, 1);
                drawText(renderer, x + 6, 177, "DIR", palette.faint, 1);
                drawText(renderer, x + 43, 177, directionName(track.direction), palette.text, 1);
                drawText(renderer, x + 126, 177, "SHF", palette.faint, 1);
                drawTextRight(renderer, x + columnWidth - 7, 177, decimalValue(track.shuffle), palette.text, 1);
                drawLine(renderer, x, 196, x + columnWidth, 196, selected ? palette.lineStrong : palette.line);

                for (int stepIndex = 0; stepIndex < kStepCount; ++stepIndex)
                {
                    const int y = gridTop + stepIndex * rowHeight;
                    const auto &step = track.steps[static_cast<std::size_t>(stepIndex)];
                    const bool cursor = selected && stepIndex == selectedStep;
                    const bool inSelection = selectedCell(trackIndex, stepIndex);
                    const bool playhead = transport.playheads[static_cast<std::size_t>(trackIndex)] == stepIndex;
                    const bool inLength = stepIndex < static_cast<int>(track.length);
                    if (playhead)
                        fillRect(renderer, x, y, columnWidth, rowHeight - 1, palette.accentDim);
                    if (inSelection)
                        fillRect(renderer, x, y, columnWidth, rowHeight - 1,
                                 rangeActive ? palette.accentDim : palette.raised);
                    if (!inLength)
                        fillRect(renderer, x, y, columnWidth, rowHeight - 1,
                                 {palette.background.r, palette.background.g, palette.background.b, 170});
                    if ((stepIndex + 1) % 4 == 0)
                        drawLine(renderer, x, y + rowHeight - 1, x + columnWidth, y + rowHeight - 1, palette.lineStrong);
                    else
                        drawLine(renderer, x + 4, y + rowHeight - 1, x + columnWidth, y + rowHeight - 1, palette.line);

                    if (playhead)
                        fillRect(renderer, x, y + 2, 3, rowHeight - 5, palette.accent);
                    drawText(renderer, x + 8, y + 8, hexValue(stepIndex, 1), inLength ? palette.faint : palette.lineStrong, 1);
                    if (step.active)
                    {
                        if (step.trigless)
                            strokeRect(renderer, x + 30, y + 8, 8, 8, palette.accent);
                        else
                            fillRect(renderer, x + 31, y + 9, 7, 7, palette.accent);
                    }
                    else
                    {
                        drawLine(renderer, x + 31, y + 12, x + 37, y + 12, palette.faint);
                    }

                    const int comparedParameter = gridCompareValues
                                                      ? gridParamIndex(trackIndex, parameter)
                                                      : -1;
                    const GridParam shown = comparedParameter >= 0
                                                ? parameter
                                                : (selected ? parameter
                                                            : (trackIndex == kTrackCount - 1
                                                                   ? GridParam::NoiseRate
                                                                   : GridParam::Note));
                    const Color valueColor = step.active && inLength ? palette.text : palette.muted;
                    const char *shownLabel = comparedParameter >= 0
                                                 ? gridParamItem(trackIndex, comparedParameter).shortName
                                                 : (selected
                                                        ? gridParamItem(trackIndex, clampInt(selectedParameter, 0,
                                                                                             gridParamCount(trackIndex) - 1))
                                                              .shortName
                                                        : (trackIndex == kTrackCount - 1 ? "RATE" : "NOTE"));
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

        void drawGridParameterStrip(SDL_Renderer *renderer, const AppState &app,
                                    const Palette &palette) const
        {
            const auto &step = app.tracks[static_cast<std::size_t>(selectedTrack)]
                                   .steps[static_cast<std::size_t>(selectedStep)];
            const int stepCount = gridStepParamCount(selectedTrack);
            const int totalCount = gridParamCount(selectedTrack);
            fillRect(renderer, 0, 614, kLogicalWidth, 146, palette.surface);
            drawLine(renderer, 0, 614, kLogicalWidth, 614, palette.lineStrong);
            drawText(renderer, 28, 630, "STEP", palette.muted, 1);
            drawText(renderer, 28, 645, hexValue(selectedStep, 1), palette.text, 3);
            drawText(renderer, 70, 646, selectedTrack == 4 ? "NOISE" : "FM" + decimalValue(selectedTrack + 1), palette.faint, 1);

            const auto drawRow = [&](int first, int count, int y, const char *label)
            {
                drawText(renderer, 28, y + 13, label, palette.muted, 1);
                const int itemWidth = 1140 / count;
                for (int i = 0; i < count; ++i)
                {
                    const int parameterIndex = first + i;
                    const int x = 112 + i * itemWidth;
                    const bool selected = parameterIndex == selectedParameter;
                    const auto &item = gridParamItem(selectedTrack, parameterIndex);
                    if (selected)
                        fillRect(renderer, x, y, itemWidth - 3, 48, palette.accentDim);
                    drawText(renderer, x + 5, y + 7, item.shortName,
                             selected ? palette.accent : palette.muted, 1);
                    drawText(renderer, x + 5, y + 24, gridValue(step, item.id, selectedTrack == 4),
                             selected ? palette.text : palette.faint, 1);
                    if (selected)
                        fillRect(renderer, x, y + 46, itemWidth - 3, 2, palette.accent);
                }
            };
            drawRow(0, stepCount, 622, "STEP");
            drawRow(stepCount, totalCount - stepCount, 681, selectedTrack == 4 ? "PSG" : "SYNTH");
            drawText(renderer, 28, 748, std::string("F5 SCOPE ") + scopeName() + (gridCompareValues ? "   F9 COMPARE" : ""), palette.accent, 1);
            drawTextRight(renderer, 1252, 748,
                          "SHIFT+ARROWS RANGE   C/X/V COPY CUT PASTE   P PALETTE   R NUDGE",
                          palette.muted, 1);
        }

        void drawAlgorithmDiagram(SDL_Renderer *renderer, const AdvancedFmPatch &patch,
                                  const Palette &palette, int x, int y, int width,
                                  int height) const
        {
            fillRect(renderer, x, y, width, height, palette.surface);
            strokeRect(renderer, x, y, width, height, palette.lineStrong);
            fillRect(renderer, x, y, 3, height, palette.accent);
            drawText(renderer, x + 18, y + 16, "FM ROUTING", palette.muted, 1);
            drawTextRight(renderer, x + width - 18, y + 11,
                          "ALG " + paddedDecimal(static_cast<int>(patch.algorithm) + 1),
                          palette.text, 2);

            const int nodeWidth = 62;
            const int nodeHeight = 38;
            const std::array<SDL_Point, 4> nodes{{{x + 48, y + height - 76}, {x + 48, y + height - 152}, {x + 184, y + height - 152}, {x + 184, y + height - 228}}};
            const int railX = x + width - 72;
            const int outputY = y + height - 58;
            drawLine(renderer, railX, y + 58, railX, outputY, palette.lineStrong);
            drawText(renderer, railX + 13, outputY - 3, "OUT", palette.accent, 1);

            const auto center = [&](int op)
            {
                const SDL_Point &point = nodes[static_cast<std::size_t>(op)];
                return SDL_Point{point.x + nodeWidth / 2, point.y + nodeHeight / 2};
            };
            const auto edge = [&](int from, int to)
            {
                const SDL_Point a = center(from);
                const SDL_Point b = center(to);
                drawLine(renderer, a.x, a.y, b.x, b.y, palette.muted);
                fillRect(renderer, b.x - 2, b.y - 2, 5, 5, palette.accent);
            };
            const auto carrier = [&](int op)
            {
                const SDL_Point a = center(op);
                drawLine(renderer, a.x, a.y, railX, a.y, palette.accent);
                fillRect(renderer, railX - 2, a.y - 2, 5, 5, palette.accent);
            };

            const auto &topology = advancedFmAlgorithmTopology(patch.algorithm);
            for (std::uint8_t index = 0; index < topology.modulationEdgeCount; ++index)
            {
                const auto &routing = topology.modulationEdges[static_cast<std::size_t>(index)];
                edge(static_cast<int>(routing.source),
                     static_cast<int>(routing.destination));
            }
            for (int op = 0; op < 4; ++op)
                if ((topology.carrierMask & static_cast<std::uint8_t>(1u << op)) != 0u)
                    carrier(op);
            for (int op = 0; op < 4; ++op)
            {
                const SDL_Point &point = nodes[static_cast<std::size_t>(op)];
                const bool isCarrier =
                    (topology.carrierMask & static_cast<std::uint8_t>(1u << op)) != 0u;
                fillRect(renderer, point.x, point.y, nodeWidth, nodeHeight,
                         isCarrier ? palette.accentDim : palette.raised);
                strokeRect(renderer, point.x, point.y, nodeWidth, nodeHeight,
                           isCarrier ? palette.accent : palette.lineStrong);
                drawTextCentered(renderer, point.x + nodeWidth / 2, point.y + 11,
                                 "OP" + decimalValue(op + 1),
                                 isCarrier ? palette.accent : palette.text, 1);
            }
            drawText(renderer, x + 18, y + height - 24,
                     "LINES MODULATE   ACCENTED OPS REACH OUTPUT", palette.faint, 1);
        }

        void drawSynthMacro(SDL_Renderer *renderer, const AdvancedFmPatch &patch,
                            const Palette &palette, int macro, int x, int y,
                            int width, int height) const
        {
            const bool selected = editorIndex == macro;
            if (selected)
                fillRect(renderer, x, y, width, height, palette.accentDim);
            strokeRect(renderer, x, y, width, height,
                       selected ? palette.accent : palette.lineStrong);
            if (selected)
                fillRect(renderer, x, y, 3, height, palette.accent);
            drawText(renderer, x + 12, y + 11,
                     kSynthMacroNames[static_cast<std::size_t>(macro)],
                     selected ? palette.accent : palette.muted, 1);

            const float unit = std::clamp(synthMacroUnit(patch, macro), 0.0f, 1.0f);
            const int centerX = x + 38;
            const int centerY = y + 64;
            constexpr double pi = 3.14159265358979323846;
            for (int segment = 0; segment < 20; ++segment)
            {
                const double a0 = static_cast<double>(segment) * 2.0 * pi / 20.0;
                const double a1 = static_cast<double>(segment + 1) * 2.0 * pi / 20.0;
                drawLine(renderer,
                         centerX + static_cast<int>(std::lround(std::cos(a0) * 23.0)),
                         centerY + static_cast<int>(std::lround(std::sin(a0) * 23.0)),
                         centerX + static_cast<int>(std::lround(std::cos(a1) * 23.0)),
                         centerY + static_cast<int>(std::lround(std::sin(a1) * 23.0)),
                         selected ? palette.accent : palette.lineStrong);
            }
            const double angle = (-0.75 + 1.5 * static_cast<double>(unit)) * pi;
            drawLine(renderer, centerX, centerY,
                     centerX + static_cast<int>(std::lround(std::cos(angle) * 17.0)),
                     centerY + static_cast<int>(std::lround(std::sin(angle) * 17.0)),
                     selected ? palette.text : palette.accent);
            drawTextRight(renderer, x + width - 12, y + 50,
                          synthMacroValue(patch, macro), palette.text, 2);
            drawMiniBar(renderer, x + 76, y + height - 18, width - 88, unit,
                        palette, selected);
        }

        void drawDeepSynthParameter(SDL_Renderer *renderer, const AdvancedFmPatch &patch,
                                    const Palette &palette, int parameter, int x, int y,
                                    int width) const
        {
            const bool selected = editorIndex == parameter;
            if (selected)
                fillRect(renderer, x, y, width, 20, palette.accentDim);
            if (selected)
                fillRect(renderer, x, y, 3, 20, palette.accent);
            drawText(renderer, x + 8, y + 6, synthParameterLabel(parameter),
                     selected ? palette.accent : palette.muted, 1);
            drawTextRight(renderer, x + width - 8, y + 6,
                          synthParameterValue(patch, parameter),
                          selected ? palette.text : palette.faint, 1);
            drawLine(renderer, x + 5, y + 20, x + width - 5, y + 20, palette.line);
        }

        void drawSynth(SDL_Renderer *renderer, const AppState &app, const Palette &palette) const
        {
            drawSectionTitle(renderer, palette, "SYNTH LAB",
                             synthPerformanceMode ? "BASIC - EVOLVING SOUND MACROS" : "DEEP - ALL 50 ENGINE FIELDS");
            fillRect(renderer, 520, 121, 86, 33,
                     synthPerformanceMode ? palette.accentDim : palette.raised);
            strokeRect(renderer, 520, 121, 86, 33,
                       synthPerformanceMode ? palette.accent : palette.lineStrong);
            drawTextCentered(renderer, 563, 133, "BASIC", synthPerformanceMode ? palette.accent : palette.muted, 1);
            fillRect(renderer, 612, 121, 86, 33,
                     !synthPerformanceMode ? palette.accentDim : palette.raised);
            strokeRect(renderer, 612, 121, 86, 33,
                       !synthPerformanceMode ? palette.accent : palette.lineStrong);
            drawTextCentered(renderer, 655, 133, "DEEP", !synthPerformanceMode ? palette.accent : palette.muted, 1);
            drawTextCentered(renderer, 609, 160, "B TOGGLE", palette.faint, 1);
            if (selectedTrack == kTrackCount - 1)
            {
                drawTextCentered(renderer, 640, 286, "THE 4-OP SYNTH LAB IS AVAILABLE ON FM TRACKS 1-4",
                                 palette.muted, 2);
                drawTextCentered(renderer, 640, 332, "SELECT TRACK 1-4  /  PSG SOUND REMAINS IN GRID",
                                 palette.faint, 1);
                drawEditorFooter(renderer, palette, "P OPENS THE NOISE SOUND PALETTE   PAGE UP/DOWN SELECTS STEP");
                return;
            }
            const auto &step = app.tracks[static_cast<std::size_t>(selectedTrack)]
                                   .steps[static_cast<std::size_t>(selectedStep)];
            const AdvancedFmPatch &patch = step.advancedFm;
            drawText(renderer, 28, 174,
                     "STEP " + hexValue(selectedStep, 1) + "  FM" + decimalValue(selectedTrack + 1) +
                         "  SCOPE " + scopeName(),
                     palette.accent, 1);
            drawTextRight(renderer, 1250, 174,
                          patch.enabled ? "4-OP ACTIVE" : "LEGACY 2-OP ACTIVE", palette.text, 1);

            if (synthPerformanceMode)
            {
                drawAlgorithmDiagram(renderer, patch, palette, 28, 194, 448, 432);
                constexpr int macroWidth = 177;
                constexpr int macroHeight = 128;
                for (int macro = 0; macro < kSynthMacroCount; ++macro)
                {
                    const int column = macro % 4;
                    const int row = macro / 4;
                    drawSynthMacro(renderer, patch, palette, macro,
                                   500 + column * 188, 194 + row * 144,
                                   macroWidth, macroHeight);
                }
                drawText(renderer, 500, 632,
                         kSynthMacroDescriptions[static_cast<std::size_t>(
                             clampInt(editorIndex, 0, kSynthMacroCount - 1))],
                         palette.muted, 1);
                drawEditorFooter(renderer, palette,
                                 "B DEEP   LEFT/RIGHT SELECT   UP/DOWN OR -/= TURN   SHIFT COARSE   R NUDGE   P PALETTE");
                return;
            }

            const auto group = [&](int x, int y, int width, const char *label)
            {
                drawText(renderer, x, y, label, palette.accent, 1);
                drawLine(renderer, x, y + 17, x + width, y + 17, palette.lineStrong);
            };
            group(28, 194, 588, "ENGINE");
            drawDeepSynthParameter(renderer, patch, palette, 0, 28, 214, 290);
            drawDeepSynthParameter(renderer, patch, palette, 1, 326, 214, 290);

            for (int op = 0; op < 4; ++op)
            {
                const int column = op % 2;
                const int row = op / 2;
                const int x = 28 + column * 298;
                const int y = 252 + row * 184;
                group(x, y, 290, ("OPERATOR " + decimalValue(op + 1)).c_str());
                for (int field = 0; field < 5; ++field)
                    drawDeepSynthParameter(renderer, patch, palette, 2 + op * 5 + field,
                                           x, y + 22 + field * 27, 290);
            }

            group(646, 194, 282, "AMPLITUDE ENVELOPE");
            for (int field = 0; field < 4; ++field)
                drawDeepSynthParameter(renderer, patch, palette, 22 + field,
                                       646, 216 + field * 27, 282);
            group(946, 194, 306, "FILTER / DRIVE");
            for (int field = 0; field < 5; ++field)
                drawDeepSynthParameter(renderer, patch, palette, 26 + field,
                                       946, 216 + field * 27, 306);
            group(646, 344, 282, "UNISON");
            for (int field = 0; field < 3; ++field)
                drawDeepSynthParameter(renderer, patch, palette, 31 + field,
                                       646, 366 + field * 27, 282);

            fillRect(renderer, 946, 344, 306, 91, palette.surface);
            strokeRect(renderer, 946, 344, 306, 91, palette.lineStrong);
            drawText(renderer, 960, 359, "ROUTING", palette.muted, 1);
            drawText(renderer, 960, 383,
                     "ALG " + paddedDecimal(static_cast<int>(patch.algorithm) + 1), palette.text, 2);
            drawTextRight(renderer, 1238, 383, "BASIC SHOWS DIAGRAM", palette.accent, 1);

            group(646, 462, 606, "MODULATION MATRIX");
            for (int slot = 0; slot < 4; ++slot)
            {
                const int x = 646 + slot * 151;
                drawText(renderer, x + 8, 484, "MOD " + decimalValue(slot + 1), palette.faint, 1);
                for (int field = 0; field < 4; ++field)
                    drawDeepSynthParameter(renderer, patch, palette, 34 + slot * 4 + field,
                                           x, 501 + field * 27, 146);
            }
            drawEditorFooter(renderer, palette,
                             "B BASIC   GROUPED DEEP EDITOR   LEFT/RIGHT SELECT   -/= EDIT   PAGE UP/DOWN STEP");
        }

        std::string echoValue(const EchoSettings &echo, int index) const
        {
            if (selectedTrack == kTrackCount - 1 &&
                (index == 2 || index == 3 || index == 5 || index == 6))
                return "N/A";
            switch (index)
            {
            case 0:
                return decimalValue(echo.repeats);
            case 1:
                return decimalValue(echo.speedTicks) + " TICKS";
            case 2:
                return signedValue(echo.transpose) + " ST";
            case 3:
                return "EVERY " + decimalValue(echo.transposeModulo);
            case 4:
                return signedValue(echo.volumeDelta);
            case 5:
                return signedValue(echo.modDelta);
            case 6:
                return signedValue(echo.feedbackDelta);
            case 7:
                return echoPanName(echo.pan);
            default:
                return "--";
            }
        }

        float echoUnit(const EchoSettings &echo, int index) const
        {
            if (selectedTrack == kTrackCount - 1 &&
                (index == 2 || index == 3 || index == 5 || index == 6))
                return 0.0f;
            switch (index)
            {
            case 0:
                return static_cast<float>(echo.repeats) / 8.0f;
            case 1:
                return static_cast<float>(echo.speedTicks - 1u) / 95.0f;
            case 2:
                return static_cast<float>(echo.transpose + 24) / 48.0f;
            case 3:
                return static_cast<float>(echo.transposeModulo - 1u) / 7.0f;
            case 4:
                return static_cast<float>(echo.volumeDelta + 64) / 127.0f;
            case 5:
                return static_cast<float>(echo.modDelta + 64) / 127.0f;
            case 6:
                return static_cast<float>(echo.feedbackDelta + 64) / 127.0f;
            case 7:
                return static_cast<float>(static_cast<int>(echo.pan)) / 3.0f;
            default:
                return 0.0f;
            }
        }

        void drawEcho(SDL_Renderer *renderer, const AppState &app, const Palette &palette) const
        {
            drawSectionTitle(renderer, palette, "NOTE ECHO", "ALGORITHMIC REPEATS PER TRACK");
            const auto &echo = app.tracks[static_cast<std::size_t>(selectedTrack)].echo;
            drawLine(renderer, 54, 269, 1226, 269, palette.lineStrong);
            const int count = static_cast<int>(echo.repeats) + 1;
            for (int i = 0; i < count; ++i)
            {
                const float decay = std::max(0.12f, 1.0f + static_cast<float>(echo.volumeDelta) *
                                                               static_cast<float>(i) / 64.0f);
                const int x = count == 1 ? 640 : 80 + i * (1080 / (count - 1));
                const int height = static_cast<int>(std::lround(78.0f * decay));
                drawLine(renderer, x, 269, x, 269 - height, i == 0 ? palette.text : palette.accent);
                fillRect(renderer, x - 3, 266 - height, 7, 7, i == 0 ? palette.text : palette.accent);
                drawTextCentered(renderer, x, 286, i == 0 ? "DRY" : decimalValue(i), palette.faint, 1);
            }
            if (echo.repeats == 0)
                drawTextCentered(renderer, 640, 225, "NO REPEATS", palette.muted, 2);

            static constexpr std::array<const char *, 8> labels{
                "REPEATS", "SPEED", "TRANSPOSE", "TSP MODULO",
                "VOLUME DELTA", "MOD DELTA", "FEEDBACK DELTA", "PAN"};
            for (int index = 0; index < 8; ++index)
            {
                const int column = index / 4;
                const int row = index % 4;
                const int x = 35 + column * 605;
                const int y = 335 + row * 62;
                const bool selected = editorIndex == index;
                const bool available = selectedTrack != kTrackCount - 1 ||
                                       (index != 2 && index != 3 && index != 5 && index != 6);
                if (column == 1)
                    drawLine(renderer, 622, 330, 622, 583, palette.line);
                drawText(renderer, x + 8, y + 10, labels[static_cast<std::size_t>(index)],
                         available ? (selected ? palette.accent : palette.muted) : palette.faint, 1);
                drawTextRight(renderer, x + 572, y + 8, echoValue(echo, index),
                              available ? palette.text : palette.faint, 2);
                drawMiniBar(renderer, x + 8, y + 42, 564, echoUnit(echo, index), palette, selected);
                drawLine(renderer, x, y + 60, x + 582, y + 60, palette.line);
                if (selected)
                    fillRect(renderer, x, y + 3, 3, 48, palette.accent);
            }
            drawEditorFooter(renderer, palette, "LEFT/RIGHT SELECT   UP/DOWN OR -/= EDIT   RIGHT CLICK DECREASE");
        }

        void drawTranspose(SDL_Renderer *renderer, const AppState &app, const Palette &palette) const
        {
            drawSectionTitle(renderer, palette, "TRANSPOSE", "8-STEP SEMITONE SEQUENCER");
            if (selectedTrack == kTrackCount - 1)
            {
                drawLine(renderer, 180, 320, 1100, 320, palette.lineStrong);
                drawTextCentered(renderer, 640, 272, "FM TRACKS ONLY", palette.muted, 3);
                drawTextCentered(renderer, 640, 345,
                                 "NOISE PITCH IS EDITED WITH PER-STEP RATE", palette.faint, 1);
                drawEditorFooter(renderer, palette,
                                 "SELECT FM TRACK 1-4   NOISE RATE LIVES IN THE GRID SYNTH ROW");
                return;
            }
            const auto &transpose = app.tracks[static_cast<std::size_t>(selectedTrack)].transpose;
            drawLine(renderer, 58, 320, 1226, 320, palette.lineStrong);
            drawText(renderer, 30, 313, "0", palette.faint, 1);
            for (int i = 0; i < 8; ++i)
            {
                const int x = 58 + i * 146;
                const int value = transpose.values[static_cast<std::size_t>(i)];
                const bool selected = editorIndex == i;
                const bool inLength = i < static_cast<int>(transpose.length);
                if (selected)
                    fillRect(renderer, x + 4, 185, 136, 270, palette.accentDim);
                drawLine(renderer, x + 72, 190, x + 72, 446, palette.line);
                const int bar = value * 5;
                const int top = std::min(320, 320 - bar);
                const int height = std::max(2, std::abs(bar));
                fillRect(renderer, x + 48, top, 48, height, inLength ? palette.accent : palette.faint);
                drawTextCentered(renderer, x + 72, 212, signedValue(value), inLength ? palette.text : palette.muted, 2);
                drawTextCentered(renderer, x + 72, 428, decimalValue(i + 1), selected ? palette.accent : palette.faint, 1);
                if (!inLength)
                    drawLine(renderer, x + 19, 401, x + 125, 401, palette.lineStrong);
                if (selected)
                    fillRect(renderer, x + 16, 451, 112, 3, palette.accent);
            }

            static constexpr std::array<const char *, 3> labels{"LENGTH", "RATE", "ADVANCE"};
            const std::array<std::string, 3> values{
                decimalValue(transpose.length), decimalValue(transpose.rate), advanceName(transpose.advance)};
            for (int i = 0; i < 3; ++i)
            {
                const int x = 96 + i * 362;
                const bool selected = editorIndex == 8 + i;
                drawText(renderer, x + 8, 505, labels[static_cast<std::size_t>(i)],
                         selected ? palette.accent : palette.muted, 1);
                drawText(renderer, x + 8, 530, values[static_cast<std::size_t>(i)], palette.text, 2);
                drawLine(renderer, x, 570, x + 330, 570, selected ? palette.accent : palette.lineStrong);
            }
            drawEditorFooter(renderer, palette, "CLICK/DRAG HEIGHT TO SET   RIGHT CLICK ZERO   ARROWS EDIT");
        }

        float modWaveSample(ModWave wave, float phase, std::uint32_t seed) const
        {
            phase -= std::floor(phase);
            switch (wave)
            {
            case ModWave::RampDown:
                return 1.0f - phase * 2.0f;
            case ModWave::RampUp:
                return phase * 2.0f - 1.0f;
            case ModWave::Triangle:
                return 1.0f - 4.0f * std::abs(phase - 0.5f);
            case ModWave::Square:
                return phase < 0.5f ? 1.0f : -1.0f;
            case ModWave::Random:
            {
                std::uint32_t value = seed + static_cast<std::uint32_t>(phase * 8.0f) * 0x9E3779B9u;
                value ^= value >> 16u;
                value *= 0x7FEB352Du;
                value ^= value >> 15u;
                return static_cast<float>(value & 0xFFFFu) / 32767.5f - 1.0f;
            }
            }
            return 0.0f;
        }

        std::string modValue(const ModulatorSettings &mod, int index) const
        {
            switch (index)
            {
            case 0:
                return mod.targetTrack == 4 ? "NOISE" : "FM " + decimalValue(mod.targetTrack + 1);
            case 1:
                return destinationName(mod.destination);
            case 2:
                return decimalValue(mod.speed) + " STEPS";
            case 3:
                return waveName(mod.wave);
            case 4:
                return signedValue(mod.depth);
            case 5:
                return decimalValue(mod.offset);
            default:
                return "--";
            }
        }

        void drawMod(SDL_Renderer *renderer, const AppState &app, const Palette &palette) const
        {
            drawSectionTitle(renderer, palette, "STEP MODULATOR", "PER-STEP SAMPLE AND HOLD MODULATION");
            const auto &mod = app.tracks[static_cast<std::size_t>(selectedTrack)].modulator;
            drawText(renderer, 52, 190, waveName(mod.wave), palette.muted, 1);
            drawTextRight(renderer, 774, 190, "DEPTH " + signedValue(mod.depth), palette.muted, 1);
            drawLine(renderer, 52, 362, 778, 362, palette.lineStrong);
            drawLine(renderer, 52, 208, 52, 516, palette.line);
            drawLine(renderer, 778, 208, 778, 516, palette.line);
            const float amplitude = static_cast<float>(std::abs(static_cast<int>(mod.depth))) / 64.0f;
            int previousX = 52;
            int previousY = 362;
            for (int i = 1; i <= 128; ++i)
            {
                const float phase = static_cast<float>(i) / 32.0f + static_cast<float>(mod.offset) / 64.0f;
                float sample = modWaveSample(mod.wave, phase, static_cast<std::uint32_t>(selectedTrack + 1));
                if (mod.depth < 0)
                    sample = -sample;
                const int x = 52 + i * 726 / 128;
                const int y = 362 - static_cast<int>(std::lround(sample * amplitude * 135.0f));
                drawLine(renderer, previousX, previousY, x, y, palette.accent);
                previousX = x;
                previousY = y;
            }
            for (int i = 0; i < 9; ++i)
            {
                const int x = 52 + i * 726 / 8;
                drawLine(renderer, x, 354, x, 370, palette.lineStrong);
            }

            static constexpr std::array<const char *, 6> labels{
                "TARGET TRACK", "DESTINATION", "SPEED", "WAVE", "DEPTH", "OFFSET"};
            drawLine(renderer, 810, 174, 810, 570, palette.lineStrong);
            for (int i = 0; i < 6; ++i)
            {
                const int y = 174 + i * 66;
                const bool selected = editorIndex == i;
                if (selected)
                    fillRect(renderer, 822, y, 426, 64, palette.accentDim);
                drawText(renderer, 838, y + 10, labels[static_cast<std::size_t>(i)],
                         selected ? palette.accent : palette.muted, 1);
                drawTextRight(renderer, 1232, y + 27, modValue(mod, i), palette.text, 2);
                drawLine(renderer, 822, y + 64, 1248, y + 64, palette.line);
                if (selected)
                    fillRect(renderer, 822, y + 5, 3, 52, palette.accent);
            }
            drawEditorFooter(renderer, palette, "MODULATOR LIVES WITH ORIGIN TRACK   TARGET CAN BE ANY TRACK");
        }

        void drawScale(SDL_Renderer *renderer, const AppState &app, const Palette &palette) const
        {
            drawSectionTitle(renderer, palette, "SCALE", "GLOBAL INPUT AND PLAYBACK QUANTIZATION", false);
            static constexpr std::array<const char *, 12> notes{
                "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            drawText(renderer, 46, 173, "ROOT", editorIndex == 0 ? palette.accent : palette.muted, 1);
            drawText(renderer, 124, 166, notes[app.scaleRoot], palette.text, 3);
            drawText(renderer, 204, 173, "CLICK / RIGHT CLICK", palette.faint, 1);
            drawLine(renderer, 46, 217, 318, 217, editorIndex == 0 ? palette.accent : palette.lineStrong);

            for (int degree = 0; degree < 12; ++degree)
            {
                const int x = 46 + degree * 99;
                const bool enabled = (app.scaleMask & (1u << degree)) != 0u;
                const bool selected = editorIndex == degree + 1;
                if (enabled)
                    fillRect(renderer, x + 3, 242, 91, 178, palette.accentDim);
                if (selected)
                    strokeRect(renderer, x + 2, 242, 92, 178, palette.accent);
                drawTextCentered(renderer, x + 48, 273, notes[(degree + app.scaleRoot) % 12],
                                 enabled ? palette.text : palette.muted, 2);
                drawTextCentered(renderer, x + 48, 344, enabled ? "ON" : "OFF",
                                 enabled ? palette.accent : palette.faint, 2);
                drawLine(renderer, x + 3, 419, x + 94, 419, enabled ? palette.accent : palette.line);
            }
            static constexpr std::array<const char *, 6> presets{
                "CHROMATIC", "MAJOR", "MINOR", "DORIAN", "MIXOLYD", "WHOLE"};
            drawText(renderer, 46, 454, "PRESETS", palette.muted, 1);
            for (int i = 0; i < 6; ++i)
            {
                const int x = 46 + i * 198;
                const bool selected = editorIndex == 13 + i;
                if (selected)
                    fillRect(renderer, x, 476, 190, 72, palette.accentDim);
                drawTextCentered(renderer, x + 95, 503, presets[static_cast<std::size_t>(i)],
                                 selected ? palette.accent : palette.text, 1);
                drawLine(renderer, x, 547, x + 190, 547, selected ? palette.accent : palette.lineStrong);
            }
            drawEditorFooter(renderer, palette, "ENTER/CLICK TO TOGGLE NOTES   ROOT SHIFTS NOTE NAMES, MASK STAYS RELATIVE");
        }

        void drawData(SDL_Renderer *renderer, const AppState &app, const Palette &palette) const
        {
            drawSectionTitle(renderer, palette, "PATTERN DATA",
                             dataWorkspace == DataWorkspace::Perform
                                 ? "PERFORM - LOAD, QUEUE, AND SAVE WITHOUT MENU DIVING"
                                 : "MANAGE - NAME, COLOR, LOCK, AND CURATE YOUR SET");
            fillRect(renderer, 520, 121, 86, 33,
                     dataWorkspace == DataWorkspace::Perform ? palette.accentDim : palette.raised);
            strokeRect(renderer, 520, 121, 86, 33,
                       dataWorkspace == DataWorkspace::Perform ? palette.accent : palette.lineStrong);
            drawTextCentered(renderer, 563, 133, "PERFORM",
                             dataWorkspace == DataWorkspace::Perform ? palette.accent : palette.muted, 1);
            fillRect(renderer, 612, 121, 86, 33,
                     dataWorkspace == DataWorkspace::Manage ? palette.accentDim : palette.raised);
            strokeRect(renderer, 612, 121, 86, 33,
                       dataWorkspace == DataWorkspace::Manage ? palette.accent : palette.lineStrong);
            drawTextCentered(renderer, 655, 133, "MANAGE",
                             dataWorkspace == DataWorkspace::Manage ? palette.accent : palette.muted, 1);
            drawTextCentered(renderer, 609, 160, "V / F8 TOGGLE", palette.faint, 1);

            const auto &currentBank = app.banks[static_cast<std::size_t>(dataBank)];
            const std::string bankName(currentBank.name.data(), 4);
            static constexpr std::array<const char *, 7> ribbonLabels{{"LOAD", "QUEUE", "SAVE", "CLEAR", "RANDOM", "TARGET", "MODE"}};
            constexpr int ribbonLeft = 28;
            constexpr int ribbonWidth = 174;
            for (int action = 0; action < 7; ++action)
            {
                const int x = ribbonLeft + action * ribbonWidth;
                const bool stateAction = action >= 5;
                fillRect(renderer, x, 180, ribbonWidth - 8, 54,
                         stateAction ? palette.accentDim : palette.raised);
                strokeRect(renderer, x, 180, ribbonWidth - 8, 54,
                           stateAction ? palette.accent : palette.lineStrong);
                drawText(renderer, x + 12, 192, ribbonLabels[static_cast<std::size_t>(action)],
                         action == 1 || stateAction ? palette.accent : palette.text, 1);
                std::string detail;
                if (action == 0)
                    detail = "ENTER";
                else if (action == 1)
                    detail = "Q";
                else if (action == 2)
                    detail = "SHIFT+ENTER";
                else if (action == 3)
                    detail = "X / CONFIRM";
                else if (action == 4)
                    detail = "R";
                else if (action == 5)
                    detail = dataAllTracks ? "ALL 5" : "TRACK";
                else
                    detail = dataLoadMode == DataLoadMode::Reset ? "RESET" : "IN PLACE";
                drawText(renderer, x + 12, 212, detail,
                         stateAction ? palette.text : palette.muted, 1);
            }

            const bool blink = std::fmod(elapsed, 0.5) < 0.25;
            const auto markerColor = [&](const PatternMetadata &metadata)
            {
                if (metadata.color == 0u)
                    return palette.accent;
                const std::size_t index = static_cast<std::size_t>((metadata.color - 1u) % 6u);
                return app.lightTheme ? kLightAccents[index] : kDarkAccents[index];
            };
            const auto drawSlot = [&](int bank, int column, int x, int y, int width)
            {
                const int pattern = bank * 16 + column;
                const PatternMetadata &metadata = app.patternMetadata[static_cast<std::size_t>(pattern)];
                bool occupied = true;
                bool queued = false;
                const int first = dataAllTracks ? 0 : selectedTrack;
                const int last = dataAllTracks ? kTrackCount - 1 : selectedTrack;
                for (int track = first; track <= last; ++track)
                {
                    occupied = occupied && app.patterns[static_cast<std::size_t>(track)]
                                                       [static_cast<std::size_t>(pattern)]
                                                           .occupied;
                    queued = queued || queuedPattern[static_cast<std::size_t>(track)] == pattern;
                }
                const bool selected = dataBank == bank && dataColumn == column;
                if (selected)
                    fillRect(renderer, x, y, width, 34, palette.accentDim);
                const Color slotColor = markerColor(metadata);
                if (metadata.color != 0u)
                    fillRect(renderer, x, y, width, 3, slotColor);
                if (occupied)
                    fillRect(renderer, x + width - 12, y + 9, 6, 6,
                             queued && blink ? palette.text : slotColor);
                else
                    drawLine(renderer, x + width - 13, y + 12, x + width - 6, y + 12, palette.faint);
                std::string label(metadata.name.data());
                if (label.empty())
                    label = hexValue(column, 1);
                const std::size_t maxChars = static_cast<std::size_t>(std::max(1, (width - 12) / 6));
                if (label.size() > maxChars)
                    label.resize(maxChars);
                drawText(renderer, x + 6, y + 18, label,
                         selected ? palette.text : (occupied ? palette.muted : palette.faint), 1);
                if (queued && blink)
                {
                    strokeRect(renderer, x + 2, y + 3, width - 4, 28, palette.accent);
                    drawText(renderer, x + 5, y + 6, "Q", palette.accent, 1);
                }
                if (selected)
                    strokeRect(renderer, x, y, width, 34, palette.accent);
            };

            if (dataWorkspace == DataWorkspace::Perform)
            {
                for (int column = 0; column < 16; ++column)
                    drawTextCentered(renderer, 188 + column * 65 + 30, 244,
                                     hexValue(column, 1), palette.faint, 1);
                for (int bank = 0; bank < 8; ++bank)
                {
                    const int y = 260 + bank * 39;
                    const auto &settings = app.banks[static_cast<std::size_t>(bank)];
                    const std::string name(settings.name.data(), 4);
                    if (bank == dataBank)
                        fillRect(renderer, 28, y, 146, 34, palette.raised);
                    drawText(renderer, 36, y + 7,
                             decimalValue(bank + 1) + "  " + name,
                             bank == dataBank ? palette.accent : palette.muted, 1);
                    if (settings.locked)
                        drawTextRight(renderer, 166, y + 7, "LOCK", palette.accent, 1);
                    for (int column = 0; column < 16; ++column)
                        drawSlot(bank, column, 188 + column * 65, y, 60);
                }
            }
            else
            {
                for (int visual = 0; visual < 18; ++visual)
                {
                    const int column = visual - 2;
                    drawTextCentered(renderer, 72 + visual * 63 + 29, 244,
                                     column == -2 ? "X" : (column == -1 ? "?" : hexValue(column, 1)),
                                     column < 0 ? palette.accent : palette.faint, 1);
                }
                for (int bank = 0; bank < 8; ++bank)
                {
                    const int y = 260 + bank * 39;
                    drawText(renderer, 28, y + 11, decimalValue(bank + 1),
                             bank == dataBank ? palette.accent : palette.muted, 1);
                    if (app.banks[static_cast<std::size_t>(bank)].locked)
                        drawText(renderer, 45, y + 11, "L", palette.accent, 1);
                    for (int visual = 0; visual < 18; ++visual)
                    {
                        const int column = visual - 2;
                        const int x = 72 + visual * 63;
                        if (column >= 0)
                            drawSlot(bank, column, x, y, 58);
                        else
                        {
                            const bool selected = dataBank == bank && dataColumn == column;
                            if (selected)
                                fillRect(renderer, x, y, 58, 34, palette.accentDim);
                            drawTextCentered(renderer, x + 29, y + 11,
                                             column == -2 ? "X" : "?",
                                             selected ? palette.accent : palette.muted, 2);
                            if (selected)
                                strokeRect(renderer, x, y, 58, 34, palette.accent);
                        }
                    }
                }
            }

            const int selectedPattern = dataBank * 16 + std::max(0, dataColumn);
            const PatternMetadata &metadata = app.patternMetadata[static_cast<std::size_t>(selectedPattern)];
            const std::string patternName(metadata.name.data());
            drawText(renderer, 28, 580,
                     "BANK " + decimalValue(dataBank + 1) + " " + bankName +
                         "   SLOT " + hexValue(selectedPattern) + "   " +
                         (patternName.empty() ? "UNTITLED" : patternName),
                     palette.text, 1);
            if (dataWorkspace == DataWorkspace::Manage)
            {
                drawText(renderer, 28, 604,
                         "E NAME   C COLOR " + decimalValue(metadata.color) +
                             "   N BANK NAME   K " + (currentBank.locked ? "UNLOCK" : "LOCK"),
                         palette.accent, 1);
                drawTextRight(renderer, 1252, 580,
                              std::string("BPM ") + (currentBank.hasTempo ? decimalValue(currentBank.tempo) : "--") +
                                  (dataArmTempo ? " ARMED" : "") + "   SCALE " +
                                  (currentBank.hasScale ? hexValue(currentBank.scaleMask, 3) : "---") +
                                  (dataArmScale ? " ARMED" : ""),
                              palette.muted, 1);
                drawTextRight(renderer, 1252, 604,
                              "B/G RECALL   SHIFT STORE   CTRL ARM   ALT TIMED", palette.faint, 1);
            }
            else
            {
                drawText(renderer, 28, 604, "ENTER LOAD   Q QUEUE   SHIFT+ENTER SAVE", palette.accent, 1);
                drawTextRight(renderer, 1252, 604,
                              std::string(dataAllTracks ? "ALL 5 TRACKS" : "CURRENT TRACK") +
                                  "   " + (dataLoadMode == DataLoadMode::Reset ? "RESET" : "IN PLACE"),
                              palette.text, 1);
            }
            drawEditorFooter(renderer, palette,
                             dataWorkspace == DataWorkspace::Perform
                                 ? "PERFORM KEEPS LOAD AND QUEUE OBVIOUS   V/F8 OPENS MANAGEMENT"
                                 : "X / ? REMAIN SELECTABLE CLEAR / RANDOM OPERATIONS   LOCKS GUARD SAVES");
        }

        void drawEditorFooter(SDL_Renderer *renderer, const Palette &palette,
                              std::string_view hint) const
        {
            fillRect(renderer, 0, 650, kLogicalWidth, 110, palette.surface);
            drawLine(renderer, 0, 650, kLogicalWidth, 650, palette.lineStrong);
            drawText(renderer, 28, 672, "1-5 TRACK   [ ] FIELD   - = VALUE   SHIFT COARSE",
                     palette.text, 1);
            drawTextRight(renderer, 1252, 672, "SPACE PLAY   S SNAPSHOT   CTRL+K COMMANDS   CTRL+S SAVE", palette.text, 1);
            drawText(renderer, 28, 708, hint, palette.muted, 1);
            drawTextRight(renderer, 1252, 738,
                          "F2 THEME   F3 ACCENT   F4 PAD   F5 SCOPE   F6 HINTS   F7 GUIDE",
                          palette.faint, 1);
        }

        void drawPaletteOverlay(SDL_Renderer *renderer, const AppState &app,
                                const Palette &palette) const
        {
            fillRect(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {0, 0, 0, 190});
            fillRect(renderer, 132, 236, 1016, 264, palette.surface);
            strokeRect(renderer, 132, 236, 1016, 264, palette.lineStrong);
            fillRect(renderer, 132, 236, 1016, 4, palette.accent);
            drawText(renderer, 166, 270,
                     selectedTrack == kTrackCount - 1 ? "NOISE SOUND PALETTE" : "FM SOUND PALETTE",
                     palette.text, 3);
            drawTextRight(renderer, 1110, 278, "14 GLOBAL USER SLOTS  0-D", palette.muted, 1);
            const auto &sounds = selectedTrack == kTrackCount - 1 ? app.noisePalette : app.fmPalette;
            for (int index = 0; index < kPaletteSize + 2; ++index)
            {
                const int x = 166 + index * 59;
                const bool selected = paletteCursor == index;
                if (selected)
                    fillRect(renderer, x, 326, 54, 64, palette.accentDim);
                strokeRect(renderer, x, 326, 54, 64, selected ? palette.accent : palette.lineStrong);
                const std::string label = index == 0 ? "X" : (index == 1 ? "?" : hexValue(index - 2, 1));
                drawTextCentered(renderer, x + 27, 339, label,
                                 selected ? palette.accent : palette.text, 2);
                if (index >= 2)
                {
                    const Step &sound = sounds[static_cast<std::size_t>(index - 2)];
                    if (sound.active)
                        fillRect(renderer, x + 23, 369, 8, 8, palette.accent);
                    else
                        drawLine(renderer, x + 22, 373, x + 32, 373, palette.faint);
                }
                else
                {
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

        void drawControllerOverlay(SDL_Renderer *renderer, const AppState &app,
                                   const Palette &palette) const
        {
            fillRect(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {0, 0, 0, 200});
            fillRect(renderer, 132, 92, 1016, 568, palette.surface);
            strokeRect(renderer, 132, 92, 1016, 568, palette.lineStrong);
            fillRect(renderer, 132, 92, 1016, 4, palette.accent);
            drawText(renderer, 168, 122, "CONTROLLER MAPPING", palette.text, 3);
            const char *rawName = controller ? SDL_GameControllerName(controller) : nullptr;
            const std::string status = controller
                                           ? std::string("CONNECTED  ") + (rawName ? asciiOnly(rawName) : "GAMEPAD")
                                           : "NO CONTROLLER - HOTPLUG READY";
            drawText(renderer, 168, 157, status, controller ? palette.accent : palette.muted, 1);
            drawTextRight(renderer, 1112, 157, app.controller.enabled ? "INPUT ENABLED" : "INPUT DISABLED",
                          app.controller.enabled ? palette.text : palette.accent, 1);
            drawLine(renderer, 640, 184, 640, 526, palette.lineStrong);
            for (int index = 0; index < static_cast<int>(kControllerActionCount); ++index)
            {
                const int column = index / 9;
                const int row = index % 9;
                const int x = 168 + column * 490;
                const int y = 190 + row * 36;
                const bool selected = controllerMapCursor == index;
                if (selected)
                    fillRect(renderer, x, y, 452, 32, palette.accentDim);
                if (selected)
                    fillRect(renderer, x, y, 3, 32, palette.accent);
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

        void drawBankNameOverlay(SDL_Renderer *renderer, const Palette &palette) const
        {
            fillRect(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {0, 0, 0, 200});
            fillRect(renderer, 410, 248, 460, 254, palette.surface);
            strokeRect(renderer, 410, 248, 460, 254, palette.lineStrong);
            fillRect(renderer, 410, 248, 460, 4, palette.accent);
            drawTextCentered(renderer, 640, 282, "EDIT BANK NAME", palette.text, 3);
            for (int index = 0; index < 4; ++index)
            {
                const int x = 494 + index * 76;
                const bool selected = bankNameCursor == index;
                if (selected)
                    fillRect(renderer, x, 338, 62, 62, palette.accentDim);
                strokeRect(renderer, x, 338, 62, 62, selected ? palette.accent : palette.lineStrong);
                const std::string value(1, bankNameEdit[static_cast<std::size_t>(index)]);
                drawTextCentered(renderer, x + 31, 354, value, selected ? palette.accent : palette.text, 4);
            }
            drawTextCentered(renderer, 640, 424, "TYPE 4 CHARACTERS   ARROWS MOVE   ENTER SAVE", palette.muted, 1);
            drawTextCentered(renderer, 640, 454, "ESC CANCEL   BANK LOCK BLOCKS THE SAVE", palette.faint, 1);
        }

        void drawPatternNameOverlay(SDL_Renderer *renderer, const Palette &palette) const
        {
            fillRect(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {0, 0, 0, 205});
            fillRect(renderer, 378, 208, 524, 366, palette.surface);
            strokeRect(renderer, 378, 208, 524, 366, palette.lineStrong);
            fillRect(renderer, 378, 208, 524, 4, palette.accent);
            drawTextCentered(renderer, 640, 246, "NAME PATTERN " + hexValue(dataPattern()), palette.text, 3);
            drawTextCentered(renderer, 640, 280, "A SHORT PERFORMANCE NAME SHARED BY ALL FIVE TRACKS", palette.muted, 1);
            fillRect(renderer, 414, 332, 452, 78, palette.raised);
            strokeRect(renderer, 414, 332, 452, 78, palette.accent);
            drawText(renderer, 436, 360,
                     patternNameEdit.empty() ? "UNTITLED" : asciiOnly(patternNameEdit),
                     patternNameEdit.empty() ? palette.muted : palette.text, 2);
            drawTextCentered(renderer, 640, 430, "UP TO 12 CHARACTERS   EMPTY RESTORES UNTITLED", palette.faint, 1);
            fillRect(renderer, 414, 462, 452, 38, palette.accentDim);
            drawTextCentered(renderer, 640, 476, "ENTER / CLICK SAVE NAME", palette.accent, 1);
            drawTextCentered(renderer, 640, 526, "ESC / CLICK CANCEL   BANK LOCK GUARDS METADATA", palette.muted, 1);
        }

        void drawProjectMenuOverlay(SDL_Renderer *renderer, const Palette &palette) const
        {
            fillRect(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {0, 0, 0, 205});
            fillRect(renderer, 314, 102, 652, 558, palette.surface);
            strokeRect(renderer, 314, 102, 652, 558, palette.lineStrong);
            fillRect(renderer, 314, 102, 652, 4, palette.accent);
            drawText(renderer, 346, 136, "PROJECT ACTIONS", palette.text, 3);
            drawText(renderer, 346, 169, "ACTIVE " + projectLabel(), palette.accent, 1);
            drawTextRight(renderer, 934, 169, isDirty() ? "UNSAVED CHANGES" : "SAVED",
                          isDirty() ? palette.accent : palette.muted, 1);
            static constexpr std::array<std::array<const char *, 2>, 5> actions{{
                {{"START NEW SESSION", "RESET PATTERNS, PALETTES, BANKS, BPM, AND SCALE"}},
                {{"CLEAR WORKING TRACKS", "CLEAR THE FIVE LIVE TRACKS; SAVED SLOTS STAY INTACT"}},
                {{"SAVE NOW", "WRITE THE CURRENT SESSION TO THE ACTIVE SAVE PATH"}},
                {{"SAVE AS PROJECT", "TYPE A NAME; SAVES AN ATOMIC .FMS PROJECT FILE"}},
                {{"OPEN RECENT PROJECT", "BROWSE THE MOST RECENT SAVED PROJECT FILES"}},
            }};
            for (int index = 0; index < 5; ++index)
            {
                const int y = 202 + index * 70;
                const bool selected = projectActionCursor == index;
                if (selected)
                    fillRect(renderer, 346, y, 588, 62, palette.accentDim);
                if (selected)
                    fillRect(renderer, 346, y, 3, 62, palette.accent);
                drawText(renderer, 364, y + 12, actions[static_cast<std::size_t>(index)][0],
                         selected ? palette.accent : palette.text, 2);
                drawText(renderer, 364, y + 39, actions[static_cast<std::size_t>(index)][1],
                         selected ? palette.text : palette.muted, 1);
                drawLine(renderer, 346, y + 64, 934, y + 64, palette.line);
            }
            const char *confirmation = projectActionArmed
                                           ? "CONFIRM ARMED - ENTER OR CLICK THE SELECTED ACTION AGAIN"
                                           : "ARROWS SELECT   ENTER ACTIVATE   ESC CANCEL";
            drawTextCentered(renderer, 640, 568, confirmation,
                             projectActionArmed ? palette.accent : palette.muted, 1);
            drawTextCentered(renderer, 640, 596,
                             "CTRL+N NEW   CTRL+SHIFT+BACKSPACE CLEAR   CTRL+SHIFT+S SAVE AS", palette.faint, 1);
            drawTextCentered(renderer, 640, 626,
                             "DROP ANY .FMS FILE ONTO THE WINDOW TO OPEN", palette.faint, 1);
        }

        void drawProjectNameOverlay(SDL_Renderer *renderer, const Palette &palette) const
        {
            fillRect(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {0, 0, 0, 205});
            fillRect(renderer, 378, 208, 524, 366, palette.surface);
            strokeRect(renderer, 378, 208, 524, 366, palette.lineStrong);
            fillRect(renderer, 378, 208, 524, 4, palette.accent);
            drawTextCentered(renderer, 640, 246, "SAVE AS PROJECT", palette.text, 3);
            drawTextCentered(renderer, 640, 280, "TYPE A NAME FOR A REUSABLE PROJECT FILE", palette.muted, 1);
            fillRect(renderer, 414, 332, 452, 78, palette.raised);
            strokeRect(renderer, 414, 332, 452, 78, palette.accent);
            drawText(renderer, 436, 360, asciiOnly(projectNameEdit), palette.text, 2);
            drawTextCentered(renderer, 640, 430, "NAME BECOMES projects/NAME.fms", palette.faint, 1);
            fillRect(renderer, 414, 462, 452, 38, palette.accentDim);
            drawTextCentered(renderer, 640, 476, "ENTER / CLICK SAVE AS", palette.accent, 1);
            drawTextCentered(renderer, 640, 526, "ESC / CLICK CANCEL", palette.muted, 1);
        }

        void drawProjectBrowserOverlay(SDL_Renderer *renderer, const Palette &palette) const
        {
            fillRect(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {0, 0, 0, 205});
            fillRect(renderer, 314, 152, 652, 474, palette.surface);
            strokeRect(renderer, 314, 152, 652, 474, palette.lineStrong);
            fillRect(renderer, 314, 152, 652, 4, palette.accent);
            drawText(renderer, 346, 186, "OPEN RECENT PROJECT", palette.text, 3);
            drawTextRight(renderer, 934, 193, "ESC CANCEL", palette.muted, 1);
            if (projectPaths.empty())
            {
                drawTextCentered(renderer, 640, 354, "NO PROJECT FILES FOUND YET", palette.muted, 2);
                drawTextCentered(renderer, 640, 390,
                                 "DROP ANY .FMS FILE ONTO THE WINDOW TO OPEN", palette.faint, 1);
                return;
            }
            for (int index = 0; index < static_cast<int>(projectPaths.size()); ++index)
            {
                const int y = 218 + index * 42;
                const bool selected = index == projectBrowserCursor;
                if (selected)
                    fillRect(renderer, 346, y, 588, 36, palette.accentDim);
                if (selected)
                    fillRect(renderer, 346, y, 3, 36, palette.accent);
                drawText(renderer, 364, y + 11, projectPathLabel(projectPaths[static_cast<std::size_t>(index)]),
                         selected ? palette.accent : palette.text, 2);
                drawTextRight(renderer, 916, y + 14,
                              asciiOnly(projectPaths[static_cast<std::size_t>(index)]), palette.faint, 1);
                drawLine(renderer, 346, y + 38, 934, y + 38, palette.line);
            }
            drawTextCentered(renderer, 640, 574,
                             projectBrowserArmed ? "OPEN ARMED - ENTER OR CLICK AGAIN"
                                                 : "ARROWS SELECT   ENTER ARM OPEN",
                             projectBrowserArmed ? palette.accent : palette.muted, 1);
        }

        std::string tooltipAt(const AppState &app) const
        {
            if (!hoverActive)
                return {};
            const float x = hoverX;
            const float y = hoverY;
            if (y >= 15.0f && y < 57.0f)
            {
                if (x >= 468.0f && x < 590.0f)
                    return "PROJECT / NEW, CLEAR, SAVE AS, OPEN RECENT, OR DROP ANY .FMS FILE";
                if (x >= 606.0f && x < 790.0f)
                    return "TRANSPORT / CLICK OR SPACE TO START AND STOP";
                if (x >= 800.0f && x < 925.0f)
                    return "TEMPO / LEFT CLICK UP, RIGHT CLICK DOWN, WHEEL ADJUSTS";
            }
            if (y >= 68.0f && y < 108.0f)
            {
                if (x >= 1080.0f)
                    return "CONTEXT HINTS / RESERVES AN INSPECTOR ON WIDE WINDOWS";
                if (x >= 28.0f && x < 812.0f)
                    return "VIEW TAB / CLICK TO SWITCH, TAB AND SHIFT+TAB ALSO WORK";
            }
            if (view == View::Grid)
            {
                if (y >= 202.0f && y < 602.0f)
                    return "STEP / LEFT TOGGLE, RIGHT TRIGLESS, SHIFT EXTENDS A RANGE";
                if (y >= 622.0f && y < 738.0f)
                {
                    const auto &item = gridParamItem(selectedTrack, selectedParameter);
                    const auto &step = app.tracks[static_cast<std::size_t>(selectedTrack)]
                                           .steps[static_cast<std::size_t>(selectedStep)];
                    return std::string(item.fullName) + " / " +
                           gridValue(step, item.id, selectedTrack == kTrackCount - 1) +
                           " / CLICK SELECTS, WHEEL ADJUSTS";
                }
                if (y >= 118.0f && y < 198.0f)
                    return "TRACK CLOCK / RATE, LENGTH, DIRECTION, AND SHUFFLE";
            }
            if (view == View::Synth)
            {
                if (y >= 121.0f && y < 154.0f && x >= 520.0f && x < 698.0f)
                    return "BASIC IS TACTILE; DEEP EXPOSES EVERY ENGINE FIELD";
                if (synthPerformanceMode && x >= 500.0f && y >= 194.0f && y < 626.0f)
                {
                    const int column = clampInt(static_cast<int>((x - 500.0f) / 188.0f), 0, 3);
                    const int row = clampInt(static_cast<int>((y - 194.0f) / 144.0f), 0, 2);
                    const int macro = row * 4 + column;
                    return std::string(kSynthMacroNames[static_cast<std::size_t>(macro)]) +
                           " / CLICK OR WHEEL TO TURN, RIGHT CLICK DECREASES";
                }
                if (!synthPerformanceMode && y >= 194.0f && y < 626.0f)
                    return "DEEP SYNTH / GROUPED OPERATORS, ENVELOPE, FILTER, UNISON, AND MODULATION";
            }
            if (view == View::Data)
            {
                if (y >= 121.0f && y < 154.0f && x >= 520.0f && x < 698.0f)
                    return "PERFORM LOADS PATTERNS; MANAGE NAMES, COLORS, AND LOCKS";
                if (y >= 180.0f && y < 234.0f)
                {
                    const int action = clampInt(static_cast<int>((x - 28.0f) / 174.0f), 0, 6);
                    return std::string(commandDescription(action == 0 ? 4 : action == 1 ? 4
                                                                                        : 5));
                }
                if (y >= 260.0f && y < 572.0f)
                    return "PATTERN SLOT / CLICK LOADS, SHIFT+CLICK SAVES, RIGHT CLICK QUEUES";
            }
            if (view != View::Grid && y >= 119.0f && y < 153.0f && x >= 868.0f)
                return "TRACK SELECTOR / CHOOSE FM 1-4 OR NOISE";
            return "CTRL+K OPENS COMMANDS / F6 SHOWS CONTEXT HELP";
        }

        void drawTooltip(SDL_Renderer *renderer, const AppState &app,
                         const Palette &palette) const
        {
            if (!hoverActive || hoverSeconds < 0.45 || overlay != Overlay::None || helpVisible)
                return;
            std::string message = tooltipAt(app);
            if (message.empty())
                return;
            if (message.size() > 78u)
                message.resize(78u);
            const int width = std::min(720, textWidth(message, 1) + 26);
            const int x = clampInt(static_cast<int>(hoverX) + 14, 8, kLogicalWidth - width - 8);
            const int y = clampInt(static_cast<int>(hoverY) + 18, 112, kLogicalHeight - 42);
            fillRect(renderer, x, y, width, 30, palette.raised);
            strokeRect(renderer, x, y, width, 30, palette.lineStrong);
            fillRect(renderer, x, y, 3, 30, palette.accent);
            drawText(renderer, x + 12, y + 10, message, palette.text, 1);
        }

        void drawToast(SDL_Renderer *renderer, const Palette &palette) const
        {
            if (toastSeconds <= 0.0 || toastMessage.empty())
                return;
            const double fade = std::min(1.0, toastSeconds * 2.0);
            const std::uint8_t alpha = static_cast<std::uint8_t>(std::lround(235.0 * fade));
            const int width = std::min(760, textWidth(toastMessage, 2) + 54);
            const int x = (kLogicalWidth - width) / 2;
            const int y = overlay == Overlay::ControllerMap ? 686 : 574;
            const Color background{palette.raised.r, palette.raised.g, palette.raised.b, alpha};
            const Color border = toastIsError ? palette.text : palette.accent;
            fillRect(renderer, x, y, width, 44, background);
            fillRect(renderer, x, y, 4, 44, border);
            drawTextCentered(renderer, kLogicalWidth / 2, y + 15,
                             (toastIsError ? "! " : "") + toastMessage, palette.text, 2);
        }

        void drawHintPanel(SDL_Renderer *renderer, const Palette &palette,
                           int panelX = 954, int panelY = 118,
                           int panelWidth = 298, int panelHeight = 512) const
        {
            fillRect(renderer, panelX, panelY, panelWidth, panelHeight, palette.surface);
            strokeRect(renderer, panelX, panelY, panelWidth, panelHeight, palette.lineStrong);
            fillRect(renderer, panelX, panelY, panelWidth, 4, palette.accent);
            drawText(renderer, panelX + 18, panelY + 18, "CONTEXT HINTS", palette.text, 2);
            drawTextRight(renderer, panelX + panelWidth - 18, panelY + 23, "F6 HIDE", palette.muted, 1);
            drawLine(renderer, panelX + 18, panelY + 52, panelX + panelWidth - 18, panelY + 52,
                     palette.lineStrong);

            std::string context;
            if (overlay == Overlay::Palette)
                context = "PALETTE";
            else if (overlay == Overlay::ControllerMap)
                context = "CONTROLLER MAP";
            else if (overlay == Overlay::BankName)
                context = "BANK NAME";
            else if (overlay == Overlay::PatternName)
                context = "PATTERN NAME";
            else if (overlay == Overlay::ProjectMenu)
                context = "PROJECT ACTIONS";
            else if (overlay == Overlay::CommandPalette)
                context = "COMMAND PALETTE";
            else
                context = std::string(kViewNames[static_cast<std::size_t>(view)]) + " / " +
                          (selectedTrack == kTrackCount - 1 ? "NOISE" : "FM " + decimalValue(selectedTrack + 1));
            drawText(renderer, panelX + 18, panelY + 68, context, palette.accent, 1);

            int row = 0;
            const auto hint = [&](const char *keys, const char *description)
            {
                const int y = panelY + 98 + row * 46;
                drawText(renderer, panelX + 18, y, keys, palette.accent, 1);
                drawText(renderer, panelX + 18, y + 18, description, palette.text, 1);
                drawLine(renderer, panelX + 18, y + 37, panelX + panelWidth - 18, y + 37,
                         palette.line);
                ++row;
            };

            if (overlay == Overlay::Palette)
            {
                hint("ARROWS", "SELECT SOUND SLOT");
                hint("ENTER", "RECALL SOUND TO STEP");
                hint("SHIFT+ENTER", "STORE STEP SOUND");
                hint("CTRL+ENTER", "APPLY SOUND TO TRACK");
                hint("DELETE / R", "CLEAR SLOT / RANDOM SOUND");
                hint("P / ESC", "CLOSE PALETTE");
            }
            else if (overlay == Overlay::ControllerMap)
            {
                hint("ARROWS", "SELECT CONTROLLER ACTION");
                hint("ENTER", "CAPTURE NEXT BUTTON");
                hint("DELETE", "UNBIND ACTION");
                hint("D / E", "DEFAULTS / ENABLE INPUT");
                hint("F4 / ESC", "CLOSE CONTROLLER MAP");
            }
            else if (overlay == Overlay::BankName)
            {
                hint("TYPE", "ENTER FOUR BANK CHARACTERS");
                hint("LEFT / RIGHT", "MOVE CHARACTER CURSOR");
                hint("DELETE", "CLEAR CURRENT CHARACTER");
                hint("ENTER", "SAVE BANK NAME");
                hint("ESC", "CANCEL NAME EDIT");
            }
            else if (overlay == Overlay::PatternName)
            {
                hint("TYPE", "ENTER UP TO 12 CHARACTERS");
                hint("BACKSPACE", "REMOVE LAST CHARACTER");
                hint("ENTER", "SAVE PATTERN NAME");
                hint("ESC", "CANCEL NAME EDIT");
            }
            else if (overlay == Overlay::ProjectMenu)
            {
                hint("UP / DOWN", "SELECT PROJECT ACTION");
                hint("ENTER", "ARM OR CONFIRM ACTION");
                hint("ESC", "CANCEL PROJECT MENU");
                hint("CTRL+N", "OPEN START NEW ACTION");
                hint("CTRL+SHIFT+BKSP", "OPEN CLEAR TRACKS ACTION");
            }
            else if (overlay == Overlay::CommandPalette)
            {
                hint("UP / DOWN", "SELECT A COMMAND");
                hint("ENTER", "RUN THE SELECTED COMMAND");
                hint("CTRL+K / ESC", "CLOSE COMMAND PALETTE");
            }
            else
            {
                switch (view)
                {
                case View::Grid:
                    hint("ARROWS", "MOVE TRACK OR STEP");
                    hint("SHIFT+ARROWS", "EXTEND RECTANGULAR RANGE");
                    hint("ENTER / DELETE", "TOGGLE OR CLEAR STEP");
                    hint("[ ] / - =", "SELECT FIELD / CHANGE VALUE");
                    hint("F5", "CYCLE STEP, TRACK, ALL SCOPE");
                    hint("P / T", "PALETTE / TRIGLESS STEP");
                    hint("R / SHIFT+R", "NUDGE FIELD / RANDOM TRACK");
                    break;
                case View::Synth:
                    hint("LEFT / RIGHT", "SELECT SYNTH FIELD");
                    hint("UP / DOWN", "ADJUST SELECTED FIELD");
                    hint("- =", "FINE OR COARSE VALUE EDIT");
                    hint("B", "TOGGLE BASIC / DEEP SYNTH");
                    hint("PAGE UP/DOWN", "SELECT PREVIOUS/NEXT STEP");
                    hint("P", "OPEN STEP SOUND PALETTE");
                    break;
                case View::Echo:
                    hint("LEFT / RIGHT", "SELECT ECHO FIELD");
                    hint("UP / DOWN", "ADJUST REPEAT SETTINGS");
                    hint("[ ]", "MOVE FIELD FOCUS");
                    hint("TAB", "MOVE TO ANOTHER VIEW");
                    break;
                case View::Transpose:
                    hint("LEFT / RIGHT", "SELECT TRANSPOSE STEP");
                    hint("UP / DOWN", "ADJUST SEMITONE OFFSET");
                    hint("ENTER", "ACTIVATE SELECTED CONTROL");
                    hint("1 - 4", "SELECT FM TRACK");
                    break;
                case View::Mod:
                    hint("LEFT / RIGHT", "SELECT MODULATOR FIELD");
                    hint("UP / DOWN", "ADJUST TARGET OR SHAPE");
                    hint("[ ]", "MOVE FIELD FOCUS");
                    hint("TAB", "MOVE TO ANOTHER VIEW");
                    break;
                case View::Scale:
                    hint("LEFT / RIGHT", "SELECT ROOT, NOTE, PRESET");
                    hint("UP / DOWN", "CHANGE SELECTED VALUE");
                    hint("ENTER", "TOGGLE NOTE OR APPLY PRESET");
                    hint("TAB", "MOVE TO ANOTHER VIEW");
                    break;
                case View::Data:
                    hint("ARROWS", "SELECT BANK AND SLOT");
                    hint("V / F8", "TOGGLE PERFORM / MANAGE");
                    if (dataWorkspace == DataWorkspace::Perform)
                    {
                        hint("ENTER / Q", "LOAD / QUEUE SELECTED SLOT");
                        hint("SHIFT+ENTER", "SAVE SLOT OR COLUMN");
                        hint("A / I", "TARGET ALL / TOGGLE LOAD MODE");
                    }
                    else
                    {
                        hint("E / C", "NAME / COLOR PATTERN SLOT");
                        hint("N / K", "NAME / LOCK CURRENT BANK");
                        hint("B / G", "BANK BPM / SCALE CONTROLS");
                    }
                    break;
                }
            }
            drawText(renderer, panelX + 18, panelY + panelHeight - 31,
                     "F1 FULL MAP   F6 TOGGLE PANEL", palette.muted, 1);
        }

        void drawCommandPaletteOverlay(SDL_Renderer *renderer, const Palette &palette) const
        {
            fillRect(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {0, 0, 0, 190});
            fillRect(renderer, 290, 82, 700, 588, palette.surface);
            strokeRect(renderer, 290, 82, 700, 588, palette.lineStrong);
            fillRect(renderer, 290, 82, 700, 4, palette.accent);
            drawText(renderer, 322, 112, "COMMAND PALETTE", palette.text, 3);
            drawTextRight(renderer, 958, 119, "CTRL+K / ESC CLOSE", palette.muted, 1);
            drawText(renderer, 322, 145, "FAST ROUTES WITHOUT MEMORIZING THE WHOLE CONTROL MAP", palette.faint, 1);
            for (int command = 0; command < kCommandCount; ++command)
            {
                const int y = 174 + command * 38;
                const bool selected = commandCursor == command;
                if (selected)
                    fillRect(renderer, 318, y, 644, 34, palette.accentDim);
                if (selected)
                    fillRect(renderer, 318, y, 3, 34, palette.accent);
                drawText(renderer, 334, y + 7, commandName(command),
                         selected ? palette.accent : palette.text, 1);
                drawTextRight(renderer, 946, y + 7, commandDescription(command),
                              selected ? palette.text : palette.muted, 1);
                drawLine(renderer, 318, y + 35, 962, y + 35, palette.line);
            }
            drawTextCentered(renderer, 640, 642, "ARROWS SELECT   ENTER RUN", palette.muted, 1);
        }

        void drawOnboardingPanel(SDL_Renderer *renderer, const Palette &palette,
                                 int x = 872, int y = 374, int width = 380,
                                 int height = 256) const
        {
            fillRect(renderer, x, y, width, height, palette.surface);
            strokeRect(renderer, x, y, width, height, palette.lineStrong);
            fillRect(renderer, x, y, 4, height, palette.accent);
            drawText(renderer, x + 18, y + 17, "MAKE AN AMBIENT LOOP", palette.text, 2);
            drawTextRight(renderer, x + width - 18, y + 20,
                          onboardingStartedTransport && onboardingPlacedStep &&
                                  onboardingChangedSound && onboardingSavedPattern
                              ? "READY"
                              : "QUICK START",
                          palette.accent, 1);
            drawText(renderer, x + 18, y + 46,
                     "FOUR SMALL MOVES. NOTHING HERE IS MODAL.", palette.muted, 1);
            const std::array<bool, 4> complete{{onboardingStartedTransport, onboardingPlacedStep,
                                                onboardingChangedSound, onboardingSavedPattern}};
            static constexpr std::array<const char *, 4> keys{{"SPACE", "GRID / ENTER", "SYNTH BASIC", "DATA / SHIFT+ENTER"}};
            static constexpr std::array<const char *, 4> descriptions{{"START THE TRANSPORT", "PLACE OR TOGGLE A STEP", "TURN MOTION, RELEASE, OR SPACE",
                                                                       "SAVE THE PATTERN SLOT"}};
            for (int item = 0; item < 4; ++item)
            {
                const int rowY = y + 76 + item * 36;
                strokeRect(renderer, x + 18, rowY, 14, 14,
                           complete[static_cast<std::size_t>(item)] ? palette.accent : palette.lineStrong);
                if (complete[static_cast<std::size_t>(item)])
                    fillRect(renderer, x + 21, rowY + 3, 8, 8, palette.accent);
                drawText(renderer, x + 44, rowY + 2, keys[static_cast<std::size_t>(item)],
                         complete[static_cast<std::size_t>(item)] ? palette.accent : palette.text, 1);
                drawTextRight(renderer, x + width - 18, rowY + 2,
                              descriptions[static_cast<std::size_t>(item)], palette.muted, 1);
            }
            fillRect(renderer, x + width - 128, y + height - 34, 110, 22, palette.raised);
            strokeRect(renderer, x + width - 128, y + height - 34, 110, 22, palette.lineStrong);
            drawTextCentered(renderer, x + width - 73, y + height - 27, "F7 DISMISS", palette.faint, 1);
            drawText(renderer, x + 18, y + height - 27, "CTRL+K REOPENS", palette.faint, 1);
        }

        void drawHelp(SDL_Renderer *renderer, const Palette &palette) const
        {
            fillRect(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {0, 0, 0, 205});
            fillRect(renderer, 128, 78, 1024, 604, palette.surface);
            strokeRect(renderer, 128, 78, 1024, 604, palette.lineStrong);
            fillRect(renderer, 128, 78, 1024, 4, palette.accent);
            drawText(renderer, 164, 112, "FMS CONTROL MAP", palette.text, 3);
            drawTextRight(renderer, 1116, 119, "F1 / ? / CLICK TO CLOSE", palette.muted, 1);
            drawLine(renderer, 164, 154, 1116, 154, palette.lineStrong);

            static constexpr std::array<std::array<const char *, 2>, 14> controls{{
                {{"SPACE", "START / STOP TRANSPORT"}},
                {{"ARROWS", "MOVE CURSOR / SELECT FIELD"}},
                {{"SHIFT+ARROWS", "EXTEND RECTANGULAR RANGE"}},
                {{"ENTER", "TOGGLE STEP / ACTIVATE / LOAD"}},
                {{"[  ]", "SELECT PARAMETER"}},
                {{"-  =", "ADJUST VALUE  /  SHIFT COARSE"}},
                {{"1 - 5", "SELECT TRACK"}},
                {{"TAB", "NEXT VIEW  /  SHIFT PREVIOUS"}},
                {{"CTRL+K", "SEARCH AND RUN COMMANDS"}},
                {{"F5", "EDIT SCOPE STEP/RANGE/TRACK/ALL"}},
                {{"C / X / V", "COPY / CUT / PASTE RANGE"}},
                {{"R / SHIFT+R", "NUDGE PARAM / RANDOMIZE TRACK"}},
                {{"P", "SOUND PALETTE 14 FM + 14 NOISE"}},
                {{"S", "CAPTURE / SWAP SNAPSHOT"}},
            }};
            static constexpr std::array<std::array<const char *, 2>, 10> controlsRight{{
                {{",  .", "BPM DOWN / UP"}},
                {{"O / CTRL+O", "ROTATE TRACK / ALL TRACKS"}},
                {{"D / ALT UP/DOWN", "DIRECTION / SHUFFLE; MODIFIERS ALL"}},
                {{"M / SHIFT+M / U", "MUTE / SOLO / UNMUTE ALL"}},
                {{"SYNTH B", "BASIC MACROS / GROUPED DEEP EDITOR"}},
                {{"DATA V / F8", "PERFORM / MANAGE WORKSPACES"}},
                {{"DATA X / R", "CLEAR / RANDOM (X / ? SLOTS)"}},
                {{"DATA E / C / N / K", "PATTERN NAME/COLOR; BANK NAME/LOCK"}},
                {{"CTRL+N / DROP .FMS", "NEW SESSION / OPEN ANY PROJECT FILE"}},
                {{"F2-F4 / F6 / F7", "STYLE / PAD / HINTS / QUICK START"}},
            }};
            for (int i = 0; i < static_cast<int>(controls.size()); ++i)
            {
                const int y = 180 + i * 31;
                drawText(renderer, 164, y, controls[static_cast<std::size_t>(i)][0], palette.accent, 1);
                drawText(renderer, 306, y, controls[static_cast<std::size_t>(i)][1], palette.text, 1);
            }
            drawLine(renderer, 628, 176, 628, 625, palette.line);
            for (int i = 0; i < static_cast<int>(controlsRight.size()); ++i)
            {
                const int y = 180 + i * 39;
                drawText(renderer, 660, y, controlsRight[static_cast<std::size_t>(i)][0], palette.accent, 1);
                drawText(renderer, 660, y + 17, controlsRight[static_cast<std::size_t>(i)][1], palette.text, 1);
            }
            drawText(renderer, 660, 584, "GRID MOUSE", palette.muted, 1);
            drawText(renderer, 660, 605, "LEFT TOGGLE  RIGHT TRIGLESS", palette.text, 1);
            drawText(renderer, 164, 647,
                     "DATA RIBBON: LOAD / QUEUE / SAVE / CLEAR / RANDOM / TARGET / MODE",
                     palette.muted, 1);
        }

        void render(SDL_Renderer *renderer, int width, int height)
        {
            outputWidth = std::max(1, width);
            outputHeight = std::max(1, height);
            constexpr int inspectorWidth = 330;
            constexpr int inspectorGap = 12;
            wideHintInspector = !helpVisible && (hintPanelVisible || onboardingVisible) &&
                                static_cast<double>(outputWidth) / static_cast<double>(outputHeight) >= 2.05;
            const int combinedLogicalWidth = kLogicalWidth +
                                             (wideHintInspector ? inspectorWidth + inspectorGap : 0);
            viewportScale = std::min(static_cast<float>(outputWidth) /
                                         static_cast<float>(combinedLogicalWidth),
                                     static_cast<float>(outputHeight) /
                                         static_cast<float>(kLogicalHeight));
            const int scaledWidth = static_cast<int>(std::lround(static_cast<double>(kLogicalWidth) * viewportScale));
            const int scaledHeight = static_cast<int>(std::lround(static_cast<double>(kLogicalHeight) * viewportScale));
            const int scaledInspectorWidth = wideHintInspector
                                                 ? static_cast<int>(std::lround(
                                                       static_cast<double>(inspectorWidth) * viewportScale))
                                                 : 0;
            const int scaledInspectorGap = wideHintInspector
                                               ? static_cast<int>(std::lround(
                                                     static_cast<double>(inspectorGap) * viewportScale))
                                               : 0;
            const int totalScaledWidth = scaledWidth + scaledInspectorGap + scaledInspectorWidth;
            viewportX = (outputWidth - totalScaledWidth) / 2;
            viewportY = (outputHeight - scaledHeight) / 2;

            SDL_RenderSetViewport(renderer, nullptr);
            SDL_RenderSetScale(renderer, 1.0f, 1.0f);
            const AppState app = stateCopy();
            const Palette palette = makePalette(app.lightTheme, app.accent);
            fillRect(renderer, 0, 0, outputWidth, outputHeight, palette.outside);

            const SDL_Rect viewport{viewportX, viewportY, scaledWidth, scaledHeight};
            SDL_RenderSetViewport(renderer, &viewport);
            SDL_RenderSetScale(renderer, viewportScale, viewportScale);
            fillRect(renderer, 0, 0, kLogicalWidth, kLogicalHeight, palette.background);
            drawHeader(renderer, app, palette);
            switch (view)
            {
            case View::Grid:
                drawGrid(renderer, app, palette);
                break;
            case View::Synth:
                drawSynth(renderer, app, palette);
                break;
            case View::Echo:
                drawEcho(renderer, app, palette);
                break;
            case View::Transpose:
                drawTranspose(renderer, app, palette);
                break;
            case View::Mod:
                drawMod(renderer, app, palette);
                break;
            case View::Scale:
                drawScale(renderer, app, palette);
                break;
            case View::Data:
                drawData(renderer, app, palette);
                break;
            }
            if (overlay == Overlay::Palette)
                drawPaletteOverlay(renderer, app, palette);
            else if (overlay == Overlay::ControllerMap)
                drawControllerOverlay(renderer, app, palette);
            else if (overlay == Overlay::BankName)
                drawBankNameOverlay(renderer, palette);
            else if (overlay == Overlay::PatternName)
                drawPatternNameOverlay(renderer, palette);
            else if (overlay == Overlay::ProjectMenu)
                drawProjectMenuOverlay(renderer, palette);
            else if (overlay == Overlay::ProjectName)
                drawProjectNameOverlay(renderer, palette);
            else if (overlay == Overlay::ProjectBrowser)
                drawProjectBrowserOverlay(renderer, palette);
            else if (overlay == Overlay::CommandPalette)
                drawCommandPaletteOverlay(renderer, palette);
            if (hintPanelVisible && !helpVisible && !wideHintInspector)
                drawHintPanel(renderer, palette);
            if (onboardingVisible && overlay == Overlay::None && !helpVisible &&
                !(wideHintInspector && !hintPanelVisible))
                drawOnboardingPanel(renderer, palette);
            drawTooltip(renderer, app, palette);
            drawToast(renderer, palette);
            if (shutterSeconds > 0.0)
            {
                const std::uint8_t alpha = static_cast<std::uint8_t>(
                    std::lround(150.0 * std::min(1.0, shutterSeconds / 0.10)));
                fillRect(renderer, 0, 0, kLogicalWidth, kLogicalHeight,
                         {palette.text.r, palette.text.g, palette.text.b, alpha});
            }
            if (helpVisible)
                drawHelp(renderer, palette);

            SDL_RenderSetScale(renderer, 1.0f, 1.0f);
            SDL_RenderSetViewport(renderer, nullptr);
            if (wideHintInspector)
            {
                const SDL_Rect inspectorViewport{
                    viewportX + scaledWidth + scaledInspectorGap, viewportY,
                    scaledInspectorWidth, scaledHeight};
                SDL_RenderSetViewport(renderer, &inspectorViewport);
                SDL_RenderSetScale(renderer, viewportScale, viewportScale);
                fillRect(renderer, 0, 0, inspectorWidth, kLogicalHeight, palette.surface);
                if (hintPanelVisible)
                    drawHintPanel(renderer, palette, 0, 0, inspectorWidth, kLogicalHeight);
                else if (onboardingVisible)
                    drawOnboardingPanel(renderer, palette, 0, 0, inspectorWidth, 320);
                SDL_RenderSetScale(renderer, 1.0f, 1.0f);
                SDL_RenderSetViewport(renderer, nullptr);
            }
        }
    };

    UiController::UiController(SharedState &state, AudioEngine &audio)
        : impl_(new Impl(state, audio)) {}

    UiController::~UiController()
    {
        delete impl_;
    }

    bool UiController::handleEvent(const SDL_Event &event)
    {
        return impl_->handleEvent(event);
    }

    void UiController::update(double deltaSeconds)
    {
        impl_->update(deltaSeconds);
    }

    void UiController::render(SDL_Renderer *renderer, int width, int height)
    {
        impl_->render(renderer, width, height);
    }

    bool UiController::consumeSaveRequest()
    {
        const bool requested = impl_->saveRequested;
        impl_->saveRequested = false;
        return requested;
    }

    bool UiController::consumeProjectRequest(ProjectRequest &request)
    {
        if (!impl_->projectRequest.has_value())
            return false;
        request = std::move(*impl_->projectRequest);
        impl_->projectRequest.reset();
        return true;
    }

    bool UiController::isDirty() const
    {
        return impl_->isDirty();
    }

    void UiController::markSaved()
    {
        impl_->markSaved();
    }

    void UiController::setProjectPath(const std::string &path)
    {
        impl_->setProjectPath(path);
    }

    void UiController::projectStarted(const std::string &path)
    {
        impl_->projectStarted(path);
    }

    void UiController::projectLoaded(const std::string &path)
    {
        impl_->projectLoaded(path);
    }

    void UiController::showToast(const std::string &message, bool error)
    {
        impl_->toast(message, error);
    }

} // namespace fms
