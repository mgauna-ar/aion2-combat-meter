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
    /// The switch's low four bits: 4..7.
    uint8_t layout{0};
    bool crit{false};
    /// hit_modifier bits.
    uint8_t modifiers{0};
    bool back_attack{false};
    /// The main hit.
    uint64_t amount{0};
    /// The extra hits a multi-hit carries.
    std::vector<uint64_t> extra_hits;

    /// The main hit and every extra one.
    [[nodiscard]] uint64_t total() const noexcept {
        uint64_t t = amount;
        for (const uint64_t h : extra_hits) t += h;
        return t;
    }
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

/// 00 8D, whose layout is not known yet: the bytes, for --unknown.
struct RemainingHp {
    std::vector<uint8_t> raw;
};

using Event = std::variant<Heartbeat, Identity, Spawn, Death, MapLoad, Damage, Tick, PartyRoster, RemainingHp>;

struct ParsedEvent {
    Micros ts{0};
    uint16_t opcode{0};
    Event event;
};

} // namespace aion2::proto
