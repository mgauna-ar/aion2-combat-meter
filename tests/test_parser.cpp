#include "test_framework.hpp"
#include "wire_builder.hpp"

#include "protocol/entity_id.hpp"
#include "protocol/identity.hpp"
#include "protocol/opcodes.hpp"
#include "protocol/packet_parser.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace aion2;
using namespace aion2::proto;
using namespace aion2::test::wire;

namespace {

constexpr uint16_t kServer = 0x0123;

/// Parses one frame of `opcode` with `body`.
std::vector<ParsedEvent> parse_one(PacketParser& parser, uint16_t op, const Bytes& body) {
    Bytes payload;
    put_be16(payload, op);
    put_bytes(payload, body);
    Frame f;
    f.opcode = op;
    f.body = std::span<const uint8_t>(payload).subspan(2);
    f.raw = payload;
    f.ts = 1000;
    std::vector<ParsedEvent> out;
    parser.parse(f, out);
    return out;
}

/// The one event in `events`, copied: callers pass the temporary parse_one returns.
template <typename T>
T only(const std::vector<ParsedEvent>& events) {
    if (events.size() != 1) throw std::runtime_error("expected one event, got " + std::to_string(events.size()));
    const T* e = std::get_if<T>(&events[0].event);
    if (e == nullptr) throw std::runtime_error("event of another type");
    return *e;
}

} // namespace

TEST_CASE(EntityId, FoldsLargeIds) {
    TEST_ASSERT_EQ(normalize_entity_id(4242), EntityId{4242});
    TEST_ASSERT_EQ(normalize_entity_id(1'000'000), EntityId{1'000'000});
    TEST_ASSERT_EQ(normalize_entity_id(2'000'123), EntityId{(2'000'123 & 0x3FFF) | 0x4000});
    set_entity_id_folding(false);
    TEST_ASSERT_EQ(normalize_entity_id(2'000'123), EntityId{2'000'123});
    set_entity_id_folding(true);
}

TEST_CASE(Names, CharacterNameRules) {
    TEST_ASSERT(is_character_name("Daeva"));
    TEST_ASSERT(is_character_name("Kaisinel77"));
    TEST_ASSERT(is_character_name("Zoë"));
    TEST_ASSERT(is_character_name("천족"));
    TEST_ASSERT_FALSE(is_character_name(""));
    TEST_ASSERT_FALSE(is_character_name("12345"));
    TEST_ASSERT_FALSE(is_character_name("$tutorial"));
    TEST_ASSERT_FALSE(is_character_name("has space"));
    TEST_ASSERT_FALSE(is_character_name("ThisNameIsWayTooLong"));
    TEST_ASSERT_FALSE(is_character_name(std::string("bad\xFF", 4)));
    // What a UTF-8 decoder refuses: a C1 control, an overlong form, a surrogate,
    // past U+10FFFF.
    TEST_ASSERT_FALSE(is_character_name("a\xC2\x85"));
    TEST_ASSERT_FALSE(is_character_name("a\xE0\x80\xAF"));
    TEST_ASSERT_FALSE(is_character_name("a\xED\xA0\x80"));
    TEST_ASSERT_FALSE(is_character_name("a\xF4\x90\x80\x80"));
    TEST_ASSERT_EQ(character_name_chars("Zoë"), size_t{3});
}

TEST_CASE(Parser, OwnAndOtherIdentity) {
    PacketParser p;
    const auto own = parse_one(p, opcode::OwnCharacter, own_identity_body(501, "Ariel", kServer, 7));
    const auto& me = only<Identity>(own);
    TEST_ASSERT_EQ(me.id, EntityId{501});
    TEST_ASSERT_EQ(me.name, std::string("Ariel"));
    TEST_ASSERT_EQ(me.server, kServer);
    TEST_ASSERT_EQ(me.class_code, 7u);
    TEST_ASSERT(me.own);
    TEST_ASSERT_EQ(p.known_servers().size(), size_t{1});

    const auto other = parse_one(p, opcode::OtherPlayer, identity_body(502, "Israphel", kServer, 3));
    TEST_ASSERT_FALSE(only<Identity>(other).own);
    TEST_ASSERT_EQ(p.stats().decoded, uint64_t{2});
}

TEST_CASE(Parser, ScansUnknownPacketsOnlyForKnownServers) {
    PacketParser p;
    Bytes blob = {0x10, 0x20, 0x30};
    put_bytes(blob, identity_body(777, "Lumiel", kServer, 2));
    put_bytes(blob, {0xAA, 0xBB});
    // No server known yet: a look-alike could be anything.
    TEST_ASSERT(parse_one(p, 0x7777, blob).empty());

    (void)parse_one(p, opcode::OwnCharacter, own_identity_body(501, "Ariel", kServer, 7));
    const auto found = parse_one(p, 0x7777, blob);
    const auto& id = only<Identity>(found);
    TEST_ASSERT_EQ(id.id, EntityId{777});
    TEST_ASSERT_EQ(id.name, std::string("Lumiel"));
    TEST_ASSERT(id.scanned);
    TEST_ASSERT_EQ(p.stats().scanned_identities, uint64_t{1});
}

