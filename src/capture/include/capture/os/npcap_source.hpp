#pragma once

#include "capture/packet_source.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace aion2::capture {

struct NpcapOptions {
    uint16_t server_port{13328};
    /// The game's executable, to find its connections (connection_finder.hpp).
    std::string game_process{"Aion2.exe"};
    /// Npcap's kernel buffer per adapter.
    int buffer_bytes{8 << 20};
};

/**
 * @brief Live capture through Npcap, read only.
 *
 * wpcap.dll is loaded at run time from System32\Npcap (Npcap's licence forbids
 * shipping it, so the user installs Npcap; the meter links nothing of it). Every
 * adapter that is up is opened, plus Npcap's loopback adapter, each with a filter
 * for the game server's side of the connection, in non-promiscuous mode. A watcher
 * looks up the game's connections every kWatchInterval; when the game goes
 * through a local relay (a ping reducer or VPN), the loopback filter follows the
 * relay's port. Each capture thread owns its pcap handle and applies filter
 * changes itself, since a handle is not safe to share between threads.
 *
 * Nothing is ever sent: no send function is even looked up.
 */
class NpcapSource final : public PacketSource {
public:
    static constexpr int kWatchIntervalMs = 2000;

    explicit NpcapSource(NpcapOptions options);
    ~NpcapSource() override;
    NpcapSource(const NpcapSource&) = delete;
    NpcapSource& operator=(const NpcapSource&) = delete;

    bool start(Push push, std::string& error) override;
    void stop() override;
    [[nodiscard]] Micros now() const override { return wall_clock_now(); }
    [[nodiscard]] uint64_t dropped() const override;
    [[nodiscard]] std::string status() const override;

    /// The BPF filter for the loopback adapter given the relays' ports: the
    /// server port, plus each relay port as a source. Exposed for tests.
    [[nodiscard]] static std::string loopback_filter(uint16_t server_port, const std::vector<uint16_t>& relay_ports);
    /// The filter for every other adapter.
    [[nodiscard]] static std::string adapter_filter(uint16_t server_port);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace aion2::capture
