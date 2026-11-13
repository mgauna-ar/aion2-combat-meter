#include "test_framework.hpp"
#include "wire_builder.hpp"

#include "app/power_view.hpp"
#include "combat_power/power_plugin.hpp"
#include "combat_power/power_tracker.hpp"
#include "pipeline/pipeline.hpp"

#include <string>

using namespace aion2;
using namespace aion2::combat_power;

namespace {

constexpr Micros kStart = 1'792'152'000'000'000ULL;
constexpr EntityId kMe = 13119;
/// Templar and Sorcerer skills, by their class digits.
constexpr uint32_t kTemplarSkill = 12240340;
constexpr uint32_t kSorcererSkill = 15010230;

[[nodiscard]] Micros at(double seconds) { return kStart + from_seconds(seconds); }

proto::Identity player(EntityId id, const char* name, uint32_t power, uint16_t server = 9) {
    proto::Identity i;
    i.id = id;
    i.name = name;
    i.server = server;
    i.combat_power = power;
    return i;
}

proto::Identity own(EntityId id) {
    proto::Identity i = player(id, "Ariel", 0);
    i.own = true;
    i.faction = 1;
    return i;
}

/// A player of `faction`, standing `metres` east of the origin.
proto::Identity placed(EntityId id, const char* name, uint32_t power, uint8_t faction, float metres) {
    proto::Identity i = player(id, name, power);
    i.faction = faction;
    i.has_position = true;
    i.position = proto::Point{metres * 100.0f, 0.0f, 2400.0f};
    return i;
}

proto::SkillCast press(EntityId caster, EntityId target, uint32_t skill = kTemplarSkill) {
    proto::SkillCast c;
    c.caster = caster;
    c.target = target;
    c.skill = skill;
    c.pressed = true;
    return c;
}

proto::Damage hit(EntityId actor, EntityId target, uint32_t skill = kTemplarSkill) {
    proto::Damage d;
    d.actor = actor;
    d.target = target;
    d.skill = skill;
    d.amount = 1000;
    return d;
}

proto::Position step(EntityId id, float x, float y, float z = 2400.0f) {
    return proto::Position{id, proto::Point{x, y, z}};
}

/// An effect cast by `caster` from where they stand.
proto::BuffApplied effect_from(EntityId caster, float x, float y, float z = 2400.0f) {
    proto::BuffApplied b;
    b.target = caster;
    b.caster = caster;
    b.buff_id = 120'900'001;
    b.caster_position = proto::Point{x, y, z};
    return b;
}

/// The local player and an arena opponent, on connection 2.
PowerTracker duel() {
    PowerTracker t;
    t.on_map_load(proto::MapLoad{1, 61}, 2, at(0));
    t.on_identity(own(kMe), 2, at(1));
    t.on_identity(player(227, "Nezekan", 134'667), 2, at(1.4));
    return t;
}

} // namespace

TEST_CASE(CombatPower, AnArenaOpponentIsListedAsTheMatchLoads) {
    const PowerTracker t = duel();
    const PowerSnapshot s = t.snapshot(at(2));
    TEST_ASSERT_EQ(s.map_id, 61u);
    TEST_ASSERT_EQ(s.in_view, 1u);
    TEST_ASSERT_EQ(s.players.size(), size_t{1});
    TEST_ASSERT_EQ(s.players[0].name, std::string("Nezekan"));
    TEST_ASSERT_EQ(s.players[0].combat_power, 134'667u);
    // No skill used on them yet: listed, and no target.
    TEST_ASSERT_EQ(s.target.id, EntityId{0});
    // The local player is not in their own list.
    TEST_ASSERT_EQ(t.local(), kMe);
}

TEST_CASE(CombatPower, TheTargetIsThePlayerASkillWasPressedOn) {
    PowerTracker t = duel();
    t.on_cast(press(kMe, 227), at(5));
    TEST_ASSERT_EQ(t.snapshot(at(6)).target.id, EntityId{227});
    TEST_ASSERT_EQ(t.snapshot(at(6)).target.combat_power, 134'667u);
    // It lasts kTargetMemory after the last skill, and a hit renews it.
    TEST_ASSERT_EQ(t.snapshot(at(5) + PowerTracker::kTargetMemory).target.id, EntityId{227});
    TEST_ASSERT_EQ(t.snapshot(at(5.1) + PowerTracker::kTargetMemory).target.id, EntityId{0});
    t.on_damage(hit(kMe, 227), at(40));
    TEST_ASSERT_EQ(t.snapshot(at(41)).target.id, EntityId{227});
}

TEST_CASE(CombatPower, OnlyTheLocalPlayersPressesAim) {
    PowerTracker t = duel();
    t.on_identity(player(300, "Zikel", 90'000), 2, at(2));
    // Someone else's skill, a self-cast and a passive going off say nothing.
    t.on_cast(press(227, 300), at(3));
    t.on_cast(press(kMe, kMe), at(3));
    proto::SkillCast proc = press(kMe, 300);
    proc.pressed = false;
    t.on_cast(proc, at(3));
    TEST_ASSERT_EQ(t.snapshot(at(4)).target.id, EntityId{0});

    t.on_cast(press(kMe, 300), at(5));
    TEST_ASSERT_EQ(t.snapshot(at(6)).target.id, EntityId{300});
    // An area skill's hit on a monster beside them keeps the target.
    t.on_damage(hit(kMe, 9001), at(7));
    TEST_ASSERT_EQ(t.snapshot(at(8)).target.id, EntityId{300});
    // A skill pressed on the monster lets go of it.
    t.on_cast(press(kMe, 9001), at(9));
    TEST_ASSERT_EQ(t.snapshot(at(10)).target.id, EntityId{0});
}

TEST_CASE(CombatPower, PlayersAreListedStrongestFirstAndOfACrowdTheStrongest) {
    PowerTracker t = duel();
    t.on_identity(player(300, "Zikel", 201'900), 2, at(2));
    t.on_identity(player(301, "Vaizel", 0), 2, at(2));  // a record whose end did not read
    PowerSnapshot s = t.snapshot(at(3));
    TEST_ASSERT_EQ(s.players.size(), size_t{3});
    TEST_ASSERT_EQ(s.players[0].name, std::string("Zikel"));
    TEST_ASSERT_EQ(s.players[1].name, std::string("Nezekan"));
    TEST_ASSERT_EQ(s.players[2].combat_power, 0u);

    // A town: more than a snapshot lists. The strongest stay, and all are counted.
    for (EntityId id = 1000; id < 1000 + PowerTracker::kMaxListed; ++id) {
        t.on_identity(player(id, ("Crowd" + std::to_string(id)).c_str(), 50'000 + id), 2, at(4));
    }
    t.on_cast(press(kMe, 301), at(5));
    s = t.snapshot(at(6));
    TEST_ASSERT_EQ(s.players.size(), PowerTracker::kMaxListed);
    TEST_ASSERT_EQ(s.in_view, static_cast<uint32_t>(3 + PowerTracker::kMaxListed));
    TEST_ASSERT_EQ(s.players[0].name, std::string("Zikel"));
    TEST_ASSERT_EQ(s.players[1].name, std::string("Nezekan"));
    TEST_ASSERT(s.players.back().combat_power > 50'000u);
    // The target is whoever was aimed at, listed or not.
    TEST_ASSERT_EQ(s.target.name, std::string("Vaizel"));
}

TEST_CASE(CombatPower, TheClassComesFromTheSkillsTheyUse) {
    PowerTracker t = duel();
    TEST_ASSERT(t.snapshot(at(2)).players[0].cls == game::AionClass::Unknown);
    t.on_damage(hit(227, kMe, kSorcererSkill), at(3));
    TEST_ASSERT(t.snapshot(at(4)).players[0].cls == game::AionClass::Sorcerer);
    // A code that names no class leaves the one known.
    t.on_cast(press(227, kMe, 3000019), at(5));
    TEST_ASSERT(t.snapshot(at(6)).players[0].cls == game::AionClass::Sorcerer);
}

TEST_CASE(CombatPower, ANewConnectionForgetsTheMapBefore) {
    PowerTracker t = duel();
    t.on_cast(press(kMe, 227), at(5));
    // Back to town: another connection, where every id is someone else's.
    t.on_values(proto::RemainingHp{4987, true, false, 0, false, 0}, 3);
    TEST_ASSERT_EQ(t.players(), size_t{0});
    TEST_ASSERT_EQ(t.local(), EntityId{4987});
    TEST_ASSERT_EQ(t.snapshot(at(60)).target.id, EntityId{0});
    t.on_map_load(proto::MapLoad{1, 1010}, 3, at(60));
    // The local player's old id is now another player's.
    t.on_identity(player(kMe, "Triniel", 88'002), 3, at(61));
    TEST_ASSERT_EQ(t.local(), EntityId{4987});
    TEST_ASSERT_EQ(t.snapshot(at(62)).players.size(), size_t{1});
    // A straggler from the connection before is ignored.
    t.on_identity(player(227, "Nezekan", 134'667), 2, at(62));
    TEST_ASSERT_EQ(t.players(), size_t{1});
}

TEST_CASE(CombatPower, AnotherMapOverTheSameConnectionKeepsOnlyEarlyArrivals) {
    PowerTracker t = duel();
    // Named just ahead of the next map's load: they are on that map.
    t.on_identity(player(400, "Lumiel", 120'000), 2, at(99));
    t.on_map_load(proto::MapLoad{2, 1010}, 2, at(100));
    const PowerSnapshot s = t.snapshot(at(101));
    TEST_ASSERT_EQ(s.players.size(), size_t{1});
    TEST_ASSERT_EQ(s.players[0].name, std::string("Lumiel"));
    // The same map again is a reload: everyone stays.
    t.on_map_load(proto::MapLoad{3, 1010}, 2, at(200));
    TEST_ASSERT_EQ(t.players(), size_t{1});
}

TEST_CASE(CombatPower, ACharacterSeenAgainKeepsOneRow) {
    PowerTracker t = duel();
    t.on_cast(press(kMe, 227), at(2));
    // Sent again, its end unread this time: the figure stays.
    t.on_identity(player(227, "Nezekan", 0), 2, at(3));
    TEST_ASSERT_EQ(t.snapshot(at(4)).players[0].combat_power, 134'667u);
    // Back under a new id: one row, and still the target.
    t.on_identity(player(555, "Nezekan", 135'000), 2, at(5));
    PowerSnapshot s = t.snapshot(at(6));
    TEST_ASSERT_EQ(s.players.size(), size_t{1});
    TEST_ASSERT_EQ(s.players[0].id, EntityId{555});
    TEST_ASSERT_EQ(s.target.id, EntityId{555});
    // The id handed to someone else is no longer the target.
    t.on_identity(player(555, "Marchutan", 60'000), 2, at(7));
    s = t.snapshot(at(8));
    TEST_ASSERT_EQ(s.players[0].name, std::string("Marchutan"));
    TEST_ASSERT_EQ(s.target.id, EntityId{0});
    // A scan's look-alike names no one.
    proto::Identity scanned = player(777, "Israphel", 99'999);
    scanned.scanned = true;
    t.on_identity(scanned, 2, at(9));
    TEST_ASSERT_EQ(t.players(), size_t{1});
}

TEST_CASE(CombatPower, ThePipelineFeedsItOnlyWhileSwitchedOn) {
    using namespace aion2::test::wire;
    PowerPlugin plugin;
    proto::ParsedEvent e;
    e.connection = 1;
    e.ts = at(0);
    e.event = own(kMe);
    plugin.on_event(e);
    e.event = player(227, "Nezekan", 134'667);
    plugin.on_event(e);
    e.event = press(kMe, 227);
    plugin.on_event(e);
    TEST_ASSERT_EQ(plugin.snapshot(at(1)).target.combat_power, 134'667u);
    e.event = proto::LeftView{227, 1};
    plugin.on_event(e);
    TEST_ASSERT_EQ(plugin.snapshot(at(1)).in_view, 0u);
    plugin.clear();
    TEST_ASSERT_EQ(plugin.snapshot(at(1)).in_view, 0u);

    // Off by default in the app; the pipeline's own default feeds every plugin.
    pipeline::Pipeline live;
    TEST_ASSERT(live.plugin_switches().combat_power);
    aion2::plugins::PluginSwitches off = aion2::plugins::ALL_ON;
    off.combat_power = false;
    live.set_plugins(off, at(2));
    TEST_ASSERT_EQ(live.snapshot(at(2)).power.in_view, 0u);
}

TEST_CASE(CombatPower, DistanceIsFromWhereTheLocalPlayerLastActed) {
    PowerTracker t;
    t.on_map_load(proto::MapLoad{1, 61}, 2, at(0));
    // Their own record places them on entering the map.
    proto::Identity me = own(kMe);
    me.has_position = true;
    me.position = proto::Point{0.0f, 0.0f, 2400.0f};
    t.on_identity(me, 2, at(1));
    proto::Identity foe = player(227, "Nezekan", 134'667);
    foe.has_position = true;
    foe.position = proto::Point{3000.0f, 4000.0f, 2400.0f};
    t.on_identity(foe, 2, at(1.4));
    PowerSnapshot s = t.snapshot(at(2));
    TEST_ASSERT(s.has_local_position);
    TEST_ASSERT_EQ(s.local_position_age, from_seconds(1.0));
    TEST_ASSERT_NEAR(s.players[0].distance_m, 50.0f, 1e-3f);  // 5,000 units
    TEST_ASSERT_NEAR(s.nearest[0].distance_m, 50.0f, 1e-3f);

    // The opponent's steps are sent; the local player's are not.
    t.on_position(step(227, 300.0f, 400.0f), 2, at(10));
    s = t.snapshot(at(11));
    TEST_ASSERT_NEAR(s.players[0].distance_m, 5.0f, 1e-3f);
    TEST_ASSERT_EQ(s.local_position_age, from_seconds(10.0));
    // An effect they cast says where they stand now; so does a skill moving them.
    t.on_effect(effect_from(kMe, 300.0f, 100.0f), 2, at(12));
    s = t.snapshot(at(12));
    TEST_ASSERT_NEAR(s.players[0].distance_m, 3.0f, 1e-3f);
    TEST_ASSERT_EQ(s.local_position_age, Micros{0});
    t.on_position(step(kMe, 300.0f, 400.0f, 2500.0f), 2, at(13));
    TEST_ASSERT_NEAR(t.snapshot(at(13)).players[0].distance_m, 1.0f, 1e-3f);  // height counts
    // The opponent's own effect moves the opponent, and the target carries it too.
    t.on_cast(press(kMe, 227), at(14));
    t.on_effect(effect_from(227, 300.0f, 1400.0f, 2500.0f), 2, at(14));
    TEST_ASSERT_NEAR(t.snapshot(at(15)).target.distance_m, 10.0f, 1e-3f);

    // Someone not listed, another connection's packet: no one is placed by them.
    t.on_position(step(9001, 0.0f, 0.0f), 2, at(16));
    t.on_position(step(227, 0.0f, 0.0f), 1, at(16));
    t.on_effect(effect_from(kMe, 9000.0f, 9000.0f), 3, at(16));
    TEST_ASSERT_NEAR(t.snapshot(at(17)).target.distance_m, 10.0f, 1e-3f);
}

TEST_CASE(CombatPower, WithNoPositionThereIsNoDistance) {
    // The meter started mid-map: no record of the local player's own.
    PowerTracker t = duel();
    t.on_position(step(227, 300.0f, 400.0f), 2, at(2));
    PowerSnapshot s = t.snapshot(at(3));
    TEST_ASSERT_FALSE(s.has_local_position);
    TEST_ASSERT_FALSE(s.players[0].has_distance());
    TEST_ASSERT(s.nearest.empty());
    // Once the local player acts, there is one.
    t.on_effect(effect_from(kMe, 0.0f, 0.0f), 2, at(4));
    s = t.snapshot(at(5));
    TEST_ASSERT_NEAR(s.players[0].distance_m, 5.0f, 1e-3f);
    // A player never placed has none, and comes last among the nearest.
    t.on_identity(player(300, "Zikel", 201'900), 2, at(6));
    s = t.snapshot(at(7));
    TEST_ASSERT_EQ(s.players[0].name, std::string("Zikel"));
    TEST_ASSERT_FALSE(s.players[0].has_distance());
    TEST_ASSERT_EQ(s.nearest.size(), size_t{2});
    TEST_ASSERT_EQ(s.nearest[0].name, std::string("Nezekan"));
    TEST_ASSERT_EQ(s.nearest[1].name, std::string("Zikel"));
}

TEST_CASE(CombatPower, OfACrowdTheNearestAreListedNearestFirst) {
    PowerTracker t = duel();
    t.on_effect(effect_from(kMe, 0.0f, 0.0f), 2, at(2));
    // A town: each one a metre farther than the last, the farthest the strongest.
    const size_t crowd = PowerTracker::kMaxListed + 8;
    for (size_t i = 0; i < crowd; ++i) {
        const auto id = static_cast<EntityId>(1000 + i);
        proto::Identity someone = player(id, ("Crowd" + std::to_string(i)).c_str(), static_cast<uint32_t>(50'000 + i));
        someone.has_position = true;
        someone.position = proto::Point{100.0f * static_cast<float>(i + 1), 0.0f, 2400.0f};
        t.on_identity(someone, 2, at(3));
    }
    PowerSnapshot s = t.snapshot(at(4));
    TEST_ASSERT_EQ(s.nearest.size(), PowerTracker::kMaxListed);
    TEST_ASSERT_EQ(s.nearest[0].name, std::string("Crowd0"));
    TEST_ASSERT_NEAR(s.nearest[0].distance_m, 1.0f, 1e-3f);
    TEST_ASSERT_NEAR(s.nearest.back().distance_m, static_cast<float>(PowerTracker::kMaxListed), 1e-3f);
    // The strongest are still the strongest: Nezekan, then the farthest.
    TEST_ASSERT_EQ(s.players[0].name, std::string("Nezekan"));
    TEST_ASSERT_EQ(s.players[1].name, "Crowd" + std::to_string(crowd - 1));
    // One of them walks up.
    t.on_position(step(static_cast<EntityId>(1000 + crowd - 1), 10.0f, 0.0f), 2, at(5));
    s = t.snapshot(at(6));
    TEST_ASSERT_EQ(s.nearest[0].name, "Crowd" + std::to_string(crowd - 1));
    TEST_ASSERT_NEAR(s.nearest[0].distance_m, 0.1f, 1e-3f);
}

TEST_CASE(CombatPower, AnotherMapForgetsWhereEveryoneStood) {
    PowerTracker t = duel();
    t.on_effect(effect_from(kMe, 0.0f, 0.0f), 2, at(2));
    t.on_position(step(227, 300.0f, 400.0f), 2, at(2));
    TEST_ASSERT(t.snapshot(at(3)).has_local_position);
    // The same connection, another map: the local player's old spot is gone, but
    // for a record of theirs that came just ahead of the load.
    t.on_map_load(proto::MapLoad{2, 1010}, 2, at(100));
    TEST_ASSERT_FALSE(t.snapshot(at(101)).has_local_position);
    t.on_effect(effect_from(kMe, 50.0f, 50.0f), 2, at(199));
    t.on_map_load(proto::MapLoad{3, 62}, 2, at(200));
    TEST_ASSERT(t.snapshot(at(201)).has_local_position);
    // A newer connection, and a new id on it: nothing carries over.
    t.on_identity(own(4987), 3, at(300));
    TEST_ASSERT_FALSE(t.snapshot(at(301)).has_local_position);
    // An id that passes to someone else does not keep the last holder's spot.
    t.on_effect(effect_from(4987, 0.0f, 0.0f), 3, at(302));
    proto::Identity first = player(700, "Lumiel", 90'000);
    first.has_position = true;
    first.position = proto::Point{500.0f, 0.0f, 2400.0f};
    t.on_identity(first, 3, at(303));
    TEST_ASSERT_NEAR(t.snapshot(at(304)).players[0].distance_m, 5.0f, 1e-3f);
    t.on_identity(player(700, "Marchutan", 60'000), 3, at(305));
    TEST_ASSERT_FALSE(t.snapshot(at(306)).players[0].has_distance());
}

TEST_CASE(PowerView, SortedByDistanceTheNearestAreListedWithTheirDistance) {
    PowerTracker t = duel();
    t.on_effect(effect_from(kMe, 0.0f, 0.0f), 2, at(2));
    t.on_position(step(227, 1200.0f, 0.0f), 2, at(2));
    proto::Identity strong = player(300, "Zikel", 201'900);
    strong.has_position = true;
    strong.position = proto::Point{4000.0f, 0.0f, 2400.0f};
    t.on_identity(strong, 2, at(2));
    app::PowerSettings settings;

    // Off: strongest first, and no distances drawn.
    app::PowerLayout layout = app::power_layout(t.snapshot(at(3)), settings);
    TEST_ASSERT_FALSE(layout.distances);
    TEST_ASSERT_EQ(layout.rows[0].name, std::string("Zikel"));
    // On: nearest first; the bars still measure against the strongest drawn.
    settings.sort_by_distance = true;
    layout = app::power_layout(t.snapshot(at(3)), settings);
    TEST_ASSERT(layout.distances);
    TEST_ASSERT_FALSE(layout.stale);
    TEST_ASSERT_EQ(layout.rows[0].name, std::string("Nezekan"));
    TEST_ASSERT_NEAR(layout.rows[0].distance_m, 12.0f, 1e-3f);
    TEST_ASSERT_EQ(layout.rows[1].name, std::string("Zikel"));
    TEST_ASSERT_EQ(layout.top, 201'900u);
    // Of more than the list holds, the nearest stay and the rest are counted.
    settings.list_max_players = 1;
    layout = app::power_layout(t.snapshot(at(3)), settings);
    TEST_ASSERT_EQ(layout.rows.size(), size_t{1});
    TEST_ASSERT_EQ(layout.rows[0].name, std::string("Nezekan"));
    TEST_ASSERT_EQ(layout.more, 1u);
    // The local player has not acted for a while: the same order, marked stale.
    settings.list_max_players = 8;
    layout = app::power_layout(t.snapshot(at(2) + app::kDistanceStaleAfter + 1), settings);
    TEST_ASSERT(layout.distances && layout.stale);
    // Their position not known at all: strongest first, as with the sort off.
    const PowerTracker lost = duel();
    layout = app::power_layout(lost.snapshot(at(3)), settings);
    TEST_ASSERT_FALSE(layout.distances);
    TEST_ASSERT_EQ(layout.rows.size(), size_t{1});
}

TEST_CASE(CombatPower, APlayerOfTheOtherFactionIsHostile) {
    PowerTracker t;
    t.on_map_load(proto::MapLoad{1, 20}, 2, at(0));
    // Before the local player's own record: no one can be called an enemy.
    t.on_identity(placed(300, "Zikel", 204'221, 2, 30.0f), 2, at(1));
    TEST_ASSERT_FALSE(t.snapshot(at(1)).players[0].hostile);
    TEST_ASSERT_EQ(t.snapshot(at(1)).hostile_in_view, 0u);
    t.on_identity(own(kMe), 2, at(2));
    t.on_identity(placed(301, "Lumiel", 90'000, 1, 5.0f), 2, at(2));
    t.on_identity(player(302, "Vaizel", 120'000), 2, at(2));  // faction not read
    const PowerSnapshot s = t.snapshot(at(3));
    TEST_ASSERT_EQ(s.hostile_in_view, 1u);
    TEST_ASSERT_EQ(s.players[0].name, std::string("Zikel"));
    TEST_ASSERT(s.players[0].hostile);
    TEST_ASSERT_FALSE(s.players[1].hostile);
    TEST_ASSERT_FALSE(s.players[2].hostile);
    // The target says so too.
    PowerTracker fight = t;
    fight.on_cast(press(kMe, 300), at(4));
    TEST_ASSERT(fight.snapshot(at(5)).target.hostile);

    // A member of the party is no enemy, whatever their faction; nor is anyone in
    // a dungeon, whose parties mix both.
    PowerTracker grouped = t;
    proto::PartyRoster roster;
    roster.members = {proto::PartyMember{"Ariel"}, proto::PartyMember{"Zikel"}};
    grouped.on_party(roster);
    TEST_ASSERT_FALSE(grouped.snapshot(at(4)).players[0].hostile);
    TEST_ASSERT_EQ(grouped.snapshot(at(4)).hostile_in_view, 0u);
    grouped.on_party(proto::PartyRoster{});
    TEST_ASSERT_EQ(grouped.snapshot(at(4)).hostile_in_view, 1u);
    PowerTracker instanced = t;
    instanced.on_map_load(proto::MapLoad{1, 600072}, 2, at(4));
    instanced.on_identity(placed(300, "Zikel", 204'221, 2, 30.0f), 2, at(5));
    TEST_ASSERT_FALSE(instanced.snapshot(at(6)).players[0].hostile);

    // The faction is the character's: it outlasts the map and its ids.
    t.on_values(proto::RemainingHp{4987, true, false, 0, false, 0}, 3);
    t.on_identity(placed(700, "Triniel", 50'000, 2, 10.0f), 3, at(60));
    TEST_ASSERT(t.snapshot(at(61)).players[0].hostile);
    // An id that passes to an ally is no enemy's any more.
    t.on_identity(placed(700, "Marchutan", 60'000, 1, 10.0f), 3, at(62));
    TEST_ASSERT_FALSE(t.snapshot(at(63)).players[0].hostile);
}

TEST_CASE(CombatPower, EnemiesLeadBothListsAndNoCrowdHidesOne) {
    PowerTracker t;
    t.on_map_load(proto::MapLoad{1, 20}, 2, at(0));
    proto::Identity me = own(kMe);
    me.has_position = true;
    me.position = proto::Point{0.0f, 0.0f, 2400.0f};
    t.on_identity(me, 2, at(1));
    // A crowd of allies, every one stronger and nearer than the two enemies.
    for (size_t i = 0; i < PowerTracker::kMaxListed + 6; ++i) {
        t.on_identity(placed(static_cast<EntityId>(1000 + i), ("Ally" + std::to_string(i)).c_str(),
                             static_cast<uint32_t>(200'000 + i), 1, static_cast<float>(i + 1)),
                      2, at(2));
    }
    t.on_identity(placed(300, "Zikel", 60'000, 2, 90.0f), 2, at(3));
    t.on_identity(placed(301, "Triniel", 80'000, 2, 120.0f), 2, at(3));
    const PowerSnapshot s = t.snapshot(at(4));
    TEST_ASSERT_EQ(s.hostile_in_view, 2u);
    TEST_ASSERT_EQ(s.players.size(), PowerTracker::kMaxListed);
    // Strongest first within each group.
    TEST_ASSERT_EQ(s.players[0].name, std::string("Triniel"));
    TEST_ASSERT_EQ(s.players[1].name, std::string("Zikel"));
    TEST_ASSERT_EQ(s.players[2].name, "Ally" + std::to_string(PowerTracker::kMaxListed + 5));
    // Nearest first within each group.
    TEST_ASSERT_EQ(s.nearest[0].name, std::string("Zikel"));
    TEST_ASSERT_NEAR(s.nearest[0].distance_m, 90.0f, 1e-3f);
    TEST_ASSERT_EQ(s.nearest[1].name, std::string("Triniel"));
    TEST_ASSERT_EQ(s.nearest[2].name, std::string("Ally0"));

    // One who has fallen out of reach keeps their mark and loses their lead.
    PowerTracker later = t;
    later.on_position(step(301, 100.0f * (PowerTracker::kEnemyRange + 50.0f), 0.0f), 2, at(5));
    const PowerSnapshot far = later.snapshot(at(6));
    TEST_ASSERT_EQ(far.hostile_in_view, 1u);
    TEST_ASSERT_EQ(far.nearest[0].name, std::string("Zikel"));
    TEST_ASSERT_EQ(far.nearest[1].name, std::string("Ally0"));
    TEST_ASSERT_EQ(far.players[0].name, std::string("Zikel"));
    TEST_ASSERT_FALSE(far.players[1].hostile);

    // The view keeps them first, or with the switch off puts all in one order.
    app::PowerSettings settings;
    settings.list_max_players = 3;
    app::PowerLayout layout = app::power_layout(s, settings);
    TEST_ASSERT_EQ(layout.rows[0].name, std::string("Triniel"));
    TEST_ASSERT(layout.rows[0].hostile && layout.rows[1].hostile && !layout.rows[2].hostile);
    settings.enemies_first = false;
    layout = app::power_layout(s, settings);
    TEST_ASSERT_FALSE(layout.rows[0].hostile);
    TEST_ASSERT_EQ(layout.rows[0].name, "Ally" + std::to_string(PowerTracker::kMaxListed + 5));
    settings.sort_by_distance = true;
    layout = app::power_layout(s, settings);
    TEST_ASSERT_EQ(layout.rows[0].name, std::string("Ally0"));
    settings.enemies_first = true;
    layout = app::power_layout(s, settings);
    TEST_ASSERT_EQ(layout.rows[0].name, std::string("Zikel"));
}

TEST_CASE(CombatPower, APlayerWhoLeavesViewLeavesTheList) {
    PowerTracker t = duel();
    t.on_identity(player(300, "Zikel", 90'000), 2, at(2));
    t.on_cast(press(kMe, 227), at(3));
    TEST_ASSERT_EQ(t.snapshot(at(4)).in_view, 2u);

    // Another connection's, and the local player's own, say nothing of this list.
    t.on_left_view(proto::LeftView{300, 1}, 1);
    t.on_left_view(proto::LeftView{kMe, 4}, 2);
    TEST_ASSERT_EQ(t.snapshot(at(4)).in_view, 2u);
    TEST_ASSERT_EQ(t.local(), kMe);

    t.on_left_view(proto::LeftView{300, 1}, 2);
    PowerSnapshot s = t.snapshot(at(5));
    TEST_ASSERT_EQ(s.in_view, 1u);
    TEST_ASSERT_EQ(s.players.size(), size_t{1});
    TEST_ASSERT_EQ(s.players[0].name, std::string("Nezekan"));
    // The target leaving takes the target with them.
    t.on_left_view(proto::LeftView{227, 10}, 2);
    s = t.snapshot(at(6));
    TEST_ASSERT_EQ(s.in_view, 0u);
    TEST_ASSERT_EQ(s.target.id, EntityId{0});
    // Back in view, their record comes again, and so do they.
    t.on_identity(player(300, "Zikel", 90'000), 2, at(20));
    TEST_ASSERT_EQ(t.snapshot(at(21)).players[0].name, std::string("Zikel"));
}

TEST_CASE(CombatPower, ADeadEnemyLeadsNothing) {
    PowerTracker t;
    t.on_map_load(proto::MapLoad{1, 20}, 2, at(0));
    proto::Identity me = own(kMe);
    me.has_position = true;
    me.position = proto::Point{0.0f, 0.0f, 2400.0f};
    t.on_identity(me, 2, at(1));
    t.on_identity(placed(300, "Zikel", 60'000, 2, 10.0f), 2, at(2));
    t.on_identity(placed(301, "Lumiel", 200'000, 1, 5.0f), 2, at(2));
    TEST_ASSERT_EQ(t.snapshot(at(3)).players[0].name, std::string("Zikel"));

    // HP 0: still listed, still marked, but no threat.
    t.on_values(proto::RemainingHp{300, false, true, 0, false, 0}, 2);
    PowerSnapshot s = t.snapshot(at(4));
    TEST_ASSERT_EQ(s.in_view, 2u);
    TEST_ASSERT_EQ(s.hostile_in_view, 0u);
    TEST_ASSERT_EQ(s.players[0].name, std::string("Lumiel"));
    TEST_ASSERT(s.players[1].dead);
    TEST_ASSERT(s.players[1].hostile);
    TEST_ASSERT_EQ(s.nearest[0].name, std::string("Lumiel"));
    // Another connection's HP is someone else's.
    t.on_values(proto::RemainingHp{300, false, true, 18'000, false, 0}, 1);
    TEST_ASSERT(t.snapshot(at(5)).players[1].dead);
    // Up again, they lead again.
    t.on_values(proto::RemainingHp{300, false, true, 18'000, false, 0}, 2);
    s = t.snapshot(at(6));
    TEST_ASSERT_EQ(s.hostile_in_view, 1u);
    TEST_ASSERT_EQ(s.players[0].name, std::string("Zikel"));
    TEST_ASSERT_FALSE(s.players[0].dead);
    // HP of someone not listed changes nothing, and names no local player.
    t.on_values(proto::RemainingHp{999, false, true, 0, false, 0}, 2);
    TEST_ASSERT_EQ(t.snapshot(at(7)).in_view, 2u);
    TEST_ASSERT_EQ(t.local(), kMe);
    // An id that passes to someone else starts alive.
    t.on_values(proto::RemainingHp{300, false, true, 0, false, 0}, 2);
    t.on_identity(placed(300, "Triniel", 70'000, 2, 10.0f), 2, at(8));
    TEST_ASSERT_FALSE(t.snapshot(at(9)).players[0].dead);
}

TEST_CASE(PowerView, FormatsADistanceInWholeMetres) {
    TEST_ASSERT_EQ(app::format_distance(-1.0f), std::string("-"));
    TEST_ASSERT_EQ(app::format_distance(0.2f), std::string("0 m"));
    TEST_ASSERT_EQ(app::format_distance(11.6f), std::string("12 m"));
    TEST_ASSERT_EQ(app::format_distance(1240.0f), std::string("1,240 m"));
}

TEST_CASE(PowerView, FormatsWithThousandsSeparators) {
    TEST_ASSERT_EQ(app::format_power(0), std::string("-"));
    TEST_ASSERT_EQ(app::format_power(999), std::string("999"));
    TEST_ASSERT_EQ(app::format_power(4'756), std::string("4,756"));
    TEST_ASSERT_EQ(app::format_power(134'667), std::string("134,667"));
    TEST_ASSERT_EQ(app::format_power(1'234'567), std::string("1,234,567"));
}

TEST_CASE(PowerView, ShortensTheOverlaysFiguresToThreeDigits) {
    TEST_ASSERT_EQ(app::format_power_short(0), std::string("-"));
    TEST_ASSERT_EQ(app::format_power_short(999), std::string("999"));
    TEST_ASSERT_EQ(app::format_power_short(4'756), std::string("4.76k"));
    TEST_ASSERT_EQ(app::format_power_short(9'996), std::string("10.0k"));
    TEST_ASSERT_EQ(app::format_power_short(98'300), std::string("98.3k"));
    TEST_ASSERT_EQ(app::format_power_short(99'960), std::string("100k"));
    TEST_ASSERT_EQ(app::format_power_short(100'000), std::string("100k"));
    TEST_ASSERT_EQ(app::format_power_short(134'667), std::string("135k"));
    TEST_ASSERT_EQ(app::format_power_short(999'600), std::string("1.00M"));
    TEST_ASSERT_EQ(app::format_power_short(1'234'567), std::string("1.23M"));
}

TEST_CASE(PowerView, TheListMarksTheTargetOrShowsItAboveTheStrongest) {
    PowerTracker t = duel();
    t.on_identity(player(300, "Zikel", 201'900), 2, at(2));
    t.on_cast(press(kMe, 227), at(3));
    const PowerSnapshot s = t.snapshot(at(4));
    app::PowerSettings settings;

    app::PowerLayout layout = app::power_layout(s, settings);
    TEST_ASSERT_EQ(layout.rows.size(), size_t{2});
    TEST_ASSERT_EQ(layout.target, EntityId{227});
    TEST_ASSERT_EQ(layout.target_line.id, EntityId{0});  // marked in its row instead
    TEST_ASSERT_EQ(layout.top, 201'900u);

    TEST_ASSERT_EQ(layout.more, 0u);

    // More players than the setting lists: the strongest, a count of the rest, and
    // the target on a line of their own when they are not among them.
    settings.list_max_players = 1;
    layout = app::power_layout(s, settings);
    TEST_ASSERT_EQ(layout.rows.size(), size_t{1});
    TEST_ASSERT_EQ(layout.rows[0].name, std::string("Zikel"));
    TEST_ASSERT_EQ(layout.more, 1u);
    TEST_ASSERT_EQ(layout.target, EntityId{0});
    TEST_ASSERT_EQ(layout.target_line.id, EntityId{227});
    TEST_ASSERT_EQ(layout.top, 201'900u);
    TEST_ASSERT_FALSE(layout.empty());
    // The list switched off: the target alone.
    settings = app::PowerSettings{};
    settings.show_list = false;
    layout = app::power_layout(s, settings);
    TEST_ASSERT(layout.rows.empty());
    TEST_ASSERT_EQ(layout.more, 0u);
    TEST_ASSERT_EQ(layout.target_line.name, std::string("Nezekan"));
    TEST_ASSERT_EQ(layout.top, 134'667u);
    // With no target, a crowd still lists its strongest.
    PowerSnapshot crowd = s;
    crowd.target = PowerRow{};
    crowd.in_view = 40;
    layout = app::power_layout(crowd, app::PowerSettings{});
    TEST_ASSERT_EQ(layout.rows.size(), size_t{2});
    TEST_ASSERT_EQ(layout.more, 38u);

    // Nothing to draw before anyone is seen.
    TEST_ASSERT(app::power_layout(PowerSnapshot{}, app::PowerSettings{}).empty());
}
