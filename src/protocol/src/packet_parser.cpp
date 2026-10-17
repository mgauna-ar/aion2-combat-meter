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
/// are as NOIA2, a maintained public meter, reads them.
constexpr std::array<size_t, 4> kBlockSize{8, 12, 10, 14};
constexpr uint64_t kDamageTypeCrit = 3;
/// The skill code of a tick is the skill id times this.
constexpr uint32_t kTickSkillScale = 100;
/// Positions are world coordinates: anything beyond this is not a position.
constexpr float kMaxCoordinate = 1.0e7f;
constexpr size_t kPartyMemberPrefix = 10;  // mask, slot, account u64
/// 00 8D kinds, and the stat that is current HP.
constexpr uint8_t kValuesSelf = 1;
constexpr uint8_t kValuesOther = 2;
constexpr uint8_t kStatHp = 0;

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

std::optional<Tick> read_tick(ByteReader& r) {
    Tick t;
    t.target = normalize_entity_id(r.varint());
    t.effect = r.u8();
    t.actor = normalize_entity_id(r.varint());
    (void)r.varint();  // meaning unknown
    t.skill = r.u32le() / kTickSkillScale;
    t.amount = r.varint();
    if (!r.ok()) return std::nullopt;
    switch (t.effect) {
        case 0x02:
        case 0x0A:
            t.heal = false;
            return t;
        case 0x01:
        case 0x09:
        case 0x0B:
            t.heal = true;
            return t;
        default:
            return std::nullopt;
    }
}

[[nodiscard]] bool plausible_coordinate(float v) noexcept { return std::isfinite(v) && std::fabs(v) < kMaxCoordinate; }

std::optional<Spawn> read_spawn(std::span<const uint8_t> body) {
    ByteReader r(body);
    Spawn s;
    s.id = normalize_entity_id(r.varint());
    s.mask = r.u24le();
    if (!r.ok() || s.id == 0) return std::nullopt;
    s.kind = static_cast<uint8_t>(s.mask & 0xFF);
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
/// values, of which stat 0 is current HP.
std::optional<RemainingHp> read_entity_values(ByteReader& r) {
    RemainingHp v;
    v.id = normalize_entity_id(r.varint());
    const uint8_t kind = r.u8();
    const uint8_t count = r.u8();
    if (!r.ok() || v.id == 0 || count == 0 || count > PacketParser::kMaxEntityValues) return std::nullopt;
    if (kind != kValuesSelf && kind != kValuesOther) return std::nullopt;
    v.self = kind == kValuesSelf;
    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t stat = r.u8();
        const uint64_t value = v.self ? r.u32le() : r.u64le();
        if (!v.self && stat == kStatHp) {
            v.has_hp = true;
            v.hp = value;
        }
    }
    if (!r.ok() || !r.at_end()) return std::nullopt;
    return v;
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
            if (parse_known(frame, out)) {
                ++m_stats.decoded;
            } else {
                ++m_stats.failed;
                ++m_stats.failures_by_opcode[frame.opcode];
            }
            return;
        case opcode::Pong:
        case opcode::Cast:
            return;  // read by nothing in v1, and never carry identities
        default:
            break;
    }

    ++m_stats.unknown;
    for (Identity& id : scan_identities(frame.body, m_known_servers)) {
        ++m_stats.scanned_identities;
        out.push_back(ParsedEvent{frame.ts, frame.opcode, std::move(id)});
    }
}

bool PacketParser::parse_known(const Frame& frame, std::vector<ParsedEvent>& out) {
    const auto emit = [&](Event e) { out.push_back(ParsedEvent{frame.ts, frame.opcode, std::move(e)}); };
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
            auto id = read_identity_record(r);
            if (!id) return false;
            id->own = frame.opcode == opcode::OwnCharacter;
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
            const auto tick = read_tick(r);
            if (!tick) return false;
            emit(*tick);
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
        default:
            return false;
    }
}

} // namespace aion2::proto
