#pragma once

#include "common/micros.hpp"
#include "game/plugin.hpp"
#include "ping/ping_tracker.hpp"
#include "protocol/events.hpp"

#include <optional>

namespace aion2::ping {

/**
 * @brief The Ping plugin: the latency to the game server, its jitter and a graph of
 * both, for the ping overlay.
 *
 * Fed by the pipeline like the others, on the same thread and the same capture
 * clock. It reads two events, the pong and the heartbeat, and sends nothing: the
 * ping is the game's own, read off its echo (PingTracker).
 */
class PingPlugin final : public game::Plugin {
public:
    void on_event(const proto::ParsedEvent& event) override;
    void tick(Micros now) override;
    /// Forgets everything: the plugin was switched off.
    void clear() { m_tracker.clear(); }

    /// How far the capture's clock sits from the machine's performance counter
    /// (capture::PacketSource::counter_offset): the one figure from outside the
    /// capture, without which a pong gives no ping.
    void set_counter_offset(std::optional<Micros> offset) noexcept { m_tracker.set_counter_offset(offset); }

    /// What the overlay draws at `now`.
    [[nodiscard]] PingSnapshot snapshot(Micros now) const { return m_tracker.snapshot(now); }

    [[nodiscard]] const PingTracker& tracker() const noexcept { return m_tracker; }

private:
    PingTracker m_tracker;
};

} // namespace aion2::ping
