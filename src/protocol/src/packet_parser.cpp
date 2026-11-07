#include "protocol/packet_parser.hpp"

#include "protocol/entity_id.hpp"
#include "protocol/identity.hpp"
#include "protocol/opcodes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>

namespace aion2::proto {

namespace {

constexpr uint8_t kLayoutCastMarker = 0;
constexpr uint8_t kLayoutShort = 4;
constexpr uint8_t kLayoutLongest = 7;
/// The switch's layout bits. Bit 0x08 is a flag over them (a u32 follows the
/// damage), so 0x0C is layout 4.
constexpr uint64_t kSwitchLayout = 0x07;
constexpr uint64_t kSwitchExtraHits = 0x20;
/// The block after the damage type, by layout 4..7. Layout 4's is a hit id (u32)
/// and a count (u32); the longer ones put the modifiers, a byte and the direction
/// in front. Layout 6's was seen in a real capture, followed by the 00 pad; 5 and 7
/// are as the community's notes give them, unconfirmed.
constexpr std::array<size_t, 4> kBlockSize{8, 12, 10, 14};
constexpr uint64_t kDamageTypeCrit = 3;
/// The skill code of a tick is the skill id times this.
constexpr uint32_t kTickSkillScale = 100;
/// 05 38 effects with their own layout (read_tick).
constexpr uint8_t kTickNoAmount = 0x00;
constexpr uint8_t kTickHealOnHit = 0x30;
/// Positions are world coordinates: anything beyond this is not a position.
constexpr float kMaxCoordinate = 1.0e7f;
constexpr size_t kPartyMemberPrefix = 10;  // mask, slot, account u64
/// 00 8D kinds, and the stats that are current and max HP.
constexpr uint8_t kValuesSelf = 1;
constexpr uint8_t kValuesOther = 2;
/// The local player's, both lists in one: kind 1's u32 values, then a count and
/// kind 2's u64 values, which carry their own HP.
constexpr uint8_t kValuesBoth = 3;
constexpr uint8_t kStatHp = 0;
/// Max HP: at login and on each map, the local player's climbed 11,988, 17,421,
/// 21,320 and 25,373 as their gear's figures added up, and current HP never passed it.
constexpr uint8_t kStatMaxHp = 7;
/// 2a 38 / 2b 38's kind byte: 0x11, or 0x13 with the applying skill (a u32) after
/// the level. No other was seen in 13,887 records.
constexpr uint8_t kBuffKind = 0x11;
constexpr uint8_t kBuffHasSkill = 0x02;
/// A duration of all ones: the effect does not end.
constexpr uint64_t kBuffPermanent = ~uint64_t{0};
/// Most entries one 47 38 is believed to carry: every one seen had one.
constexpr uint8_t kMaxCooldownEntries = 16;
/// Most skills one 18 38 or 19 38 is believed to carry: all 874 seen had one.
constexpr uint8_t kMaxChainSkills = 4;
/// 02 38's mode byte: a press carries a position (2); a passive going off, a chain's
/// follow-up or a mantra's pulse does not (0). No other was seen in 28,550 records.
constexpr uint8_t kCastPressed = 2;
constexpr uint8_t kCastUnpressed = 0;

enum class Record : uint8_t { Damage, CastMarker, Failed };

Record read_damage_record(ByteReader& r, Damage& d) {
    d.target = normalize_entity_id(r.varint());
    const uint64_t sw = r.varint();
    if (!r.ok()) return Record::Failed;
    d.layout = static_cast<uint8_t>(sw & kSwitchLayout);
    if (d.layout == kLayoutCastMarker) return Record::CastMarker;
    if (d.layout < kLayoutShort || d.layout > kLayoutLongest) return Record::Failed;

    (void)r.varint();  // flag: meaning unknown
    d.actor = normalize_entity_id(r.varint());
    d.skill = r.u32le();
    (void)r.u8();  // hit uid
    d.crit = r.varint() == kDamageTypeCrit;
    const auto block = r.bytes(kBlockSize[d.layout - kLayoutShort]);
    if (!r.ok()) return Record::Failed;
    if (d.layout != kLayoutShort) {
        d.modifiers = block[0];
        d.direction = block[2];
    }

    if (!r.at_end() && r.rest()[0] == 0x00) r.skip(1);  // optional pad
    (void)r.varint();  // scalar
    d.amount = r.varint();

    if ((sw & kSwitchExtraHits) != 0) {
        const uint64_t count = r.varint();
        if (count > PacketParser::kMaxExtraHits) return Record::Failed;
        for (uint64_t i = 0; i < count && r.ok(); ++i) d.extra_hits.push_back(r.varint());
    } else if (d.layout == kLayoutShort) {
        (void)r.varint();  // tail
    }
    return r.ok() ? Record::Damage : Record::Failed;
}

/// 02 38: caster varint, a byte (0 in nearly all), skill u32, a sequence u8 (counting
/// up per caster), the mode u8, target varint, then a position (mode 2) or a shorter
/// tail (mode 0), neither read. An unknown mode fails, so a patch that moves it shows.
std::optional<SkillCast> read_cast(ByteReader& r) {
    SkillCast c;
    c.caster = normalize_entity_id(r.varint());
    (void)r.u8();  // meaning unknown: 0, or 8, 12, 32 on a few
    c.skill = r.u32le();
    (void)r.u8();  // sequence
    const uint8_t mode = r.u8();
    c.target = normalize_entity_id(r.varint());
    if (!r.ok() || c.caster == 0 || c.skill == 0) return std::nullopt;
    if (mode != kCastPressed && mode != kCastUnpressed) return std::nullopt;
    c.pressed = mode == kCastPressed;
    return c;
}

/// 05 38 into `out`, left empty for a record with nothing to count. False when the
/// body does not parse, or its effect is one not yet understood.
bool read_tick(ByteReader& r, std::optional<Tick>& out) {
    Tick t;
    t.target = normalize_entity_id(r.varint());
    t.effect = r.u8();
    t.actor = normalize_entity_id(r.varint());
    (void)r.varint();  // meaning unknown
    if (!r.ok()) return false;
    if (t.effect == kTickNoAmount) {
        // A record on the target itself, a boss's, with a u32 and no amount.
        (void)r.u32le();
        return r.ok();
    }
    const uint32_t skill = r.u32le();
    t.amount = r.varint();
    if (t.effect == kTickHealOnHit) {
        // The skill unscaled (2001 for every class), then the enemy skill whose hit
        // set it off. The game's Total Heal counts it: without it the local
        // player's healing came to half the game's figure.
        t.skill = skill;
        t.cause_skill = r.u32le();
        t.heal = true;
    } else {
        t.skill = skill / kTickSkillScale;
    }
    if (!r.ok()) return false;
    switch (t.effect) {
        case 0x02:
        case 0x0A:
            t.heal = false;
            break;
        case 0x01:
        case 0x09:
        case 0x0B:
            t.heal = true;
            break;
        case kTickHealOnHit:
            break;
        default:
            return false;
    }
    out = t;
    return true;
}

[[nodiscard]] bool plausible_coordinate(float v) noexcept { return std::isfinite(v) && std::fabs(v) < kMaxCoordinate; }

std::optional<Spawn> read_spawn(std::span<const uint8_t> body) {
    ByteReader r(body);
    Spawn s;
    s.id = normalize_entity_id(r.varint());
    s.mask = r.u24le();
    if (!r.ok() || s.id == 0) return std::nullopt;
    s.kind = static_cast<uint8_t>(s.mask & 0xFF);
    if ((s.mask & spawn_mask::AimedAtPlayer) != 0) {
        // The player's name before the code. Its bytes read as a code made every
        // such mechanic "NPC #<four bytes of a name>".
        const uint8_t len = r.u8();
        const auto name = r.bytes(len);
        if (!r.ok()) return std::nullopt;
        s.aimed_at.assign(name.begin(), name.end());
        if (!is_character_name(s.aimed_at)) return std::nullopt;
    }
    const size_t after_mask = r.pos();
    s.npc_code = r.u32le();
    if (!r.ok()) s.npc_code = 0;

    // Position and HP follow the marker 00 (00|40) 02, wherever the fields before
    // it (which vary by kind) end: x, y, z, a heading (f32), a u16, then a byte
    // that is 01, or 00 for a corpse coming into view (current HP 0), and the
    // current and max HP. The first marker followed by all of that, sane, is taken.
    // In the first capture it came straight after the npc code, its 00 being the
    // code's top byte, so the search starts at the code rather than after it.
    for (size_t at = after_mask; at + 3 <= body.size(); ++at) {
        if (body[at] != 0x00 || (body[at + 1] != 0x00 && body[at + 1] != 0x40) || body[at + 2] != 0x02) continue;
        ByteReader p(body.subspan(at + 3));
        const float x = p.f32le();
        const float y = p.f32le();
        const float z = p.f32le();
        (void)p.f32le();  // heading
        (void)p.u16le();
        const uint8_t alive = p.u8();
        const uint64_t cur = p.varint();
        const uint64_t max = p.varint();
        if (!p.ok() || alive > 0x01 || !plausible_coordinate(x) || !plausible_coordinate(y) ||
            !plausible_coordinate(z) || max == 0 || cur > max) {
            continue;
        }
        s.x = x;
        s.y = y;
        s.z = z;
        s.has_hp = true;
        s.cur_hp = cur;
        s.max_hp = max;
        break;
    }

    if (spawn_kind::carries_owner(s.kind)) {
        // The owner's position varies by class; it follows 07 02 06.
        for (size_t at = after_mask; at + 7 <= body.size(); ++at) {
            if (body[at] == 0x07 && body[at + 1] == 0x02 && body[at + 2] == 0x06) {
                ByteReader o(body.subspan(at + 3, 4));
                s.owner = normalize_entity_id(o.u32le());
                break;
            }
        }
    }
    return s;
}

/// 00 8D: id, a kind, a count, then that many (stat u8, value). The local player's
/// own values are kind 1 with u32 values; anyone else's are kind 2 with u64
/// values, of which stat 0 is current HP. Kind 3, the local player's again, is
/// kind 1's list followed by kind 2's, with its own count.
std::optional<RemainingHp> read_entity_values(ByteReader& r) {
    RemainingHp v;
    v.id = normalize_entity_id(r.varint());
    const uint8_t kind = r.u8();
    uint8_t count = r.u8();
    if (!r.ok() || v.id == 0 || count == 0 || count > PacketParser::kMaxEntityValues) return std::nullopt;
    if (kind != kValuesSelf && kind != kValuesOther && kind != kValuesBoth) return std::nullopt;
    v.self = kind != kValuesOther;
    if (v.self) {
        for (uint8_t i = 0; i < count; ++i) {
            (void)r.u8();  // stat
            (void)r.u32le();
        }
        if (kind == kValuesSelf) return r.ok() && r.at_end() ? std::optional<RemainingHp>(v) : std::nullopt;
        count = r.u8();
        if (!r.ok() || count > PacketParser::kMaxEntityValues) return std::nullopt;
    }
    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t stat = r.u8();
        const uint64_t value = r.u64le();
        if (stat == kStatHp) {
            v.has_hp = true;
            v.hp = value;
        } else if (stat == kStatMaxHp) {
            v.has_max_hp = true;
            v.max_hp = value;
        }
    }
    if (!r.ok() || !r.at_end()) return std::nullopt;
    return v;
}

/// 2a 38 and 2b 38: target, (2a 38 only: a byte, 01 in every record), kind,
/// instance varint, effect u32, duration u64 (ms, all ones for no end), expiry u64
/// (server ms), caster varint, level u8, (kind 0x13: skill u32), a byte (00 or 02),
/// and a position (three f32). The record must end there: a longer one is a layout
/// not seen yet.
std::optional<BuffApplied> read_buff(ByteReader& r, bool renewed) {
    BuffApplied b;
    b.renewed = renewed;
    b.target = normalize_entity_id(r.varint());
    if (!renewed) (void)r.u8();
    const uint8_t kind = r.u8();
    const uint64_t instance = r.varint();
    b.buff_id = r.u32le();
    const uint64_t duration = r.u64le();
    b.expires_server_ms = r.u64le();
    b.caster = normalize_entity_id(r.varint());
    b.level = r.u8();
    if ((kind & kBuffHasSkill) != 0) b.source_skill = r.u32le();
    (void)r.u8();
    const float x = r.f32le();
    const float y = r.f32le();
    const float z = r.f32le();
    if (!r.ok() || !r.at_end() || (kind & ~kBuffHasSkill) != kBuffKind || b.target == 0 ||
        instance > UINT32_MAX || !plausible_coordinate(x) || !plausible_coordinate(y) || !plausible_coordinate(z)) {
        return std::nullopt;
    }
    b.instance = static_cast<uint32_t>(instance);
    b.permanent = duration == kBuffPermanent;
    b.duration_ms = b.permanent ? 0 : duration;
    if (b.permanent) b.expires_server_ms = 0;
    return b;
}

std::optional<PartyRoster> read_party(std::span<const uint8_t> body) {
    ByteReader r(body);
    r.skip(4);  // party key
    const uint8_t name_len = r.u8();
    r.skip(name_len);
    r.skip(1 + 4 + 2 + 8 + 3);  // size, dungeon, 2 bytes, leader account, 3 bytes
    const uint64_t count = r.varint();
    if (!r.ok() || count > PacketParser::kMaxPartyMembers) return std::nullopt;

    // Each member's tail varies in length, so each name is found rather than
    // counted to: the next length-prefixed name at least a prefix past the last.
    PartyRoster roster;
    size_t cursor = r.pos();
    for (uint64_t m = 0; m < count; ++m) {
        bool found = false;
        for (size_t at = cursor + kPartyMemberPrefix; at < body.size(); ++at) {
            const uint8_t len = body[at];
            if (len == 0 || at + 1 + len > body.size()) continue;
            const std::string name(body.begin() + static_cast<std::ptrdiff_t>(at + 1),
                                   body.begin() + static_cast<std::ptrdiff_t>(at + 1 + len));
            if (character_name_chars(name) < kMinFoundNameChars) continue;
            roster.members.push_back(PartyMember{name});
            cursor = at + 1 + len;
            found = true;
            break;
        }
        if (!found) break;
    }
    if (roster.members.size() != count) return std::nullopt;
    return roster;
}

} // namespace

