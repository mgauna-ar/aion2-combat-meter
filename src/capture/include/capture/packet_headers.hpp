#pragma once

#include "common/micros.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace aion2::capture {

/// Link-layer header types (tcpdump.org LINKTYPE_*) the parser understands.
namespace link_type {
inline constexpr uint32_t Null = 0;       ///< BSD loopback: 4-byte address family. Npcap's loopback adapter.
inline constexpr uint32_t Ethernet = 1;
inline constexpr uint32_t Raw = 101;      ///< Bare IPv4 or IPv6, no link header.
inline constexpr uint32_t Loop = 108;     ///< OpenBSD loopback, like Null with a big-endian family.
inline constexpr uint32_t LinuxSll = 113;
inline constexpr uint32_t Ipv4 = 228;
inline constexpr uint32_t Ipv6 = 229;
inline constexpr uint32_t LinuxSll2 = 276;
} // namespace link_type

namespace tcp_flag {
inline constexpr uint8_t Fin = 0x01;
inline constexpr uint8_t Syn = 0x02;
inline constexpr uint8_t Rst = 0x04;
inline constexpr uint8_t Psh = 0x08;
inline constexpr uint8_t Ack = 0x10;
} // namespace tcp_flag

/// An IPv4 or IPv6 address. IPv4 is stored as an IPv4-mapped IPv6 address
/// (::ffff:a.b.c.d), so one key type covers both families.
struct IpAddress {
    std::array<uint8_t, 16> bytes{};

    [[nodiscard]] static IpAddress v4(uint32_t host_order) noexcept;
    [[nodiscard]] static IpAddress v6(std::span<const uint8_t, 16> raw) noexcept;
    [[nodiscard]] bool is_v4() const noexcept;
    [[nodiscard]] bool is_loopback() const noexcept;
    [[nodiscard]] std::string to_string() const;

    [[nodiscard]] bool operator==(const IpAddress&) const = default;
};

/// One direction of a TCP connection.
struct FlowKey {
    IpAddress src;
    IpAddress dst;
    uint16_t src_port{0};
    uint16_t dst_port{0};

    [[nodiscard]] FlowKey reversed() const noexcept { return FlowKey{dst, src, dst_port, src_port}; }
    /// "a.b.c.d:p -> e.f.g.h:q"
    [[nodiscard]] std::string to_string() const;

    [[nodiscard]] bool operator==(const FlowKey&) const = default;
};

struct FlowKeyHash {
    [[nodiscard]] size_t operator()(const FlowKey& key) const noexcept;
};

/// A TCP segment found in a captured frame. `payload` views the frame's bytes, so
/// it lives only as long as the frame does.
struct TcpSegment {
    FlowKey flow;
    uint32_t seq{0};
    uint32_t ack{0};
    uint8_t flags{0};
    Micros ts{0};
    std::span<const uint8_t> payload;

    [[nodiscard]] bool has(uint8_t flag) const noexcept { return (flags & flag) != 0; }
};

/// The TCP segment in `frame`, a captured packet of link type `link`. nullopt for
/// anything else: another protocol, an IP fragment, a truncated header. A payload
/// cut short by the capture's snap length is returned as far as it was captured.
[[nodiscard]] std::optional<TcpSegment> parse_tcp_segment(uint32_t link, std::span<const uint8_t> frame, Micros ts);

} // namespace aion2::capture
