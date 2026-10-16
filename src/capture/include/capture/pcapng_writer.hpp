#pragma once

#include "common/micros.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace aion2::capture {

/// Appends little-endian pcapng blocks to a byte buffer: one section, one
/// interface per link type, microsecond timestamps. The meter's own recordings and
/// the tests' synthetic captures are both written this way, and Wireshark opens
/// either.
class PcapngBuilder {
public:
    PcapngBuilder();

    /// The interface id for `link_type`, declaring it on first use.
    uint32_t interface_for(uint32_t link_type, const std::string& name = {});

    void add_packet(uint32_t link_type, Micros ts, std::span<const uint8_t> data);

    [[nodiscard]] const std::vector<uint8_t>& bytes() const noexcept { return m_bytes; }
    /// Hands the bytes over and starts the next batch with no header: what an
    /// append-only file writer flushes.
    [[nodiscard]] std::vector<uint8_t> take_bytes();

private:
    void put16(uint16_t v);
    void put32(uint32_t v);
    void pad_to_4();
    void finish_block(size_t start);

    std::vector<uint8_t> m_bytes;
    std::vector<uint32_t> m_interface_links;
};

/**
 * @brief A pcapng file written as packets arrive, for `--record`.
 *
 * Recordings hold character names, so they land in the app-data folder (or where
 * the user points) and are never uploaded or attached to logs.
 */
class PcapngFileWriter {
public:
    /// False, with `error` set, when the file cannot be created.
    bool open(const std::filesystem::path& path, std::string& error);
    void write(uint32_t link_type, Micros ts, std::span<const uint8_t> data);
    void close();
    [[nodiscard]] bool is_open() const noexcept { return m_out.is_open(); }
    [[nodiscard]] uint64_t bytes_written() const noexcept { return m_written; }

private:
    void flush_builder();

    std::ofstream m_out;
    PcapngBuilder m_builder;
    uint64_t m_written{0};
};

} // namespace aion2::capture
