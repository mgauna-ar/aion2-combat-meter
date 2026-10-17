#include "protocol/byte_reader.hpp"

#include <cstring>

namespace aion2::proto {

namespace {
constexpr size_t kMaxVarintBytes = 10;
} // namespace

std::optional<Varint> read_varint(std::span<const uint8_t> bytes, bool* overlong) noexcept {
    if (overlong != nullptr) *overlong = false;
    uint64_t value = 0;
    for (size_t i = 0; i < bytes.size(); ++i) {
        // A 10th byte over 1, or an 11th, would be bits past 64: never a value.
        if (i == kMaxVarintBytes || (i == kMaxVarintBytes - 1 && bytes[i] > 0x01)) {
            if (overlong != nullptr) *overlong = true;
            return std::nullopt;
        }
        value |= static_cast<uint64_t>(bytes[i] & 0x7F) << (7 * i);
        if ((bytes[i] & 0x80) == 0) return Varint{value, i + 1};
    }
    if (bytes.size() >= kMaxVarintBytes && overlong != nullptr) *overlong = true;
    return std::nullopt;
}

bool ByteReader::need(size_t n) noexcept {
    if (m_failed || m_data.size() - m_pos < n) {
        m_failed = true;
        return false;
    }
    return true;
}

uint8_t ByteReader::u8() noexcept {
    if (!need(1)) return 0;
    return m_data[m_pos++];
}

uint16_t ByteReader::u16le() noexcept {
    if (!need(2)) return 0;
    const auto v = static_cast<uint16_t>(m_data[m_pos] | (m_data[m_pos + 1] << 8));
    m_pos += 2;
    return v;
}

uint16_t ByteReader::u16be() noexcept {
    if (!need(2)) return 0;
    const auto v = static_cast<uint16_t>((m_data[m_pos] << 8) | m_data[m_pos + 1]);
    m_pos += 2;
    return v;
}

uint32_t ByteReader::u24le() noexcept {
    if (!need(3)) return 0;
    const uint32_t v = static_cast<uint32_t>(m_data[m_pos]) | (static_cast<uint32_t>(m_data[m_pos + 1]) << 8) |
                       (static_cast<uint32_t>(m_data[m_pos + 2]) << 16);
    m_pos += 3;
    return v;
}

uint32_t ByteReader::u32le() noexcept {
    if (!need(4)) return 0;
    uint32_t v = 0;
    for (size_t i = 0; i < 4; ++i) v |= static_cast<uint32_t>(m_data[m_pos + i]) << (8 * i);
    m_pos += 4;
    return v;
}

uint64_t ByteReader::u64le() noexcept {
    if (!need(8)) return 0;
    uint64_t v = 0;
    for (size_t i = 0; i < 8; ++i) v |= static_cast<uint64_t>(m_data[m_pos + i]) << (8 * i);
    m_pos += 8;
    return v;
}

float ByteReader::f32le() noexcept {
    const uint32_t bits = u32le();
    float f = 0.0f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

uint64_t ByteReader::varint() noexcept {
    if (m_failed) return 0;
    const auto v = read_varint(m_data.subspan(m_pos));
    if (!v) {
        m_failed = true;
        return 0;
    }
    m_pos += v->size;
    return v->value;
}

std::span<const uint8_t> ByteReader::bytes(size_t n) noexcept {
    if (!need(n)) return {};
    const auto out = m_data.subspan(m_pos, n);
    m_pos += n;
    return out;
}

void ByteReader::skip(size_t n) noexcept {
    if (need(n)) m_pos += n;
}

void ByteReader::seek(size_t pos) noexcept {
    if (m_failed) return;
    if (pos > m_data.size()) {
        m_failed = true;
        return;
    }
    m_pos = pos;
}

} // namespace aion2::proto
