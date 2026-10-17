#pragma once

// Events for the meter tests, built directly rather than through the wire: the
// parser has its own tests, and these are about fights.

#include "common/micros.hpp"
#include "protocol/events.hpp"

#include <string>
#include <utility>

namespace aion2::test::meter_support {

using proto::EntityId;

/// 2026-10-16 12:00:00 UTC.
inline constexpr Micros kT0 = 1'792'152'000'000'000ULL;

[[nodiscard]] inline Micros at_s(double seconds) { return kT0 + from_seconds(seconds); }

[[nodiscard]] inline proto::ParsedEvent ev(Micros ts, proto::Event e) {
    return proto::ParsedEvent{ts, 0, std::move(e)};
}

[[nodiscard]] inline proto::Identity player(EntityId id, std::string name, bool own = false,
                                            uint16_t server = 0x0123) {
    proto::Identity i;
    i.id = id;
    i.name = std::move(name);
    i.server = server;
    i.own = own;
    return i;
}

[[nodiscard]] inline proto::Spawn npc(EntityId id, uint64_t max_hp, uint32_t code = 500, uint64_t cur_hp = 0) {
    proto::Spawn s;
    s.id = id;
    s.kind = 0x10;
    s.npc_code = code;
    s.has_hp = true;
    s.max_hp = max_hp;
    s.cur_hp = cur_hp == 0 ? max_hp : cur_hp;
    return s;
}

[[nodiscard]] inline proto::Spawn summon(EntityId id, EntityId owner) {
    proto::Spawn s;
    s.id = id;
    s.kind = proto::spawn_kind::Summon;
    s.owner = owner;
    return s;
}

[[nodiscard]] inline proto::Damage hit(EntityId actor, EntityId target, uint64_t amount, bool crit = false,
                                       bool back = false) {
    proto::Damage d;
    d.actor = actor;
    d.target = target;
    d.layout = 4;
    d.amount = amount;
    d.crit = crit;
    d.direction = back ? proto::hit_direction::Back : proto::hit_direction::None;
    return d;
}

/// `d` with a skill code, and the long layout's modifiers and direction.
[[nodiscard]] inline proto::Damage skilled(proto::Damage d, uint32_t skill, uint8_t direction = 0,
                                           uint8_t modifiers = 0) {
    d.skill = skill;
    if (direction != 0 || modifiers != 0) d.layout = 6;
    if (direction != 0) d.direction = direction;
    d.modifiers = modifiers;
    return d;
}

[[nodiscard]] inline proto::Tick heal_tick(EntityId actor, EntityId target, uint64_t amount, uint32_t skill = 0) {
    proto::Tick t;
    t.actor = actor;
    t.target = target;
    t.effect = 0x09;
    t.heal = true;
    t.skill = skill;
    t.amount = amount;
    return t;
}

[[nodiscard]] inline proto::Tick dot(EntityId actor, EntityId target, uint64_t amount) {
    proto::Tick t;
    t.actor = actor;
    t.target = target;
    t.effect = 0x02;
    t.amount = amount;
    return t;
}

/// 00 8D with someone's current HP.
[[nodiscard]] inline proto::RemainingHp hp(EntityId id, uint64_t value) {
    proto::RemainingHp v;
    v.id = id;
    v.has_hp = true;
    v.hp = value;
    return v;
}

/// 00 8D with the local player's own values.
[[nodiscard]] inline proto::RemainingHp self_values(EntityId id) {
    proto::RemainingHp v;
    v.id = id;
    v.self = true;
    return v;
}

[[nodiscard]] inline proto::PartyRoster roster(std::initializer_list<const char*> names) {
    proto::PartyRoster r;
    for (const char* n : names) r.members.push_back(proto::PartyMember{n});
    return r;
}

} // namespace aion2::test::meter_support
