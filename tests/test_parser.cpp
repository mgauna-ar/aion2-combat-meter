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
    const auto own = parse_one(p, opcode::OwnCharacter, identity_body(501, "Ariel", kServer, 7));
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

    (void)parse_one(p, opcode::OwnCharacter, identity_body(501, "Ariel", kServer, 7));
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
    TEST_ASSERT_EQ(s.npc_code, 0x0A1B2Cu);
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
    TEST_ASSERT_EQ(p.stats().failed, uint64_t{0});

    Bytes odd_kind = hp_body(100, 5);  // a one-byte id: the kind is byte 1
    odd_kind[1] = 0x03;
    (void)parse_one(p, opcode::RemainingHp, odd_kind);
    Bytes trailing = hp_body(100, 5);
    trailing.push_back(0x00);
    (void)parse_one(p, opcode::RemainingHp, trailing);
    TEST_ASSERT_EQ(p.stats().failed, uint64_t{2});
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

TEST_CASE(Parser, IgnoredOpcodesAreNeitherDecodedNorUnknown) {
    PacketParser p;
    const Bytes body = {1, 2, 3, 4};
    (void)parse_one(p, opcode::Cast, body);
    (void)parse_one(p, opcode::Pong, body);
    TEST_ASSERT_EQ(p.stats().decoded, uint64_t{0});
    TEST_ASSERT_EQ(p.stats().unknown, uint64_t{0});
    TEST_ASSERT_EQ(p.stats().frames, uint64_t{2});
}
