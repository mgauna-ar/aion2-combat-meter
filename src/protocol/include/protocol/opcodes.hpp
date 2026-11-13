#pragma once

#include <cstdint>
#include <string_view>

namespace aion2::proto {

/// Server-to-client opcodes, written as the two bytes appear on the wire (first
/// byte high): damage arrives as `04 38`, so it is 0x0438.
///
/// The one place opcodes are spelled. A game patch that renumbers them changes
/// this file and nothing else; `aion2meter-replay --opcodes` on a fresh capture
/// shows which moved (.claude/skills/after-game-patch).
namespace opcode {
inline constexpr uint16_t Heartbeat = 0x0036;
inline constexpr uint16_t Pong = 0x0336;
inline constexpr uint16_t OwnCharacter = 0x3336;
inline constexpr uint16_t OtherPlayer = 0x4536;
inline constexpr uint16_t Spawn = 0x4136;
inline constexpr uint16_t Death = 0x4236;
/// Someone left the local player's view: walked off, died and was taken away, or
/// left the map. Coming back sends their record again.
inline constexpr uint16_t LeftView = 0x4736;
inline constexpr uint16_t MapLoad = 0x2136;
inline constexpr uint16_t Cast = 0x0238;
inline constexpr uint16_t Damage = 0x0438;
inline constexpr uint16_t Tick = 0x0538;
/// The local player started a skill: what a cooldown runs from.
inline constexpr uint16_t SkillUse = 0x0138;
/// What is left of one of the local player's cooldowns, after a cut or a reset.
inline constexpr uint16_t CooldownSet = 0x4738;
/// A chain skill of the local player's on offer after the skill that sets it off,
/// and the offer withdrawn: used, or let run out (about 3 s).
inline constexpr uint16_t ChainOffered = 0x1838;
inline constexpr uint16_t ChainWithdrawn = 0x1938;
/// An effect put on an entity (a buff, a debuff, a mantra's pulse), and an
/// instance of one renewed.
inline constexpr uint16_t BuffAdd = 0x2A38;
inline constexpr uint16_t BuffRenew = 0x2B38;
/// One instance of an effect left its target.
inline constexpr uint16_t BuffRemove = 0x1A92;
/// Someone moved: an id and where they are now (packet_parser.cpp, read_position).
/// Six shapes of it, by what else they carry (a heading, a speed, a destination).
inline constexpr uint16_t MoveStop = 0x1A37;
inline constexpr uint16_t MoveStart = 0x1B37;
inline constexpr uint16_t MoveStep = 0x1C37;
inline constexpr uint16_t MovePath = 0x2937;
inline constexpr uint16_t MoveJump = 0x2E37;
inline constexpr uint16_t MoveAir = 0x2F37;
/// Players flying, by the map-20 capture: most of its 107 came from players
/// following the Flight Circles there, a point every tenth of a second. What it
/// says beyond the point is not known; the name is a guess.
inline constexpr uint16_t MoveFlight = 0x2837;
/// Players' movement of yet another shape (213 of the dungeon capture's 224); what
/// sets it apart from 1b 37 and 1c 37 is not known.
inline constexpr uint16_t MoveOther = 0x1E37;
/// A skill moved someone: a charge, a pull. The local player's own are sent too.
inline constexpr uint16_t MoveBySkill = 0x3C37;
/// Layout not known yet: kept raw for the replay tool's --unknown dump.
inline constexpr uint16_t RemainingHp = 0x008D;
inline constexpr uint16_t PartyRoster = 0x0297;
/// The two bytes that open an LZ4 bundle in place of an opcode.
inline constexpr uint16_t Bundle = 0xFFFF;
} // namespace opcode

/// A short name for the opcodes above, "?" for any other.
[[nodiscard]] constexpr std::string_view opcode_name(uint16_t op) noexcept {
    switch (op) {
        case opcode::Heartbeat: return "heartbeat";
        case opcode::Pong: return "pong";
        case opcode::OwnCharacter: return "own_character";
        case opcode::OtherPlayer: return "other_player";
        case opcode::Spawn: return "spawn";
        case opcode::Death: return "death";
        case opcode::LeftView: return "left_view";
        case opcode::MapLoad: return "map_load";
        case opcode::Cast: return "cast";
        case opcode::Damage: return "damage";
        case opcode::Tick: return "tick";
        case opcode::SkillUse: return "skill_use";
        case opcode::CooldownSet: return "cooldown";
        case opcode::ChainOffered: return "chain_offered";
        case opcode::ChainWithdrawn: return "chain_withdrawn";
        case opcode::MoveStop: return "move_stop";
        case opcode::MoveStart: return "move_start";
        case opcode::MoveStep: return "move_step";
        case opcode::MovePath: return "move_path";
        case opcode::MoveJump: return "move_jump";
        case opcode::MoveAir: return "move_air";
        case opcode::MoveFlight: return "move_flight";
        case opcode::MoveOther: return "move_other";
        case opcode::MoveBySkill: return "move_by_skill";
        case opcode::BuffAdd: return "buff_add";
        case opcode::BuffRenew: return "buff_renew";
        case opcode::BuffRemove: return "buff_remove";
        case opcode::RemainingHp: return "remaining_hp";
        case opcode::PartyRoster: return "party_roster";
        case opcode::Bundle: return "bundle";
        default: return "?";
    }
}

} // namespace aion2::proto
