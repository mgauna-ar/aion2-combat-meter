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
    /// The three bytes after the id.
    uint32_t mask{0};
    /// What it is: a monster's or NPC's id, as src/meter/data/npcs.tsv lists them
    /// (seven digits). A summon's code is not listed.
    uint32_t npc_code{0};
    /// The player a boss mechanic is aimed at, by name, when the mask's third byte
    /// has bit 0 set (spawn_mask::AimedAtPlayer): a trap around them that the party
    /// breaks, 1 damage a hit. Empty for anything else.
    std::string aimed_at;
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    bool has_hp{false};
    uint64_t cur_hp{0};
    uint64_t max_hp{0};
    /// The player that summoned it, for the kinds that carry one; 0 otherwise.
    EntityId owner{0};
};

namespace spawn_mask {
/// In the mask's third byte: a name (u8 length, UTF-8) comes before the npc code.
inline constexpr uint32_t AimedAtPlayer = 0x010000;
} // namespace spawn_mask

namespace spawn_kind {
inline constexpr uint8_t Summon = 0x1F;
inline constexpr uint8_t SummonAlt = 0x5F;
inline constexpr uint8_t Spirit = 0x1D;
inline constexpr uint8_t Effect = 0x5D;
/// Boss mechanics. Some are aimed at a player, and name them (Spawn::aimed_at).
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

/// Where a long-layout hit (5..7) landed relative to its target.
namespace hit_direction {
inline constexpr uint8_t None = 0;
inline constexpr uint8_t Back = 1;
inline constexpr uint8_t Front = 2;
} // namespace hit_direction

namespace hit_modifier {
/// The target blocked it. Read as a parry from the community's notes until the
/// second capture: its count matched the game's BLOC (11 of 862 hits).
inline constexpr uint8_t Block = 0x02;
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
    /// hit_direction, as the wire has it. Layout 4 carries none.
    uint8_t direction{hit_direction::None};
    /// The hit: all of it. A real capture's HP updates (00 8D) fall by this alone.
    uint64_t amount{0};
    /// The values behind switch bit 0x20: a multi-hit's further strikes. A record
    /// with them is one the game's MULT counts (228 of 862 in the second capture,
    /// matching per skill); neither the game's damage nor the target's HP adds them.
    std::vector<uint64_t> extra_hits;

    [[nodiscard]] bool multi_hit() const noexcept { return !extra_hits.empty(); }

    [[nodiscard]] bool back() const noexcept { return direction == hit_direction::Back; }
    [[nodiscard]] bool front() const noexcept { return direction == hit_direction::Front; }
};

/// A damage-over-time or heal-over-time tick.
struct Tick {
    EntityId target{0};
    EntityId actor{0};
    uint32_t skill{0};
    uint8_t effect{0};
    bool heal{false};
    uint64_t amount{0};
    /// Effect 0x30's: the skill of the hit that set it off, an enemy's on the player.
    uint32_t cause_skill{0};
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
    /// Current HP, exact: an NPC's falls with each hit and reaches 0 when it dies.
    /// Anyone else's, and the local player's own in a kind 3 record.
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
