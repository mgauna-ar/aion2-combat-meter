#pragma once

#include "capture/packet_headers.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace aion2::capture {

/// One established TCP connection of the game process.
struct GameConnection {
    uint32_t pid{0};
    IpAddress local;
    uint16_t local_port{0};
    IpAddress remote;
    uint16_t remote_port{0};

    /// A ping reducer or VPN relays the game through a local proxy.
    [[nodiscard]] bool relayed() const noexcept { return remote.is_loopback(); }
};

/**
 * @brief The game's TCP connections, found without touching the game.
 *
 * The process is found by executable name in a Toolhelp process snapshot, and its
 * connections in the system TCP table (GetExtendedTcpTable). Neither opens a handle
 * to the game process: the meter never does (AGENTS.md, "Passive only").
 * Connections to `server_port`, or to a loopback relay, are returned. Empty off
 * Windows, and when the game is not running.
 */
[[nodiscard]] std::vector<GameConnection> find_game_connections(const std::string& process_name,
                                                                uint16_t server_port);

/// Whether a process named `process_name` (case-insensitive) is running.
[[nodiscard]] bool game_running(const std::string& process_name);

/// The ids of the processes named `process_name` (case-insensitive), from the same
/// Toolhelp snapshot: names and ids only, no handle opened. Empty off Windows.
[[nodiscard]] std::vector<uint32_t> game_process_ids(const std::string& process_name);

} // namespace aion2::capture
