#include "model.hpp"
#include "persistence.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;
using Tag = std::array<std::uint8_t, 4>;

constexpr std::array<std::uint8_t, 8> kLegacyMagic {
    'F', 'M', 'S', 'S', 'T', 'A', 'T', 'E'
};
constexpr std::uint32_t kLegacyHeaderSize = 32;
constexpr std::uint16_t kLegacySectionVersion = 1;

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

class Writer {
public:
    void putU8(std::uint8_t value) { bytes_.push_back(value); }
    void putI8(std::int8_t value) { putU8(static_cast<std::uint8_t>(value)); }

    void putU16(std::uint16_t value) {
        putU8(static_cast<std::uint8_t>(value & 0xFFU));
        putU8(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    }

    void putU32(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            putU8(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
        }
    }

    void putU64(std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            putU8(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
        }
    }

    void putRaw(const void* source, std::size_t size) {
        const auto* first = static_cast<const std::uint8_t*>(source);
        bytes_.insert(bytes_.end(), first, first + size);
    }

    const Bytes& data() const { return bytes_; }
    std::size_t size() const { return bytes_.size(); }

private:
    Bytes bytes_;
};

void writeBool(Writer& writer, bool value) {
    writer.putU8(value ? 1U : 0U);
}

std::uint32_t crc32(const std::uint8_t* bytes, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= bytes[index];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

void writeLegacyFmPatch(Writer& writer, const fms::FmPatch& patch) {
    writer.putU8(patch.ampAttack);
    writer.putU8(patch.ampHold);
    writer.putU8(patch.ampRelease);
    writer.putU8(patch.modRatio);
    writer.putU8(patch.modDepth);
    writer.putU8(patch.modFeedback);
    writer.putU8(patch.modAttack);
    writer.putU8(patch.modRelease);
    writer.putU8(patch.modEnd);
    writer.putI8(patch.sweepDepth);
    writer.putU8(patch.sweepRelease);
}

void writeLegacyNoisePatch(Writer& writer, const fms::NoisePatch& patch) {
    writer.putU8(patch.ampAttack);
    writer.putU8(patch.ampHold);
    writer.putU8(patch.ampRelease);
    writer.putU8(patch.rate);
    writeBool(writer, patch.narrow);
}

// This deliberately duplicates the released 1.0 schema instead of calling the
// production encoder. It remains a regression fixture after new saves move on.
void writeLegacyStep(Writer& writer, const fms::Step& step) {
    writeBool(writer, step.active);
    writeBool(writer, step.trigless);
    writer.putU8(step.note);
    writer.putU8(step.level);
    writer.putU8(static_cast<std::uint8_t>(step.pan));
    writer.putU8(step.portamento);
    writer.putU8(step.condition);
    writer.putI8(step.microTicks);
    writeBool(writer, step.echo);
    writeBool(writer, step.transpose);
    writer.putU8(static_cast<std::uint8_t>(step.mode));
    for (const std::int8_t interval : step.chord) writer.putI8(interval);
    writeLegacyFmPatch(writer, step.fm);
    writeLegacyNoisePatch(writer, step.noise);
}

void writeLegacyTrack(Writer& writer, const fms::TrackData& track) {
    for (const fms::Step& step : track.steps) writeLegacyStep(writer, step);
    writer.putU8(track.length);
    writer.putU8(track.rateIndex);
    writer.putU8(static_cast<std::uint8_t>(track.direction));
    writer.putU8(track.shuffle);
    writeBool(writer, track.muted);
    writeBool(writer, track.solo);

    writer.putU8(track.echo.repeats);
    writer.putU8(track.echo.speedTicks);
    writer.putI8(track.echo.transpose);
    writer.putU8(track.echo.transposeModulo);
    writer.putI8(track.echo.volumeDelta);
    writer.putI8(track.echo.modDelta);
    writer.putI8(track.echo.feedbackDelta);
    writer.putU8(static_cast<std::uint8_t>(track.echo.pan));

    for (const std::int8_t value : track.transpose.values) writer.putI8(value);
    writer.putU8(track.transpose.length);
    writer.putU8(track.transpose.rate);
    writer.putU8(static_cast<std::uint8_t>(track.transpose.advance));

    writer.putU8(track.modulator.targetTrack);
    writer.putU8(static_cast<std::uint8_t>(track.modulator.destination));
    writer.putU8(track.modulator.speed);
    writer.putU8(static_cast<std::uint8_t>(track.modulator.wave));
    writer.putI8(track.modulator.depth);
    writer.putU8(track.modulator.offset);
}

void appendLegacyChunk(Writer& payload, const Tag& tag, const Writer& contents) {
    payload.putRaw(tag.data(), tag.size());
    payload.putU16(kLegacySectionVersion);
    payload.putU16(0U);
    payload.putU32(static_cast<std::uint32_t>(contents.size()));
    payload.putRaw(contents.data().data(), contents.size());
}

Bytes makeLegacyFixture(const fms::AppState& state) {
    constexpr Tag globalTag {'G', 'L', 'O', 'B'};
    constexpr Tag tracksTag {'T', 'R', 'A', 'K'};
    constexpr Tag patternsTag {'P', 'A', 'T', 'T'};
    constexpr Tag banksTag {'B', 'A', 'N', 'K'};
    constexpr Tag fmPaletteTag {'F', 'M', 'P', 'A'};
    constexpr Tag noisePaletteTag {'N', 'S', 'P', 'A'};

    Writer payload;
    payload.putU32(6U);

    Writer global;
    global.putU16(state.bpm);
    global.putU8(state.scaleRoot);
    global.putU16(state.scaleMask);
    writeBool(global, state.lightTheme);
    global.putU8(state.accent);
    global.putU64(state.editRevision);
    appendLegacyChunk(payload, globalTag, global);

    Writer tracks;
    tracks.putU16(static_cast<std::uint16_t>(state.tracks.size()));
    for (const fms::TrackData& track : state.tracks) writeLegacyTrack(tracks, track);
    appendLegacyChunk(payload, tracksTag, tracks);

    Writer patterns;
    patterns.putU16(static_cast<std::uint16_t>(state.patterns.size()));
    patterns.putU16(static_cast<std::uint16_t>(state.patterns.front().size()));
    for (const auto& trackPatterns : state.patterns) {
        for (const fms::StoredPattern& pattern : trackPatterns) {
            writeBool(patterns, pattern.occupied);
            writeLegacyTrack(patterns, pattern.track);
        }
    }
    appendLegacyChunk(payload, patternsTag, patterns);

    Writer banks;
    banks.putU16(static_cast<std::uint16_t>(state.banks.size()));
    for (const fms::BankSettings& bank : state.banks) {
        for (const char character : bank.name) {
            banks.putU8(static_cast<std::uint8_t>(static_cast<unsigned char>(character)));
        }
        writeBool(banks, bank.locked);
        writeBool(banks, bank.hasTempo);
        writeBool(banks, bank.hasScale);
        banks.putU16(bank.tempo);
        banks.putU16(bank.scaleMask);
        banks.putU8(bank.scaleRoot);
    }
    appendLegacyChunk(payload, banksTag, banks);

    Writer fmPalette;
    fmPalette.putU16(static_cast<std::uint16_t>(state.fmPalette.size()));
    for (const fms::Step& step : state.fmPalette) writeLegacyStep(fmPalette, step);
    appendLegacyChunk(payload, fmPaletteTag, fmPalette);

    Writer noisePalette;
    noisePalette.putU16(static_cast<std::uint16_t>(state.noisePalette.size()));
    for (const fms::Step& step : state.noisePalette) writeLegacyStep(noisePalette, step);
    appendLegacyChunk(payload, noisePaletteTag, noisePalette);

    Writer header;
    header.putRaw(kLegacyMagic.data(), kLegacyMagic.size());
    header.putU16(1U);
    header.putU16(0U);
    header.putU32(kLegacyHeaderSize);
    header.putU64(static_cast<std::uint64_t>(payload.size()));
    header.putU32(crc32(payload.data().data(), payload.size()));
    header.putU32(crc32(header.data().data(), header.size()));

    Writer file;
    file.putRaw(header.data().data(), header.size());
    file.putRaw(payload.data().data(), payload.size());
    return file.data();
}

bool writeFile(const fs::path& path, const Bytes& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

Bytes readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("could not open test file");
    input.seekg(0, std::ios::end);
    const std::streamoff end = input.tellg();
    if (end < 0) throw std::runtime_error("could not size test file");
    input.seekg(0, std::ios::beg);
    Bytes bytes(static_cast<std::size_t>(end));
    input.read(reinterpret_cast<char*>(bytes.data()), end);
    if (!input) throw std::runtime_error("could not read test file");
    return bytes;
}

std::uint16_t readU16At(const Bytes& bytes, std::size_t position) {
    if (position > bytes.size() || bytes.size() - position < 2U) {
        throw std::runtime_error("test fixture is truncated while reading u16");
    }
    return static_cast<std::uint16_t>(bytes[position]) |
           static_cast<std::uint16_t>(
               static_cast<std::uint16_t>(bytes[position + 1U]) << 8U);
}

std::uint32_t readU32At(const Bytes& bytes, std::size_t position) {
    if (position > bytes.size() || bytes.size() - position < 4U) {
        throw std::runtime_error("test fixture is truncated while reading u32");
    }
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(bytes[position++]) << shift;
    }
    return value;
}

Tag readTagAt(const Bytes& bytes, std::size_t position) {
    if (position > bytes.size() || bytes.size() - position < 4U) {
        throw std::runtime_error("test fixture is truncated while reading tag");
    }
    return Tag {bytes[position], bytes[position + 1U], bytes[position + 2U],
                bytes[position + 3U]};
}

void expectCurrentSchema(const Bytes& bytes) {
    constexpr Tag globalTag {'G', 'L', 'O', 'B'};
    constexpr Tag tracksTag {'T', 'R', 'A', 'K'};
    constexpr Tag patternsTag {'P', 'A', 'T', 'T'};
    constexpr Tag banksTag {'B', 'A', 'N', 'K'};
    constexpr Tag fmPaletteTag {'F', 'M', 'P', 'A'};
    constexpr Tag noisePaletteTag {'N', 'S', 'P', 'A'};
    constexpr Tag controllerTag {'C', 'T', 'R', 'L'};

    if (bytes.size() < kLegacyHeaderSize) {
        expect(false, "current save has a complete header");
        return;
    }
    expect(readU16At(bytes, 8U) == 1U && readU16At(bytes, 10U) == 1U,
           "current save advertises format 1.1");
    const std::uint32_t headerSize = readU32At(bytes, 12U);
    if (headerSize > bytes.size() || bytes.size() - headerSize < 4U) {
        expect(false, "current save has a valid payload offset");
        return;
    }

    std::size_t position = headerSize;
    const std::uint32_t sectionCount = readU32At(bytes, position);
    position += 4U;
    expect(sectionCount == 7U, "current save writes seven sections");

    bool sawGlobal = false;
    bool sawTracks = false;
    bool sawPatterns = false;
    bool sawBanks = false;
    bool sawFmPalette = false;
    bool sawNoisePalette = false;
    bool sawController = false;
    for (std::uint32_t index = 0; index < sectionCount; ++index) {
        if (position > bytes.size() || bytes.size() - position < 12U) {
            throw std::runtime_error("current save has a truncated section header");
        }
        const Tag tag = readTagAt(bytes, position);
        const std::uint16_t version = readU16At(bytes, position + 4U);
        const std::uint16_t flags = readU16At(bytes, position + 6U);
        const std::uint32_t length = readU32At(bytes, position + 8U);
        position += 12U;
        if (length > bytes.size() - position) {
            throw std::runtime_error("current save has a truncated section body");
        }
        expect(flags == 0U, "current save section flags remain zero");

        if (tag == globalTag) {
            sawGlobal = true;
            expect(version == 1U, "GLOB remains at section version 1");
        } else if (tag == tracksTag) {
            sawTracks = true;
            expect(version == 2U, "TRAK uses Step-bearing section version 2");
        } else if (tag == patternsTag) {
            sawPatterns = true;
            expect(version == 2U, "PATT uses Step-bearing section version 2");
        } else if (tag == banksTag) {
            sawBanks = true;
            expect(version == 1U, "BANK remains at section version 1");
        } else if (tag == fmPaletteTag) {
            sawFmPalette = true;
            expect(version == 2U, "FMPA uses Step-bearing section version 2");
        } else if (tag == noisePaletteTag) {
            sawNoisePalette = true;
            expect(version == 2U, "NSPA uses Step-bearing section version 2");
        } else if (tag == controllerTag) {
            sawController = true;
            expect(version == 1U, "CTRL uses section version 1");
            expect(length == 1U + fms::kControllerActionCount,
                   "CTRL stores one enabled byte and one byte per action");
        }
        position += length;
    }
    expect(position == bytes.size(), "current save sections consume the entire payload");
    expect(sawGlobal && sawTracks && sawPatterns && sawBanks && sawFmPalette &&
               sawNoisePalette && sawController,
           "current save contains every required and optional phase-2 section");
}

fms::AdvancedFmPatch makeAdvancedPatch() {
    using namespace fms;
    AdvancedFmPatch patch;
    patch.enabled = true;
    patch.algorithm = AdvancedFmAlgorithm::Algorithm12;
    patch.operators = {{
        {AdvancedOscShape::Triangle, 17, 21, 31, -23},
        {AdvancedOscShape::Saw, 26, 37, 41, 13},
        {AdvancedOscShape::Square, 35, 53, 59, -43},
        {AdvancedOscShape::Noise, 44, 71, 79, 47},
    }};
    patch.ampEnvelope = {11, 22, 33, 44};
    patch.filterMode = AdvancedFilterMode::Notch;
    patch.filterCutoff = 91;
    patch.resonance = 82;
    patch.driveMode = AdvancedDriveMode::Wavefold;
    patch.driveAmount = 73;
    patch.unisonVoices = 4;
    patch.unisonDetune = 63;
    patch.unisonWidth = 117;
    patch.modulation = {{
        {AdvancedModSource::SineLfo, 12, -13, AdvancedModDestination::Pitch},
        {AdvancedModSource::TriangleLfo, 24, 25,
         AdvancedModDestination::FilterCutoff},
        {AdvancedModSource::SampleAndHold, 36, -37,
         AdvancedModDestination::Operator3Level},
        {AdvancedModSource::AmpEnvelope, 48, 49,
         AdvancedModDestination::Operator4Ratio},
    }};
    return patch;
}

fs::path makeTemporaryDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path parent = fs::temp_directory_path();
    for (unsigned attempt = 0; attempt < 128U; ++attempt) {
        const fs::path candidate =
            parent / ("fms-persistence-test-" + std::to_string(stamp) + "-" +
                      std::to_string(attempt));
        std::error_code error;
        if (fs::create_directory(candidate, error)) return candidate;
        if (error && error != std::errc::file_exists) {
            throw std::runtime_error("could not create persistence test directory: " +
                                     error.message());
        }
    }
    throw std::runtime_error("could not allocate persistence test directory");
}

