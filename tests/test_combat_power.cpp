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

TEST_CASE(PowerView, FormatsWithThousandsSeparators) {
    TEST_ASSERT_EQ(app::format_power(0), std::string("-"));
    TEST_ASSERT_EQ(app::format_power(999), std::string("999"));
    TEST_ASSERT_EQ(app::format_power(4'756), std::string("4,756"));
    TEST_ASSERT_EQ(app::format_power(134'667), std::string("134,667"));
    TEST_ASSERT_EQ(app::format_power(1'234'567), std::string("1,234,567"));
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
