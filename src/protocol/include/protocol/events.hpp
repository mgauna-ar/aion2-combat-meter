#pragma once

#include "common/micros.hpp"
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace aion2::proto {

/// An entity in combat: a player, an NPC, a summon. Always normalized
/// (normalize_entity_id) before it leaves the parser.
using EntityId = uint32_t;

/// The server clock, about twenty times a second.
struct Heartbeat {
    uint64_t server_ms{0};
};

/// A player's name and class: the local character (`own`), another player, or one
/// found inside some other packet by scan_identities.
struct Identity {
    EntityId id{0};
    std::string name;
    uint16_t server{0};
    /// The wire's class code; meter::class_from_code names it.
    uint32_t class_code{0};
    bool own{false};
    /// Found by scanning an unknown packet rather than read from 33 36 / 45 36.
    bool scanned{false};
};

/// Something appeared: an NPC, a summon, a skill effect.
struct Spawn {
    EntityId id{0};
    /// The mask's first byte: what kind of thing it is (spawn_kind).
    uint8_t kind{0};
    uint32_t mask{0};
    uint32_t npc_code{0};
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    bool has_hp{false};
    uint64_t cur_hp{0};
    uint64_t max_hp{0};
    /// The player that summoned it, for the kinds that carry one; 0 otherwise.
    EntityId owner{0};
};

namespace spawn_kind {
inline constexpr uint8_t Summon = 0x1F;
inline constexpr uint8_t SummonAlt = 0x5F;
inline constexpr uint8_t Spirit = 0x1D;
inline constexpr uint8_t Effect = 0x5D;
/// Boss mechanics: their inline name is the targeted player, not an owner.
inline constexpr uint8_t Mechanic = 0x1C;

/// Summons, spirits, pets and skill effects: things a player's damage comes from.
[[nodiscard]] constexpr bool is_player_object(uint8_t kind) noexcept {
    return kind == Summon || kind == SummonAlt || kind == Spirit || kind == Effect;
}
[[nodiscard]] constexpr bool carries_owner(uint8_t kind) noexcept { return kind == Summon || kind == SummonAlt; }
} // namespace spawn_kind

/// 42 36. For a mob it comes about 4 s after its HP (00 8D) reaches 0: the corpse
/// going, rather than the moment it died.
struct Death {
    EntityId id{0};
};

struct MapLoad {
    uint32_t load_count{0};
    uint32_t map_id{0};

    /// Instanced dungeons and arenas.
    [[nodiscard]] bool is_instance() const noexcept { return map_id >= 600000 && map_id < 700000; }
};

namespace hit_modifier {
inline constexpr uint8_t Parry = 0x02;
inline constexpr uint8_t Perfect = 0x04;
inline constexpr uint8_t Double = 0x08;
inline constexpr uint8_t Heavy = 0x20;
} // namespace hit_modifier

/// One damage record. A 04 38 packet may hold several.
struct Damage {
    EntityId target{0};
    EntityId actor{0};
    uint32_t skill{0};
    /// The switch's low three bits: 4..7.
    uint8_t layout{0};
    bool crit{false};
    /// hit_modifier bits. Only the long layouts (5..7) carry them.
    uint8_t modifiers{0};
    bool back_attack{false};
    /// The hit: all of it. A real capture's HP updates (00 8D) fall by this alone.
    uint64_t amount{0};
    /// The values behind switch bit 0x20. Not damage: the target's HP does not fall
    /// by them. Kept for --events while their meaning is unknown.
    std::vector<uint64_t> extra_hits;
};

/// A damage-over-time or heal-over-time tick.
struct Tick {
    EntityId target{0};
    EntityId actor{0};
    uint32_t skill{0};
    uint8_t effect{0};
    bool heal{false};
    uint64_t amount{0};
};

struct PartyMember {
    std::string name;
};

/// The party list. Members carry no entity id, so they are matched to players by name.
struct PartyRoster {
    std::vector<PartyMember> members;
};

/// 00 8D: an entity's current values, sent when they change.
struct RemainingHp {
    EntityId id{0};
    /// The local player's own values (resources, by stat codes not yet known). No
    /// one else is sent these, so they name the local player when 33 36 was missed.
    bool self{false};
    /// Anyone else's current HP, exact: an NPC's falls with each hit and reaches 0
    /// when it dies.
    bool has_hp{false};
    uint64_t hp{0};
};

using Event = std::variant<Heartbeat, Identity, Spawn, Death, MapLoad, Damage, Tick, PartyRoster, RemainingHp>;

struct ParsedEvent {
    Micros ts{0};
    uint16_t opcode{0};
    Event event;
};

} // namespace aion2::proto
