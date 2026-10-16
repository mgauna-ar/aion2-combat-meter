#pragma once

#include "common/micros.hpp"
#include "meter/encounter_engine.hpp"
#include "meter/meter_settings.hpp"
#include "meter/types.hpp"
#include "pipeline/stream_demux.hpp"
#include "protocol/frame_decoder.hpp"
#include "protocol/packet_parser.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace aion2::pipeline {

/// What the parser made of a frame, for the replay tool's --unknown.
enum class FrameOutcome : uint8_t {
    Decoded,
    /// An opcode the parser reads, whose body did not parse.
    Failed,
    /// An opcode the parser does not read.
    Unknown,
    /// An opcode the parser knows and skips (pong, cast).
    Ignored,
};

struct PipelineOptions {
    DemuxOptions demux;
    /// Spelled from aion2:: because GCC rejects a member named `meter` after an
    /// unqualified `meter::` in the same class ("changes meaning").
    aion2::meter::MeterSettings meter;
};

/// Where the capture stands, for the overlay's status line.
enum class CaptureState : uint8_t {
    /// No game traffic seen yet: the game is not running, or capture is not working.
    Waiting,
    Live,
    /// Game traffic stopped kQuietAfter ago.
    Quiet,
};

/// Everything the overlay draws, built whole by the engine thread and read by the
/// UI thread through a SnapshotSlot.
struct LiveSnapshot {
    meter::FightSummary fight;
    /// Finished fights in memory, oldest first.
    std::vector<meter::FightSummary> recent;
    uint64_t recent_revision{0};
    CaptureState capture{CaptureState::Waiting};
    std::string local_name;
    uint32_t map_id{0};
    Micros now{0};
    proto::ParseStats parse;
    meter::CaptureQuality capture_totals;
};

/**
 * @brief Captured packets to fights: demux, frame decoder, parser and engine.
 *
 * One instance runs on one thread: the live engine thread, or the replay tool.
 * Both drive it the same way, on the capture clock, so a replay of a recording
 * reproduces the live meter's fights exactly.
 */
class Pipeline final : private proto::FrameSink {
public:
    static constexpr Micros kQuietAfter = 5 * kMicrosPerSecond;
    /// How often, in capture time, holes are expired and fight ends checked when
    /// packets keep coming.
    static constexpr Micros kTickEvery = 100 * kMicrosPerMilli;

    explicit Pipeline(PipelineOptions options = {});

    void on_packet(uint32_t link, std::span<const uint8_t> data, Micros ts);
    /// Capture time has reached `now` with no packet: expire holes, end fights.
    void tick(Micros now);
    void end_fight(meter::EndReason reason, Micros now);
    /// Packets the capture threads dropped, cumulative.
    void set_dropped_packets(uint64_t dropped) noexcept { m_dropped = dropped; }
    void set_meter_settings(const meter::MeterSettings& settings) { m_engine.set_settings(settings); }

    /// Fights finished since the last call, oldest first.
    [[nodiscard]] std::vector<meter::FightSummary> take_finished() { return m_engine.take_finished(); }
    [[nodiscard]] LiveSnapshot snapshot(Micros now) const;

    /// Taps for aion2meter-replay: every game frame once parsed, every parsed event.
    void set_frame_observer(std::function<void(const proto::Frame&, FrameOutcome)> fn) {
        m_frame_observer = std::move(fn);
    }
    void set_event_observer(std::function<void(const proto::ParsedEvent&)> fn) { m_event_observer = std::move(fn); }

    [[nodiscard]] const StreamDemux& demux() const noexcept { return m_demux; }
    [[nodiscard]] const proto::PacketParser& parser() const noexcept { return m_parser; }
    [[nodiscard]] const meter::EncounterEngine& engine() const noexcept { return m_engine; }

private:
    void on_frame(const proto::Frame& frame) override;
    [[nodiscard]] meter::CaptureQuality capture_totals() const;

    StreamDemux m_demux;
    proto::PacketParser m_parser;
    meter::EncounterEngine m_engine;
    std::vector<proto::ParsedEvent> m_events;
    uint64_t m_dropped{0};
    Micros m_last_tick{0};
    std::function<void(const proto::Frame&, FrameOutcome)> m_frame_observer;
    std::function<void(const proto::ParsedEvent&)> m_event_observer;
};

} // namespace aion2::pipeline