bool PacketParser::parse_damage(std::span<const uint8_t> body, std::vector<Damage>& out) {
    ByteReader r(body);
    for (int record = 0; record < kMaxDamageRecords; ++record) {
        Damage d;
        const Record result = read_damage_record(r, d);
        if (result == Record::Failed) return record > 0;
        if (result == Record::Damage) out.push_back(std::move(d));
        if (result == Record::CastMarker) return true;
        // Another record follows behind 01 00.
        const auto rest = r.rest();
        if (rest.size() < 2 || rest[0] != 0x01 || rest[1] != 0x00) return true;
        r.skip(2);
    }
    return true;
}

void PacketParser::remember_server(uint16_t server) {
    if (std::find(m_known_servers.begin(), m_known_servers.end(), server) == m_known_servers.end()) {
        m_known_servers.push_back(server);
    }
}

void PacketParser::parse(const Frame& frame, std::vector<ParsedEvent>& out) {
    ++m_stats.frames;
    switch (frame.opcode) {
        case opcode::Heartbeat:
        case opcode::OwnCharacter:
        case opcode::OtherPlayer:
        case opcode::Spawn:
        case opcode::Death:
        case opcode::MapLoad:
        case opcode::Damage:
        case opcode::Tick:
        case opcode::PartyRoster:
        case opcode::RemainingHp:
        case opcode::SkillUse:
        case opcode::CooldownSet:
        case opcode::ChainOffered:
        case opcode::ChainWithdrawn:
        case opcode::BuffAdd:
        case opcode::BuffRenew:
        case opcode::BuffRemove:
        case opcode::Cast:
            if (parse_known(frame, out)) {
                ++m_stats.decoded;
            } else {
                ++m_stats.failed;
                ++m_stats.failures_by_opcode[frame.opcode];
            }
            return;
        case opcode::Pong:
            return;  // read by nothing, and never carries identities
        default:
            break;
    }

    ++m_stats.unknown;
    for (Identity& id : scan_identities(frame.body, m_known_servers)) {
        ++m_stats.scanned_identities;
        out.push_back(ParsedEvent{frame.ts, frame.opcode, std::move(id), frame.connection});
    }
}

