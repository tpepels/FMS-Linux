#pragma once

#include "model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace fms {

enum class TrackLoadMode : std::uint8_t { InPlace, Reset };

struct TimedGlobalSettings {
    bool applyTempo = false;
    std::uint16_t bpm = 120;
    bool applyScale = false;
    std::uint8_t scaleRoot = 0;
    std::uint16_t scaleMask = 0x0FFF;

    bool operator==(const TimedGlobalSettings&) const = default;
};

enum class TransportCommandFamily : std::uint8_t {
    Track,
    Column,
    GlobalSettings,
};

enum class TransportSettlementOutcome : std::uint8_t {
    Applied,
    Cancelled,
};

struct TransportSettlement {
    std::uint64_t sequence = 0;
    TransportCommandFamily family = TransportCommandFamily::Track;
    TransportSettlementOutcome outcome = TransportSettlementOutcome::Cancelled;
    std::uint64_t token = 0;
    // Track-family tokens are scoped per track. Other families use -1.
    int track = -1;
    // Exact portions committed by an Applied column/global transaction.
    // Cancelled records contain zero/false component fields.
    std::uint8_t appliedTrackMask = 0;
    bool appliedTempo = false;
    bool appliedScale = false;
};

constexpr std::size_t kTransportSettlementCapacity = 64u;

struct TransportStatus {
    bool running = false;
    std::array<int, kTrackCount> playheads {-1, -1, -1, -1, -1};
    std::array<std::uint64_t, kTrackCount> loops {};
    // Tokens for the latest accepted and latest applied queued replacement.
    // Tokens are monotonic for the lifetime of an open device; an applied
    // value can jump when a newer command supersedes an armed older command.
    std::array<std::uint64_t, kTrackCount> submittedPatternGenerations {};
    std::array<std::uint64_t, kTrackCount> appliedPatternGenerations {};
    std::uint64_t submittedColumnGeneration = 0;
    std::uint64_t appliedColumnGeneration = 0;
    std::uint64_t submittedGlobalSettingsGeneration = 0;
    std::uint64_t appliedGlobalSettingsGeneration = 0;
    // Settlement high-water marks advance for either application or
    // cancellation. Target-aware scheduling can settle tokens out of order,
    // so consult the exact recent records rather than inferring the outcome of
    // every lower token from a high-water mark.
    std::array<std::uint64_t, kTrackCount> settledPatternGenerations {};
    std::uint64_t settledColumnGeneration = 0;
    std::uint64_t settledGlobalSettingsGeneration = 0;
    // Fixed recent-outcome window. Slots are identified by sequence (zero is
    // unused); callers match family/token/track rather than array position.
    std::uint64_t latestSettlementSequence = 0;
    std::array<TransportSettlement, kTransportSettlementCapacity> settlements {};
    float peakLeft = 0.0f;
    float peakRight = 0.0f;
    std::uint64_t renderedFrames = 0;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool open(SharedState& state);
    void close();
    void setRunning(bool running);
    void toggleRunning();
    void reset();
    void preview(int track, const Step& step, int note = -1);
    // Queue a complete track replacement. The latest accepted replacement for
    // a track is armed and applied at that track's next not-yet-scheduled
    // nominal loop boundary. A negative microtime offset on the first new step
    // is clipped to that boundary. Returns false for an invalid track, an
    // unavailable engine, or a temporarily full command queue.
    bool queuePattern(int track, const TrackData& pattern);
    // Asynchronously replace one track on the next audio callback. InPlace
    // preserves its cursor/clock phase; Reset clears only that track's pending
    // events, voices and echo events and starts at its direction-correct first
    // cursor. Existing per-track generation counters acknowledge the command.
    bool loadTrackImmediate(int track, const TrackData& pattern, TrackLoadMode mode);
    // Atomically arm a masked pattern column for the next global 16-step
    // (96 PPQN tick) boundary. Commands targeting the same boundary compose:
    // disjoint tracks/settings are retained and overlapping values use shared
    // submission order. When stopped, the command applies on the next callback.
    bool queuePatternColumn(const std::array<TrackData, kTrackCount>& patterns,
                            std::uint8_t trackMask =
                                static_cast<std::uint8_t>((1u << kTrackCount) - 1u),
                            const TimedGlobalSettings& settings = {});
    // Atomically load a masked group on the next callback. Optional global
    // settings commit under the same SharedState lock and generation token.
    bool loadPatternColumnImmediate(
        const std::array<TrackData, kTrackCount>& patterns, TrackLoadMode mode,
        std::uint8_t trackMask = static_cast<std::uint8_t>((1u << kTrackCount) - 1u),
        const TimedGlobalSettings& settings = {});
    // Apply selected global settings on the same deterministic global boundary
    // used by column cues. Same-boundary tempo and scale requests compose with
    // column settings by shared submission order. At least one apply flag must
    // be true.
    bool queueGlobalSettings(const TimedGlobalSettings& settings);
    // Request cancellation of one exact still-pending command. The request is
    // asynchronous; inspect its exact settlement record to learn whether
    // cancellation won or the audio callback had already applied it.
    bool cancelTransportCommand(TransportCommandFamily family, std::uint64_t token,
                                int track = -1);
    TransportStatus status() const;
    bool available() const;
    const std::string& error() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace fms
