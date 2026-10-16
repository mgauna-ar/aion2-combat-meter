#include "capture/packet_headers.hpp"

#include <cstdio>

namespace aion2::capture {

namespace {

constexpr uint16_t kEtherTypeIpv4 = 0x0800;
constexpr uint16_t kEtherTypeIpv6 = 0x86DD;
constexpr uint16_t kEtherTypeVlan = 0x8100;
constexpr uint16_t kEtherTypeQinQ = 0x88A8;
constexpr uint8_t kIpProtoTcp = 6;
constexpr size_t kEthernetHeader = 14;
constexpr size_t kVlanTag = 4;
constexpr size_t kLoopbackHeader = 4;
constexpr size_t kSllHeader = 16;
constexpr size_t kSll2Header = 20;
constexpr size_t kIpv4MinHeader = 20;
constexpr size_t kIpv6Header = 40;
constexpr size_t kTcpMinHeader = 20;

[[nodiscard]] uint16_t be16(std::span<const uint8_t> b, size_t at) noexcept {
    return static_cast<uint16_t>((b[at] << 8) | b[at + 1]);
}

[[nodiscard]] uint32_t be32(std::span<const uint8_t> b, size_t at) noexcept {
    return (static_cast<uint32_t>(b[at]) << 24) | (static_cast<uint32_t>(b[at + 1]) << 16) |
           (static_cast<uint32_t>(b[at + 2]) << 8) | static_cast<uint32_t>(b[at + 3]);
}

/// The TCP segment in `tcp`, the bytes after the IP header.
std::optional<TcpSegment> parse_tcp(std::span<const uint8_t> tcp, const IpAddress& src, const IpAddress& dst,
                                    Micros ts) {
    if (tcp.size() < kTcpMinHeader) return std::nullopt;
    const size_t header_len = static_cast<size_t>(tcp[12] >> 4) * 4;
    if (header_len < kTcpMinHeader || header_len > tcp.size()) return std::nullopt;

    TcpSegment seg;
    seg.flow = FlowKey{src, dst, be16(tcp, 0), be16(tcp, 2)};
    seg.seq = be32(tcp, 4);
    seg.ack = be32(tcp, 8);
    seg.flags = tcp[13];
    seg.ts = ts;
    seg.payload = tcp.subspan(header_len);
    return seg;
}

std::optional<TcpSegment> parse_ipv4(std::span<const uint8_t> ip, Micros ts) {
    if (ip.size() < kIpv4MinHeader || (ip[0] >> 4) != 4) return std::nullopt;
    const size_t header_len = static_cast<size_t>(ip[0] & 0x0F) * 4;
    if (header_len < kIpv4MinHeader || header_len > ip.size()) return std::nullopt;
    // A fragment is either not the first (offset set) or not the last (MF); the game
    // stream never needs reassembling at the IP layer, so neither is worth a buffer.
    const uint16_t frag = be16(ip, 6);
    if ((frag & 0x3FFF) != 0) return std::nullopt;
    if (ip[9] != kIpProtoTcp) return std::nullopt;

    // The total length trims the Ethernet padding a short frame carries. A frame
    // cut by the snap length is shorter than it, and keeps what it has.
    size_t total = be16(ip, 2);
    if (total < header_len) return std::nullopt;
    if (total > ip.size()) total = ip.size();

    return parse_tcp(ip.subspan(header_len, total - header_len), IpAddress::v4(be32(ip, 12)),
                     IpAddress::v4(be32(ip, 16)), ts);
}

std::optional<TcpSegment> parse_ipv6(std::span<const uint8_t> ip, Micros ts) {
    if (ip.size() < kIpv6Header || (ip[0] >> 4) != 6) return std::nullopt;
    const IpAddress src = IpAddress::v6(ip.subspan<8, 16>());
    const IpAddress dst = IpAddress::v6(ip.subspan<24, 16>());

    size_t payload_len = be16(ip, 4);
    std::span<const uint8_t> rest = ip.subspan(kIpv6Header);
    if (payload_len < rest.size()) rest = rest.first(payload_len);

    // Walk the extension headers the stack may put before TCP. A fragment header
    // ends the walk, as an IPv4 fragment does.
    uint8_t next = ip[6];
    for (int hops = 0; hops < 8; ++hops) {
        if (next == kIpProtoTcp) return parse_tcp(rest, src, dst, ts);
        const bool skippable = next == 0 || next == 43 || next == 60;  // hop-by-hop, routing, dest opts
        if (!skippable || rest.size() < 8) return std::nullopt;
        const size_t ext_len = (static_cast<size_t>(rest[1]) + 1) * 8;
        if (ext_len > rest.size()) return std::nullopt;
        next = rest[0];
        rest = rest.subspan(ext_len);
    }
    return std::nullopt;
}

/// IPv4 or IPv6 by the version nibble, for link types that do not say which.
std::optional<TcpSegment> parse_ip(std::span<const uint8_t> ip, Micros ts) {
    if (ip.empty()) return std::nullopt;
    switch (ip[0] >> 4) {
        case 4: return parse_ipv4(ip, ts);
        case 6: return parse_ipv6(ip, ts);
        default: return std::nullopt;
    }
}

std::optional<TcpSegment> parse_ethernet(std::span<const uint8_t> frame, Micros ts) {
    if (frame.size() < kEthernetHeader) return std::nullopt;
    size_t offset = 12;
    uint16_t ether_type = be16(frame, offset);
    // Up to two VLAN tags (802.1Q, or QinQ outside one).
    for (int tags = 0; tags < 2 && (ether_type == kEtherTypeVlan || ether_type == kEtherTypeQinQ); ++tags) {
        offset += kVlanTag;
        if (frame.size() < offset + 2) return std::nullopt;
        ether_type = be16(frame, offset);
    }
    offset += 2;
    if (ether_type == kEtherTypeIpv4) return parse_ipv4(frame.subspan(offset), ts);
    if (ether_type == kEtherTypeIpv6) return parse_ipv6(frame.subspan(offset), ts);
    return std::nullopt;
}

} // namespace

IpAddress IpAddress::v4(uint32_t host_order) noexcept {
    IpAddress a;
    a.bytes[10] = 0xFF;
    a.bytes[11] = 0xFF;
    a.bytes[12] = static_cast<uint8_t>(host_order >> 24);
    a.bytes[13] = static_cast<uint8_t>(host_order >> 16);
    a.bytes[14] = static_cast<uint8_t>(host_order >> 8);
    a.bytes[15] = static_cast<uint8_t>(host_order);
    return a;
}

IpAddress IpAddress::v6(std::span<const uint8_t, 16> raw) noexcept {
    IpAddress a;
    for (size_t i = 0; i < 16; ++i) a.bytes[i] = raw[i];
    return a;
}

bool IpAddress::is_v4() const noexcept {
    for (size_t i = 0; i < 10; ++i) {
        if (bytes[i] != 0) return false;
    }
    return bytes[10] == 0xFF && bytes[11] == 0xFF;
}

bool IpAddress::is_loopback() const noexcept {
    if (is_v4()) return bytes[12] == 127;
    for (size_t i = 0; i < 15; ++i) {
        if (bytes[i] != 0) return false;
    }
    return bytes[15] == 1;
}

std::string IpAddress::to_string() const {
    char buf[48];
    if (is_v4()) {
        std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", unsigned{bytes[12]}, unsigned{bytes[13]}, unsigned{bytes[14]},
                      unsigned{bytes[15]});
        return buf;
    }
    // Full form without zero compression: unambiguous, and only read by people.
    std::string out;
    for (size_t i = 0; i < 16; i += 2) {
        std::snprintf(buf, sizeof(buf), "%s%x", i == 0 ? "" : ":", (unsigned{bytes[i]} << 8) | unsigned{bytes[i + 1]});
        out += buf;
    }
    return out;
}

