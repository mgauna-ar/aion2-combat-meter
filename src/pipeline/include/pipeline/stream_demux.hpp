#pragma once

#include "capture/packet_headers.hpp"
#include "capture/tcp_reassembler.hpp"
#include "common/micros.hpp"
#include "protocol/frame_decoder.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace aion2::pipeline {

struct DemuxOptions {
    /// The game server's TCP port. Its server-to-client flows are the game's from
    /// the first byte.
    uint16_t server_port{13328};
    /// Also try every other TCP flow, and take it once its bytes hold
    /// kConfirmHeartbeats heartbeats: a ping reducer or VPN relays the game over
    /// loopback on some other port.
    bool any_port{true};
};

/// What one direction of one TCP connection turned out to be.
enum class FlowRole : uint8_t {
    /// Not known yet: decoded and held back until heartbeats confirm it.
    Candidate,
    Game,
    /// Plenty of bytes and no heartbeat: not the game. No longer decoded.
    Rejected,
};

struct FlowSummary {
    capture::FlowKey key;
    FlowRole role{FlowRole::Candidate};
    uint64_t segments{0};
    uint64_t bytes{0};
    uint64_t gap_bytes{0};
    Micros first_ts{0};
    Micros last_ts{0};
    proto::DecoderStats decoder;
};

/**
 * @brief Captured packets in, game frames out.
 *
 * Every TCP flow gets a reassembler and a frame decoder. Server-port flows are the
 * game's at once. Any other is a candidate: its frames are held (up to
 * kMaxHeldBytes) until kConfirmHeartbeats heartbeats prove it a game stream, then
 * released in order, so the identities sent right after login are not lost to the
 * wait. A candidate that has passed kRejectAfterBytes with no heartbeat is dropped.
 * Frames of every game flow go to one sink: the game uses one connection at a time,
 * and a reconnect is a new flow. Each frame says which flow it came on
 * (Frame::connection): entering a dungeon opens a connection to its server.
 */
class StreamDemux {
public:
    static constexpr uint64_t kConfirmHeartbeats = 3;
    static constexpr uint64_t kRejectAfterBytes = 256u << 10;
    static constexpr size_t kMaxHeldBytes = 4u << 20;

    explicit StreamDemux(DemuxOptions options = {});
    ~StreamDemux();
    StreamDemux(const StreamDemux&) = delete;
    StreamDemux& operator=(const StreamDemux&) = delete;

    /// One captured frame of link type `link`. Frames of game flows go to `sink`.
    void on_packet(uint32_t link, std::span<const uint8_t> data, Micros ts, proto::FrameSink& sink);
    /// One parsed segment, for callers that have parsed it already.
    void on_segment(const capture::TcpSegment& segment, proto::FrameSink& sink);
    /// Reports holes that have waited long enough; call on a quiet capture.
    void tick(Micros now, proto::FrameSink& sink);

    [[nodiscard]] std::vector<FlowSummary> flows() const;
    [[nodiscard]] uint64_t packets() const noexcept { return m_packets; }
    [[nodiscard]] uint64_t tcp_segments() const noexcept { return m_segments; }
    /// Gap bytes, resyncs and bundle errors summed over the game flows.
    [[nodiscard]] uint64_t game_gap_bytes() const;
    [[nodiscard]] uint64_t game_resyncs() const;
    [[nodiscard]] uint64_t game_bundle_errors() const;
    /// Capture time of the latest byte of any game flow; 0 before one.
    [[nodiscard]] Micros last_game_ts() const noexcept { return m_last_game_ts; }

private:
    class Flow;

    DemuxOptions m_options;
    std::unordered_map<capture::FlowKey, std::unique_ptr<Flow>, capture::FlowKeyHash> m_flows;
    uint64_t m_packets{0};
    uint64_t m_segments{0};
    /// Frame::connection of the next flow seen.
    uint32_t m_next_connection{1};
    Micros m_last_game_ts{0};
};

} // namespace aion2::pipeline
