#include "protocol/identity.hpp"

#include "protocol/entity_id.hpp"

#include <algorithm>
#include <cstdint>

namespace aion2::proto {

namespace {

constexpr uint8_t kHasName = 0x01;
/// In another player's record (45 36): their side follows the class. Set in every
/// record of the two Abyss captures (22 and 67), in none of the 278 of a town, the
/// arena or a dungeon.
constexpr uint8_t kSideFollows = 0x10;
/// In the local player's own record (33 36): their side is in the list at
/// kOwnSideListAt. 0x3F in the Abyss's five, 0x37 in the ten of other maps.
constexpr uint8_t kOwnSideListed = 0x08;
/// Where that list starts after the class, in every own record of the captures:
/// `05 <side> 03 01 7f` with a side, `05 03 01 7f` (7e in Ludra's) without.
constexpr size_t kOwnSideListAt = 133;
constexpr uint8_t kOwnSideListTag = 0x05;
/// 16 code points of up to 3 UTF-8 bytes each.
constexpr size_t kMaxNameBytes = kMaxNameChars * 3;

[[nodiscard]] bool is_side(uint8_t b) noexcept { return b == 1 || b == 2; }

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
    id.mask = r.u32le();
    const uint8_t flags = r.u8();
    if (!r.ok() || raw_id == 0) return std::nullopt;
    id.id = normalize_entity_id(raw_id);
    id.flags = flags;
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
    // The side is looked at, not read past: the position after it is found where
    // the side may or may not be (read_identity_position).
    const std::span<const uint8_t> after = r.rest();
    if (!own && (flags & kSideFollows) != 0 && !after.empty() && is_side(after[0])) id.side = after[0];
    if (own && (flags & kOwnSideListed) != 0 && after.size() > kOwnSideListAt + 1 &&
        after[kOwnSideListAt] == kOwnSideListTag && is_side(after[kOwnSideListAt + 1])) {
        id.side = after[kOwnSideListAt + 1];
    }
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
/// The most the byte some records have between the rank and the figure is taken to
/// be: of the six such records in the three captures, four had 01 and two 03.
constexpr uint8_t kMaxInsertedByte = 3;

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
        const uint32_t plain = combat_power_at(body, after_tag + kTitleAndRank);
        // Six had one byte more (01 or 03) between the rank and the figure.
        const uint32_t inserted = combat_power_at(body, after_tag + kTitleAndRank + 1);
        if (plain != 0 && inserted != 0) {
            // Both read when the record ends in a 00 more after the zeros, and when
            // that byte comes before a figure under 39,063: read from the byte, the
            // figure comes out 256 times itself plus the byte (4,756 read 1,217,537).
            // The bytes look alike then. A figure's low byte is anything, so one of 1
            // to 3 there is the byte far more often than a figure ending in it.
            return body[after_tag + kTitleAndRank] <= kMaxInsertedByte ? inserted : plain;
        }
        if (plain != 0) return plain;
        if (inserted != 0) return inserted;
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
            // What follows a look-alike's name is no record's.
            id->faction = 0;
            id->side = 0;
            found.push_back(std::move(*id));
            start += r.pos();
        } else {
            ++start;
        }
    }
    return found;
}

