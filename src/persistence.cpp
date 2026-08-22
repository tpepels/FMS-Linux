#include "persistence.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <pwd.h>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace fms
{
    namespace
    {

        namespace fs = std::filesystem;

        constexpr std::array<std::uint8_t, 8> kMagic{
            'F', 'M', 'S', 'S', 'T', 'A', 'T', 'E'};
        constexpr std::uint16_t kFormatMajor = 1;
        constexpr std::uint16_t kFormatMinor = 2;
        constexpr std::uint32_t kHeaderSize = 32;
        constexpr std::uint64_t kMaxPayloadSize = 64ULL * 1024ULL * 1024ULL;
        constexpr std::uint32_t kMaxHeaderSize = 4096;
        constexpr std::uint32_t kMaxSectionCount = 1024;
        constexpr std::uint16_t kBaseSectionVersion = 1;
        constexpr std::uint16_t kStepSectionVersion = 2;
        constexpr std::uint16_t kControllerSectionVersion = 1;
        constexpr std::uint16_t kPatternMetadataSectionVersion = 1;
        constexpr std::uint16_t kUiStateSectionVersion = 1;

        using Tag = std::array<std::uint8_t, 4>;
        constexpr Tag kGlobalTag{'G', 'L', 'O', 'B'};
        constexpr Tag kTracksTag{'T', 'R', 'A', 'K'};
        constexpr Tag kPatternsTag{'P', 'A', 'T', 'T'};
        constexpr Tag kBanksTag{'B', 'A', 'N', 'K'};
        constexpr Tag kFmPaletteTag{'F', 'M', 'P', 'A'};
        constexpr Tag kNoisePaletteTag{'N', 'S', 'P', 'A'};
        constexpr Tag kControllerTag{'C', 'T', 'R', 'L'};
        constexpr Tag kPatternMetadataTag{'P', 'M', 'E', 'T'};
        constexpr Tag kUiStateTag{'U', 'I', 'S', 'T'};
        constexpr std::uint32_t kBaseRequiredSectionCount = 6;
        constexpr std::uint32_t kWrittenSectionCount = 9;

        class ParseError final : public std::runtime_error
        {
        public:
            explicit ParseError(const std::string &message) : std::runtime_error(message) {}
        };

        class ByteWriter
        {
        public:
            explicit ByteWriter(std::size_t reserve = 0) { bytes_.reserve(reserve); }

            void putU8(std::uint8_t value) { bytes_.push_back(value); }

            void putI8(std::int8_t value) { putU8(static_cast<std::uint8_t>(value)); }

            void putU16(std::uint16_t value)
            {
                putU8(static_cast<std::uint8_t>(value & 0xFFU));
                putU8(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
            }

            void putU32(std::uint32_t value)
            {
                for (unsigned shift = 0; shift < 32; shift += 8)
                {
                    putU8(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
                }
            }

            void putU64(std::uint64_t value)
            {
                for (unsigned shift = 0; shift < 64; shift += 8)
                {
                    putU8(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
                }
            }

            void putRaw(const void *source, std::size_t size)
            {
                const auto *first = static_cast<const std::uint8_t *>(source);
                bytes_.insert(bytes_.end(), first, first + size);
            }

            [[nodiscard]] const std::vector<std::uint8_t> &data() const { return bytes_; }
            [[nodiscard]] std::size_t size() const { return bytes_.size(); }

        private:
            std::vector<std::uint8_t> bytes_;
        };

        class ByteReader
        {
        public:
            ByteReader(const std::uint8_t *bytes, std::size_t size, std::string context)
                : bytes_(bytes), size_(size), context_(std::move(context)) {}

            [[nodiscard]] std::uint8_t getU8(const char *field)
            {
                require(1, field);
                return bytes_[position_++];
            }

            [[nodiscard]] std::int8_t getI8(const char *field)
            {
                const std::uint8_t encoded = getU8(field);
                const int decoded = encoded < 0x80U ? static_cast<int>(encoded)
                                                    : static_cast<int>(encoded) - 256;
                return static_cast<std::int8_t>(decoded);
            }

            [[nodiscard]] std::uint16_t getU16(const char *field)
            {
                require(2, field);
                const std::uint16_t value = static_cast<std::uint16_t>(bytes_[position_]) |
                                            static_cast<std::uint16_t>(
                                                static_cast<std::uint16_t>(bytes_[position_ + 1]) << 8U);
                position_ += 2;
                return value;
            }

            [[nodiscard]] std::uint32_t getU32(const char *field)
            {
                require(4, field);
                std::uint32_t value = 0;
                for (unsigned shift = 0; shift < 32; shift += 8)
                {
                    value |= static_cast<std::uint32_t>(bytes_[position_++]) << shift;
                }
                return value;
            }

            [[nodiscard]] std::uint64_t getU64(const char *field)
            {
                require(8, field);
                std::uint64_t value = 0;
                for (unsigned shift = 0; shift < 64; shift += 8)
                {
                    value |= static_cast<std::uint64_t>(bytes_[position_++]) << shift;
                }
                return value;
            }

            [[nodiscard]] Tag getTag()
            {
                require(4, "section tag");
                Tag result{};
                std::copy_n(bytes_ + position_, result.size(), result.begin());
                position_ += result.size();
                return result;
            }

            [[nodiscard]] ByteReader take(std::size_t size, const std::string &context)
            {
                require(size, "section contents");
                ByteReader result(bytes_ + position_, size, context);
                position_ += size;
                return result;
            }

            void requireEnd() const
            {
                if (position_ != size_)
                {
                    throw ParseError(context_ + ": contains " +
                                     std::to_string(size_ - position_) + " unexpected trailing bytes");
                }
            }

        private:
            void require(std::size_t amount, const char *field) const
            {
                if (amount > size_ - position_)
                {
                    throw ParseError(context_ + ": truncated while reading " + field);
                }
            }

            const std::uint8_t *bytes_ = nullptr;
            std::size_t size_ = 0;
            std::size_t position_ = 0;
            std::string context_;
        };

        class ScopedFd
        {
        public:
            explicit ScopedFd(int descriptor = -1) : descriptor_(descriptor) {}
            ~ScopedFd()
            {
                if (descriptor_ >= 0)
                    (void)::close(descriptor_);
            }

            ScopedFd(const ScopedFd &) = delete;
            ScopedFd &operator=(const ScopedFd &) = delete;

            [[nodiscard]] int get() const { return descriptor_; }

            int release()
            {
                const int result = descriptor_;
                descriptor_ = -1;
                return result;
            }

        private:
            int descriptor_ = -1;
        };

        class TemporaryFile
        {
        public:
            TemporaryFile(int descriptor, fs::path path)
                : descriptor_(descriptor), path_(std::move(path)) {}

            ~TemporaryFile()
            {
                if (descriptor_ >= 0)
                    (void)::close(descriptor_);
                if (!committed_)
                    (void)::unlink(path_.c_str());
            }

            TemporaryFile(const TemporaryFile &) = delete;
            TemporaryFile &operator=(const TemporaryFile &) = delete;

            [[nodiscard]] int descriptor() const { return descriptor_; }

            int releaseDescriptor()
            {
                const int result = descriptor_;
                descriptor_ = -1;
                return result;
            }

            [[nodiscard]] const fs::path &path() const { return path_; }
            void markCommitted() { committed_ = true; }

        private:
            int descriptor_ = -1;
            fs::path path_;
            bool committed_ = false;
        };

        [[nodiscard]] std::uint32_t crc32(const std::uint8_t *bytes, std::size_t size)
        {
            std::uint32_t crc = 0xFFFFFFFFU;
            for (std::size_t index = 0; index < size; ++index)
            {
                crc ^= bytes[index];
                for (int bit = 0; bit < 8; ++bit)
                {
                    const std::uint32_t mask = 0U - (crc & 1U);
                    crc = (crc >> 1U) ^ (0xEDB88320U & mask);
                }
            }
            return ~crc;
        }

        [[nodiscard]] std::string tagName(const Tag &tag)
        {
            std::string result;
            result.reserve(tag.size());
            for (const std::uint8_t byte : tag)
            {
                result.push_back(byte >= 0x20U && byte <= 0x7EU ? static_cast<char>(byte) : '?');
            }
            return result;
        }

        [[nodiscard]] std::string systemError(const std::string &operation, const fs::path &path,
                                              int errorNumber)
        {
            return operation + " '" + path.string() + "': " +
                   std::error_code(errorNumber, std::generic_category()).message();
        }

        void writeBool(ByteWriter &writer, bool value)
        {
            writer.putU8(value ? 1U : 0U);
        }

        [[nodiscard]] bool readBool(ByteReader &reader, const char *field)
        {
            const std::uint8_t value = reader.getU8(field);
            if (value > 1U)
            {
                throw ParseError(std::string("invalid boolean value for ") + field);
            }
            return value != 0U;
        }

        template <typename Enum>
        [[nodiscard]] Enum readEnum(ByteReader &reader, std::uint8_t maximum, const char *field)
        {
            const std::uint8_t value = reader.getU8(field);
            if (value > maximum)
            {
                throw ParseError(std::string("invalid enum value for ") + field);
            }
            return static_cast<Enum>(value);
        }

        void writeFmPatch(ByteWriter &writer, const FmPatch &patch)
        {
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

        [[nodiscard]] FmPatch readFmPatch(ByteReader &reader)
        {
            FmPatch patch;
            patch.ampAttack = reader.getU8("FM amp attack");
            patch.ampHold = reader.getU8("FM amp hold");
            patch.ampRelease = reader.getU8("FM amp release");
            patch.modRatio = reader.getU8("FM mod ratio");
            patch.modDepth = reader.getU8("FM mod depth");
            patch.modFeedback = reader.getU8("FM mod feedback");
            patch.modAttack = reader.getU8("FM mod attack");
            patch.modRelease = reader.getU8("FM mod release");
            patch.modEnd = reader.getU8("FM mod end");
            patch.sweepDepth = reader.getI8("FM sweep depth");
            patch.sweepRelease = reader.getU8("FM sweep release");
            return patch;
        }

        void writeNoisePatch(ByteWriter &writer, const NoisePatch &patch)
        {
            writer.putU8(patch.ampAttack);
            writer.putU8(patch.ampHold);
            writer.putU8(patch.ampRelease);
            writer.putU8(patch.rate);
            writeBool(writer, patch.narrow);
        }

        [[nodiscard]] NoisePatch readNoisePatch(ByteReader &reader)
        {
            NoisePatch patch;
            patch.ampAttack = reader.getU8("noise amp attack");
            patch.ampHold = reader.getU8("noise amp hold");
            patch.ampRelease = reader.getU8("noise amp release");
            patch.rate = reader.getU8("noise rate");
            patch.narrow = readBool(reader, "noise narrow");
            return patch;
        }

        void writeAdvancedFmOperator(ByteWriter &writer, const AdvancedFmOperator &operatorData)
        {
            writer.putU8(static_cast<std::uint8_t>(operatorData.shape));
            writer.putU8(operatorData.ratio);
            writer.putU8(operatorData.level);
            writer.putU8(operatorData.feedback);
            writer.putI8(operatorData.detune);
        }

        [[nodiscard]] AdvancedFmOperator readAdvancedFmOperator(ByteReader &reader)
        {
            AdvancedFmOperator operatorData;
            operatorData.shape = readEnum<AdvancedOscShape>(
                reader, static_cast<std::uint8_t>(AdvancedOscShape::Noise),
                "advanced operator shape");
            operatorData.ratio = reader.getU8("advanced operator ratio");
            operatorData.level = reader.getU8("advanced operator level");
            operatorData.feedback = reader.getU8("advanced operator feedback");
            operatorData.detune = reader.getI8("advanced operator detune");
            return operatorData;
        }

        void writeAdsrEnvelope(ByteWriter &writer, const AdsrEnvelope &envelope)
        {
            writer.putU8(envelope.attack);
            writer.putU8(envelope.decay);
            writer.putU8(envelope.sustain);
            writer.putU8(envelope.release);
        }

        [[nodiscard]] AdsrEnvelope readAdsrEnvelope(ByteReader &reader)
        {
            AdsrEnvelope envelope;
            envelope.attack = reader.getU8("advanced amp attack");
            envelope.decay = reader.getU8("advanced amp decay");
            envelope.sustain = reader.getU8("advanced amp sustain");
            envelope.release = reader.getU8("advanced amp release");
            return envelope;
        }

        void writeAdvancedModSlot(ByteWriter &writer, const AdvancedModSlot &slot)
        {
            writer.putU8(static_cast<std::uint8_t>(slot.source));
            writer.putU8(slot.rate);
            writer.putI8(slot.depth);
            writer.putU8(static_cast<std::uint8_t>(slot.destination));
        }

        [[nodiscard]] AdvancedModSlot readAdvancedModSlot(ByteReader &reader)
        {
            AdvancedModSlot slot;
            slot.source = readEnum<AdvancedModSource>(
                reader, static_cast<std::uint8_t>(AdvancedModSource::AmpEnvelope),
                "advanced modulation source");
            slot.rate = reader.getU8("advanced modulation rate");
            slot.depth = reader.getI8("advanced modulation depth");
            slot.destination = readEnum<AdvancedModDestination>(
                reader, static_cast<std::uint8_t>(AdvancedModDestination::Operator4Ratio),
                "advanced modulation destination");
            return slot;
        }

        void writeAdvancedFmPatch(ByteWriter &writer, const AdvancedFmPatch &patch)
        {
            writeBool(writer, patch.enabled);
            writer.putU8(static_cast<std::uint8_t>(patch.algorithm));
            for (const AdvancedFmOperator &operatorData : patch.operators)
            {
                writeAdvancedFmOperator(writer, operatorData);
            }
            writeAdsrEnvelope(writer, patch.ampEnvelope);
            writer.putU8(static_cast<std::uint8_t>(patch.filterMode));
            writer.putU8(patch.filterCutoff);
            writer.putU8(patch.resonance);
            writer.putU8(static_cast<std::uint8_t>(patch.driveMode));
            writer.putU8(patch.driveAmount);
            writer.putU8(patch.unisonVoices);
            writer.putU8(patch.unisonDetune);
            writer.putU8(patch.unisonWidth);
            for (const AdvancedModSlot &slot : patch.modulation)
                writeAdvancedModSlot(writer, slot);
        }

        [[nodiscard]] AdvancedFmPatch readAdvancedFmPatch(ByteReader &reader)
        {
            AdvancedFmPatch patch;
            patch.enabled = readBool(reader, "advanced FM enabled");
            patch.algorithm = readEnum<AdvancedFmAlgorithm>(
                reader, static_cast<std::uint8_t>(AdvancedFmAlgorithm::Algorithm12),
                "advanced FM algorithm");
            for (AdvancedFmOperator &operatorData : patch.operators)
            {
                operatorData = readAdvancedFmOperator(reader);
            }
            patch.ampEnvelope = readAdsrEnvelope(reader);
            patch.filterMode = readEnum<AdvancedFilterMode>(
                reader, static_cast<std::uint8_t>(AdvancedFilterMode::Notch),
                "advanced filter mode");
            patch.filterCutoff = reader.getU8("advanced filter cutoff");
            patch.resonance = reader.getU8("advanced filter resonance");
            patch.driveMode = readEnum<AdvancedDriveMode>(
                reader, static_cast<std::uint8_t>(AdvancedDriveMode::Wavefold),
                "advanced drive mode");
            patch.driveAmount = reader.getU8("advanced drive amount");
            patch.unisonVoices = reader.getU8("advanced unison voices");
            patch.unisonDetune = reader.getU8("advanced unison detune");
            patch.unisonWidth = reader.getU8("advanced unison width");
            for (AdvancedModSlot &slot : patch.modulation)
                slot = readAdvancedModSlot(reader);
            return patch;
        }

        void writeStep(ByteWriter &writer, const Step &step)
        {
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
            for (const std::int8_t interval : step.chord)
                writer.putI8(interval);
            writeFmPatch(writer, step.fm);
            writeNoisePatch(writer, step.noise);
            writeAdvancedFmPatch(writer, step.advancedFm);
        }

        [[nodiscard]] Step readStep(ByteReader &reader, std::uint16_t sectionVersion)
        {
            Step step;
            step.active = readBool(reader, "step active");
            step.trigless = readBool(reader, "step trigless");
            step.note = reader.getU8("step note");
            step.level = reader.getU8("step level");
            step.pan = readEnum<Pan>(reader, static_cast<std::uint8_t>(Pan::Right), "step pan");
            step.portamento = reader.getU8("step portamento");
            step.condition = reader.getU8("step condition");
            step.microTicks = reader.getI8("step microtiming");
            step.echo = readBool(reader, "step echo");
            step.transpose = readBool(reader, "step transpose");
            step.mode = readEnum<SynthMode>(
                reader, static_cast<std::uint8_t>(SynthMode::Parallel), "step synth mode");
            for (std::int8_t &interval : step.chord)
                interval = reader.getI8("chord interval");
            step.fm = readFmPatch(reader);
            step.noise = readNoisePatch(reader);
            if (sectionVersion >= kStepSectionVersion)
            {
                step.advancedFm = readAdvancedFmPatch(reader);
            }
            return step;
        }

        void writeTrack(ByteWriter &writer, const TrackData &track)
        {
            for (const Step &step : track.steps)
                writeStep(writer, step);
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

            for (const std::int8_t value : track.transpose.values)
                writer.putI8(value);
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

        [[nodiscard]] TrackData readTrack(ByteReader &reader, std::uint16_t sectionVersion)
        {
            TrackData track;
            for (Step &step : track.steps)
                step = readStep(reader, sectionVersion);
            track.length = reader.getU8("track length");
            track.rateIndex = reader.getU8("track rate");
            track.direction = readEnum<Direction>(
                reader, static_cast<std::uint8_t>(Direction::Random), "track direction");
            track.shuffle = reader.getU8("track shuffle");
            track.muted = readBool(reader, "track muted");
            track.solo = readBool(reader, "track solo");

            track.echo.repeats = reader.getU8("echo repeats");
            track.echo.speedTicks = reader.getU8("echo speed");
            track.echo.transpose = reader.getI8("echo transpose");
            track.echo.transposeModulo = reader.getU8("echo transpose modulo");
            track.echo.volumeDelta = reader.getI8("echo volume delta");
            track.echo.modDelta = reader.getI8("echo modulation delta");
            track.echo.feedbackDelta = reader.getI8("echo feedback delta");
            track.echo.pan = readEnum<EchoPan>(
                reader, static_cast<std::uint8_t>(EchoPan::PingPong), "echo pan");

            for (std::int8_t &value : track.transpose.values)
            {
                value = reader.getI8("transpose value");
            }
            track.transpose.length = reader.getU8("transpose length");
            track.transpose.rate = reader.getU8("transpose rate");
            track.transpose.advance = readEnum<TransposeAdvance>(
                reader, static_cast<std::uint8_t>(TransposeAdvance::Trigger), "transpose advance");

            track.modulator.targetTrack = reader.getU8("modulator target track");
            track.modulator.destination = readEnum<ModDest>(
                reader, static_cast<std::uint8_t>(ModDest::NoiseRate), "modulator destination");
            track.modulator.speed = reader.getU8("modulator speed");
            track.modulator.wave = readEnum<ModWave>(
                reader, static_cast<std::uint8_t>(ModWave::Random), "modulator wave");
            track.modulator.depth = reader.getI8("modulator depth");
            track.modulator.offset = reader.getU8("modulator offset");
            return track;
        }

        void appendChunk(ByteWriter &payload, const Tag &tag, std::uint16_t version,
                         const ByteWriter &contents)
        {
            if (contents.size() > std::numeric_limits<std::uint32_t>::max())
            {
                throw std::runtime_error("save section is too large");
            }
            payload.putRaw(tag.data(), tag.size());
            payload.putU16(version);
            payload.putU16(0); // Reserved flags. A future version may define these.
            payload.putU32(static_cast<std::uint32_t>(contents.size()));
            payload.putRaw(contents.data().data(), contents.size());
        }

        [[nodiscard]] std::vector<std::uint8_t> serializeState(const AppState &state)
        {
            ByteWriter payload(384U * 1024U);
            payload.putU32(kWrittenSectionCount);

            ByteWriter global;
            global.putU16(state.bpm);
            global.putU8(state.scaleRoot);
            global.putU16(state.scaleMask);
            writeBool(global, state.lightTheme);
            global.putU8(state.accent);
            global.putU64(state.editRevision);
            appendChunk(payload, kGlobalTag, kBaseSectionVersion, global);

            ByteWriter tracks;
            tracks.putU16(static_cast<std::uint16_t>(state.tracks.size()));
            for (const TrackData &track : state.tracks)
                writeTrack(tracks, track);
            appendChunk(payload, kTracksTag, kStepSectionVersion, tracks);

            ByteWriter patterns;
            patterns.putU16(static_cast<std::uint16_t>(state.patterns.size()));
            patterns.putU16(static_cast<std::uint16_t>(state.patterns.front().size()));
            for (const auto &trackPatterns : state.patterns)
            {
                for (const StoredPattern &pattern : trackPatterns)
                {
                    writeBool(patterns, pattern.occupied);
                    writeTrack(patterns, pattern.track);
                }
            }
            appendChunk(payload, kPatternsTag, kStepSectionVersion, patterns);

            ByteWriter patternMetadata;
            patternMetadata.putU16(
                static_cast<std::uint16_t>(state.patternMetadata.size()));
            for (const PatternMetadata &metadata : state.patternMetadata)
            {
                for (const char character : metadata.name)
                {
                    patternMetadata.putU8(
                        static_cast<std::uint8_t>(static_cast<unsigned char>(character)));
                }
                patternMetadata.putU8(metadata.color);
            }
            appendChunk(payload, kPatternMetadataTag, kPatternMetadataSectionVersion,
                        patternMetadata);

            ByteWriter banks;
            banks.putU16(static_cast<std::uint16_t>(state.banks.size()));
            for (const BankSettings &bank : state.banks)
            {
                for (const char character : bank.name)
                {
                    banks.putU8(static_cast<std::uint8_t>(static_cast<unsigned char>(character)));
                }
                writeBool(banks, bank.locked);
                writeBool(banks, bank.hasTempo);
                writeBool(banks, bank.hasScale);
                banks.putU16(bank.tempo);
                banks.putU16(bank.scaleMask);
                banks.putU8(bank.scaleRoot);
            }
            appendChunk(payload, kBanksTag, kBaseSectionVersion, banks);

            ByteWriter fmPalette;
            fmPalette.putU16(static_cast<std::uint16_t>(state.fmPalette.size()));
            for (const Step &step : state.fmPalette)
                writeStep(fmPalette, step);
            appendChunk(payload, kFmPaletteTag, kStepSectionVersion, fmPalette);

            ByteWriter noisePalette;
            noisePalette.putU16(static_cast<std::uint16_t>(state.noisePalette.size()));
            for (const Step &step : state.noisePalette)
                writeStep(noisePalette, step);
            appendChunk(payload, kNoisePaletteTag, kStepSectionVersion, noisePalette);

            ByteWriter controller;
            writeBool(controller, state.controller.enabled);
            for (const std::uint8_t button : state.controller.buttons)
                controller.putU8(button);
            appendChunk(payload, kControllerTag, kControllerSectionVersion, controller);

            ByteWriter uiState;
            writeBool(uiState, state.onboardingDismissed);
            appendChunk(payload, kUiStateTag, kUiStateSectionVersion, uiState);

            const std::uint32_t payloadChecksum = crc32(payload.data().data(), payload.size());
            ByteWriter header(kHeaderSize);
            header.putRaw(kMagic.data(), kMagic.size());
            header.putU16(kFormatMajor);
            header.putU16(kFormatMinor);
            header.putU32(kHeaderSize);
            header.putU64(static_cast<std::uint64_t>(payload.size()));
            header.putU32(payloadChecksum);
            const std::uint32_t headerChecksum = crc32(header.data().data(), header.size());
            header.putU32(headerChecksum);

            if (header.size() != kHeaderSize)
            {
                throw std::logic_error("internal save header size mismatch");
            }

            ByteWriter file(kHeaderSize + payload.size());
            file.putRaw(header.data().data(), header.size());
            file.putRaw(payload.data().data(), payload.size());
            return file.data();
        }

        void requireSectionVersion(std::uint16_t version, std::uint16_t expected,
                                   std::uint16_t flags, const Tag &tag)
        {
            if (version != expected)
            {
                throw ParseError("section " + tagName(tag) + " uses unsupported schema version " +
                                 std::to_string(version));
            }
            if (flags != 0U)
            {
                throw ParseError("section " + tagName(tag) + " uses unsupported flags");
            }
        }

        void requireStepSectionVersion(std::uint16_t version, std::uint16_t flags,
                                       const Tag &tag)
        {
            if (version != kBaseSectionVersion && version != kStepSectionVersion)
            {
                throw ParseError("section " + tagName(tag) + " uses unsupported schema version " +
                                 std::to_string(version));
            }
            if (flags != 0U)
            {
                throw ParseError("section " + tagName(tag) + " uses unsupported flags");
            }
        }

        [[nodiscard]] AppState readVersion1Payload(const std::uint8_t *bytes, std::size_t size)
        {
            ByteReader payload(bytes, size, "save payload");
            const std::uint32_t sectionCount = payload.getU32("section count");
            if (sectionCount < kBaseRequiredSectionCount || sectionCount > kMaxSectionCount)
            {
                throw ParseError("save payload declares an invalid section count");
            }

            AppState decoded;
            bool haveGlobal = false;
            bool haveTracks = false;
            bool havePatterns = false;
            bool haveBanks = false;
            bool haveFmPalette = false;
            bool haveNoisePalette = false;
            bool haveController = false;
            bool havePatternMetadata = false;
            bool haveUiState = false;

            for (std::uint32_t sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex)
            {
                const Tag tag = payload.getTag();
                const std::uint16_t version = payload.getU16("section version");
                const std::uint16_t flags = payload.getU16("section flags");
                const std::uint32_t length = payload.getU32("section length");
                ByteReader section = payload.take(length, "section " + tagName(tag));

                if (tag == kGlobalTag)
                {
                    if (haveGlobal)
                        throw ParseError("duplicate GLOB section");
                    haveGlobal = true;
                    requireSectionVersion(version, kBaseSectionVersion, flags, tag);
                    decoded.bpm = section.getU16("tempo");
                    decoded.scaleRoot = section.getU8("scale root");
                    decoded.scaleMask = section.getU16("scale mask");
                    decoded.lightTheme = readBool(section, "light theme");
                    decoded.accent = section.getU8("accent");
                    decoded.editRevision = section.getU64("edit revision");
                    section.requireEnd();
                }
                else if (tag == kTracksTag)
                {
                    if (haveTracks)
                        throw ParseError("duplicate TRAK section");
                    haveTracks = true;
                    requireStepSectionVersion(version, flags, tag);
                    const std::uint16_t trackCount = section.getU16("track count");
                    if (trackCount != decoded.tracks.size())
                    {
                        throw ParseError("TRAK section has an incompatible track count");
                    }
                    for (TrackData &track : decoded.tracks)
                        track = readTrack(section, version);
                    section.requireEnd();
                }
                else if (tag == kPatternsTag)
                {
                    if (havePatterns)
                        throw ParseError("duplicate PATT section");
                    havePatterns = true;
                    requireStepSectionVersion(version, flags, tag);
                    const std::uint16_t trackCount = section.getU16("pattern track count");
                    const std::uint16_t patternCount = section.getU16("patterns per track");
                    if (trackCount != decoded.patterns.size() ||
                        patternCount != decoded.patterns.front().size())
                    {
                        throw ParseError("PATT section has incompatible dimensions");
                    }
                    for (auto &trackPatterns : decoded.patterns)
                    {
                        for (StoredPattern &pattern : trackPatterns)
                        {
                            pattern.occupied = readBool(section, "pattern occupied");
                            pattern.track = readTrack(section, version);
                        }
                    }
                    section.requireEnd();
                }
                else if (tag == kBanksTag)
                {
                    if (haveBanks)
                        throw ParseError("duplicate BANK section");
                    haveBanks = true;
                    requireSectionVersion(version, kBaseSectionVersion, flags, tag);
                    const std::uint16_t bankCount = section.getU16("bank count");
                    if (bankCount != decoded.banks.size())
                    {
                        throw ParseError("BANK section has an incompatible bank count");
                    }
                    for (BankSettings &bank : decoded.banks)
                    {
                        for (char &character : bank.name)
                        {
                            character = static_cast<char>(section.getU8("bank name"));
                        }
                        bank.locked = readBool(section, "bank locked");
                        bank.hasTempo = readBool(section, "bank has tempo");
                        bank.hasScale = readBool(section, "bank has scale");
                        bank.tempo = section.getU16("bank tempo");
                        bank.scaleMask = section.getU16("bank scale mask");
                        bank.scaleRoot = section.getU8("bank scale root");
                    }
                    section.requireEnd();
                }
                else if (tag == kFmPaletteTag)
                {
                    if (haveFmPalette)
                        throw ParseError("duplicate FMPA section");
                    haveFmPalette = true;
                    requireStepSectionVersion(version, flags, tag);
                    const std::uint16_t paletteSize = section.getU16("FM palette size");
                    if (paletteSize != decoded.fmPalette.size())
                    {
                        throw ParseError("FMPA section has an incompatible palette size");
                    }
                    for (Step &step : decoded.fmPalette)
                        step = readStep(section, version);
                    section.requireEnd();
                }
                else if (tag == kNoisePaletteTag)
                {
                    if (haveNoisePalette)
                        throw ParseError("duplicate NSPA section");
                    haveNoisePalette = true;
                    requireStepSectionVersion(version, flags, tag);
                    const std::uint16_t paletteSize = section.getU16("noise palette size");
                    if (paletteSize != decoded.noisePalette.size())
                    {
                        throw ParseError("NSPA section has an incompatible palette size");
                    }
                    for (Step &step : decoded.noisePalette)
                        step = readStep(section, version);
                    section.requireEnd();
                }
                else if (tag == kControllerTag)
                {
                    if (haveController)
                        throw ParseError("duplicate CTRL section");
                    haveController = true;
                    requireSectionVersion(version, kControllerSectionVersion, flags, tag);
                    decoded.controller.enabled = readBool(section, "controller enabled");
                    for (std::uint8_t &button : decoded.controller.buttons)
                    {
                        button = section.getU8("controller button mapping");
                    }
                    section.requireEnd();
                }
                else if (tag == kPatternMetadataTag)
                {
                    if (havePatternMetadata)
                        throw ParseError("duplicate PMET section");
                    havePatternMetadata = true;
                    requireSectionVersion(version, kPatternMetadataSectionVersion, flags, tag);
                    const std::uint16_t patternCount =
                        section.getU16("pattern metadata count");
                    if (patternCount != decoded.patternMetadata.size())
                    {
                        throw ParseError("PMET section has an incompatible pattern count");
                    }
                    for (PatternMetadata &metadata : decoded.patternMetadata)
                    {
                        for (char &character : metadata.name)
                        {
                            character = static_cast<char>(section.getU8("pattern metadata name"));
                        }
                        metadata.color = section.getU8("pattern metadata color");
                    }
                    section.requireEnd();
                }
                else if (tag == kUiStateTag)
                {
                    if (haveUiState)
                        throw ParseError("duplicate UIST section");
                    haveUiState = true;
                    requireSectionVersion(version, kUiStateSectionVersion, flags, tag);
                    decoded.onboardingDismissed =
                        readBool(section, "onboarding dismissed");
                    section.requireEnd();
                }
                // Unknown chunks are deliberately ignored so minor format revisions can add data.
            }

            payload.requireEnd();
            if (!haveGlobal || !haveTracks || !havePatterns || !haveBanks || !haveFmPalette ||
                !haveNoisePalette)
            {
                throw ParseError("save payload is missing one or more required sections");
            }
            return decoded;
        }

        [[nodiscard]] std::uint32_t littleEndianU32At(const std::uint8_t *bytes)
        {
            return static_cast<std::uint32_t>(bytes[0]) |
                   (static_cast<std::uint32_t>(bytes[1]) << 8U) |
                   (static_cast<std::uint32_t>(bytes[2]) << 16U) |
                   (static_cast<std::uint32_t>(bytes[3]) << 24U);
        }

        [[nodiscard]] AppState deserializeState(const std::vector<std::uint8_t> &file)
        {
            if (file.size() < kHeaderSize)
            {
                throw ParseError("save file is truncated before its header is complete");
            }
            if (!std::equal(kMagic.begin(), kMagic.end(), file.begin()))
            {
                throw ParseError("save file has an invalid signature");
            }

            const std::uint32_t storedHeaderChecksum = littleEndianU32At(file.data() + 28);
            const std::uint32_t computedHeaderChecksum = crc32(file.data(), 28);
            if (storedHeaderChecksum != computedHeaderChecksum)
            {
                throw ParseError("save header checksum mismatch");
            }

            ByteReader header(file.data(), kHeaderSize, "save header");
            for (std::size_t index = 0; index < kMagic.size(); ++index)
            {
                (void)header.getU8("signature");
            }
            const std::uint16_t major = header.getU16("major version");
            const std::uint16_t minor = header.getU16("minor version");
            const std::uint32_t headerSize = header.getU32("header size");
            const std::uint64_t payloadSize = header.getU64("payload size");
            const std::uint32_t storedPayloadChecksum = header.getU32("payload checksum");
            (void)header.getU32("header checksum");
            header.requireEnd();

            if (major != kFormatMajor)
            {
                const std::string relationship = major > kFormatMajor ? "newer than" : "older than";
                throw ParseError("save format " + std::to_string(major) + "." +
                                 std::to_string(minor) + " is " + relationship +
                                 " the supported format " + std::to_string(kFormatMajor) + "." +
                                 std::to_string(kFormatMinor));
            }
            if (headerSize < kHeaderSize || headerSize > kMaxHeaderSize)
            {
                throw ParseError("save file declares an invalid header size");
            }
            if (payloadSize > kMaxPayloadSize)
            {
                throw ParseError("save payload exceeds the supported size limit");
            }
            if (headerSize > file.size() || payloadSize != file.size() - headerSize)
            {
                throw ParseError("save file length does not match its header");
            }

            const auto *payload = file.data() + headerSize;
            if (crc32(payload, static_cast<std::size_t>(payloadSize)) != storedPayloadChecksum)
            {
                throw ParseError("save payload checksum mismatch");
            }

            return readVersion1Payload(payload, static_cast<std::size_t>(payloadSize));
        }

        [[nodiscard]] bool readWholeFile(const fs::path &path, std::vector<std::uint8_t> &bytes,
                                         std::string &error, bool &missing)
        {
            missing = false;
            int descriptor = -1;
            do
            {
                descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
            } while (descriptor < 0 && errno == EINTR);
            if (descriptor < 0)
            {
                const int openError = errno;
                if (openError == ENOENT || openError == ENOTDIR)
                {
                    missing = true;
                    return false;
                }
                error = systemError("could not open save file", path, openError);
                return false;
            }
            ScopedFd file(descriptor);

            struct stat status{};
            if (::fstat(file.get(), &status) != 0)
            {
                error = systemError("could not inspect save file", path, errno);
                return false;
            }
            if (!S_ISREG(status.st_mode))
            {
                error = "save path is not a regular file: '" + path.string() + "'";
                return false;
            }
            if (status.st_size < 0)
            {
                error = "save file reports an invalid size: '" + path.string() + "'";
                return false;
            }
            const std::uint64_t fileSize = static_cast<std::uint64_t>(status.st_size);
            const std::uint64_t maximumFileSize =
                static_cast<std::uint64_t>(kMaxHeaderSize) + kMaxPayloadSize;
            if (fileSize > maximumFileSize ||
                fileSize > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
            {
                error = "save file is too large: '" + path.string() + "'";
                return false;
            }

            bytes.resize(static_cast<std::size_t>(fileSize));
            std::size_t offset = 0;
            while (offset < bytes.size())
            {
                const ssize_t count = ::read(file.get(), bytes.data() + offset, bytes.size() - offset);
                if (count < 0)
                {
                    if (errno == EINTR)
                        continue;
                    error = systemError("could not read save file", path, errno);
                    return false;
                }
                if (count == 0)
                {
                    error = "save file was truncated while being read: '" + path.string() + "'";
                    return false;
                }
                offset += static_cast<std::size_t>(count);
            }
            return true;
        }

        [[nodiscard]] bool writeAll(int descriptor, const std::vector<std::uint8_t> &bytes,
                                    const fs::path &path, std::string &error)
        {
            std::size_t offset = 0;
            while (offset < bytes.size())
            {
                const ssize_t count = ::write(descriptor, bytes.data() + offset, bytes.size() - offset);
                if (count < 0)
                {
                    if (errno == EINTR)
                        continue;
                    error = systemError("could not write temporary save file", path, errno);
                    return false;
                }
                if (count == 0)
                {
                    error = "could not write temporary save file '" + path.string() +
                            "': write made no progress";
                    return false;
                }
                offset += static_cast<std::size_t>(count);
            }
            return true;
        }

        [[nodiscard]] bool syncFile(int descriptor, const fs::path &path, std::string &error)
        {
            int result = -1;
            do
            {
                result = ::fsync(descriptor);
            } while (result != 0 && errno == EINTR);
            if (result != 0)
            {
                error = systemError("could not flush temporary save file", path, errno);
                return false;
            }
            return true;
        }

        void syncDirectoryBestEffort(const fs::path &directory)
        {
            int descriptor = -1;
            do
            {
                descriptor = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            } while (descriptor < 0 && errno == EINTR);
            if (descriptor < 0)
                return;
            ScopedFd directoryFd(descriptor);
            int result = -1;
            do
            {
                result = ::fsync(directoryFd.get());
            } while (result != 0 && errno == EINTR);
        }

        enum class WriteDisposition
        {
            Replace,
            CreateNew,
        };

        [[nodiscard]] bool writeAtomically(const fs::path &destination,
                                           const std::vector<std::uint8_t> &bytes,
                                           WriteDisposition disposition, std::string &error)
        {
            fs::path parent = destination.parent_path();
            if (parent.empty())
                parent = ".";

            std::error_code directoryError;
            fs::create_directories(parent, directoryError);
            if (directoryError)
            {
                error = "could not create save directory '" + parent.string() + "': " +
                        directoryError.message();
                return false;
            }
            if (!fs::is_directory(parent, directoryError))
            {
                error = directoryError
                            ? "could not inspect save directory '" + parent.string() + "': " +
                                  directoryError.message()
                            : "save parent is not a directory: '" + parent.string() + "'";
                return false;
            }

            static std::atomic<std::uint64_t> sequence{0};
            const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
            const std::uint64_t serial = sequence.fetch_add(1, std::memory_order_relaxed);
            const std::string stem = ".fms-save-" + std::to_string(static_cast<long long>(::getpid())) +
                                     "-" + std::to_string(ticks) + "-" + std::to_string(serial);

            int descriptor = -1;
            fs::path temporaryPath;
            for (unsigned attempt = 0; attempt < 128; ++attempt)
            {
                temporaryPath = parent / (stem + "-" + std::to_string(attempt) + ".tmp");
                do
                {
                    descriptor = ::open(temporaryPath.c_str(),
                                        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
                } while (descriptor < 0 && errno == EINTR);
                if (descriptor >= 0)
                    break;
                if (errno != EEXIST)
                {
                    error = systemError("could not create temporary save file", temporaryPath, errno);
                    return false;
                }
            }
            if (descriptor < 0)
            {
                error = "could not allocate a unique temporary save file in '" + parent.string() + "'";
                return false;
            }

            TemporaryFile temporary(descriptor, temporaryPath);
            if (!writeAll(temporary.descriptor(), bytes, temporary.path(), error) ||
                !syncFile(temporary.descriptor(), temporary.path(), error))
            {
                return false;
            }

            const int descriptorToClose = temporary.releaseDescriptor();
            if (::close(descriptorToClose) != 0)
            {
                error = systemError("could not close temporary save file", temporary.path(), errno);
                return false;
            }
            if (disposition == WriteDisposition::CreateNew)
            {
                // link(2) publishes the already-flushed inode atomically and,
                // unlike rename(2), fails when the destination exists. Both
                // names are in the same directory/filesystem by construction.
                if (::link(temporary.path().c_str(), destination.c_str()) != 0)
                {
                    const int linkError = errno;
                    error = linkError == EEXIST
                                ? "project already exists and was preserved: '" +
                                      destination.string() + "'"
                                : systemError("could not create new save file", destination,
                                              linkError);
                    return false;
                }
                if (::unlink(temporary.path().c_str()) == 0)
                    temporary.markCommitted();
                // If unlink is interrupted or otherwise fails, the destination
                // is already safely committed. TemporaryFile retries cleanup.
            }
            else if (::rename(temporary.path().c_str(), destination.c_str()) != 0)
            {
                error = systemError("could not replace save file", destination, errno);
                return false;
            }
            else
            {
                temporary.markCommitted();
            }
            syncDirectoryBestEffort(parent);
            return true;
        }

        [[nodiscard]] fs::path homeDirectory()
        {
            if (const char *home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
            {
                return fs::path(home);
            }

            long requestedSize = ::sysconf(_SC_GETPW_R_SIZE_MAX);
            if (requestedSize < 1024)
                requestedSize = 16384;
            const long maximumSize = 1024L * 1024L;
            requestedSize = std::min(requestedSize, maximumSize);
            std::vector<char> buffer(static_cast<std::size_t>(requestedSize));
            struct passwd entry{};
            struct passwd *result = nullptr;
            if (::getpwuid_r(::getuid(), &entry, buffer.data(), buffer.size(), &result) == 0 &&
                result != nullptr && result->pw_dir != nullptr && result->pw_dir[0] != '\0')
            {
                return fs::path(result->pw_dir);
            }
            return fs::path(".");
        }

    } // namespace

    std::string defaultSavePath()
    {
        fs::path dataRoot;
        if (const char *xdgDataHome = std::getenv("XDG_DATA_HOME");
            xdgDataHome != nullptr && xdgDataHome[0] != '\0' &&
            fs::path(xdgDataHome).is_absolute())
        {
            dataRoot = fs::path(xdgDataHome);
        }
        else
        {
            dataRoot = homeDirectory() / ".local" / "share";
        }
        return (dataRoot / "fms-linux" / "state.bin").string();
    }

    std::string projectPathForName(std::string_view name)
    {
        std::string clean;
        clean.reserve(name.size());
        bool previousSeparator = false;
        for (const unsigned char raw : name)
        {
            if ((raw >= 'A' && raw <= 'Z') || (raw >= 'a' && raw <= 'z') ||
                (raw >= '0' && raw <= '9') || raw == '_' || raw == '-')
            {
                clean.push_back(static_cast<char>(raw));
                previousSeparator = false;
            }
            else if ((raw == ' ' || raw == '.') && !clean.empty() && !previousSeparator)
            {
                clean.push_back('-');
                previousSeparator = true;
            }
            if (clean.size() >= 40u)
                break;
        }
        while (!clean.empty() && clean.back() == '-')
            clean.pop_back();
        if (clean.empty())
            clean = "untitled";
        const fs::path root = fs::path(defaultSavePath()).parent_path();
        const fs::path projectDirectory = root / "projects";
        for (unsigned suffix = 1; suffix <= 9999U; ++suffix)
        {
            const std::string fileName = suffix == 1U
                                             ? clean + ".fms"
                                             : clean + "-" + std::to_string(suffix) + ".fms";
            const fs::path candidate = projectDirectory / fileName;
            std::error_code statusError;
            const bool occupied = fs::exists(candidate, statusError);
            if (!occupied || statusError)
                return candidate.string();
        }

        // Reaching this branch requires thousands of colliding project names.
        // Keep the result deterministic within this process and let
        // saveStateNew perform the final race-free existence check.
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        return (projectDirectory /
                (clean + "-" + std::to_string(static_cast<long long>(::getpid())) + "-" +
                 std::to_string(ticks) + ".fms"))
            .string();
    }

    std::vector<std::string> recentProjectPaths()
    {
        std::vector<std::pair<fs::file_time_type, fs::path>> candidates;
        const fs::path defaultPath(defaultSavePath());
        std::error_code error;
        if (fs::is_regular_file(defaultPath, error))
        {
            const fs::file_time_type modified = fs::last_write_time(defaultPath, error);
            if (!error)
                candidates.emplace_back(modified, defaultPath);
        }

        const fs::path projectDirectory = defaultPath.parent_path() / "projects";
        error.clear();
        if (fs::is_directory(projectDirectory, error))
        {
            for (const fs::directory_entry &entry : fs::directory_iterator(projectDirectory, error))
            {
                if (error)
                    break;
                if (!entry.is_regular_file(error) || entry.path().extension() != ".fms")
                    continue;
                const fs::file_time_type modified = entry.last_write_time(error);
                if (!error)
                    candidates.emplace_back(modified, entry.path());
            }
        }

        std::sort(candidates.begin(), candidates.end(),
                  [](const auto &left, const auto &right)
                  { return left.first > right.first; });
        std::vector<std::string> result;
        result.reserve(std::min<std::size_t>(candidates.size(), 8u));
        for (const auto &candidate : candidates)
        {
            const std::string path = candidate.second.string();
            if (std::find(result.begin(), result.end(), path) == result.end())
                result.push_back(path);
            if (result.size() == 8u)
                break;
        }
        return result;
    }

    namespace
    {
        bool saveStateWithDisposition(const AppState &state, const std::string &path,
                                      WriteDisposition disposition, std::string &error)
        {
            error.clear();
            if (path.empty() || path.find('\0') != std::string::npos)
            {
                error = "save path is empty or invalid";
                return false;
            }

            try
            {
                auto clean = std::make_unique<AppState>(state);
                sanitize(*clean);
                const std::vector<std::uint8_t> bytes = serializeState(*clean);
                return writeAtomically(fs::path(path), bytes, disposition, error);
            }
            catch (const std::exception &exception)
            {
                error = std::string("could not save state: ") + exception.what();
                return false;
            }
        }
    } // namespace

    bool saveState(const AppState &state, const std::string &path, std::string &error)
    {
        return saveStateWithDisposition(state, path, WriteDisposition::Replace, error);
    }

    bool saveStateNew(const AppState &state, const std::string &path, std::string &error)
    {
        return saveStateWithDisposition(state, path, WriteDisposition::CreateNew, error);
    }

    bool saveProjectState(const AppState &state, ProjectFileTarget &target,
                          bool recoveryOnFailure, ProjectSaveResult &result,
                          std::string &error)
    {
        bool saved = false;
        if (!target.readableOrMissing)
        {
            error = "current file is unreadable; the original was preserved";
        }
        else if (target.path.empty())
        {
            error = "project has no save path; use Save As";
        }
        else
        {
            saved = target.exists ? saveState(state, target.path, error)
                                  : saveStateNew(state, target.path, error);
        }

        if (saved)
        {
            target.exists = true;
            result = ProjectSaveResult::Saved;
            return true;
        }
        if (!recoveryOnFailure)
            return false;

        const std::string primaryError = error;
        std::string recoveryError;
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const std::string recoveryPath = projectPathForName("recovered-session");
            if (saveStateNew(state, recoveryPath, recoveryError))
            {
                target = ProjectFileTarget{recoveryPath, true, true};
                result = ProjectSaveResult::Recovered;
                error.clear();
                return true;
            }
        }
        error = primaryError;
        if (!recoveryError.empty())
            error += "; recovery failed: " + recoveryError;
        return false;
    }

    bool prepareNewProject(AppState &state, ProjectFileTarget &target,
                           bool preserveCurrent, ProjectSaveResult &preservationResult,
                           std::string &error)
    {
        ProjectFileTarget preservedTarget = target;
        if (preserveCurrent &&
            !saveProjectState(state, preservedTarget, true, preservationResult, error))
        {
            return false;
        }
        if (!preserveCurrent)
            preservationResult = ProjectSaveResult::Saved;

        const bool lightTheme = state.lightTheme;
        const std::uint8_t accent = state.accent;
        const ControllerSettings controller = state.controller;
        const bool onboardingDismissed = state.onboardingDismissed;
        const std::uint64_t nextRevision = state.editRevision + 1U;

        auto fresh = std::make_unique<AppState>(makeDefaultState());
        fresh->lightTheme = lightTheme;
        fresh->accent = accent;
        fresh->controller = controller;
        fresh->onboardingDismissed = onboardingDismissed;
        fresh->editRevision = nextRevision;

        target = ProjectFileTarget{projectPathForName("untitled"), true, false};
        state = std::move(*fresh);
        error.clear();
        return true;
    }

    bool loadState(AppState &state, const std::string &path, std::string &error)
    {
        error.clear();
        if (path.empty() || path.find('\0') != std::string::npos)
        {
            error = "save path is empty or invalid";
            return false;
        }

        try
        {
            std::vector<std::uint8_t> bytes;
            bool missing = false;
            if (!readWholeFile(fs::path(path), bytes, error, missing))
            {
                if (missing)
                    error.clear();
                return false;
            }

            AppState decoded = deserializeState(bytes);
            sanitize(decoded);
            state = std::move(decoded);
            return true;
        }
        catch (const ParseError &exception)
        {
            error = std::string("could not load state: ") + exception.what();
            return false;
        }
        catch (const std::exception &exception)
        {
            error = std::string("could not load state: ") + exception.what();
            return false;
        }
    }

} // namespace fms
