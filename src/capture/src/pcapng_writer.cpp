#include "capture/pcapng_writer.hpp"

#include "aion2/version.hpp"

namespace aion2::capture {

namespace {
constexpr uint32_t kSectionHeader = 0x0A0D0D0A;
constexpr uint32_t kByteOrderMagic = 0x1A2B3C4D;
constexpr uint32_t kInterfaceBlock = 1;
constexpr uint32_t kEnhancedPacket = 6;
constexpr uint16_t kOptEnd = 0;
constexpr uint16_t kShbUserAppl = 4;
constexpr uint16_t kIfName = 2;
constexpr uint32_t kSnaplen = 262144;
} // namespace

PcapngBuilder::PcapngBuilder() {
    const size_t start = m_bytes.size();
    put32(kSectionHeader);
    put32(0);  // total length, patched by finish_block
    put32(kByteOrderMagic);
    put16(1);  // major
    put16(0);  // minor
    put32(0xFFFFFFFF);  // section length: unknown
    put32(0xFFFFFFFF);
    const std::string appl = std::string("aion2-combat-meter ") + VERSION_STRING;
    put16(kShbUserAppl);
    put16(static_cast<uint16_t>(appl.size()));
    m_bytes.insert(m_bytes.end(), appl.begin(), appl.end());
    pad_to_4();
    put16(kOptEnd);
    put16(0);
    finish_block(start);
}

void PcapngBuilder::put16(uint16_t v) {
    m_bytes.push_back(static_cast<uint8_t>(v));
    m_bytes.push_back(static_cast<uint8_t>(v >> 8));
}

void PcapngBuilder::put32(uint32_t v) {
    for (int i = 0; i < 4; ++i) m_bytes.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void PcapngBuilder::pad_to_4() {
    while (m_bytes.size() % 4 != 0) m_bytes.push_back(0);
}

void PcapngBuilder::finish_block(size_t start) {
    const auto total = static_cast<uint32_t>(m_bytes.size() - start + 4);
    put32(total);
    for (int i = 0; i < 4; ++i) m_bytes[start + 4 + static_cast<size_t>(i)] = static_cast<uint8_t>(total >> (8 * i));
}

uint32_t PcapngBuilder::interface_for(uint32_t link_type, const std::string& name) {
    for (size_t i = 0; i < m_interface_links.size(); ++i) {
        if (m_interface_links[i] == link_type) return static_cast<uint32_t>(i);
    }
    const size_t start = m_bytes.size();
    put32(kInterfaceBlock);
    put32(0);
    put16(static_cast<uint16_t>(link_type));
    put16(0);
    put32(kSnaplen);
    if (!name.empty()) {
        put16(kIfName);
        put16(static_cast<uint16_t>(name.size()));
        m_bytes.insert(m_bytes.end(), name.begin(), name.end());
        pad_to_4();
    }
    // No if_tsresol: the default is microseconds, which is what we write.
    put16(kOptEnd);
    put16(0);
    finish_block(start);
    m_interface_links.push_back(link_type);
    return static_cast<uint32_t>(m_interface_links.size() - 1);
}

void PcapngBuilder::add_packet(uint32_t link_type, Micros ts, std::span<const uint8_t> data) {
    const uint32_t iface = interface_for(link_type);
    const size_t start = m_bytes.size();
    put32(kEnhancedPacket);
    put32(0);
    put32(iface);
    put32(static_cast<uint32_t>(ts >> 32));
    put32(static_cast<uint32_t>(ts));
    put32(static_cast<uint32_t>(data.size()));
    put32(static_cast<uint32_t>(data.size()));
    m_bytes.insert(m_bytes.end(), data.begin(), data.end());
    pad_to_4();
    finish_block(start);
}

std::vector<uint8_t> PcapngBuilder::take_bytes() {
    std::vector<uint8_t> out;
    out.swap(m_bytes);
    return out;
}

bool PcapngFileWriter::open(const std::filesystem::path& path, std::string& error) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    m_out.open(path, std::ios::binary | std::ios::trunc);
    if (!m_out) {
        error = "cannot create recording file";
        return false;
    }
    m_builder = PcapngBuilder();
    m_written = 0;
    flush_builder();
    return true;
}

void PcapngFileWriter::write(uint32_t link_type, Micros ts, std::span<const uint8_t> data) {
    if (!m_out.is_open()) return;
    m_builder.add_packet(link_type, ts, data);
    flush_builder();
}

void PcapngFileWriter::flush_builder() {
    const std::vector<uint8_t> chunk = m_builder.take_bytes();
    m_out.write(reinterpret_cast<const char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
    m_written += chunk.size();
}

void PcapngFileWriter::close() {
    if (m_out.is_open()) m_out.close();
}

} // namespace aion2::capture