bool PacketParser::parse_known(const Frame& frame, std::vector<ParsedEvent>& out) {
    const auto emit = [&](Event e) {
        out.push_back(ParsedEvent{frame.ts, frame.opcode, std::move(e), frame.connection});
    };
    ByteReader r(frame.body);

    switch (frame.opcode) {
        case opcode::Heartbeat: {
            const uint64_t ms = r.u64le();
            if (!r.ok()) return false;
            emit(Heartbeat{ms});
            return true;
        }
        case opcode::OwnCharacter:
        case opcode::OtherPlayer: {
            const bool own = frame.opcode == opcode::OwnCharacter;
            auto id = read_identity_record(r, own);
            if (!id) return false;
            id->own = own;
            if (!id->name.empty()) remember_server(id->server);
            emit(std::move(*id));
            return true;
        }
        case opcode::Spawn: {
            auto spawn = read_spawn(frame.body);
            if (!spawn) return false;
            emit(*spawn);
            return true;
        }
        case opcode::Death: {
            const EntityId id = normalize_entity_id(r.varint());
            if (!r.ok() || id == 0) return false;
            emit(Death{id});
            return true;
        }
        case opcode::MapLoad: {
            MapLoad m;
            m.load_count = r.u32le();
            m.map_id = r.u32le();
            if (!r.ok()) return false;
            emit(m);
            return true;
        }
        case opcode::Damage: {
            std::vector<Damage> records;
            if (!parse_damage(frame.body, records)) return false;
            for (Damage& d : records) emit(std::move(d));
            return true;
        }
        case opcode::Tick: {
            std::optional<Tick> tick;
            if (!read_tick(r, tick)) return false;
            if (tick) emit(*tick);
            return true;
        }
        case opcode::PartyRoster: {
            auto roster = read_party(frame.body);
            if (!roster) return false;
            emit(std::move(*roster));
            return true;
        }
        case opcode::RemainingHp: {
            const auto values = read_entity_values(r);
            if (!values) return false;
            emit(*values);
            return true;
        }
        case opcode::Cast: {
            const auto cast = read_cast(r);
            if (!cast) return false;
            emit(*cast);
            return true;
        }
        case opcode::SkillUse: {
            // Three bytes (00 00 00 in 1,154 of 1,176), then the skill.
            r.skip(3);
            const uint32_t skill = r.u32le();
            if (!r.ok() || !r.at_end() || skill == 0) return false;
            emit(SkillUsed{skill});
            return true;
        }
        case opcode::CooldownSet: {
            const uint8_t count = r.u8();
            if (!r.ok() || count == 0 || count > kMaxCooldownEntries) return false;
            std::vector<CooldownSet> entries;
            for (uint8_t i = 0; i < count; ++i) {
                CooldownSet c;
                c.skill = r.u32le();
                const uint64_t remaining = r.varint();
                if (!r.ok() || remaining > UINT32_MAX) return false;
                c.remaining_ms = static_cast<uint32_t>(remaining);
                entries.push_back(c);
            }
            if (!r.at_end()) return false;
            for (const CooldownSet& c : entries) emit(c);
            return true;
        }
        case opcode::ChainOffered:
        case opcode::ChainWithdrawn: {
            // 18 38: the skill that set it off, then a count and the skills on offer.
            // 19 38: a count and the skills withdrawn.
            const bool offered = frame.opcode == opcode::ChainOffered;
            const uint32_t trigger = offered ? r.u32le() : 0;
            const uint8_t count = r.u8();
            if (!r.ok() || (offered && trigger == 0) || count == 0 || count > kMaxChainSkills) return false;
            std::vector<uint32_t> skills;
            for (uint8_t i = 0; i < count; ++i) skills.push_back(r.u32le());
            if (!r.ok() || !r.at_end()) return false;
            if (std::find(skills.begin(), skills.end(), 0u) != skills.end()) return false;
            for (const uint32_t skill : skills) {
                if (offered) {
                    emit(ChainOffered{trigger, skill});
                } else {
                    emit(ChainWithdrawn{skill});
                }
            }
            return true;
        }
        case opcode::BuffAdd:
        case opcode::BuffRenew: {
            const auto buff = read_buff(r, frame.opcode == opcode::BuffRenew);
            if (!buff) return false;
            emit(*buff);
            return true;
        }
        case opcode::BuffRemove: {
            BuffRemoved b;
            b.target = normalize_entity_id(r.varint());
            b.buff_id = r.u32le();
            if (!r.ok() || !r.at_end() || b.target == 0) return false;
            emit(b);
            return true;
        }
        default:
            return false;
    }
}

} // namespace aion2::proto