struct DirectoryCleanup {
    fs::path path;
    ~DirectoryCleanup() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

} // namespace

int main() {
    using namespace fms;

    try {
        const DirectoryCleanup temporary {makeTemporaryDirectory()};
        const fs::path legacyPath = temporary.path / "legacy-v1.0.state";
        const fs::path currentPath = temporary.path / "current.state";
        const fs::path corruptPath = temporary.path / "corrupt.state";

        AppState legacySource = makeDefaultState();
        legacySource.bpm = 173;
        legacySource.scaleRoot = 7;
        legacySource.lightTheme = true;
        legacySource.accent = 4;
        legacySource.editRevision = 0x0102030405060708ULL;
        Step& legacyStep = legacySource.tracks[2].steps[5];
        legacyStep.active = true;
        legacyStep.note = 91;
        legacyStep.fm.modRatio = 47;
        legacyStep.noise.rate = 109;
        legacyStep.advancedFm = makeAdvancedPatch();
        legacySource.patterns[2][77].occupied = true;
        legacySource.patterns[2][77].track = legacySource.tracks[2];
        legacySource.banks[3].name = {'T', 'E', 'S', 'T', '\0'};
        legacySource.banks[3].locked = true;
        legacySource.banks[3].hasTempo = true;
        legacySource.banks[3].hasScale = true;
        legacySource.banks[3].tempo = 207;
        legacySource.banks[3].scaleRoot = 5;
        legacySource.banks[3].scaleMask = 0x06ADU;
        legacySource.fmPalette[4].fm.modDepth = 101;
        legacySource.fmPalette[4].advancedFm = makeAdvancedPatch();
        legacySource.noisePalette[9].noise.narrow = true;
        legacySource.noisePalette[9].noise.rate = 117;
        legacySource.noisePalette[9].advancedFm = makeAdvancedPatch();
        legacySource.controller.enabled = false;
        legacySource.controller.buttons.fill(kControllerButtonUnbound);

        const Bytes legacyBytes = makeLegacyFixture(legacySource);
        expect(writeFile(legacyPath, legacyBytes), "write independent v1.0 fixture");
        AppState legacyLoaded;
        std::string error;
        expect(loadState(legacyLoaded, legacyPath.string(), error),
               "load released v1.0 fixture: " + error);
        expect(legacyLoaded.bpm == 173 && legacyLoaded.scaleRoot == 7 &&
                   legacyLoaded.lightTheme && legacyLoaded.accent == 4,
               "legacy global settings survive migration");
        expect(legacyLoaded.tracks[2].steps[5].active &&
                   legacyLoaded.tracks[2].steps[5].note == 91 &&
                   legacyLoaded.tracks[2].steps[5].fm.modRatio == 47 &&
                   legacyLoaded.tracks[2].steps[5].noise.rate == 109,
               "legacy step and patch fields survive migration");
        expect(legacyLoaded.patterns[2][77].occupied &&
                   legacyLoaded.patterns[2][77].track.steps[5].note == 91,
               "legacy pattern library survives migration");
        expect(legacyLoaded.banks[3].name == legacySource.banks[3].name &&
                   legacyLoaded.banks[3].locked && legacyLoaded.banks[3].tempo == 207 &&
                   legacyLoaded.banks[3].scaleMask == 0x06ADU,
               "legacy bank settings survive migration");
        expect(legacyLoaded.fmPalette[4].fm.modDepth == 101 &&
                   legacyLoaded.noisePalette[9].noise.narrow &&
                   legacyLoaded.noisePalette[9].noise.rate == 117,
               "legacy sound palettes survive migration");
        const AdvancedFmPatch advancedDefaults {};
        expect(legacyLoaded.tracks[2].steps[5].advancedFm == advancedDefaults &&
                   legacyLoaded.patterns[2][77].track.steps[5].advancedFm ==
                       advancedDefaults &&
                   legacyLoaded.fmPalette[4].advancedFm == advancedDefaults &&
                   legacyLoaded.noisePalette[9].advancedFm == advancedDefaults,
               "legacy Step-bearing sections receive safe disabled advanced defaults");
        expect(legacyLoaded.controller == ControllerSettings {},
               "legacy save without CTRL receives default controller mappings");

        AppState current = makeDefaultState();
        current.bpm = 219;
        const AdvancedFmPatch trackPatch = makeAdvancedPatch();
        AdvancedFmPatch patternPatch = trackPatch;
        patternPatch.algorithm = AdvancedFmAlgorithm::Algorithm10;
        patternPatch.filterCutoff = 90;
        AdvancedFmPatch fmPalettePatch = trackPatch;
        fmPalettePatch.algorithm = AdvancedFmAlgorithm::Algorithm8;
        fmPalettePatch.driveAmount = 72;
        AdvancedFmPatch noisePalettePatch = trackPatch;
        noisePalettePatch.algorithm = AdvancedFmAlgorithm::Algorithm6;
        noisePalettePatch.unisonWidth = 116;
        current.tracks[1].steps[6].advancedFm = trackPatch;
        current.patterns[4][127].occupied = true;
        current.patterns[4][127].track = current.tracks[4];
        current.patterns[4][127].track.steps[3].advancedFm = patternPatch;
        current.banks[7].name = {'L', 'I', 'V', 'E', '\0'};
        current.banks[7].locked = true;
        current.banks[7].hasTempo = true;
        current.banks[7].hasScale = true;
        current.banks[7].tempo = 141;
        current.banks[7].scaleRoot = 9;
        current.banks[7].scaleMask = 0x05ADU;
        current.fmPalette[13].fm.modFeedback = 93;
        current.fmPalette[13].advancedFm = fmPalettePatch;
        current.noisePalette[13].noise.rate = 123;
        current.noisePalette[13].advancedFm = noisePalettePatch;
        current.controller.enabled = false;
        current.controller.buttons = {{
            31, kControllerButtonUnbound, 29, 28, 27, 26, 25, 24, 23,
            22, 21, 20, 19, 18, 17, 16, 15, 14,
        }};

        expect(saveState(current, currentPath.string(), error), "save current state: " + error);
        expectCurrentSchema(readFile(currentPath));
        AppState roundTrip;
        expect(loadState(roundTrip, currentPath.string(), error), "load current state: " + error);
        expect(roundTrip.bpm == 219 && roundTrip.patterns[4][127].occupied,
               "current global and pattern values round-trip");
        expect(roundTrip.banks[7].name == current.banks[7].name &&
                   roundTrip.banks[7].locked && roundTrip.banks[7].tempo == 141 &&
                   roundTrip.banks[7].scaleRoot == 9 &&
                   roundTrip.banks[7].scaleMask == 0x05ADU,
               "current bank settings round-trip");
        expect(roundTrip.fmPalette[13].fm.modFeedback == 93 &&
                   roundTrip.noisePalette[13].noise.rate == 123,
               "current palettes round-trip");
        expect(roundTrip.tracks[1].steps[6].advancedFm == trackPatch,
               "every advanced operator, envelope, filter, drive, unison, and mod field "
               "round-trips in live tracks");
        expect(roundTrip.patterns[4][127].track.steps[3].advancedFm == patternPatch,
               "advanced FM fields round-trip in stored patterns");
        expect(roundTrip.fmPalette[13].advancedFm == fmPalettePatch &&
                   roundTrip.noisePalette[13].advancedFm == noisePalettePatch,
               "advanced FM fields round-trip in both sound palettes");
        expect(roundTrip.controller == current.controller,
               "controller enabled state and every action mapping round-trip");

        Bytes corrupt = readFile(currentPath);
        if (corrupt.size() > kLegacyHeaderSize + 32U) {
            corrupt[kLegacyHeaderSize + 32U] ^= 0x5AU;
        }
        expect(writeFile(corruptPath, corrupt), "write corrupt current fixture");
        AppState untouched = makeDefaultState();
        untouched.bpm = 88;
        untouched.banks[0].name = {'S', 'A', 'F', 'E', '\0'};
        untouched.tracks[0].steps[0].advancedFm = patternPatch;
        untouched.controller.enabled = false;
        untouched.controller.buttons.fill(kControllerButtonUnbound);
        const AdvancedFmPatch untouchedAdvanced =
            untouched.tracks[0].steps[0].advancedFm;
        const ControllerSettings untouchedController = untouched.controller;
        expect(!loadState(untouched, corruptPath.string(), error),
               "corrupt current save is rejected");
        expect(untouched.bpm == 88 &&
                   untouched.banks[0].name ==
                       std::array<char, 5> {'S', 'A', 'F', 'E', '\0'} &&
                   untouched.tracks[0].steps[0].advancedFm == untouchedAdvanced &&
                   untouched.controller == untouchedController,
               "failed current load leaves destination unchanged");

        if (failures == 0) {
            std::cout << "FMS persistence compatibility tests passed.\n";
            return EXIT_SUCCESS;
        }
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: persistence test exception: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cerr << failures << " persistence test(s) failed.\n";
    return EXIT_FAILURE;
}