TEST_CASE(Parser, TutorialNameIsRejected) {
    PacketParser p;
    (void)parse_one(p, opcode::OtherPlayer, identity_body(9, "$Guide", kServer, 1));
    TEST_ASSERT_EQ(p.stats().failed, uint64_t{1});
}

TEST_CASE(Parser, SpawnWithHpAndOwner) {
    PacketParser p;
    SpawnSpec boss;
    boss.raw_id = 2'000'123;
    boss.cur_hp = 4'500'000;
    boss.max_hp = 5'000'000;
    const auto& s = only<Spawn>(parse_one(p, opcode::Spawn, spawn_body(boss)));
    TEST_ASSERT_EQ(s.id, normalize_entity_id(2'000'123));
    TEST_ASSERT_EQ(s.npc_code, 2'345'678u);
    TEST_ASSERT(s.has_hp);
    TEST_ASSERT_EQ(s.cur_hp, uint64_t{4'500'000});
    TEST_ASSERT_EQ(s.max_hp, uint64_t{5'000'000});
    TEST_ASSERT_NEAR(s.x, 1024.5f, 1e-3f);
    TEST_ASSERT_EQ(s.owner, EntityId{0});

    SpawnSpec pet;
    pet.raw_id = 3'000'001;
    pet.kind = spawn_kind::Summon;
    pet.owner = 501;
    pet.max_hp = 9000;
    pet.cur_hp = 9000;
    const auto& summon = only<Spawn>(parse_one(p, opcode::Spawn, spawn_body(pet)));
    TEST_ASSERT_EQ(summon.kind, spawn_kind::Summon);
    TEST_ASSERT_EQ(summon.owner, EntityId{501});
    TEST_ASSERT(summon.has_hp);
}

