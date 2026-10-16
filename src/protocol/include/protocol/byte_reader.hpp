#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace aion2::proto {

/// A LEB128 varint at the front of `bytes`: its value and how many bytes it took.
struct Varint {
    uint64_t value{0};
    size_t size{0};
};

/// The varint starting `bytes`. nullopt when `bytes` ends inside it, or when it
/// runs past the 10 bytes a 64-bit value can need (then `overlong` is set: those
/// bytes are not a varint, as opposed to not all here yet).
[[nodiscard]] std::optional<Varint> read_varint(std::span<const uint8_t> bytes, bool* overlong = nullptr) noexcept;

/**
 * @brief Bounds-checked cursor over a packet body.
 *
 * A read past the end fails, returns 0, and leaves the reader failed: every later
 * read returns 0 too, and ok() stays false. A parser reads a whole record and
 * checks ok() once, rather than after each field. Never throws.
 */
class ByteReader {
public:
    explicit ByteReader(std::span<const uint8_t> data) noexcept : m_data(data) {}

    [[nodiscard]] uint8_t u8() noexcept;
    [[nodiscard]] uint16_t u16le() noexcept;
    /// The opcode's byte order.
    [[nodiscard]] uint16_t u16be() noexcept;
    [[nodiscard]] uint32_t u24le() noexcept;
    [[nodiscard]] uint32_t u32le() noexcept;
    [[nodiscard]] uint64_t u64le() noexcept;
    [[nodiscard]] float f32le() noexcept;
    [[nodiscard]] uint64_t varint() noexcept;
    /// The next `n` bytes; empty, and failed, when fewer remain.
    [[nodiscard]] std::span<const uint8_t> bytes(size_t n) noexcept;
    void skip(size_t n) noexcept;
    /// Moves to `pos`, which must lie within the data.
    void seek(size_t pos) noexcept;

    [[nodiscard]] bool ok() const noexcept { return !m_failed; }
    [[nodiscard]] size_t pos() const noexcept { return m_pos; }
    [[nodiscard]] size_t size() const noexcept { return m_data.size(); }
    [[nodiscard]] size_t remaining() const noexcept { return m_failed ? 0 : m_data.size() - m_pos; }
    [[nodiscard]] bool at_end() const noexcept { return remaining() == 0; }
    [[nodiscard]] std::span<const uint8_t> rest() const noexcept {
        return m_failed ? std::span<const uint8_t>{} : m_data.subspan(m_pos);
    }
    [[nodiscard]] std::span<const uint8_t> data() const noexcept { return m_data; }

private:
    /// Fails unless `n` more bytes remain.
    bool need(size_t n) noexcept;

    std::span<const uint8_t> m_data;
    size_t m_pos{0};
    bool m_failed{false};
};

} // namespace aion2::proto
