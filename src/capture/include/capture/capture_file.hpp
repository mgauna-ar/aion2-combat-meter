#pragma once

#include "common/micros.hpp"
#include <cstdint>
#include <filesystem>
#include <istream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace aion2::capture {

/// One packet read from a capture file. `data` views the reader's buffer and is
/// valid until the next call to next().
struct CapturedPacket {
    Micros ts{0};
    uint32_t link_type{0};
    uint32_t interface_id{0};
    /// The packet's length on the wire; `data` is shorter when the snap length cut it.
    uint32_t original_length{0};
    std::span<const uint8_t> data;
};

/**
 * @brief Streaming reader for classic pcap and pcapng files.
 *
 * pcap: either byte order, microsecond or nanosecond timestamps. pcapng: any byte
 * order per section, several sections and interfaces, if_tsresol and if_tsoffset,
 * Enhanced, Simple and the obsolete Packet blocks; every other block is skipped.
 * Reads block by block, so a capture of any size fits in memory.
 */
class CaptureFileReader {
public:
    enum class Format : uint8_t { Pcap, Pcapng };

    /// nullopt with `error` set when the file cannot be opened or is neither format.
    [[nodiscard]] static std::optional<CaptureFileReader> open(const std::filesystem::path& path, std::string& error);
    /// The same over bytes in memory, for tests.
    [[nodiscard]] static std::optional<CaptureFileReader> from_bytes(std::vector<uint8_t> bytes, std::string& error);

    /// The next packet. False at the end of the file, and on a malformed block,
    /// which error() then names: a capture cut off mid-write still yields every
    /// whole packet before the cut.
    bool next(CapturedPacket& out);

    [[nodiscard]] const std::string& error() const noexcept { return m_error; }
    [[nodiscard]] Format format() const noexcept { return m_format; }

    CaptureFileReader(CaptureFileReader&&) noexcept = default;
    CaptureFileReader& operator=(CaptureFileReader&&) noexcept = default;
    ~CaptureFileReader() = default;

private:
    struct Interface {
        uint32_t link_type{0};
        uint32_t snaplen{0};
        /// Timestamp units per second.
        uint64_t units_per_second{1'000'000};
        int64_t offset_seconds{0};
    };

    explicit CaptureFileReader(std::unique_ptr<std::istream> in);
    static std::optional<CaptureFileReader> start(std::unique_ptr<std::istream> in, std::string& error);

    bool read_exact(uint8_t* dst, size_t n);
    bool next_pcap(CapturedPacket& out);
    bool next_pcapng(CapturedPacket& out);
    bool read_section_header();
    bool parse_interface_block(std::span<const uint8_t> body);
    [[nodiscard]] uint16_t rd16(std::span<const uint8_t> b, size_t at) const noexcept;
    [[nodiscard]] uint32_t rd32(std::span<const uint8_t> b, size_t at) const noexcept;
    [[nodiscard]] Micros to_micros(const Interface& iface, uint64_t units) const noexcept;
    bool fail(std::string why);

    std::unique_ptr<std::istream> m_in;
    Format m_format{Format::Pcap};
    /// The file (pcap) or current section (pcapng) is big-endian.
    bool m_big_endian{false};
    bool m_nanoseconds{false};
    uint32_t m_pcap_link{0};
    std::vector<Interface> m_interfaces;
    std::vector<uint8_t> m_block;
    std::string m_error;
    /// The first block's type was read by open() to tell the formats apart.
    std::optional<uint32_t> m_pending_type;
};

} // namespace aion2::capture