namespace {

/// A player record's list of running effects (read_listed_effects): its entries'
/// kind byte is 0x10 to 0x13, bit 0 naming a caster and bit 1 the applying skill.
constexpr uint8_t kListedKindMin = 0x10;
constexpr uint8_t kListedKindMax = 0x13;
constexpr uint8_t kListedHasCaster = 0x01;
constexpr uint8_t kListedHasSkill = 0x02;
/// Most entries a list is believed to hold: what the effect books keep on one
/// entity. The captures' longest held 19.
constexpr uint8_t kMaxListedEffects = 128;
/// A listed duration past 100 days is no effect's (Effect 20100's 34 days was the
/// longest timed one).
constexpr uint64_t kMaxListedDurationMs = 100ULL * 24 * 3600 * 1000;
/// A listed end, in Unix ms: 2017 to 2103. A permanent effect's is 2100-01-01.
constexpr uint64_t kMinListedEndMs = 1'500'000'000'000ULL;
constexpr uint64_t kMaxListedEndMs = 4'200'000'000'000ULL;
/// A duration of all ones: the effect does not end (as 2a 38's).
constexpr uint64_t kListedPermanent = ~uint64_t{0};
/// What every listed entry ends with: a position, three f32 (all FLT_MAX in the
/// local player's own items'), and before it, in some entries, one byte more.
constexpr size_t kListedPositionBytes = 12;

/// A listed entry's fixed head: the kind, instance, effect, duration and end.
struct ListedHead {
    uint8_t kind{0};
    uint32_t instance{0};
    uint32_t buff_id{0};
    uint64_t duration{0};
    uint64_t end{0};
    /// Where the fields after the end start.
    size_t next{0};
};

std::optional<ListedHead> read_listed_head(std::span<const uint8_t> body, size_t at) {
    if (at >= body.size()) return std::nullopt;
    ByteReader r(body);
    r.seek(at);
    ListedHead h;
    h.kind = r.u8();
    const uint64_t instance = r.varint();
    h.buff_id = r.u32le();
    h.duration = r.u64le();
    h.end = r.u64le();
    if (!r.ok() || h.kind < kListedKindMin || h.kind > kListedKindMax || instance > UINT32_MAX || h.buff_id == 0 ||
        (h.duration != kListedPermanent && h.duration > kMaxListedDurationMs) || h.end < kMinListedEndMs ||
        h.end > kMaxListedEndMs) {
        return std::nullopt;
    }
    h.instance = static_cast<uint32_t>(instance);
    h.next = r.pos();
    return h;
}

/// The list that starts with its count at `at`, every entry of it, or nothing.
std::optional<std::vector<BuffApplied>> read_listed_at(std::span<const uint8_t> body, size_t at, EntityId target) {
    const uint8_t count = body[at];
    if (count == 0 || count > kMaxListedEffects) return std::nullopt;
    std::vector<BuffApplied> out;
    size_t cursor = at + 1;
    for (uint8_t k = 0; k < count; ++k) {
        const auto head = read_listed_head(body, cursor);
        if (!head) return std::nullopt;
        ByteReader r(body);
        r.seek(head->next);
        BuffApplied b;
        b.listed = true;
        b.target = target;
        b.instance = head->instance;
        b.buff_id = head->buff_id;
        if ((head->kind & kListedHasCaster) != 0) b.caster = normalize_entity_id(r.varint());
        b.level = r.u8();
        if ((head->kind & kListedHasSkill) != 0) b.source_skill = r.u32le();
        if (!r.ok()) return std::nullopt;
        b.permanent = head->duration == kListedPermanent;
        b.duration_ms = b.permanent ? 0 : head->duration;
        b.expires_server_ms = b.permanent ? 0 : head->end;
        out.push_back(b);
        // The position, after one byte more in some entries (00, 70, 80, C0, never
        // a kind byte): the next entry says which.
        const size_t plain = r.pos() + kListedPositionBytes;
        if (k + 1 == count) {
            if (plain > body.size()) return std::nullopt;
        } else if (read_listed_head(body, plain)) {
            cursor = plain;
        } else if (read_listed_head(body, plain + 1)) {
            cursor = plain + 1;
        } else {
            return std::nullopt;
        }
    }
    return out;
}

} // namespace

/// Where the list sits depends on the name, the legion's name and fields not read (104 to
/// 159 bytes into the 45 36 records of the captures, 329 into the 33 36), so it is
/// found: the first count whose every entry reads, each where the last one ends.
/// In all 338 records of the four captures it was found once, its count right.
std::vector<BuffApplied> read_listed_effects(std::span<const uint8_t> body, size_t from, EntityId target) {
    for (size_t at = from; at + 1 < body.size(); ++at) {
        if (!read_listed_head(body, at + 1)) continue;
        if (auto list = read_listed_at(body, at, target)) return std::move(*list);
    }
    return {};
}

} // namespace aion2::proto
