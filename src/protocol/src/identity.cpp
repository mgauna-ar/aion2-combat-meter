#include "protocol/identity.hpp"

#include "protocol/entity_id.hpp"

#include <algorithm>

namespace aion2::proto {

namespace {

constexpr uint8_t kHasName = 0x01;
/// 16 code points of up to 3 UTF-8 bytes each.
constexpr size_t kMaxNameBytes = kMaxNameChars * 3;

[[nodiscard]] bool is_ascii_letter(uint8_t c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
[[nodiscard]] bool is_ascii_digit(uint8_t c) noexcept { return c >= '0' && c <= '9'; }

/// Whether a multi-byte sequence starting `lead, second` is one a UTF-8 decoder
/// accepts: not an overlong form, a UTF-16 surrogate, past U+10FFFF or a C1 control.
[[nodiscard]] bool valid_lead_pair(uint8_t lead, uint8_t second) noexcept {
    switch (lead) {
        case 0xC2: return second >= 0xA0;  // U+0080..U+009F are C1 controls
        case 0xE0: return second >= 0xA0;
        case 0xED: return second < 0xA0;
        case 0xF0: return second >= 0x90;
        case 0xF4: return second < 0x90;
        default: return true;
    }
}

} // namespace

size_t character_name_chars(std::string_view utf8) noexcept {
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
        if (len > 1 && !valid_lead_pair(c, static_cast<uint8_t>(utf8[i + 1]))) return 0;
        if (len > 1) has_letter = true;
        i += len;
        if (++chars > kMaxNameChars) return 0;
    }
    return has_letter ? chars : 0;
}

bool is_character_name(std::string_view utf8) noexcept { return character_name_chars(utf8) != 0; }

std::optional<Identity> read_identity_record(ByteReader& r, bool own) {
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
    if (own) r.skip(2);
    id.server = r.u16le();
    id.class_code = r.u32le();
    if (!r.ok()) return std::nullopt;
    const auto faction = static_cast<uint8_t>((id.class_code >> 16) & 0xFF);
    if (faction == 1 || faction == 2) id.faction = faction;
    return id;
}

namespace {

/// The three bytes every 45 36 of the arena capture had 12 bytes before its combat
/// power, after a first byte of 21, 24 or 26: a u32 of unknown meaning.
constexpr uint8_t kPowerTag[] = {0xF4, 0xCB, 0x1F};
/// How far from the end the tag is looked for: it sat 20 to 21 bytes from it.
constexpr size_t kPowerTagWindow = 40;
/// The 00 bytes after the combat power.
constexpr size_t kPowerZeros = 5;

/// The combat power at `at`: a u32 no player's is above, then kPowerZeros 00 bytes.
[[nodiscard]] uint32_t combat_power_at(std::span<const uint8_t> body, size_t at) noexcept {
    if (at + sizeof(uint32_t) + kPowerZeros > body.size()) return 0;
    const auto field = body.subspan(at, sizeof(uint32_t) + kPowerZeros);
    if (!std::all_of(field.begin() + sizeof(uint32_t), field.end(), [](uint8_t b) { return b == 0; })) return 0;
    const uint32_t power = uint32_t{field[0]} | (uint32_t{field[1]} << 8) | (uint32_t{field[2]} << 16) |
                           (uint32_t{field[3]} << 24);
    return power <= kMaxCombatPower ? power : 0;
}

} // namespace

uint32_t read_combat_power(std::span<const uint8_t> body) noexcept {
    if (body.size() < kMinCombatPowerBody) return 0;
    // The tag's u32, then a title and a rank (u32 each), then the figure. From the
    // tag, since the record's end varies: 11 of the capture's 77 had a byte more
    // after the zeros (00 or 01), and a small figure's own high bytes are zeros too.
    const auto window = body.last(kPowerTagWindow);
    const auto tag = std::find_end(window.begin(), window.end(), std::begin(kPowerTag), std::end(kPowerTag));
    if (tag != window.end()) {
        const size_t after_tag = body.size() - static_cast<size_t>(window.end() - tag) + std::size(kPowerTag);
        constexpr size_t kTitleAndRank = 2 * sizeof(uint32_t);
        if (const uint32_t power = combat_power_at(body, after_tag + kTitleAndRank)) return power;
        // Two had one byte more (01) between the rank and the figure.
        if (const uint32_t power = combat_power_at(body, after_tag + kTitleAndRank + 1)) return power;
        // A level-11 character's had neither title nor rank, and ended there.
        if (after_tag + sizeof(uint32_t) + kPowerZeros == body.size()) return combat_power_at(body, after_tag);
        return 0;
    }
    // No tag (another patch's, perhaps): from the end, past a last byte that is not
    // a zero.
    if (body.back() != 0) body = body.first(body.size() - 1);
    return combat_power_at(body, body.size() - sizeof(uint32_t) - kPowerZeros);
}

std::vector<Identity> scan_identities(std::span<const uint8_t> bytes, std::span<const uint16_t> known_servers) {
    std::vector<Identity> found;
    if (known_servers.empty()) return found;
    for (size_t start = 0; start < bytes.size();) {
        ByteReader r(bytes.subspan(start));
        auto id = read_identity_record(r);
        if (id && !id->name.empty() && character_name_chars(id->name) >= kMinFoundNameChars &&
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