std::string FlowKey::to_string() const {
    const auto endpoint = [](const IpAddress& ip, uint16_t port) {
        return ip.is_v4() ? ip.to_string() + ":" + std::to_string(port)
                          : "[" + ip.to_string() + "]:" + std::to_string(port);
    };
    return endpoint(src, src_port) + " -> " + endpoint(dst, dst_port);
}

size_t FlowKeyHash::operator()(const FlowKey& key) const noexcept {
    // FNV-1a over the four fields: flows are few, so the hash only needs to be fair.
    uint64_t h = 1469598103934665603ULL;
    const auto mix = [&h](uint8_t b) {
        h ^= b;
        h *= 1099511628211ULL;
    };
    for (const uint8_t b : key.src.bytes) mix(b);
    for (const uint8_t b : key.dst.bytes) mix(b);
    mix(static_cast<uint8_t>(key.src_port >> 8));
    mix(static_cast<uint8_t>(key.src_port));
    mix(static_cast<uint8_t>(key.dst_port >> 8));
    mix(static_cast<uint8_t>(key.dst_port));
    return static_cast<size_t>(h);
}

std::optional<TcpSegment> parse_tcp_segment(uint32_t link, std::span<const uint8_t> frame, Micros ts) {
    switch (link) {
        case link_type::Ethernet:
            return parse_ethernet(frame, ts);
        case link_type::Null:
        case link_type::Loop:
            // The family is in host order for Null and big-endian for Loop, and its
            // value for IPv6 differs by OS; the IP version nibble says it plainly.
            if (frame.size() < kLoopbackHeader) return std::nullopt;
            return parse_ip(frame.subspan(kLoopbackHeader), ts);
        case link_type::Raw:
        case link_type::Ipv4:
        case link_type::Ipv6:
            return parse_ip(frame, ts);
        case link_type::LinuxSll: {
            if (frame.size() < kSllHeader) return std::nullopt;
            const uint16_t proto = be16(frame, 14);
            if (proto == kEtherTypeIpv4) return parse_ipv4(frame.subspan(kSllHeader), ts);
            if (proto == kEtherTypeIpv6) return parse_ipv6(frame.subspan(kSllHeader), ts);
            return std::nullopt;
        }
        case link_type::LinuxSll2: {
            if (frame.size() < kSll2Header) return std::nullopt;
            const uint16_t proto = be16(frame, 0);
            if (proto == kEtherTypeIpv4) return parse_ipv4(frame.subspan(kSll2Header), ts);
            if (proto == kEtherTypeIpv6) return parse_ipv6(frame.subspan(kSll2Header), ts);
            return std::nullopt;
        }
        default:
            return std::nullopt;
    }
}

} // namespace aion2::capture
