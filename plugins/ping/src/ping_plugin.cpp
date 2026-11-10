#include "ping/ping_plugin.hpp"

#include <variant>

namespace aion2::ping {

void PingPlugin::on_event(const proto::ParsedEvent& event) {
    if (const auto* pong = std::get_if<proto::Pong>(&event.event)) {
        m_tracker.on_pong(*pong, event.connection, event.ts);
    } else if (const auto* beat = std::get_if<proto::Heartbeat>(&event.event)) {
        m_tracker.on_heartbeat(beat->server_ms, event.connection, event.ts);
    }
}

void PingPlugin::tick(Micros now) { m_tracker.tick(now); }

} // namespace aion2::ping
