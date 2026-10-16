#include "protocol/identity.hpp"

#include "protocol/entity_id.hpp"

#include <algorithm>

namespace aion2::proto {

namespace {

constexpr uint8_t kHasName = 0x01;
/// 16 code points of up to 3 UTF-8 bytes each.
constexpr size_t kMaxNameBytes = kMaxNameChars * 3;
constexpr size_t kMinScannedNameChars = 2;

[[nodiscard]] bool is_ascii_letter(uint8_t c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
[[nodiscard]] bool is_ascii_digit(uint8_t c) noexcept { return c >= '0' && c <= '9'; }

/// Code points in `utf8` if it is a valid name, else 0.
size_t name_chars(std::string_view utf8) noexcept {
    if (utf8.empty() || utf8.front() == '$') return 0;
    size_t chars = 0;
    bool has_letter = false;
    for (size_t i = 0; i < utf8.size();) {
        const auto c = static_cast<uint8_t>(utf8[i]);
        size_t len = 0;
        if (c < 0x80) {
            if (is_ascii_letter(c)) has_letter = true;
            else if (!is_ascii_digit(c)) return 0;
            len = 1;
        } else if ((c & 0xE0) == 0xC0 && c >= 0xC2) {
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
        } else if ((c & 0xF8) == 0xF0 && c <= 0xF4) {
            len = 4;
        } else {
            return 0;
        }
        if (i + len > utf8.size()) return 0;
        for (size_t k = 1; k < len; ++k) {
            if ((static_cast<uint8_t>(utf8[i + k]) & 0xC0) != 0x80) return 0;
        }
        if (len > 1) has_letter = true;
        i += len;
        if (++chars > kMaxNameChars) return 0;
    }
    return has_letter ? chars : 0;
}

} // namespace

bool is_character_name(std::string_view utf8) noexcept { return name_chars(utf8) != 0; }

std::optional<Identity> read_identity_record(ByteReader& r) {
    Identity id;
    const uint64_t raw_id = r.varint();
    r.skip(4);  // mask
    const uint8_t flags = r.u8();
    if (!r.ok() || raw_id == 0) return std::nullopt;
    id.id = normalize_entity_id(raw_id);
    if ((flags & kHasName) == 0) return id;

    const uint8_t len = r.u8();
    const auto name = r.bytes(len);
    if (!r.ok() || len == 0 || len > kMaxNameBytes) return std::nullopt;
    id.name.assign(name.begin(), name.end());
    if (!is_character_name(id.name)) return std::nullopt;
    id.server = r.u16le();
    id.class_code = r.u32le();
    if (!r.ok()) return std::nullopt;
    return id;
}

std::vector<Identity> scan_identities(std::span<const uint8_t> bytes, std::span<const uint16_t> known_servers) {
    std::vector<Identity> found;
    if (known_servers.empty()) return found;
    for (size_t start = 0; start < bytes.size();) {
        ByteReader r(bytes.subspan(start));
        auto id = read_identity_record(r);
        if (id && !id->name.empty() && name_chars(id->name) >= kMinScannedNameChars &&
            std::find(known_servers.begin(), known_servers.end(), id->server) != known_servers.end()) {
            id->scanned = true;
            found.push_back(std::move(*id));
            start += r.pos();
        } else {
            ++start;
        }
    }
    return found;
}

} // namespace aion2::proto
