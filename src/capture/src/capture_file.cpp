#include "capture/capture_file.hpp"

#include <fstream>
#include <sstream>

namespace aion2::capture {

namespace {

constexpr uint32_t kPcapMagicMicros = 0xA1B2C3D4;
constexpr uint32_t kPcapMagicNanos = 0xA1B23C4D;
constexpr uint32_t kPcapngSectionHeader = 0x0A0D0D0A;
constexpr uint32_t kPcapngByteOrderMagic = 0x1A2B3C4D;
constexpr uint32_t kBlockInterface = 1;
constexpr uint32_t kBlockPacketObsolete = 2;
constexpr uint32_t kBlockSimplePacket = 3;
constexpr uint32_t kBlockEnhancedPacket = 6;
constexpr uint16_t kOptEnd = 0;
constexpr uint16_t kOptTsResol = 9;
constexpr uint16_t kOptTsOffset = 14;
constexpr size_t kPcapFileHeader = 24;
constexpr size_t kPcapRecordHeader = 16;
/// No sane packet or block is larger; a bigger length means a corrupt file, and
/// reading it would allocate whatever the corrupt bytes say.
constexpr uint32_t kMaxBlockBytes = 64u << 20;

[[nodiscard]] uint32_t le32(const uint8_t* b) noexcept {
    return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
           (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
}

[[nodiscard]] uint32_t be32(const uint8_t* b) noexcept {
    return (static_cast<uint32_t>(b[0]) << 24) | (static_cast<uint32_t>(b[1]) << 16) |
           (static_cast<uint32_t>(b[2]) << 8) | static_cast<uint32_t>(b[3]);
}

} // namespace

CaptureFileReader::CaptureFileReader(std::unique_ptr<std::istream> in) : m_in(std::move(in)) {}

std::optional<CaptureFileReader> CaptureFileReader::open(const std::filesystem::path& path, std::string& error) {
    auto in = std::make_unique<std::ifstream>(path, std::ios::binary);
    if (!*in) {
        error = "cannot open file";
        return std::nullopt;
    }
    return start(std::move(in), error);
}

std::optional<CaptureFileReader> CaptureFileReader::from_bytes(std::vector<uint8_t> bytes, std::string& error) {
    auto in = std::make_unique<std::istringstream>(std::string(bytes.begin(), bytes.end()), std::ios::binary);
    return start(std::move(in), error);
}

std::optional<CaptureFileReader> CaptureFileReader::start(std::unique_ptr<std::istream> in, std::string& error) {
    CaptureFileReader reader(std::move(in));
    uint8_t magic[4];
    if (!reader.read_exact(magic, sizeof(magic))) {
        error = "file is shorter than a capture header";
        return std::nullopt;
    }

    const uint32_t as_le = le32(magic);
    const uint32_t as_be = be32(magic);
    if (as_le == kPcapngSectionHeader) {
        reader.m_format = Format::Pcapng;
        reader.m_pending_type = kPcapngSectionHeader;
        return reader;
    }

    if (as_le == kPcapMagicMicros || as_le == kPcapMagicNanos) {
        reader.m_big_endian = false;
        reader.m_nanoseconds = as_le == kPcapMagicNanos;
    } else if (as_be == kPcapMagicMicros || as_be == kPcapMagicNanos) {
        reader.m_big_endian = true;
        reader.m_nanoseconds = as_be == kPcapMagicNanos;
    } else {
        error = "not a pcap or pcapng file";
        return std::nullopt;
    }

    reader.m_format = Format::Pcap;
    uint8_t header[kPcapFileHeader - 4];
    if (!reader.read_exact(header, sizeof(header))) {
        error = "truncated pcap header";
        return std::nullopt;
    }
    // The low 16 bits are the link type; the rest carries FCS flags we don't use.
    reader.m_pcap_link = reader.rd32(header, 16) & 0xFFFF;
    return reader;
}

bool CaptureFileReader::read_exact(uint8_t* dst, size_t n) {
    m_in->read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(n));
    return static_cast<size_t>(m_in->gcount()) == n;
}

uint16_t CaptureFileReader::rd16(std::span<const uint8_t> b, size_t at) const noexcept {
    return m_big_endian ? static_cast<uint16_t>((b[at] << 8) | b[at + 1])
                        : static_cast<uint16_t>(b[at] | (b[at + 1] << 8));
}

uint32_t CaptureFileReader::rd32(std::span<const uint8_t> b, size_t at) const noexcept {
    return m_big_endian ? be32(b.data() + at) : le32(b.data() + at);
}

Micros CaptureFileReader::to_micros(const Interface& iface, uint64_t units) const noexcept {
    const uint64_t ups = iface.units_per_second;
    // Split so neither product overflows for any resolution up to 2^-40 s.
    const uint64_t whole = units / ups;
    const uint64_t frac = units % ups;
    const auto frac_us = static_cast<uint64_t>(static_cast<long double>(frac) * 1'000'000.0L /
                                               static_cast<long double>(ups));
    const int64_t secs = static_cast<int64_t>(whole) + iface.offset_seconds;
    return (secs < 0 ? 0 : static_cast<uint64_t>(secs) * kMicrosPerSecond) + frac_us;
}

bool CaptureFileReader::fail(std::string why) {
    m_error = std::move(why);
    return false;
}

bool CaptureFileReader::next(CapturedPacket& out) {
    if (!m_error.empty()) return false;
    return m_format == Format::Pcap ? next_pcap(out) : next_pcapng(out);
}

bool CaptureFileReader::next_pcap(CapturedPacket& out) {
    uint8_t rec[kPcapRecordHeader];
    m_in->read(reinterpret_cast<char*>(rec), sizeof(rec));
    const auto got = static_cast<size_t>(m_in->gcount());
    if (got == 0) return false;  // clean end of file
    if (got != sizeof(rec)) return fail("truncated packet header");

    const uint32_t sec = rd32(rec, 0);
    const uint32_t frac = rd32(rec, 4);
    const uint32_t incl = rd32(rec, 8);
    const uint32_t orig = rd32(rec, 12);
    if (incl > kMaxBlockBytes) return fail("packet length out of range");

    m_block.resize(incl);
    if (incl > 0 && !read_exact(m_block.data(), incl)) return fail("truncated packet data");

    out.ts = static_cast<Micros>(sec) * kMicrosPerSecond + (m_nanoseconds ? frac / 1000 : frac);
    out.link_type = m_pcap_link;
    out.interface_id = 0;
    out.original_length = orig;
    out.data = std::span<const uint8_t>(m_block.data(), incl);
    return true;
}

bool CaptureFileReader::read_section_header() {
    // Type was consumed. Then total length and the byte-order magic, whose order
    // says how to read everything in this section, the length included.
    uint8_t head[8];
    if (!read_exact(head, sizeof(head))) return fail("truncated section header");
    if (le32(head + 4) == kPcapngByteOrderMagic) {
        m_big_endian = false;
    } else if (be32(head + 4) == kPcapngByteOrderMagic) {
        m_big_endian = true;
    } else {
        return fail("bad pcapng byte-order magic");
    }
    const uint32_t total = rd32(head, 0);
    if (total < 28 || total % 4 != 0 || total > kMaxBlockBytes) return fail("bad section header length");
    m_block.resize(total - 12);
    if (!read_exact(m_block.data(), m_block.size())) return fail("truncated section header");
    m_interfaces.clear();
    return true;
}

bool CaptureFileReader::parse_interface_block(std::span<const uint8_t> body) {
    if (body.size() < 8) return fail("short interface block");
    Interface iface;
    iface.link_type = rd16(body, 0);
    iface.snaplen = rd32(body, 4);

    size_t at = 8;
    while (at + 4 <= body.size()) {
        const uint16_t code = rd16(body, at);
        const uint16_t len = rd16(body, at + 2);
        at += 4;
        if (code == kOptEnd || at + len > body.size()) break;
        if (code == kOptTsResol && len >= 1) {
            const uint8_t v = body[at];
            const auto exp = static_cast<uint8_t>(v & 0x7F);
            uint64_t ups = 1;
            if ((v & 0x80) != 0) {
                if (exp <= 40) ups = 1ULL << exp;
            } else if (exp <= 12) {
                for (uint8_t i = 0; i < exp; ++i) ups *= 10;
            }
            iface.units_per_second = ups;
        } else if (code == kOptTsOffset && len >= 8) {
            const uint64_t lo = rd32(body, at);
            const uint64_t hi = rd32(body, at + 4);
            iface.offset_seconds = static_cast<int64_t>(m_big_endian ? (lo << 32) | hi : (hi << 32) | lo);
        }
        at += (static_cast<size_t>(len) + 3) & ~size_t{3};
    }
    m_interfaces.push_back(iface);
    return true;
}

bool CaptureFileReader::next_pcapng(CapturedPacket& out) {
    for (;;) {
        uint32_t type = 0;
        if (m_pending_type) {
            type = *m_pending_type;
            m_pending_type.reset();
        } else {
            uint8_t raw[4];
            m_in->read(reinterpret_cast<char*>(raw), sizeof(raw));
            const auto got = static_cast<size_t>(m_in->gcount());
            if (got == 0) return false;  // clean end of file
            if (got != sizeof(raw)) return fail("truncated block header");
            // The section header's type reads the same in either order.
            type = le32(raw) == kPcapngSectionHeader ? kPcapngSectionHeader : rd32(raw, 0);
        }

        if (type == kPcapngSectionHeader) {
            if (!read_section_header()) return false;
            continue;
        }

        uint8_t len_raw[4];
        if (!read_exact(len_raw, sizeof(len_raw))) return fail("truncated block header");
        const uint32_t total = rd32(len_raw, 0);
        if (total < 12 || total % 4 != 0 || total > kMaxBlockBytes) return fail("bad block length");
        m_block.resize(total - 8);
        if (!read_exact(m_block.data(), m_block.size())) return fail("truncated block");
        const std::span<const uint8_t> body(m_block.data(), m_block.size() - 4);

        switch (type) {
            case kBlockInterface:
                if (!parse_interface_block(body)) return false;
                break;

            case kBlockEnhancedPacket: {
                if (body.size() < 20) return fail("short packet block");
                const uint32_t iface_id = rd32(body, 0);
                if (iface_id >= m_interfaces.size()) return fail("packet on an undeclared interface");
                const uint64_t units = (static_cast<uint64_t>(rd32(body, 4)) << 32) | rd32(body, 8);
                const uint32_t caplen = rd32(body, 12);
                if (caplen > body.size() - 20) return fail("packet longer than its block");
                out.ts = to_micros(m_interfaces[iface_id], units);
                out.link_type = m_interfaces[iface_id].link_type;
                out.interface_id = iface_id;
                out.original_length = rd32(body, 16);
                out.data = body.subspan(20, caplen);
                return true;
            }

            case kBlockPacketObsolete: {
                if (body.size() < 20) return fail("short packet block");
                const uint32_t iface_id = rd16(body, 0);
                if (iface_id >= m_interfaces.size()) return fail("packet on an undeclared interface");
                const uint64_t units = (static_cast<uint64_t>(rd32(body, 4)) << 32) | rd32(body, 8);
                const uint32_t caplen = rd32(body, 12);
                if (caplen > body.size() - 20) return fail("packet longer than its block");
                out.ts = to_micros(m_interfaces[iface_id], units);
                out.link_type = m_interfaces[iface_id].link_type;
                out.interface_id = iface_id;
                out.original_length = rd32(body, 16);
                out.data = body.subspan(20, caplen);
                return true;
            }

            case kBlockSimplePacket: {
                if (body.size() < 4) return fail("short packet block");
                if (m_interfaces.empty()) return fail("packet on an undeclared interface");
                const uint32_t orig = rd32(body, 0);
                size_t caplen = body.size() - 4;
                if (orig < caplen) caplen = orig;
                if (m_interfaces[0].snaplen != 0 && m_interfaces[0].snaplen < caplen) caplen = m_interfaces[0].snaplen;
                // A simple packet block has no timestamp: 0 says "unknown".
                out.ts = 0;
                out.link_type = m_interfaces[0].link_type;
                out.interface_id = 0;
                out.original_length = orig;
                out.data = body.subspan(4, caplen);
                return true;
            }

            default:
                break;  // statistics, name resolution, custom blocks: nothing for us
        }
    }
}

} // namespace aion2::capture