TEST_CASE(Parser, SpawnCarriesItsNpcCodeAfterTheMask) {
    // As the first capture's open-world mobs: the mask's three bytes, then the code as
    // a u32. Read a byte late, every 2,100,7xx code there came out 8206.
    PacketParser p;
    SpawnSpec mob;
    mob.kind = 0x05;
    mob.npc_code = 2'100'795;
    mob.cur_hp = 99'393;
    mob.max_hp = 119'700;
    const auto& s = only<Spawn>(parse_one(p, opcode::Spawn, spawn_body(mob)));
    TEST_ASSERT_EQ(s.kind, uint8_t{0x05});
    TEST_ASSERT_EQ(s.npc_code, 2'100'795u);
    TEST_ASSERT(s.has_hp);
    TEST_ASSERT_EQ(s.cur_hp, uint64_t{99'393});
    TEST_ASSERT_EQ(s.max_hp, uint64_t{119'700});
}

TEST_CASE(Parser, SpawnAimedAtAPlayerNamesThemBeforeItsCode) {
    // The second capture's traps: kind 1C, a mask of 1C 00 01, the trapped player's
    // name, then the code. Read as the code, the name's first bytes made
    // "NPC #1818840325".
    PacketParser p;
    SpawnSpec trap;
    trap.kind = spawn_kind::Mechanic;
    trap.aimed_at = "Lumiel";
    trap.npc_code = 2'920'360;
    trap.cur_hp = 10;
    trap.max_hp = 10;
    const auto& s = only<Spawn>(parse_one(p, opcode::Spawn, spawn_body(trap)));
    TEST_ASSERT_EQ(s.aimed_at, std::string("Lumiel"));
    TEST_ASSERT_EQ(s.npc_code, 2'920'360u);
    TEST_ASSERT(s.has_hp);
    TEST_ASSERT_EQ(s.max_hp, uint64_t{10});

    // Without the flag there is no name to read.
    SpawnSpec mob;
    mob.kind = 0x0C;
    TEST_ASSERT(only<Spawn>(parse_one(p, opcode::Spawn, spawn_body(mob))).aimed_at.empty());

    // A flagged spawn whose name is not one does not parse.
    trap.aimed_at = "$Guide";
    TEST_ASSERT(parse_one(p, opcode::Spawn, spawn_body(trap)).empty());
    TEST_ASSERT_EQ(p.stats().failed, uint64_t{1});
}

TEST_CASE(Parser, SpawnFindsItsPositionAfterOtherFields) {
    PacketParser p;
    SpawnSpec mob;
    mob.before_marker = {0x11, 0x22, 0x33, 0x44, 0x55};
    const auto& s = only<Spawn>(parse_one(p, opcode::Spawn, spawn_body(mob)));
    TEST_ASSERT_EQ(s.npc_code, 2'345'678u);
    TEST_ASSERT(s.has_hp);
    TEST_ASSERT_NEAR(s.x, 1024.5f, 1e-3f);
    TEST_ASSERT_EQ(s.max_hp, uint64_t{1'000'000});
}

TEST_CASE(Parser, SpawnOfACorpse) {
    PacketParser p;
    SpawnSpec corpse;
    corpse.alive = 0x00;
    corpse.cur_hp = 0;
    corpse.max_hp = 119'700;
    const auto& s = only<Spawn>(parse_one(p, opcode::Spawn, spawn_body(corpse)));
    TEST_ASSERT(s.has_hp);
    TEST_ASSERT_EQ(s.cur_hp, uint64_t{0});
    TEST_ASSERT_EQ(s.max_hp, uint64_t{119'700});
}

TEST_CASE(Parser, ShortDamageRecord) {
    PacketParser p;
    DamageSpec d;
    d.target = 16500;
    d.actor = 501;
    d.amount = 12345;
    // The short block's first bytes are a hit id, not modifiers and a direction.
    d.modifiers = 0x2E;
    d.direction = 1;
    const auto& dmg = only<Damage>(parse_one(p, opcode::Damage, damage_body({d})));
    TEST_ASSERT_EQ(dmg.target, EntityId{16500});
    TEST_ASSERT_EQ(dmg.actor, EntityId{501});
    TEST_ASSERT_EQ(dmg.skill, 11000u);
    TEST_ASSERT_EQ(dmg.layout, uint8_t{4});
    TEST_ASSERT_EQ(dmg.amount, uint64_t{12345});
    TEST_ASSERT_FALSE(dmg.crit);
    TEST_ASSERT_EQ(dmg.direction, uint8_t{hit_direction::None});
    TEST_ASSERT_EQ(dmg.modifiers, uint8_t{0});
}

TEST_CASE(Parser, SwitchBit08IsAFlagOverLayout4) {
    PacketParser p;
    DamageSpec d;
    d.target = 16500;
    d.actor = 501;
    d.amount = 1058;
    d.trailer = true;
    const auto& dmg = only<Damage>(parse_one(p, opcode::Damage, damage_body({d})));
    TEST_ASSERT_EQ(dmg.layout, uint8_t{4});
    TEST_ASSERT_EQ(dmg.amount, uint64_t{1058});
    TEST_ASSERT_EQ(p.stats().failed, uint64_t{0});
}

TEST_CASE(Parser, LongCritBackAttackWithPad) {
    PacketParser p;
    DamageSpec d;
    d.target = 16500;
    d.actor = 502;
    d.layout = 6;
    d.crit = true;
    d.direction = 1;
    d.modifiers = hit_modifier::Perfect | hit_modifier::Heavy;
    d.amount = 99999;
    d.pad = true;
    const auto& dmg = only<Damage>(parse_one(p, opcode::Damage, damage_body({d})));
    TEST_ASSERT(dmg.crit);
    TEST_ASSERT(dmg.back());
    TEST_ASSERT_EQ(dmg.modifiers, uint8_t{hit_modifier::Perfect | hit_modifier::Heavy});
    TEST_ASSERT_EQ(dmg.amount, uint64_t{99999});
}

TEST_CASE(Parser, FrontHitKeepsItsDirection) {
    PacketParser p;
    DamageSpec d;
    d.target = 16500;
    d.actor = 502;
    d.layout = 6;
    d.direction = 2;
    d.amount = 1200;
    const auto& dmg = only<Damage>(parse_one(p, opcode::Damage, damage_body({d})));
    TEST_ASSERT(dmg.front());
    TEST_ASSERT_FALSE(dmg.back());
}

TEST_CASE(Parser, EachLayoutHasItsBlockSize) {
    for (const uint8_t layout : {uint8_t{4}, uint8_t{5}, uint8_t{6}, uint8_t{7}}) {
        for (const bool pad : {false, true}) {
            PacketParser p;
            DamageSpec d;
            d.target = 16500;
            d.actor = 502;
            d.layout = layout;
            d.direction = 1;
            d.amount = 3646;
            d.pad = pad;
            const auto& dmg = only<Damage>(parse_one(p, opcode::Damage, damage_body({d})));
            TEST_ASSERT_EQ(dmg.layout, layout);
            TEST_ASSERT_EQ(dmg.amount, uint64_t{3646});
            TEST_ASSERT_EQ(dmg.back(), layout != 4);
        }
    }
}

TEST_CASE(Parser, MultiRecordAndExtraHits) {
    PacketParser p;
    DamageSpec a;
    a.target = 16500;
    a.actor = 501;
    a.amount = 100;
    DamageSpec b;
    b.target = 16501;
    b.actor = 501;
    b.layout = 5;
    b.amount = 200;
    b.extra_hits = {30, 40};
    const auto events = parse_one(p, opcode::Damage, damage_body({a, b}));
    TEST_ASSERT_EQ(events.size(), size_t{2});
    const auto& second = std::get<Damage>(events[1].event);
    TEST_ASSERT_EQ(second.target, EntityId{16501});
    TEST_ASSERT_EQ(second.extra_hits.size(), size_t{2});
    // The extra values are not damage: a real target's HP falls by the amount alone.
    TEST_ASSERT_EQ(second.amount, uint64_t{200});
}

TEST_CASE(Parser, BlockedAndMultiHits) {
    // Checked against the game's Combat Analysis: modifier 0x02 counted its BLOC,
    // and a record with extra values its MULT.
    PacketParser p;
    DamageSpec blocked;
    blocked.target = 16500;
    blocked.actor = 501;
    blocked.layout = 6;
    blocked.modifiers = hit_modifier::Block;
    blocked.amount = 4000;
    blocked.pad = true;
    const auto& b = only<Damage>(parse_one(p, opcode::Damage, damage_body({blocked})));
    TEST_ASSERT((b.modifiers & hit_modifier::Block) != 0);
    TEST_ASSERT_FALSE(b.multi_hit());

    DamageSpec multi = blocked;
    multi.modifiers = 0;
    multi.extra_hits = {1};
    const auto& m = only<Damage>(parse_one(p, opcode::Damage, damage_body({multi})));
    TEST_ASSERT(m.multi_hit());
    TEST_ASSERT_EQ(m.amount, uint64_t{4000});
}

TEST_CASE(Parser, CastMarkerIsNotDamage) {
    PacketParser p;
    Bytes body;
    put_varint(body, 16500);
    put_varint(body, 0);  // layout 0
    put_bytes(body, {0x05, 0x06});
    TEST_ASSERT(parse_one(p, opcode::Damage, body).empty());
    TEST_ASSERT_EQ(p.stats().decoded, uint64_t{1});
    TEST_ASSERT_EQ(p.stats().failed, uint64_t{0});
}

TEST_CASE(Parser, GarbageDamageFails) {
    PacketParser p;
    const Bytes body = {0x80, 0x80};
    TEST_ASSERT(parse_one(p, opcode::Damage, body).empty());
    TEST_ASSERT_EQ(p.stats().failed, uint64_t{1});
    TEST_ASSERT_EQ(p.stats().failures_by_opcode.at(opcode::Damage), uint64_t{1});
    TEST_ASSERT_NEAR(p.stats().coverage_percent(), 0.0, 1e-9);
}

TEST_CASE(Parser, DotAndHotTicks) {
    PacketParser p;
    const auto& dot = only<Tick>(parse_one(p, opcode::Tick, tick_body(16500, 0x02, 501, 1234, 777)));
    TEST_ASSERT_FALSE(dot.heal);
    TEST_ASSERT_EQ(dot.skill, 1234u);
    TEST_ASSERT_EQ(dot.amount, uint64_t{777});
    const auto& hot = only<Tick>(parse_one(p, opcode::Tick, tick_body(501, 0x09, 503, 55, 300)));
    TEST_ASSERT(hot.heal);
    (void)parse_one(p, opcode::Tick, tick_body(501, 0x42, 503, 55, 300));
    TEST_ASSERT_EQ(p.stats().failed, uint64_t{1});
}

TEST_CASE(Parser, HealOnHitAndTicksWithoutAnAmount) {
    PacketParser p;
    // Effect 0x30: the skill is not scaled, and the enemy skill that set it off
    // follows the amount.
    const auto& heal = only<Tick>(parse_one(p, opcode::Tick, heal_on_hit_body(501, 67'613, 1'604'531)));
    TEST_ASSERT(heal.heal);
    TEST_ASSERT_EQ(heal.target, EntityId{501});
    TEST_ASSERT_EQ(heal.actor, EntityId{501});
    TEST_ASSERT_EQ(heal.skill, 2001u);
    TEST_ASSERT_EQ(heal.amount, uint64_t{67'613});
    TEST_ASSERT_EQ(heal.cause_skill, 1'604'531u);

    // Effect 0x00 carries no amount: decoded, and nothing to count.
    TEST_ASSERT(parse_one(p, opcode::Tick, no_amount_tick_body(16500)).empty());
    TEST_ASSERT_EQ(p.stats().decoded, uint64_t{2});
    TEST_ASSERT_EQ(p.stats().failed, uint64_t{0});
}

TEST_CASE(Parser, DeathAndMap) {
    PacketParser p;
    TEST_ASSERT_EQ(only<Death>(parse_one(p, opcode::Death, death_body(16500))).id, EntityId{16500});
    const auto& map = only<MapLoad>(parse_one(p, opcode::MapLoad, map_load_body(3, 610010)));
    TEST_ASSERT_EQ(map.map_id, 610010u);
    TEST_ASSERT(map.is_instance());
}

TEST_CASE(Parser, EntityValuesHpAndSelf) {
    PacketParser p;
    const auto& mob = only<RemainingHp>(parse_one(p, opcode::RemainingHp, hp_body(18024, 98'578)));
    TEST_ASSERT_EQ(mob.id, EntityId{18024});
    TEST_ASSERT_FALSE(mob.self);
    TEST_ASSERT(mob.has_hp);
    TEST_ASSERT_EQ(mob.hp, uint64_t{98'578});
    const auto& big = only<RemainingHp>(parse_one(p, opcode::RemainingHp, hp_body(18024, 6'000'000'000ULL)));
    TEST_ASSERT_EQ(big.hp, uint64_t{6'000'000'000ULL});

    const auto& me = only<RemainingHp>(parse_one(p, opcode::RemainingHp, self_values_body(6745)));
    TEST_ASSERT_EQ(me.id, EntityId{6745});
    TEST_ASSERT(me.self);
    TEST_ASSERT_FALSE(me.has_hp);

    // Kind 3, the local player's both lists, carries their own HP.
    const auto& both = only<RemainingHp>(parse_one(p, opcode::RemainingHp, both_values_body(6745, 23'754)));
    TEST_ASSERT(both.self);
    TEST_ASSERT(both.has_hp);
    TEST_ASSERT_EQ(both.hp, uint64_t{23'754});
    TEST_ASSERT_EQ(p.stats().failed, uint64_t{0});

    Bytes odd_kind = hp_body(100, 5);  // a one-byte id: the kind is byte 1
    odd_kind[1] = 0x04;
    (void)parse_one(p, opcode::RemainingHp, odd_kind);
    Bytes trailing = hp_body(100, 5);
    trailing.push_back(0x00);
    (void)parse_one(p, opcode::RemainingHp, trailing);
    TEST_ASSERT_EQ(p.stats().failed, uint64_t{2});
}

TEST_CASE(Parser, EntityValuesMaxHp) {
    // The capture's login: the local player's kind 3, one u32 value (stat 8) and one
    // u64 value, stat 7, their max HP once their gear's figures had added up.
    PacketParser p;
    const Bytes login = {0xc3, 0x12, 0x03, 0x01, 0x08, 0x85, 0x0a, 0x00, 0x00, 0x01,
                         0x07, 0x1d, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const auto& max = only<RemainingHp>(parse_one(p, opcode::RemainingHp, login));
    TEST_ASSERT_EQ(max.id, EntityId{2371});
    TEST_ASSERT(max.self);
    TEST_ASSERT_FALSE(max.has_hp);
    TEST_ASSERT(max.has_max_hp);
    TEST_ASSERT_EQ(max.max_hp, uint64_t{25'373});

    // Both in one record, as kind 2 and kind 3 may carry them.
    const auto& other = only<RemainingHp>(parse_one(p, opcode::RemainingHp, hp_body(2371, 21'729, 25'373)));
    TEST_ASSERT(other.has_hp && other.has_max_hp);
    TEST_ASSERT_EQ(other.hp, uint64_t{21'729});
    TEST_ASSERT_EQ(other.max_hp, uint64_t{25'373});
    const auto& both = only<RemainingHp>(parse_one(p, opcode::RemainingHp, both_values_body(2371, 6'000, 25'373)));
    TEST_ASSERT(both.self && both.has_hp && both.has_max_hp);
    TEST_ASSERT_EQ(both.max_hp, uint64_t{25'373});
    TEST_ASSERT_FALSE(only<RemainingHp>(parse_one(p, opcode::RemainingHp, hp_body(2371, 5))).has_max_hp);
    TEST_ASSERT_EQ(p.stats().failed, uint64_t{0});
}

TEST_CASE(Parser, PartyRosterNames) {
    PacketParser p;
    const auto& roster = only<PartyRoster>(parse_one(p, opcode::PartyRoster, party_body({"Ariel", "Lumiel", "Zikel"})));
    TEST_ASSERT_EQ(roster.members.size(), size_t{3});
    TEST_ASSERT_EQ(roster.members[0].name, std::string("Ariel"));
    TEST_ASSERT_EQ(roster.members[2].name, std::string("Zikel"));
}

TEST_CASE(Parser, PartyScanSkipsOneLetterLookAlikes) {
    PacketParser p;
    Bytes body = party_body({"Ariel", "Lumiel"});
    // In Ariel's tail, where the scan for the next member starts, 01 'A' reads as
    // a one-letter name.
    const Bytes ariel = bytes_of("Ariel");
    const auto at = std::search(body.begin(), body.end(), ariel.begin(), ariel.end()) - body.begin();
    const auto tail = static_cast<size_t>(at) + ariel.size();
    body[tail + 15] = 0x01;
    body[tail + 16] = 'A';
    const auto& roster = only<PartyRoster>(parse_one(p, opcode::PartyRoster, body));
    TEST_ASSERT_EQ(roster.members.size(), size_t{2});
    TEST_ASSERT_EQ(roster.members[1].name, std::string("Lumiel"));
}

// --- Pong (03 36) ---

TEST_CASE(Parser, PongAsTheCaptureHasIt) {
    PacketParser p;
    const Bytes body = {0x00, 0x00, 0x6c, 0xbe, 0x6b, 0xe8, 0x03, 0x00, 0x00, 0x00,
                        0x22, 0xb3, 0x45, 0x4a, 0xa1, 0x01, 0x00, 0x00};
    const auto& pong = only<Pong>(parse_one(p, opcode::Pong, body));
    TEST_ASSERT_EQ(pong.client_ms, uint64_t{16'784'277'100});
    TEST_ASSERT_EQ(pong.server_ms, uint64_t{1'792'247'444'258});
    TEST_ASSERT_EQ(p.stats().decoded, uint64_t{1});
}

TEST_CASE(Parser, PongOfAnotherLengthFails) {
    PacketParser p;
    TEST_ASSERT(parse_one(p, opcode::Pong, Bytes{1, 2, 3, 4}).empty());
    Bytes longer = pong_body(16'784'277'100, 1'792'247'444'258);
    longer.push_back(0x00);
    TEST_ASSERT(parse_one(p, opcode::Pong, longer).empty());
    TEST_ASSERT_EQ(p.stats().failed, uint64_t{2});
    TEST_ASSERT_EQ(p.stats().unknown, uint64_t{0});
}

// --- Casts (02 38) ---

TEST_CASE(Parser, PressedCastAsTheCaptureHasIt) {
    // The local player's Dodge, at themself: mode 02 and a position.
    PacketParser p;
    const Bytes body = {0xa0, 0x50, 0x00, 0x65, 0x1b, 0xb7, 0x00, 0x0a, 0x02, 0xa0, 0x50, 0xb4, 0x0c, 0xa6, 0xc2, 0xa2,
                        0x4d, 0x42, 0x41, 0x0f, 0x85, 0xc6, 0xc2, 0x00, 0x00, 0x00, 0x00, 0x90, 0x4e, 0x01, 0xf4, 0x03};
    const auto c = only<SkillCast>(parse_one(p, opcode::Cast, body));
    TEST_ASSERT_EQ(c.caster, EntityId{10'272});
    TEST_ASSERT_EQ(c.skill, uint32_t{12'000'101});
    TEST_ASSERT_EQ(c.target, EntityId{10'272});
    TEST_ASSERT(c.pressed);
}

TEST_CASE(Parser, MantraPulseIsNotAPress) {
    // A Chanter's Undefeated Mantra pulsing, once a second: mode 00, no position.
    PacketParser p;
    const Bytes body = {0xd9, 0x3c, 0x00, 0xd9, 0x8e, 0x15, 0x01, 0x7e, 0x00, 0xd9,
                        0x3c, 0xb1, 0x65, 0x3c, 0x43, 0x9e, 0x55, 0x01, 0x00};
    const auto c = only<SkillCast>(parse_one(p, opcode::Cast, body));
    TEST_ASSERT_EQ(c.caster, EntityId{7769});
    TEST_ASSERT_EQ(c.skill, uint32_t{18'190'041});
    TEST_ASSERT(!c.pressed);
}

TEST_CASE(Parser, CastBuilderRoundTripsAndFoldsLargeIds) {
    PacketParser p;
    const auto c = only<SkillCast>(parse_one(p, opcode::Cast, cast_body(1'234'567, 12'240'340, 2'000'001, true)));
    TEST_ASSERT_EQ(c.caster, normalize_entity_id(1'234'567));
    TEST_ASSERT_EQ(c.target, normalize_entity_id(2'000'001));
    TEST_ASSERT_EQ(c.skill, uint32_t{12'240'340});
    TEST_ASSERT(c.pressed);
    TEST_ASSERT(!only<SkillCast>(parse_one(p, opcode::Cast, cast_body(70, 12'730'001, 70, false))).pressed);
}

// --- Chain skills (18 38, 19 38) ---

TEST_CASE(Parser, ChainOfferAndItsEndAsTheCaptureHasThem) {
    // Vicious Strike offers Decisive Strike; the offer is withdrawn as it is used.
    PacketParser p;
    const Bytes offer = {0x64, 0x43, 0xb7, 0x00, 0x01, 0x74, 0x6a, 0xb7, 0x00};
    const auto o = only<ChainOffered>(parse_one(p, opcode::ChainOffered, offer));
    TEST_ASSERT_EQ(o.trigger, uint32_t{12'010'340});
    TEST_ASSERT_EQ(o.skill, uint32_t{12'020'340});
    const Bytes end = {0x01, 0x74, 0x6a, 0xb7, 0x00};
    TEST_ASSERT_EQ(only<ChainWithdrawn>(parse_one(p, opcode::ChainWithdrawn, end)).skill, uint32_t{12'020'340});
    // The builders make the same shapes.
    TEST_ASSERT(chain_offer_body(12'010'340, 12'020'340) == offer);
    TEST_ASSERT(chain_end_body(12'020'340) == end);
    TEST_ASSERT_EQ(p.stats().failed, uint64_t{0});
    TEST_ASSERT_EQ(p.stats().unknown, uint64_t{0});
}

TEST_CASE(Parser, ChainWithNoSkillOrBytesLeftFails) {
    PacketParser p;
    TEST_ASSERT(parse_one(p, opcode::ChainWithdrawn, Bytes{0x00}).empty());  // a count of 0
    Bytes many = chain_end_body(12'020'340);
    many[0] = 5;  // more than kMaxChainSkills
    TEST_ASSERT(parse_one(p, opcode::ChainWithdrawn, many).empty());
    Bytes trailing = chain_offer_body(12'010'340, 12'020'340);
    trailing.push_back(0x00);
    TEST_ASSERT(parse_one(p, opcode::ChainOffered, trailing).empty());
    TEST_ASSERT(parse_one(p, opcode::ChainOffered, chain_offer_body(0, 12'020'340)).empty());
    TEST_ASSERT(parse_one(p, opcode::ChainOffered, chain_offer_body(12'010'340, 0)).empty());
    TEST_ASSERT(parse_one(p, opcode::ChainWithdrawn, Bytes{0x01, 0x74, 0x6a}).empty());  // cut short
    TEST_ASSERT_EQ(p.stats().failed, uint64_t{6});
}

TEST_CASE(Parser, CastOfAnUnseenModeOrCutShortFails) {
    PacketParser p;
    Bytes odd_mode = cast_body(70, 12'240'340, 9000, true);
    odd_mode[7] = 0x01;  // caster (1 byte), the byte, skill (4), sequence: the mode
    TEST_ASSERT(parse_one(p, opcode::Cast, odd_mode).empty());
    const Bytes cut = {0xa0, 0x50, 0x00, 0x65, 0x1b, 0xb7};
    TEST_ASSERT(parse_one(p, opcode::Cast, cut).empty());
    TEST_ASSERT(parse_one(p, opcode::Cast, cast_body(70, 0, 9000, true)).empty());
    TEST_ASSERT_EQ(p.stats().failed, uint64_t{3});
    TEST_ASSERT_EQ(p.stats().failures_by_opcode.at(opcode::Cast), uint64_t{3});
}

// --- Effects, skill uses and cooldowns (2a 38, 2b 38, 1a 92, 01 38, 47 38) ---

TEST_CASE(Parser, BuffRenewAsTheCaptureHasIt) {
    // A Chanter's Undefeated Mantra renewed on a party member: kind 0x13, so the
    // applying skill (18190041) follows the level (20).
    PacketParser p;
    const Bytes body = {0xd9, 0x3c, 0x13, 0x33, 0x7b, 0x94, 0xd7, 0x0a, 0x60, 0x09, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0xb9, 0xd1, 0x45, 0x4a, 0xa1, 0x01, 0x00, 0x00, 0xd9, 0x3c,
                        0x14, 0xd9, 0x8e, 0x15, 0x01, 0x02, 0x4b, 0x2c, 0xd5, 0xc7, 0x37, 0x4f, 0xca,
                        0xc7, 0x00, 0x90, 0xab, 0x46};
    const auto b = only<BuffApplied>(parse_one(p, opcode::BuffRenew, body));
    TEST_ASSERT_EQ(b.target, EntityId{7769});
    TEST_ASSERT_EQ(b.caster, EntityId{7769});
    TEST_ASSERT_EQ(b.instance, uint32_t{0x33});
    TEST_ASSERT_EQ(b.buff_id, uint32_t{181'900'411});
    TEST_ASSERT_EQ(b.source_skill, uint32_t{18'190'041});
    TEST_ASSERT_EQ(unsigned{b.level}, 20u);
    TEST_ASSERT_EQ(b.duration_ms, uint64_t{2400});
    TEST_ASSERT_EQ(b.expires_server_ms, uint64_t{0x01A14A45D1B9});
    TEST_ASSERT(b.renewed);
    TEST_ASSERT(!b.permanent);
    TEST_ASSERT_EQ(p.stats().decoded, uint64_t{1});
}

TEST_CASE(Parser, PermanentBuffAsTheCaptureHasIt) {
    // A login effect on the local player: 2a 38's extra 01, kind 0x11 (no skill),
    // and a duration of all ones.
    PacketParser p;
    const Bytes body = {0xc3, 0x12, 0x01, 0x11, 0x01, 0xeb, 0x2e, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff, 0x80, 0x75, 0xd5, 0x2a, 0xbb, 0x03, 0x00, 0x00, 0xc3,
                        0x12, 0x01, 0x00, 0x30, 0x99, 0xd7, 0xc7, 0xa9, 0x64, 0xca, 0xc7, 0x00, 0x30,
                        0xab, 0x46};
    const auto b = only<BuffApplied>(parse_one(p, opcode::BuffAdd, body));
    TEST_ASSERT_EQ(b.target, EntityId{2371});
    TEST_ASSERT_EQ(b.buff_id, uint32_t{12011});
    TEST_ASSERT_EQ(b.source_skill, uint32_t{0});
    TEST_ASSERT(b.permanent);
    TEST_ASSERT_EQ(b.duration_ms, uint64_t{0});
    TEST_ASSERT_EQ(b.expires_server_ms, uint64_t{0});
    TEST_ASSERT(!b.renewed);
}

TEST_CASE(Parser, BuffBuilderRoundTrips) {
    PacketParser p;
    BuffSpec s;
    s.target = 300;
    s.caster = 70'000;  // a three-byte varint
    s.instance = 190;
    s.buff_id = 127'300'021;
    s.skill = 12'730'002;
    s.level = 9;
    s.duration_ms = 10'000;
    s.expires_server_ms = 1'792'000'010'000;
    const auto b = only<BuffApplied>(parse_one(p, opcode::BuffAdd, buff_body(s)));
    TEST_ASSERT_EQ(b.caster, EntityId{70'000});
    TEST_ASSERT_EQ(b.instance, uint32_t{190});
    TEST_ASSERT_EQ(b.source_skill, uint32_t{12'730'002});
    TEST_ASSERT_EQ(b.expires_server_ms, uint64_t{1'792'000'010'000});

    s.skill = 0;  // kind 0x11
    const auto plain = only<BuffApplied>(parse_one(p, opcode::BuffRenew, buff_body(s, true)));
    TEST_ASSERT_EQ(plain.source_skill, uint32_t{0});
    TEST_ASSERT_EQ(plain.duration_ms, uint64_t{10'000});
    TEST_ASSERT(plain.renewed);
}

TEST_CASE(Parser, BuffOfAnUnseenShapeFails) {
    PacketParser p;
    BuffSpec s;
    s.target = 300;
    s.caster = 300;
    s.buff_id = 200;
    s.duration_ms = 300;
    Bytes longer = buff_body(s);
    longer.push_back(0x00);  // a field not seen yet
    TEST_ASSERT(parse_one(p, opcode::BuffAdd, longer).empty());
    Bytes kind = buff_body(s);
    kind[3] = 0x15;  // after target (2 bytes) and the 01: a kind not seen
    TEST_ASSERT(parse_one(p, opcode::BuffAdd, kind).empty());
    TEST_ASSERT_EQ(p.stats().failed, uint64_t{2});
    TEST_ASSERT_EQ(p.stats().failures_by_opcode.at(opcode::BuffAdd), uint64_t{2});
}

TEST_CASE(Parser, BuffRemoveSkillUseAndCooldown) {
    PacketParser p;
    const Bytes removed = {0xa3, 0x41, 0xb3, 0x93, 0xd7, 0x0a};  // as the capture has it
    const auto r = only<BuffRemoved>(parse_one(p, opcode::BuffRemove, removed));
    TEST_ASSERT_EQ(r.target, EntityId{8355});
    TEST_ASSERT_EQ(r.buff_id, uint32_t{181'900'211});

    const Bytes use = {0x00, 0x00, 0x00, 0x64, 0x1b, 0xb7, 0x00};
    TEST_ASSERT_EQ(only<SkillUsed>(parse_one(p, opcode::SkillUse, use)).skill, uint32_t{12'000'100});

    const Bytes cd = {0x01, 0x30, 0x72, 0xbc, 0x00, 0xb0, 0x9f, 0x01};
    const auto c = only<CooldownSet>(parse_one(p, opcode::CooldownSet, cd));
    TEST_ASSERT_EQ(c.skill, uint32_t{12'350'000});
    TEST_ASSERT_EQ(c.remaining_ms, uint32_t{20'400});

    Bytes two = {0x02};
    for (const auto& [skill, ms] : {std::pair{12'350'000u, 450u}, std::pair{12'300'000u, 0u}}) {
        put_le32(two, skill);
        put_varint(two, ms);
    }
    const auto both = parse_one(p, opcode::CooldownSet, two);
    TEST_ASSERT_EQ(both.size(), size_t{2});
    TEST_ASSERT_EQ(std::get<CooldownSet>(both[1].event).skill, uint32_t{12'300'000});
    TEST_ASSERT_EQ(only<CooldownSet>(parse_one(p, opcode::CooldownSet, cooldown_body(12'240'000, 1500))).remaining_ms,
                   uint32_t{1500});
    TEST_ASSERT_EQ(only<SkillUsed>(parse_one(p, opcode::SkillUse, skill_use_body(12'240'340))).skill,
                   uint32_t{12'240'340});
    TEST_ASSERT_EQ(only<BuffRemoved>(parse_one(p, opcode::BuffRemove, buff_remove_body(70'000, 3015))).target,
                   EntityId{70'000});

    const Bytes truncated = {0x01, 0x30, 0x72, 0xbc};
    TEST_ASSERT(parse_one(p, opcode::CooldownSet, truncated).empty());
    TEST_ASSERT_EQ(p.stats().failed, uint64_t{1});
}
