#include "test_framework.hpp"
#include "file_test_support.hpp"
#include "synthetic_session.hpp"

#include "app/app_runtime.hpp"
#include "app/app_settings.hpp"
#include "app/aura_view.hpp"
#include "app/overlay_visibility.hpp"
#include "app/display_format.hpp"
#include "app/history_list.hpp"
#include "app/timeline_chart.hpp"
#include "capture/capture_file.hpp"
#include "capture/file_source.hpp"
#include "capture/os/npcap_source.hpp"
#include "capture/tcp_reassembler.hpp"
#include "common/time_format.hpp"
#include "meter/fight_detail.hpp"
#include "meter/history_store.hpp"
#include "meter/pull_grouping.hpp"
#include "protocol/entity_id.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace aion2;
using namespace aion2::app;
using namespace aion2::test::file_support;

namespace {

/// A source that notes whether anything read it after it was stopped.
class WatchedSource final : public capture::PacketSource {
public:
    bool start(Push, std::string&) override { return true; }
    void stop() override { m_stopped = true; }
    [[nodiscard]] Micros now() const override {
        check();
        return kMicrosPerSecond;
    }
    [[nodiscard]] uint64_t dropped() const override {
        check();
        return 0;
    }
    [[nodiscard]] std::string status() const override {
        check();
        return "watched";
    }
    [[nodiscard]] bool read_after_stop() const { return m_late.load(); }

private:
    void check() const {
        if (m_stopped.load()) m_late = true;
    }
    std::atomic<bool> m_stopped{false};
    mutable std::atomic<bool> m_late{false};
};

size_t count_packets(const std::filesystem::path& file) {
    std::string error;
    auto reader = capture::CaptureFileReader::open(file, error);
    if (!reader) return 0;
    size_t n = 0;
    capture::CapturedPacket pkt;
    while (reader->next(pkt)) ++n;
    return n;
}

} // namespace

TEST_CASE(AppSettings, RoundTripAndSanitize) {
    AppSettings s;
    s.capture.game_process = "Aion2Test.exe";
    s.overlay.opacity = 0.5f;
    s.overlay.x = 120;
    s.overlay.show_deaths = false;
    s.meter.boss_min_max_hp = 2'500'000;
    s.history.max_fights = 42;
    s.window.close_to_tray = false;
    s.window.check_for_updates = false;
    s.window.x = -1200;
    s.window.width = 1280;
    s.window.fight_list_width = 320;
    const AppSettings back = settings_from_json(settings_to_json(s));
    TEST_ASSERT_FALSE(back.window.close_to_tray);
    TEST_ASSERT_FALSE(back.window.check_for_updates);
    TEST_ASSERT_EQ(back.window.x, -1200);
    TEST_ASSERT_EQ(back.window.width, 1280);
    TEST_ASSERT_EQ(back.window.fight_list_width, 320);
    // Unset, the saved-fights list fits the window.
    TEST_ASSERT_EQ(AppSettings{}.window.fight_list_width, 0);
    TEST_ASSERT_EQ(back.capture.game_process, std::string("Aion2Test.exe"));
    TEST_ASSERT_NEAR(back.overlay.opacity, 0.5f, 1e-6f);
    TEST_ASSERT_EQ(back.overlay.x, 120);
    TEST_ASSERT_FALSE(back.overlay.show_deaths);
    TEST_ASSERT(AppSettings{}.overlay.show_deaths);
    TEST_ASSERT_EQ(back.meter.boss_min_max_hp, uint64_t{2'500'000});
    TEST_ASSERT_EQ(back.history.max_fights, 42);

    config::JsonValue bad = settings_to_json(s);
    bad["capture"]["game_process"] = "..\\..\\evil.exe";
    bad["overlay"]["scale"] = 99.0;
    bad["capture"]["server_port"] = 70000;
    bad["app"]["fight_list_width"] = 99999;
    const AppSettings clean = settings_from_json(bad);
    TEST_ASSERT_EQ(clean.capture.game_process, std::string("Aion2.exe"));
    TEST_ASSERT_NEAR(clean.overlay.scale, 2.5f, 1e-6f);
    TEST_ASSERT_EQ(clean.capture.server_port, 65535);
    TEST_ASSERT_EQ(clean.window.fight_list_width, 2000);
    bad["app"]["fight_list_width"] = -5;
    TEST_ASSERT_EQ(settings_from_json(bad).window.fight_list_width, 0);
}

TEST_CASE(AppSettings, AurasRoundTripWithTheirWatchList) {
    AppSettings s;
    s.plugins.auras = true;
    s.auras.locked = true;
    s.auras.hide_when_game_unfocused = true;
    s.auras.x = 300;
    s.auras.min_duration_s = 2.5f;
    s.auras.watch = {18'190'000, 3'539'981'065u};
    s.auras.hidden = {12'240'000};
    s.auras.icons = true;
    s.auras.icon_size = 48;
    s.auras.show_chains = false;
    s.auras.show_ready = false;
    s.auras.low_hp_alert = false;
    s.auras.low_hp_percent = 45;
    const AppSettings back = settings_from_json(settings_to_json(s));
    TEST_ASSERT(back.plugins.auras);
    TEST_ASSERT(back.auras.locked);
    TEST_ASSERT(back.auras.hide_when_game_unfocused);
    TEST_ASSERT_FALSE(back.auras.show_in_dungeons_only);
    TEST_ASSERT_EQ(back.auras.x, 300);
    TEST_ASSERT_NEAR(back.auras.min_duration_s, 2.5f, 1e-6f);
    TEST_ASSERT_EQ(back.auras.watch.size(), size_t{2});
    TEST_ASSERT_EQ(back.auras.watch[1], uint32_t{3'539'981'065u});
    TEST_ASSERT(back.auras.hidden == std::vector<uint32_t>{12'240'000});
    TEST_ASSERT(back.auras.icons);
    TEST_ASSERT_EQ(back.auras.icon_size, 48);
    TEST_ASSERT_FALSE(back.auras.show_chains);
    TEST_ASSERT_FALSE(back.auras.show_ready);
    TEST_ASSERT_FALSE(back.auras.low_hp_alert);
    TEST_ASSERT_EQ(back.auras.low_hp_percent, 45);

    // A hand-edited list keeps only whole u32 codes, each once.
    config::JsonValue bad = settings_to_json(s);
    config::JsonValue::ArrayType watch;
    for (const config::JsonValue& v : {config::JsonValue(18'190'000), config::JsonValue(18'190'000),
                                       config::JsonValue(-4), config::JsonValue(1.5), config::JsonValue("x"),
                                       config::JsonValue(5e12)}) {
        watch.push_back(v);
    }
    bad["auras"]["watch"] = config::JsonValue(std::move(watch));
    bad["auras"]["min_duration_s"] = 1000.0;
    bad["auras"]["icon_size"] = 500.0;
    bad["auras"]["low_hp_percent"] = 0.0;
    const AppSettings clean = settings_from_json(bad);
    TEST_ASSERT_EQ(clean.auras.watch.size(), size_t{1});
    TEST_ASSERT_NEAR(clean.auras.min_duration_s, 60.0f, 1e-6f);
    TEST_ASSERT_EQ(clean.auras.icon_size, kMaxAuraIcon);
    TEST_ASSERT_EQ(clean.auras.low_hp_percent, 5);

    TEST_ASSERT_FALSE(AppSettings{}.plugins.auras);
}

TEST_CASE(AppSettings, EachPluginsSwitchLivesInItsSection) {
    AppSettings s;
    TEST_ASSERT(s.plugins.combat_meter);
    s.plugins.combat_meter = false;
    s.plugins.auras = true;
    const config::JsonValue json = settings_to_json(s);
    TEST_ASSERT_FALSE(json["meter"]["plugin_enabled"].as_bool(true));
    TEST_ASSERT(json["auras"]["plugin_enabled"].as_bool(false));
    // The aura overlay's old switch is no longer written.
    TEST_ASSERT_FALSE(json["auras"].contains("visible"));
    const AppSettings back = settings_from_json(json);
    TEST_ASSERT_FALSE(back.plugins.combat_meter);
    TEST_ASSERT(back.plugins.auras);
}

TEST_CASE(AppSettings, AuraOverlayOnInAnOlderFileTurnsThePluginOn) {
    // 0.2.0 wrote auras.visible, the overlay's switch, and no plugin_enabled.
    config::JsonValue old_file;
    old_file["auras"]["visible"] = true;
    TEST_ASSERT(settings_from_json(old_file).plugins.auras);
    old_file["auras"]["visible"] = false;
    TEST_ASSERT_FALSE(settings_from_json(old_file).plugins.auras);
    // plugin_enabled, once written, wins.
    old_file["auras"]["visible"] = true;
    old_file["auras"]["plugin_enabled"] = false;
    TEST_ASSERT_FALSE(settings_from_json(old_file).plugins.auras);
    // Nothing at all: the meter on, the auras off.
    const AppSettings fresh = settings_from_json(config::JsonValue{});
    TEST_ASSERT(fresh.plugins.combat_meter);
    TEST_ASSERT_FALSE(fresh.plugins.auras);
}

TEST_CASE(AuraView, FiltersAndTheMissingWatchList) {
    const Micros now = 1'792'152'000'000'000ULL;
    auras::AuraSnapshot a;
    auras::AuraEntry mantra;
    mantra.key = 18'190'000;
    mantra.label = "Undefeated Mantra";
    mantra.duration_ms = 2400;
    mantra.expires_at = now + 1'500'000;
    auras::AuraEntry mark = mantra;
    mark.key = 200;
    mark.duration_ms = 300;
    mark.expires_at = now + 200'000;
    auras::AuraEntry passive = mantra;
    passive.key = 12'710'000;
    passive.permanent = true;
    passive.expires_at = 0;
    auras::AuraEntry debuff = mantra;
    debuff.key = 20'111'011;
    debuff.hostile = true;
    debuff.duration_ms = 180'000;
    debuff.expires_at = now + 100'000'000;
    a.on_self = {mark, mantra, debuff, passive};
    a.seen = {auras::SeenEffect{18'190'000, "Undefeated Mantra"}, auras::SeenEffect{12'000'000, "Dodge"}};

    AuraSettings s;
    TEST_ASSERT_EQ(shown_auras(a.on_self, s, now, true).size(), size_t{2});  // not the mark, not the passive
    s.show_permanent = true;
    s.show_debuffs = false;
    const auto shown = shown_auras(a.on_self, s, now, true);
    TEST_ASSERT_EQ(shown.size(), size_t{2});
    TEST_ASSERT_EQ(shown[1]->key, uint32_t{12'710'000});
    // An enemy's effect on the boss side is not the local player's to hide.
    TEST_ASSERT_EQ(shown_auras({debuff}, s, now, false).size(), size_t{1});
    // The hide list leaves an effect out wherever it is.
    s.hidden = {20'111'011, 12'710'000};
    TEST_ASSERT_EQ(shown_auras(a.on_self, s, now, true).size(), size_t{1});
    TEST_ASSERT(shown_auras({debuff}, s, now, false).empty());

    // Watched: the mantra is on; Dodge's and an unknown effect are missing.
    const auto missing = missing_effects(a, {18'190'000, 12'000'000, 777}, now);
    TEST_ASSERT_EQ(missing.size(), size_t{2});
    TEST_ASSERT_EQ(missing[0].label, std::string("Dodge"));
    TEST_ASSERT_EQ(missing[1].label, std::string("Effect 777"));
    // Once its time is up between snapshots, the mantra counts as missing too.
    TEST_ASSERT_EQ(missing_effects(a, {18'190'000}, now + 2'000'000).size(), size_t{1});

    TEST_ASSERT_EQ(aura_display_time(now, 100'000), now + 100'000);
    TEST_ASSERT_EQ(aura_display_time(now, 5'000'000), now + kAuraDisplayLead);
    TEST_ASSERT_EQ(format_time_left(125 * kMicrosPerSecond), std::string("2m05"));
    TEST_ASSERT_EQ(format_time_left(12'700'000), std::string("12"));
    TEST_ASSERT_EQ(format_time_left(4'560'000), std::string("4.5"));
}

TEST_CASE(AuraView, AWatchedEffectNotSeenYetIsNamedAndDrawnByItsKey) {
    // After a restart, before the scroll is on: it read "Effect 22101031", with no
    // tile as icons, though effects.tsv names it and the sheet has the scroll. A
    // monster's Burn read as the skill its ten thousands point to, Dodge.
    const auras::AuraSnapshot none;
    const auto missing = missing_effects(none, {22'101'031, 12'000'121, 12'350'000, 777}, 1'000'000);
    TEST_ASSERT_EQ(missing.size(), size_t{4});
    TEST_ASSERT_EQ(missing[0].label, std::string("Speed Scroll"));
    TEST_ASSERT(missing[0].icon != kNoIcon);
    TEST_ASSERT_EQ(missing[0].icon, effect_icon(22'101'031));
    TEST_ASSERT_EQ(missing[1].label, std::string("Burn"));
    TEST_ASSERT_EQ(missing[2].label, std::string("Warding Strike"));
    TEST_ASSERT(missing[2].icon != kNoIcon);
    TEST_ASSERT_EQ(missing[2].icon, skill_icon(12'350'000));
    TEST_ASSERT_EQ(missing[3].label, std::string("Effect 777"));
    TEST_ASSERT_EQ(missing[3].icon, kNoIcon);
}

TEST_CASE(AuraView, CooldownsLongEnoughAndRunning) {
    const Micros now = 1'792'152'000'000'000ULL;
    std::vector<auras::CooldownEntry> c(3);
    c[0].label = "Dodge";
    c[0].duration_ms = 500;
    c[0].ready_at = now + 300'000;
    c[1].label = "Warding Strike";
    c[1].duration_ms = 23'100;
    c[1].ready_at = now + 12'000'000;
    c[2].label = "Shield Smite";
    c[2].duration_ms = 8'850;
    c[2].ready_at = now - kReadyFlash;  // ready, and no longer new
    AuraSettings s;
    const auto shown = shown_cooldowns(c, s, now);
    TEST_ASSERT_EQ(shown.size(), size_t{1});
    TEST_ASSERT_EQ(shown[0]->label, std::string("Warding Strike"));
    s.min_cooldown_s = 0.0f;
    TEST_ASSERT_EQ(shown_cooldowns(c, s, now).size(), size_t{2});
    s.show_cooldowns = false;
    TEST_ASSERT(shown_cooldowns(c, s, now).empty());
}

TEST_CASE(AuraView, ACooldownComingBackShowsReady) {
    const Micros now = 1'792'152'000'000'000ULL;
    std::vector<auras::CooldownEntry> c(3);
    c[0].skill = 12'350'000;  // back a moment ago
    c[0].duration_ms = 23'100;
    c[0].ready_at = now - 400'000;
    c[1].skill = 12'100'000;  // cooling
    c[1].duration_ms = 8'850;
    c[1].ready_at = now + 2'000'000;
    c[2].skill = 12'040'000;  // back long ago
    c[2].duration_ms = 12'000;
    c[2].ready_at = now - 60'000'000;
    TEST_ASSERT(just_ready(c[0], now));
    TEST_ASSERT_FALSE(just_ready(c[1], now));
    TEST_ASSERT_FALSE(just_ready(c[2], now));
    TEST_ASSERT_FALSE(just_ready(c[0], now - 500'000));  // not back yet

    // Bars: the cooling one, and the one just back, in the snapshot's order.
    AuraSettings s;
    auto shown = shown_cooldowns(c, s, now);
    TEST_ASSERT_EQ(shown.size(), size_t{2});
    TEST_ASSERT(shown[0] == &c[0] && shown[1] == &c[1]);
    s.show_ready = false;
    TEST_ASSERT_EQ(shown_cooldowns(c, s, now).size(), size_t{1});

    // Icons: every one, ready or not, by skill code, so each keeps its place.
    s.icons = true;
    s.show_ready = true;
    shown = shown_cooldowns(c, s, now);
    TEST_ASSERT_EQ(shown.size(), size_t{3});
    TEST_ASSERT(shown[0] == &c[2] && shown[1] == &c[1] && shown[2] == &c[0]);
    s.show_ready = false;
    TEST_ASSERT_EQ(shown_cooldowns(c, s, now).size(), size_t{1});
}

TEST_CASE(AuraView, LowHpUnderTheThresholdOnly) {
    AuraSettings s;  // 30%
    auras::AuraSnapshot a;
    a.hp = 6'000;
    TEST_ASSERT(!low_hp_percent(a, s).has_value());  // max not known yet
    a.max_hp = 25'373;
    TEST_ASSERT_EQ(low_hp_percent(a, s).value_or(-1), 23);
    a.hp = 7'612;  // 30.0%: not under
    TEST_ASSERT(!low_hp_percent(a, s).has_value());
    a.hp = 10;
    TEST_ASSERT_EQ(low_hp_percent(a, s).value_or(-1), 1);  // never reads 0 while alive
    a.hp = 0;
    TEST_ASSERT(!low_hp_percent(a, s).has_value());  // dead: nothing to warn of
    a.hp = 6'000;
    s.low_hp_percent = 20;
    TEST_ASSERT(!low_hp_percent(a, s).has_value());
    s.low_hp_percent = 30;
    s.low_hp_alert = false;
    TEST_ASSERT(!low_hp_percent(a, s).has_value());
}

TEST_CASE(AuraView, TilesWrapAcrossTheWindow) {
    TEST_ASSERT(tile_layout(264.0f, 36.0f, 4.0f, 7) == (TileLayout{6, 2}));  // 6 * 36 + 5 * 4 = 236
    TEST_ASSERT(tile_layout(276.0f, 36.0f, 4.0f, 7) == (TileLayout{7, 1}));  // exactly 7
    TEST_ASSERT(tile_layout(20.0f, 36.0f, 4.0f, 3) == (TileLayout{1, 3}));   // narrower than one
    TEST_ASSERT(tile_layout(264.0f, 36.0f, 4.0f, 0) == (TileLayout{6, 0}));
    TEST_ASSERT_EQ(tile_initials("Delayed Explosion"), std::string("DE"));
    TEST_ASSERT_EQ(tile_initials("poison"), std::string("Po"));
    // An effect with no name: the "E" of "Effect" would say nothing.
    TEST_ASSERT_EQ(tile_initials("Effect 2920107"), std::string("?"));
    TEST_ASSERT_EQ(tile_initials("NPC #777"), std::string("?"));
    TEST_ASSERT_EQ(tile_initials("Effect of Light"), std::string("Eo"));
    TEST_ASSERT_EQ(tile_initials(""), std::string("?"));
    // An icon has room for less than a bar.
    TEST_ASSERT_EQ(format_tile_time(82 * kMicrosPerSecond), std::string("1m"));
    TEST_ASSERT_EQ(format_tile_time(17'400'000), std::string("17"));
    TEST_ASSERT_EQ(format_tile_time(3'850'000), std::string("3.8"));
}

TEST_CASE(OverlayVisibility, AurasFollowTheirPluginAndTheirOwnConditions) {
    AppSettings s;
    AuraSettings& a = s.auras;
    OverlayConditions now;
    TEST_ASSERT_FALSE(auras_shown(s, now));
    s.plugins.auras = true;
    TEST_ASSERT(auras_shown(s, now));
    // The meter overlay's conditions are the meter's.
    s.overlay.hide_when_game_unfocused = true;
    s.overlay.show_in_combat_only = true;
    s.overlay.locked = true;
    a.locked = true;
    now.game_in_front = false;
    TEST_ASSERT(auras_shown(s, now));
    a.hide_when_game_unfocused = true;
    TEST_ASSERT_FALSE(auras_shown(s, now));
    a.locked = false;
    TEST_ASSERT(auras_shown(s, now));  // unlocked: always findable
    a.locked = true;
    now.game_in_front = true;
    TEST_ASSERT(auras_shown(s, now));
    a.show_in_dungeons_only = true;
    TEST_ASSERT_FALSE(auras_shown(s, now));
    now.in_dungeon = true;
    TEST_ASSERT(auras_shown(s, now));
    // The meter switched off leaves the auras alone, and the other way round.
    s.plugins.combat_meter = false;
    TEST_ASSERT(auras_shown(s, now));
    TEST_ASSERT_FALSE(overlay_shown(s, now));
}

TEST_CASE(AppSettings, PingRoundTripsAndKeepsItsThresholdsInOrder) {
    TEST_ASSERT_FALSE(AppSettings{}.plugins.ping);
    AppSettings s;
    s.plugins.ping = true;
    s.ping.locked = true;
    s.ping.x = 900;
    s.ping.width = 260;
    s.ping.show_jitter = false;
    s.ping.show_graph = false;
    s.ping.warn_ms = 90;
    s.ping.bad_ms = 150;
    s.ping.show_in_dungeons_only = true;
    const config::JsonValue json = settings_to_json(s);
    TEST_ASSERT(json["ping"]["plugin_enabled"].as_bool(false));
    const AppSettings back = settings_from_json(json);
    TEST_ASSERT(back.plugins.ping);
    TEST_ASSERT(back.ping.locked);
    TEST_ASSERT_EQ(back.ping.x, 900);
    TEST_ASSERT_EQ(back.ping.width, 260);
    TEST_ASSERT_FALSE(back.ping.show_jitter);
    TEST_ASSERT_FALSE(back.ping.show_graph);
    TEST_ASSERT_EQ(back.ping.warn_ms, 90);
    TEST_ASSERT_EQ(back.ping.bad_ms, 150);
    TEST_ASSERT(back.ping.show_in_dungeons_only);
    TEST_ASSERT_FALSE(back.ping.hide_when_game_unfocused);
    // The other plugins' switches are their own.
    TEST_ASSERT(back.plugins.combat_meter);
    TEST_ASSERT_FALSE(back.plugins.auras);

    config::JsonValue bad = json;
    bad["ping"]["warn_ms"] = 400.0;
    bad["ping"]["bad_ms"] = 100.0;
    bad["ping"]["width"] = 5.0;
    bad["ping"]["scale"] = 9.0;
    const AppSettings clean = settings_from_json(bad);
    TEST_ASSERT_EQ(clean.ping.warn_ms, 400);
    TEST_ASSERT_EQ(clean.ping.bad_ms, 400);  // red is never under amber
    TEST_ASSERT_EQ(clean.ping.width, 160);
    TEST_ASSERT_NEAR(clean.ping.scale, 2.5f, 1e-6f);
    bad["ping"]["warn_ms"] = 0.0;
    bad["ping"]["bad_ms"] = 99999.0;
    TEST_ASSERT_EQ(settings_from_json(bad).ping.warn_ms, kMinPingThreshold);
    TEST_ASSERT_EQ(settings_from_json(bad).ping.bad_ms, kMaxPingThreshold);
}

TEST_CASE(OverlayVisibility, PingFollowsItsPluginAndItsOwnConditions) {
    AppSettings s;
    PingSettings& p = s.ping;
    OverlayConditions now;
    TEST_ASSERT_FALSE(ping_shown(s, now));
    s.plugins.ping = true;
    TEST_ASSERT(ping_shown(s, now));
    // The other overlays' conditions are theirs.
    s.overlay.locked = true;
    s.overlay.show_in_combat_only = true;
    s.auras.locked = true;
    s.auras.hide_when_game_unfocused = true;
    p.locked = true;
    now.game_in_front = false;
    TEST_ASSERT(ping_shown(s, now));
    p.hide_when_game_unfocused = true;
    TEST_ASSERT_FALSE(ping_shown(s, now));
    p.locked = false;
    TEST_ASSERT(ping_shown(s, now));  // unlocked: always findable
    p.locked = true;
    now.game_in_front = true;
    p.show_in_dungeons_only = true;
    TEST_ASSERT_FALSE(ping_shown(s, now));
    now.in_dungeon = true;
    TEST_ASSERT(ping_shown(s, now));
    // The meter switched off leaves it alone.
    s.plugins.combat_meter = false;
    TEST_ASSERT(ping_shown(s, now));
    TEST_ASSERT_FALSE(overlay_shown(s, now));
}

TEST_CASE(AppSettings, CombatPowerSettingsRoundTripAndAreSanitized) {
    AppSettings s;
    TEST_ASSERT_FALSE(s.plugins.combat_power);  // off until asked for
    s.plugins.combat_power = true;
    s.power.locked = true;
    s.power.x = 700;
    s.power.width = 300;
    s.power.show_list = false;
    s.power.list_max_players = 4;
    TEST_ASSERT_FALSE(s.power.sort_by_distance);  // strongest first until asked
    s.power.sort_by_distance = true;
    TEST_ASSERT(s.power.enemies_first);  // on until switched off
    s.power.enemies_first = false;
    TEST_ASSERT_FALSE(s.power.only_enemies);  // everyone until asked
    s.power.only_enemies = true;
    s.power.hide_when_game_unfocused = true;
    TEST_ASSERT_FALSE(s.power.hide_when_empty);
    s.power.hide_when_empty = true;
    const config::JsonValue json = settings_to_json(s);
    TEST_ASSERT(json["combat_power"]["plugin_enabled"].as_bool(false));
    const AppSettings back = settings_from_json(json);
    TEST_ASSERT(back.plugins.combat_power);
    TEST_ASSERT(back.power.locked);
    TEST_ASSERT_EQ(back.power.x, 700);
    TEST_ASSERT_EQ(back.power.width, 300);
    TEST_ASSERT_FALSE(back.power.show_list);
    TEST_ASSERT_EQ(back.power.list_max_players, 4);
    TEST_ASSERT(back.power.sort_by_distance);
    TEST_ASSERT_FALSE(back.power.enemies_first);
    TEST_ASSERT(back.power.only_enemies);
    TEST_ASSERT(back.power.hide_when_game_unfocused);
    TEST_ASSERT(back.power.hide_when_empty);
    TEST_ASSERT_FALSE(back.power.show_in_dungeons_only);
    // The other plugins' switches are their own.
    TEST_ASSERT_FALSE(back.plugins.ping);

    config::JsonValue bad = json;
    bad["combat_power"]["list_max_players"] = 500.0;
    bad["combat_power"]["width"] = 5.0;
    bad["combat_power"]["opacity"] = 7.0;
    const AppSettings clean = settings_from_json(bad);
    TEST_ASSERT_EQ(clean.power.list_max_players, kMaxPowerList);
    TEST_ASSERT_EQ(clean.power.width, 160);
    TEST_ASSERT_NEAR(clean.power.opacity, 1.0f, 1e-6f);
    bad["combat_power"]["list_max_players"] = 0.0;
    TEST_ASSERT_EQ(settings_from_json(bad).power.list_max_players, kMinPowerList);
}

TEST_CASE(OverlayVisibility, CombatPowerFollowsItsPluginAndItsOwnConditions) {
    AppSettings s;
    OverlayConditions now;
    now.game_in_front = false;
    TEST_ASSERT_FALSE(power_shown(s, now));  // the plugin is off by default
    s.plugins.combat_power = true;
    TEST_ASSERT(power_shown(s, now));
    s.power.hide_when_game_unfocused = true;
    TEST_ASSERT(power_shown(s, now));  // unlocked: always findable
    s.power.locked = true;
    TEST_ASSERT_FALSE(power_shown(s, now));
    now.game_in_front = true;
    TEST_ASSERT(power_shown(s, now));
    // An arena is no dungeon: the switch is there, and off by default.
    s.power.show_in_dungeons_only = true;
    TEST_ASSERT_FALSE(power_shown(s, now));
    now.in_dungeon = true;
    TEST_ASSERT(power_shown(s, now));
    // No one to show: hidden only when asked, and only while locked.
    now.power_empty = true;
    TEST_ASSERT(power_shown(s, now));
    s.power.hide_when_empty = true;
    TEST_ASSERT_FALSE(power_shown(s, now));
    s.power.locked = false;
    TEST_ASSERT(power_shown(s, now));
    s.power.locked = true;
    now.power_empty = false;
    TEST_ASSERT(power_shown(s, now));
    // The meter switched off leaves it alone.
    s.plugins.combat_meter = false;
    TEST_ASSERT(power_shown(s, now));
}

TEST_CASE(Plugins, EverySwitchIsReachedByItsId) {
    aion2::plugins::PluginSwitches on{false, false, false, false};
    for (const auto& d : aion2::plugins::ALL) {
        TEST_ASSERT_FALSE(on.enabled(d.id));
        on.set(d.id, true);
        TEST_ASSERT(on.enabled(d.id));
        TEST_ASSERT(aion2::plugins::find(d.id).id == d.id);
    }
    TEST_ASSERT(on == aion2::plugins::ALL_ON);
    TEST_ASSERT_EQ(aion2::plugins::ALL.size(), size_t{4});
}

TEST_CASE(AppSettings, MissingFileGivesDefaults) {
    const AppSettings s = load_settings("/nonexistent/aion2/config.json");
    TEST_ASSERT_EQ(s.capture.server_port, 13328);
    TEST_ASSERT(s.overlay.visible);
    TEST_ASSERT(s.window.check_for_updates);
}

TEST_CASE(TimeFormat, StampsAndClocks) {
    const Micros t = 1'792'152'000'000'000ULL + 5 * kMicrosPerSecond;
    TEST_ASSERT_EQ(common::utc_file_stamp(t), std::string("20261016-120005Z"));
    TEST_ASSERT_EQ(common::clock_duration(95.4), std::string("1:35"));
    TEST_ASSERT_EQ(common::clock_duration(3723), std::string("1:02:03"));
    const auto leap = common::utc_time(1'709'208'000'000'000ULL);  // 2024-02-29 12:00:00Z
    TEST_ASSERT_EQ(leap.month, 2u);
    TEST_ASSERT_EQ(leap.day, 29u);
    TEST_ASSERT_EQ(common::clock_duration(std::nan("")), std::string("0:00"));
    TEST_ASSERT_EQ(common::clock_duration(-4.0), std::string("0:00"));
}

TEST_CASE(TimeFormat, CivilDays) {
    const auto epoch = common::civil_day(1970, 1, 1);
    TEST_ASSERT_EQ(epoch.serial, int64_t{0});
    TEST_ASSERT_EQ(epoch.weekday, 4u);  // a Thursday
    const auto before = common::civil_day(1969, 12, 31);
    TEST_ASSERT_EQ(before.serial, int64_t{-1});
    TEST_ASSERT_EQ(before.weekday, 3u);
    // The tests' own time, 2026-10-16 12:00Z, is that many whole days on.
    TEST_ASSERT_EQ(common::civil_day(2026, 10, 16).serial,
                   static_cast<int64_t>(1'792'152'000'000'000ULL / kMicrosPerSecond / 86'400));
    const auto leap = common::civil_day(2024, 2, 29);
    TEST_ASSERT_EQ(common::civil_day(2024, 3, 1).serial, leap.serial + 1);
    TEST_ASSERT_EQ(common::civil_day(2026, 11, 24).weekday, 2u);  // a Tuesday
}

TEST_CASE(TimeFormat, FromSecondsGuards) {
    TEST_ASSERT_EQ(from_seconds(1.5), Micros{1'500'000});
    TEST_ASSERT_EQ(from_seconds(std::nan("")), Micros{0});
    TEST_ASSERT_EQ(from_seconds(-1.0), Micros{0});
    TEST_ASSERT_EQ(from_seconds(1e300), kForever);
}

TEST_CASE(NpcapSource, Filters) {
    TEST_ASSERT_EQ(capture::NpcapSource::adapter_filter(13328), std::string("tcp and src port 13328"));
    TEST_ASSERT_EQ(capture::NpcapSource::loopback_filter(13328, {}), std::string("tcp and (src port 13328)"));
    TEST_ASSERT_EQ(capture::NpcapSource::loopback_filter(13328, {49001, 13328}),
                   std::string("tcp and (src port 13328 or src port 49001)"));
}

TEST_CASE(NpcapSource, RefusesOffWindows) {
#ifndef _WIN32
    capture::NpcapSource source(capture::NpcapOptions{});
    std::string error;
    TEST_ASSERT_FALSE(source.start([](capture::OwnedPacket&&) {}, error));
    TEST_ASSERT_FALSE(error.empty());
#endif
}

TEST_CASE(AppRuntime, ReplaysAFightIntoHistory) {
    ScratchDir dir("aion2_test_runtime");
    const auto session = test::synthetic::boss_session();
    const auto capture_file = dir.path / "synthetic.pcapng";
    {
        std::ofstream out(capture_file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(session.pcapng.data()),
                  static_cast<std::streamsize>(session.pcapng.size()));
    }

    AppRuntime runtime(AppSettings{}, dir.path / "data");
    std::string error;
    TEST_ASSERT(runtime.start(std::make_unique<capture::FileSource>(capture_file, 0.0), error));

    // Full speed: wait for the source to finish and the fight to land in history.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline &&
           (!runtime.source_finished() || runtime.history().empty())) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    runtime.request_end_fight();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto snap = runtime.snapshot();
    runtime.stop();

    const auto history = runtime.history();
    TEST_ASSERT_EQ(history.size(), size_t{1});
    TEST_ASSERT(history[0].killed);
    TEST_ASSERT_EQ(history[0].total_damage, session.truth.boss_damage);
    const auto fight = runtime.load_fight(history[0].file);
    TEST_ASSERT(fight.has_value());
    TEST_ASSERT_EQ(fight->combatants.size(), size_t{3});
    // Saved with its detail, and read back with it.
    TEST_ASSERT(fight->detail != nullptr);
    TEST_ASSERT_EQ(fight->detail->deaths.size(), session.truth.deaths.size());
    TEST_ASSERT_FALSE(fight->detail->timeline.rows.empty());

    TEST_ASSERT(snap != nullptr);
    TEST_ASSERT_EQ(snap->local_name, std::string("Ariel"));
    TEST_ASSERT_EQ(snap->recent.size(), size_t{1});
}

TEST_CASE(AppRuntime, RecordsWhatItCaptures) {
    ScratchDir dir("aion2_test_runtime_rec");
    const auto session = test::synthetic::boss_session();
    const auto capture_file = dir.path / "in.pcapng";
    {
        std::ofstream out(capture_file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(session.pcapng.data()),
                  static_cast<std::streamsize>(session.pcapng.size()));
    }
    AppRuntime runtime(AppSettings{}, dir.path / "data");
    runtime.request_recording(true);
    std::string error;
    TEST_ASSERT(runtime.start(std::make_unique<capture::FileSource>(capture_file, 0.0), error));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline && !runtime.source_finished()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    runtime.request_recording(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    runtime.stop();
    TEST_ASSERT_FALSE(runtime.recording());
    const std::filesystem::path recorded = runtime.recording_path();
    TEST_ASSERT(std::filesystem::exists(recorded));
    TEST_ASSERT(std::filesystem::file_size(recorded) > 1000);
}

TEST_CASE(AppRuntime, RecordingStartedFirstHoldsEveryPacket) {
    ScratchDir dir("aion2_test_runtime_rec_all");
    const auto session = test::synthetic::boss_session();
    const auto capture_file = dir.path / "in.pcapng";
    {
        std::ofstream out(capture_file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(session.pcapng.data()),
                  static_cast<std::streamsize>(session.pcapng.size()));
    }
    AppRuntime runtime(AppSettings{}, dir.path / "data");
    runtime.request_recording(true);
    std::string error;
    TEST_ASSERT(runtime.start(std::make_unique<capture::FileSource>(capture_file, 0.0), error));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline && !runtime.source_finished()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    runtime.stop();
    TEST_ASSERT_EQ(count_packets(runtime.recording_path()), count_packets(capture_file));
}

TEST_CASE(AppRuntime, StopsTheEngineBeforeTheSource) {
    ScratchDir dir("aion2_test_runtime_stop");
    AppRuntime runtime(AppSettings{}, dir.path / "data");
    auto source = std::make_unique<WatchedSource>();
    const WatchedSource* watched = source.get();
    std::string error;
    TEST_ASSERT(runtime.start(std::move(source), error));
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    runtime.stop();
    TEST_ASSERT_FALSE(watched->read_after_stop());
}

TEST_CASE(DisplayFormat, UnitsFollowRounding) {
    TEST_ASSERT_EQ(format_amount(999), std::string("999"));
    TEST_ASSERT_EQ(format_amount(999'949), std::string("999.9k"));
    TEST_ASSERT_EQ(format_amount(999'960), std::string("1.00M"));
    TEST_ASSERT_EQ(format_amount(999'996'000), std::string("1.00B"));
    TEST_ASSERT_EQ(format_rate(999.96), std::string("1.0k"));
    TEST_ASSERT_EQ(format_rate(std::nan("")), std::string("0.0"));
    TEST_ASSERT_EQ(format_percent(std::nan("")), std::string("0.0%"));
}

TEST_CASE(DisplayFormat, SecondsUnderAMinuteThenAClock) {
    TEST_ASSERT_EQ(format_seconds(3.24), std::string("3.2 s"));
    TEST_ASSERT_EQ(format_seconds(-1.0), std::string("0.0 s"));
    TEST_ASSERT_EQ(format_seconds(65.0), std::string("1:05"));
}

namespace {

/// A fight of 10 seconds: Ariel hits for 100 a second, Lumiel heals for 50 in the
/// first half, and the boss goes from full to a quarter.
meter::FightSummary chart_fight() {
    meter::FightSummary f;
    f.duration_s = 10.0;
    meter::CombatantTotals ariel;
    ariel.id = 100;
    ariel.name = "Ariel";
    ariel.cls = game::AionClass::Sorcerer;
    meter::CombatantTotals lumiel;
    lumiel.id = 200;
    lumiel.name = "Lumiel";
    f.combatants = {ariel, lumiel};
    auto d = std::make_shared<meter::FightDetail>();
    d->timeline.seconds = 10;
    meter::TimelineRow a{100, std::vector<meter::TimelineBin>(10)};
    for (auto& bin : a.bins) bin.damage = 100;
    meter::TimelineRow l{200, std::vector<meter::TimelineBin>(5)};
    for (auto& bin : l.bins) bin.healing = 50;
    d->timeline.rows = {a, l};
    d->timeline.boss_hp.first_s = 2;
    d->timeline.boss_hp.permille = {1000, 750, 500, 250};
    f.detail = d;
    return f;
}

} // namespace

TEST_CASE(TimelineChart, LinesFollowTheMetricAndOrder) {
    const meter::FightSummary f = chart_fight();
    TimelineChart chart;
    TimelineView view;
    view.smoothing_s = 1;
    TEST_ASSERT(update_timeline_chart(chart, f, view, 100));
    // Lumiel dealt no damage: no line.
    TEST_ASSERT_EQ(chart.lines.size(), size_t{1});
    TEST_ASSERT_EQ(chart.lines[0].name, std::string("Ariel"));
    TEST_ASSERT_EQ(chart.lines[0].values.size(), size_t{10});
    TEST_ASSERT_NEAR(chart.lines[0].peak, 100.0, 1e-4);
    view.metric = meter::TimelineMetric::Healing;
    TEST_ASSERT(update_timeline_chart(chart, f, view, 100));
    TEST_ASSERT_EQ(chart.lines.size(), size_t{1});
    TEST_ASSERT_EQ(chart.lines[0].name, std::string("Lumiel"));
    TEST_ASSERT_NEAR(chart.lines[0].total, 250.0, 1e-9);
}

TEST_CASE(TimelineChart, RebuildsOnlyWhenSomethingChanged) {
    meter::FightSummary f = chart_fight();
    TimelineChart chart;
    TimelineView view;
    TEST_ASSERT(update_timeline_chart(chart, f, view, 100));
    TEST_ASSERT_FALSE(update_timeline_chart(chart, f, view, 100));
    // A line hidden in the legend is a drawing matter, not a rebuild.
    view.hidden.insert("Ariel");
    TEST_ASSERT_FALSE(update_timeline_chart(chart, f, view, 100));
    TEST_ASSERT(update_timeline_chart(chart, f, view, 50));
    view.smoothing_s = 5;
    TEST_ASSERT(update_timeline_chart(chart, f, view, 50));
    // The live fight's next snapshot: another detail.
    f.detail = std::make_shared<meter::FightDetail>(*f.detail);
    TEST_ASSERT(update_timeline_chart(chart, f, view, 50));
}

TEST_CASE(TimelineChart, BossLineIsAFraction) {
    const meter::FightSummary f = chart_fight();
    TimelineChart chart;
    TEST_ASSERT(update_timeline_chart(chart, f, TimelineView{}, 100));
    TEST_ASSERT_EQ(chart.boss_hp.size(), size_t{10});
    TEST_ASSERT(chart.boss_hp[1] < 0.0f);  // before its first figure
    TEST_ASSERT_NEAR(chart.boss_hp[2], 1.0, 1e-6);
    TEST_ASSERT_NEAR(chart.boss_hp[4], 0.5, 1e-6);
    TEST_ASSERT_NEAR(chart.boss_hp[9], 0.25, 1e-6);
    // Fewer points than seconds: each the figure at its span's end.
    TEST_ASSERT(update_timeline_chart(chart, f, TimelineView{}, 5));
    TEST_ASSERT_EQ(chart.boss_hp.size(), size_t{5});
    TEST_ASSERT_NEAR(chart.boss_hp[1], 0.75, 1e-6);
}

TEST_CASE(TimelineChart, NoDetailGivesNoLines) {
    meter::FightSummary f = chart_fight();
    f.detail.reset();
    TimelineChart chart;
    TEST_ASSERT(update_timeline_chart(chart, f, TimelineView{}, 100));
    TEST_ASSERT(chart.lines.empty());
    TEST_ASSERT(chart.boss_hp.empty());
    TEST_ASSERT_NEAR(chart_top(83'000.0), 100'000.0, 1e-6);
    TEST_ASSERT_NEAR(time_step(300.0, 600.0f, 70.0f), 60.0, 1e-9);
}

TEST_CASE(DisplayFormat, TheLocalPlayerAlwaysShows) {
    std::vector<meter::CombatantTotals> rows(6);
    for (size_t i = 0; i < rows.size(); ++i) rows[i].damage = 1000 - i * 100;
    rows[2].damage = 0;  // only healed
    const auto ranks = [](const std::vector<ShownRow>& shown) {
        std::vector<std::pair<size_t, int>> out;
        for (const auto& r : shown) out.emplace_back(r.index, r.rank);
        return out;
    };
    using Ranks = std::vector<std::pair<size_t, int>>;

    // Among the best: nothing moves.
    rows[1].is_local = true;
    TEST_ASSERT(ranks(shown_rows(rows, 3, false)) == (Ranks{{0, 1}, {1, 2}, {3, 3}}));
    // Below them: the last place is theirs, with their rank.
    rows[1].is_local = false;
    rows[5].is_local = true;
    TEST_ASSERT(ranks(shown_rows(rows, 3, false)) == (Ranks{{0, 1}, {1, 2}, {5, 5}}));
    TEST_ASSERT(ranks(shown_rows(rows, 3, true)) == (Ranks{{0, 1}, {1, 2}, {5, 6}}));
    TEST_ASSERT(ranks(shown_rows(rows, 1, false)) == (Ranks{{5, 5}}));
    TEST_ASSERT(ranks(shown_rows(rows, 8, false)).size() == 5);
    // A local player who dealt no damage still shows, ranked below everyone who did,
    // with the Heal column off too.
    rows[5].damage = 0;
    TEST_ASSERT(ranks(shown_rows(rows, 3, false)) == (Ranks{{0, 1}, {1, 2}, {5, 5}}));
    TEST_ASSERT(ranks(shown_rows(rows, 8, false)) == (Ranks{{0, 1}, {1, 2}, {3, 3}, {4, 4}, {5, 5}}));
    TEST_ASSERT(ranks(shown_rows(rows, 8, true)).size() == 6);
}

// README: "Your own row always shows on the overlay". A healer who only healed,
// with the Heal column off, still has theirs.
TEST_CASE(DisplayFormat, ALocalPlayerWhoOnlyHealedShows) {
    std::vector<meter::CombatantTotals> rows(2);
    rows[0].id = 1;
    rows[0].damage = 1000;
    rows[1].id = 2;
    rows[1].is_local = true;
    rows[1].healing = 500;
    const auto shown = shown_rows(rows, 8, false);
    TEST_ASSERT_EQ(shown.size(), size_t{2});
    TEST_ASSERT_EQ(shown[1].index, size_t{1});
    TEST_ASSERT_EQ(shown[1].rank, 2);
}

// The overlay's skull: a row's deaths by DeathRecord::player, bright while the
// latest has no getting up after it.
TEST_CASE(DisplayFormat, DeathsGoToTheirRowAndSayWhoIsDown) {
    meter::FightSummary f;
    // A fight saved before the meter kept its detail has no deaths to show.
    TEST_ASSERT(row_deaths(f, 1) == RowDeaths{});

    auto detail = std::make_shared<meter::FightDetail>();
    const auto death = [](meter::EntityId player, double time_s, double up_after_s) {
        meter::DeathRecord d;
        d.player = player;
        d.time_s = time_s;
        d.up_after_s = up_after_s;
        return d;
    };
    // Row 1 died twice and got up both times; row 2 died and is down; 9 has no row.
    detail->deaths.push_back(death(1, 10.0, 4.0));
    detail->deaths.push_back(death(2, 20.0, -1.0));
    detail->deaths.push_back(death(9, 25.0, 2.0));
    detail->deaths.push_back(death(1, 30.0, 5.5));
    f.detail = detail;
    TEST_ASSERT(row_deaths(f, 1) == (RowDeaths{2, false}));
    TEST_ASSERT(row_deaths(f, 2) == (RowDeaths{1, true}));
    TEST_ASSERT(row_deaths(f, 3) == RowDeaths{});
    TEST_ASSERT(row_deaths(f, 0) == RowDeaths{});

    // Down again: the latest death decides, not the first.
    detail->deaths.push_back(death(1, 40.0, -1.0));
    TEST_ASSERT(row_deaths(f, 1) == (RowDeaths{3, true}));
}

TEST_CASE(AppRuntime, MissingCaptureFileFailsToStart) {
    ScratchDir dir("aion2_test_runtime_missing");
    AppRuntime runtime(AppSettings{}, dir.path);
    std::string error;
    TEST_ASSERT_FALSE(runtime.start(std::make_unique<capture::FileSource>(dir.path / "none.pcapng", 1.0), error));
    TEST_ASSERT_FALSE(runtime.source_status().empty());
}

namespace {

/// The synthetic boss fight written to `dir` as a capture.
std::filesystem::path write_session(const std::filesystem::path& dir) {
    const auto session = test::synthetic::boss_session();
    const auto file = dir / "synthetic.pcapng";
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(session.pcapng.data()), static_cast<std::streamsize>(session.pcapng.size()));
    return file;
}

/// Waits, up to 10 s, for `done`.
template <typename F>
bool wait_for(F done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

} // namespace

TEST_CASE(AppRuntime, ClearsTheHistoryOnTheEngineThread) {
    ScratchDir dir("aion2_test_runtime_clear");
    AppRuntime runtime(AppSettings{}, dir.path / "data");
    std::string error;
    TEST_ASSERT(runtime.start(std::make_unique<capture::FileSource>(write_session(dir.path), 0.0), error));
    TEST_ASSERT(wait_for([&] { return runtime.source_finished() && !runtime.history().empty(); }));
    const uint64_t revision = runtime.history_revision();
    runtime.request_clear_history();
    TEST_ASSERT(wait_for([&] { return runtime.history_revision() != revision; }));
    runtime.stop();
    TEST_ASSERT(runtime.history().empty());
    TEST_ASSERT(meter::HistoryStore(runtime.history_dir()).load_index().empty());
}

TEST_CASE(AppRuntime, KeepsAndDeletesOnTheEngineThread) {
    ScratchDir dir("aion2_test_runtime_keep");
    AppRuntime runtime(AppSettings{}, dir.path / "data");
    std::string error;
    TEST_ASSERT(runtime.start(std::make_unique<capture::FileSource>(write_session(dir.path), 0.0), error));
    TEST_ASSERT(wait_for([&] { return runtime.source_finished() && !runtime.history().empty(); }));
    const std::string file = runtime.history()[0].file;

    uint64_t revision = runtime.history_revision();
    runtime.request_keep_fights({file}, true);
    TEST_ASSERT(wait_for([&] { return runtime.history_revision() != revision; }));
    TEST_ASSERT(runtime.history()[0].kept);

    // Clear leaves the kept fight.
    revision = runtime.history_revision();
    runtime.request_clear_history();
    TEST_ASSERT(wait_for([&] { return runtime.history_revision() != revision; }));
    TEST_ASSERT_EQ(runtime.history().size(), size_t{1});

    // Deleted by name, it goes.
    revision = runtime.history_revision();
    runtime.request_delete_fights({file});
    TEST_ASSERT(wait_for([&] { return runtime.history_revision() != revision; }));
    runtime.stop();
    TEST_ASSERT(runtime.history().empty());
    TEST_ASSERT(meter::HistoryStore(runtime.history_dir()).load_index().empty());
}

TEST_CASE(AppRuntime, NewSettingsReachTheEngine) {
    ScratchDir dir("aion2_test_runtime_settings");
    AppRuntime runtime(AppSettings{}, dir.path / "data");
    // No boss, and no fight long enough to keep: nothing is saved.
    meter::MeterSettings meter;
    meter.boss_min_max_hp = UINT64_MAX;
    meter.min_saved_duration_s = 3600.0;
    runtime.update_settings(meter, HistorySettings{}, aion2::plugins::PluginSwitches{});
    std::string error;
    TEST_ASSERT(runtime.start(std::make_unique<capture::FileSource>(write_session(dir.path), 0.0), error));
    TEST_ASSERT(wait_for([&] { return runtime.source_finished(); }));
    runtime.request_end_fight();
    TEST_ASSERT(wait_for([&] {
        const auto snap = runtime.snapshot();
        return snap != nullptr && !snap->recent.empty();
    }));
    runtime.stop();
    TEST_ASSERT(runtime.history().empty());
}

TEST_CASE(AppRuntime, QuittingSavesTheFightUnderWay) {
    // Exit and Update & restart stop the runtime with the fight still live: a boss
    // fight is, for kill_grace_s after the kill, and any for its idle wait. It ends
    // and is saved, as switching the meter off would.
    using namespace test::synthetic;
    ScratchDir dir("aion2_test_runtime_quit");
    std::vector<std::pair<double, Bytes>> script;
    for (int i = 0; i <= 30 * 20; ++i) script.emplace_back(i * 0.05, heartbeat(kStartMs + static_cast<uint64_t>(i) * 50));
    script.emplace_back(0.001, opcode_frame(proto::opcode::OwnCharacter, own_identity_body(kMe, "Ariel", 0x0123, 7)));
    script.emplace_back(0.002, opcode_frame(proto::opcode::MapLoad, map_load_body(1, kDungeon)));
    SpawnSpec boss;
    boss.raw_id = kBossRaw;
    boss.npc_code = kBossNpc;
    boss.cur_hp = boss.max_hp = kBossHp;
    script.emplace_back(0.006, opcode_frame(proto::opcode::Spawn, spawn_body(boss)));
    // 25 s of hits on a boss left standing, the capture ending 5 s later.
    for (int t = 1; t <= 25; ++t) {
        DamageSpec d;
        d.target = static_cast<uint64_t>(proto::normalize_entity_id(kBossRaw));
        d.actor = kMe;
        d.skill = 15210000;
        d.amount = 1000;
        script.emplace_back(static_cast<double>(t), opcode_frame(proto::opcode::Damage, damage_body({d})));
    }
    std::stable_sort(script.begin(), script.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    SessionWriter w;
    for (const auto& [t, frame] : script) w.at(t, frame);
    const Bytes capture = w.finish();
    const auto file = dir.path / "fight.pcapng";
    {
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(capture.data()), static_cast<std::streamsize>(capture.size()));
    }

    AppRuntime runtime(AppSettings{}, dir.path / "data");
    std::string error;
    TEST_ASSERT(runtime.start(std::make_unique<capture::FileSource>(file, 0.0), error));
    TEST_ASSERT(wait_for([&] {
        const auto snap = runtime.snapshot();
        return runtime.source_finished() && snap != nullptr && snap->fight.live && snap->fight.duration_s >= 24.0;
    }));
    TEST_ASSERT(runtime.history().empty());
    runtime.stop();
    const auto saved = meter::HistoryStore(runtime.history_dir()).load_index();
    TEST_ASSERT_EQ(saved.size(), size_t{1});
    TEST_ASSERT_EQ(saved[0].total_damage, uint64_t{25'000});
    TEST_ASSERT(saved[0].end_reason == meter::EndReason::Manual);
    TEST_ASSERT_EQ(runtime.history().size(), size_t{1});
}

namespace {

/// A live source as Npcap is: every packet queued as start() returns, and the
/// counter's offset known from the first, since a capture thread samples it before
/// it pushes. It joins mid-stream, as when the meter starts in game.
class LiveLikeSource final : public capture::PacketSource {
public:
    LiveLikeSource(test::wire::Bytes file, Micros offset) : m_file(std::move(file)), m_offset(offset) {}
    bool start(Push push, std::string& error) override {
        auto reader = capture::CaptureFileReader::from_bytes(m_file, error);
        if (!reader) return false;
        capture::CapturedPacket pkt;
        bool first = true;
        while (reader->next(pkt)) {
            if (std::exchange(first, false)) continue;  // the SYN
            capture::OwnedPacket o;
            o.link_type = pkt.link_type;
            o.ts = pkt.ts;
            o.data.assign(pkt.data.begin(), pkt.data.end());
            m_last = pkt.ts;
            push(std::move(o));
        }
        return true;
    }
    void stop() override {}
    [[nodiscard]] Micros now() const override { return m_last; }
    [[nodiscard]] uint64_t dropped() const override { return 0; }
    [[nodiscard]] std::optional<Micros> counter_offset() const override { return m_offset; }
    [[nodiscard]] std::string status() const override { return "live-like"; }

private:
    test::wire::Bytes m_file;
    Micros m_offset;
    std::atomic<Micros> m_last{0};
};

} // namespace

// The counter offset reaches the pipeline before the packet: a pong in the first
// packets the engine handles is measured, not taken for a replay's.
TEST_CASE(AppRuntime, ALivePongBeforeTheFirstTickIsMeasured) {
    using namespace aion2::test::synthetic;
    ScratchDir dir("aion2_test_runtime_ping");
    const Micros at = kStartMs * kMicrosPerMilli;
    const Micros offset = at - 3'600 * kMicrosPerSecond;
    // One packet: a heartbeat, which the decoder aligns on, and the pong, 70 ms.
    test::wire::Bytes first = heartbeat(kStartMs - 200);
    test::wire::put_bytes(first, pong(ping::kClientClockBaseMs + (at - offset) / kMicrosPerMilli - 70, kStartMs - 35));
    SessionWriter w;
    w.at(0.0, first);
    for (int i = 1; i <= 20; ++i) w.at(i * 0.05, heartbeat(kStartMs + static_cast<uint64_t>(i) * 50 - 200));
    AppSettings settings;
    settings.plugins.ping = true;
    AppRuntime runtime(settings, dir.path / "data");
    std::string error;
    TEST_ASSERT(runtime.start(std::make_unique<LiveLikeSource>(w.finish(), offset), error));
    std::shared_ptr<const pipeline::LiveSnapshot> snap;
    TEST_ASSERT(wait_for([&] {
        snap = runtime.snapshot();
        return snap != nullptr && snap->ping.pongs > 0;
    }));
    runtime.stop();
    TEST_ASSERT_EQ(snap->ping.pongs, uint64_t{1});
    TEST_ASSERT(snap->ping.state == ping::PingState::Measured);
    TEST_ASSERT_NEAR(snap->ping.ping_ms, 70.0f, 1e-3f);
}

namespace {

/// A live source whose clock the test sets, `skew` away from the stamps it gives its
/// packets, as Npcap's wall-clock now() sits apart from its counter-based stamps.
class SkewedSource final : public capture::PacketSource {
public:
    explicit SkewedSource(int64_t skew) : m_skew(skew) {}
    bool start(Push push, std::string&) override {
        m_push = std::move(push);
        return true;
    }
    void stop() override {}
    /// A packet stamped `ts`, handed over as the capture's clock reads `ts`.
    void deliver(const test::wire::Bytes& packet, Micros ts) {
        m_clock = ts;
        capture::OwnedPacket o;
        o.link_type = capture::link_type::Ethernet;
        o.ts = ts;
        o.data = packet;
        m_push(std::move(o));
    }
    /// The capture's clock now: the source's own reads `skew` from it.
    void set_clock(Micros stamp_now) { m_clock = stamp_now; }
    [[nodiscard]] Micros now() const override {
        const Micros clock = m_clock.load();
        return clock == 0 ? 0 : static_cast<Micros>(static_cast<int64_t>(clock) + m_skew);
    }
    [[nodiscard]] uint64_t dropped() const override { return 0; }
    [[nodiscard]] std::string status() const override { return "skewed"; }

private:
    int64_t m_skew;
    Push m_push;
    std::atomic<Micros> m_clock{0};
};

/// The source's clock an hour ahead of its stamps, and 2 s behind them.
constexpr int64_t kSkews[] = {3'600 * static_cast<int64_t>(kMicrosPerSecond), -2 * static_cast<int64_t>(kMicrosPerSecond)};

/// Waits for the snapshot published at `now`.
std::shared_ptr<const pipeline::LiveSnapshot> snapshot_at(AppRuntime& runtime, Micros now) {
    std::shared_ptr<const pipeline::LiveSnapshot> snap;
    if (!wait_for([&] {
            snap = runtime.snapshot();
            return snap != nullptr && snap->now == now;
        })) {
        return nullptr;
    }
    return snap;
}

} // namespace

// With the queue empty, time runs on from the latest packet by what the source's
// clock has run since, never to that clock itself: an hour off, or 2 s, it shows no
// stall between two heartbeats, and a real one as long as it is.
TEST_CASE(AppRuntime, QuietTimeRunsOnFromTheLatestPacket) {
    using namespace aion2::test::synthetic;
    for (const int64_t skew : kSkews) {
        ScratchDir dir("aion2_test_runtime_quiet");
        AppSettings settings;
        settings.plugins.ping = true;
        AppRuntime runtime(settings, dir.path / "data");
        auto owned = std::make_unique<SkewedSource>(skew);
        SkewedSource& source = *owned;
        std::string error;
        TEST_ASSERT(runtime.start(std::move(owned), error));

        const Micros t0 = kStartMs * kMicrosPerMilli;
        TcpSpec spec;
        spec.flags = capture::tcp_flag::Syn | capture::tcp_flag::Ack;
        source.deliver(ethernet_tcp(spec, Bytes{}), t0);
        spec.seq += 1;
        spec.flags = capture::tcp_flag::Ack | capture::tcp_flag::Psh;
        Micros last = t0;
        for (int i = 1; i <= 20; ++i) {
            last = t0 + static_cast<Micros>(i) * 50 * kMicrosPerMilli;
            const Bytes hb = heartbeat(last / kMicrosPerMilli - 200);
            source.deliver(ethernet_tcp(spec, hb), last);
            spec.seq += static_cast<uint32_t>(hb.size());
        }
        TEST_ASSERT(snapshot_at(runtime, last) != nullptr);

        // Between two heartbeats: no stall, and the jitter shows.
        source.set_clock(last + 40 * kMicrosPerMilli);
        auto snap = snapshot_at(runtime, last + 40 * kMicrosPerMilli);
        TEST_ASSERT(snap != nullptr);
        TEST_ASSERT(snap->ping.has_jitter);
        TEST_ASSERT_FALSE(snap->ping.seconds.empty());
        for (const ping::PingSecond& second : snap->ping.seconds) TEST_ASSERT(second.late_ms < 250.0f);

        // A second and a half with nothing: the next heartbeat is 1,450 ms late.
        source.set_clock(last + 1'500 * kMicrosPerMilli);
        snap = snapshot_at(runtime, last + 1'500 * kMicrosPerMilli);
        TEST_ASSERT(snap != nullptr);
        TEST_ASSERT_NEAR(snap->ping.seconds.back().late_ms, 1'450.0f, 1e-2f);
        runtime.stop();
    }
}

// A reassembly hole still expires TcpReassembler::kHoleTimeout of the source's
// elapsed time after the packet held behind it, however far its clock is off.
TEST_CASE(AppRuntime, AHoleExpiresOnTheSourcesElapsedTime) {
    using namespace aion2::test::synthetic;
    for (const int64_t skew : kSkews) {
        ScratchDir dir("aion2_test_runtime_hole");
        AppRuntime runtime(AppSettings{}, dir.path / "data");
        auto owned = std::make_unique<SkewedSource>(skew);
        SkewedSource& source = *owned;
        std::string error;
        TEST_ASSERT(runtime.start(std::move(owned), error));

        const Micros t0 = kStartMs * kMicrosPerMilli;
        TcpSpec spec;
        spec.flags = capture::tcp_flag::Syn | capture::tcp_flag::Ack;
        source.deliver(ethernet_tcp(spec, Bytes{}), t0);
        spec.seq += 1;
        spec.flags = capture::tcp_flag::Ack | capture::tcp_flag::Psh;
        const Bytes first = heartbeat(kStartMs + 50);
        source.deliver(ethernet_tcp(spec, first), t0 + 50 * kMicrosPerMilli);
        // The next segment is lost; the one after it waits behind the hole.
        spec.seq += static_cast<uint32_t>(first.size() * 2);
        const Micros held = t0 + 100 * kMicrosPerMilli;
        source.deliver(ethernet_tcp(spec, heartbeat(kStartMs + 150)), held);
        auto snap = snapshot_at(runtime, held);
        TEST_ASSERT(snap != nullptr);
        TEST_ASSERT_EQ(snap->capture_totals.gap_bytes, uint64_t{0});

        source.set_clock(held + capture::TcpReassembler::kHoleTimeout - 50 * kMicrosPerMilli);
        snap = snapshot_at(runtime, held + capture::TcpReassembler::kHoleTimeout - 50 * kMicrosPerMilli);
        TEST_ASSERT(snap != nullptr);
        TEST_ASSERT_EQ(snap->capture_totals.gap_bytes, uint64_t{0});

        source.set_clock(held + capture::TcpReassembler::kHoleTimeout + 50 * kMicrosPerMilli);
        snap = snapshot_at(runtime, held + capture::TcpReassembler::kHoleTimeout + 50 * kMicrosPerMilli);
        TEST_ASSERT(snap != nullptr);
        TEST_ASSERT_EQ(snap->capture_totals.gap_bytes, static_cast<uint64_t>(first.size()));
        runtime.stop();
    }
}

TEST_CASE(OverlayVisibility, CombatOnlyKeepsItUpAfterAFight) {
    AppSettings app;
    OverlaySettings& s = app.overlay;
    s.locked = true;
    s.show_in_combat_only = true;
    s.hide_after_combat_s = 5.0f;
    OverlayConditions now;
    TEST_ASSERT_FALSE(overlay_shown(app, now));  // no fight yet
    now.fight_live = true;
    TEST_ASSERT(overlay_shown(app, now));
    now.fight_live = false;
    now.seconds_since_fight = 4.9;
    TEST_ASSERT(overlay_shown(app, now));
    now.seconds_since_fight = 5.0;
    TEST_ASSERT_FALSE(overlay_shown(app, now));
}

TEST_CASE(OverlayVisibility, DungeonsOnly) {
    AppSettings app;
    OverlaySettings& s = app.overlay;
    s.show_in_dungeons_only = true;
    OverlayConditions now;
    TEST_ASSERT(overlay_shown(app, now));  // unlocked: always findable
    s.locked = true;
    TEST_ASSERT_FALSE(overlay_shown(app, now));  // no map load yet, or the open world
    now.in_dungeon = true;
    TEST_ASSERT(overlay_shown(app, now));
    // Both conditions must allow it.
    s.show_in_combat_only = true;
    TEST_ASSERT_FALSE(overlay_shown(app, now));
    now.fight_live = true;
    TEST_ASSERT(overlay_shown(app, now));
}

TEST_CASE(OverlayVisibility, TheGameNotRunningIsNotInFront) {
    const uint32_t own = 100;
    const std::vector<uint32_t> none;
    const std::vector<uint32_t> game{4242, 4243};
    // No game process: hidden, whatever has the focus, the meter's own window included.
    TEST_ASSERT_FALSE(foreground_is_game(none, 555u, own));
    TEST_ASSERT_FALSE(foreground_is_game(none, own, own));
    TEST_ASSERT_FALSE(foreground_is_game(none, std::nullopt, own));
    // The game running: in front when it or the meter has the focus.
    TEST_ASSERT(foreground_is_game(game, 4243u, own));
    TEST_ASSERT(foreground_is_game(game, own, own));
    TEST_ASSERT_FALSE(foreground_is_game(game, 555u, own));
    // Focus passing between windows does not count against it.
    TEST_ASSERT(foreground_is_game(game, std::nullopt, own));
}

TEST_CASE(OverlayVisibility, ConditionsWaitForTheLock) {
    AppSettings app;
    OverlaySettings& s = app.overlay;
    s.show_in_combat_only = true;
    s.hide_when_game_unfocused = true;
    OverlayConditions now;
    now.game_in_front = false;
    TEST_ASSERT(overlay_shown(app, now));  // unlocked: always findable
    s.locked = true;
    TEST_ASSERT_FALSE(overlay_shown(app, now));
    now.fight_live = true;
    TEST_ASSERT_FALSE(overlay_shown(app, now));  // in a fight, but the game is not in front
    now.game_in_front = true;
    TEST_ASSERT(overlay_shown(app, now));
    s.visible = false;
    TEST_ASSERT_FALSE(overlay_shown(app, now));  // the hotkey's hide rules over all
    s.visible = true;
    app.plugins.combat_meter = false;
    TEST_ASSERT_FALSE(overlay_shown(app, now));  // and the plugin switched off over that
}

namespace {

meter::HistoryEntry saved_fight(std::string file, Micros visit, Micros start) {
    meter::HistoryEntry e;
    e.file = std::move(file);
    e.map_id = 600001;
    e.visit_ts = visit;
    e.start_ts = start;
    return e;
}

/// Two runs: a and b in the first, c, d and e in the second. The list shows them
/// newest first: e d c, then b a.
std::vector<meter::HistoryEntry> two_runs() {
    const Micros t = 1'792'152'000'000'000ULL;
    const Micros later = t + 600 * kMicrosPerSecond;
    return {saved_fight("a", t, t + 10), saved_fight("b", t, t + 20), saved_fight("c", later, later + 10),
            saved_fight("d", later, later + 20), saved_fight("e", later, later + 30)};
}

} // namespace

TEST_CASE(HistoryList, OrderIsTheListsNewestFirst) {
    const auto history = two_runs();
    const auto order = list_order(history, meter::group_by_visit(history));
    TEST_ASSERT(order == (std::vector<std::string>{"e", "d", "c", "b", "a"}));
}

TEST_CASE(HistoryList, PlainCtrlAndShiftClicks) {
    const auto history = two_runs();
    const auto order = list_order(history, meter::group_by_visit(history));
    HistorySelection s;
    // A plain click opens the fight, and selects nothing.
    TEST_ASSERT(click(s, order, "d", false, false, ""));
    TEST_ASSERT(s.marked.empty());
    // The first Ctrl-click takes the fight on view in too.
    TEST_ASSERT_FALSE(click(s, order, "b", true, false, "d"));
    TEST_ASSERT(s.marked == (std::set<std::string>{"b", "d"}));
    // Taken out again, it stays out.
    TEST_ASSERT_FALSE(click(s, order, "d", true, false, "d"));
    TEST_ASSERT_FALSE(click(s, order, "a", true, false, "d"));
    TEST_ASSERT(s.marked == (std::set<std::string>{"a", "b"}));
    // Shift: the range from the last fight Ctrl-clicked, across the runs.
    TEST_ASSERT_FALSE(click(s, order, "d", false, true, "d"));
    TEST_ASSERT(s.marked == (std::set<std::string>{"a", "b", "c", "d"}));
    // Again, from the same start: the range replaces what was selected.
    TEST_ASSERT_FALSE(click(s, order, "b", false, true, "d"));
    TEST_ASSERT(s.marked == (std::set<std::string>{"a", "b"}));
    // A plain click starts over; Ctrl+Shift adds a range to the rest.
    TEST_ASSERT(click(s, order, "e", false, false, "d"));
    TEST_ASSERT_FALSE(click(s, order, "a", true, false, "e"));
    TEST_ASSERT(s.marked == (std::set<std::string>{"a", "e"}));
    TEST_ASSERT_FALSE(click(s, order, "c", true, true, "e"));
    TEST_ASSERT(s.marked == (std::set<std::string>{"a", "b", "c", "e"}));
}

TEST_CASE(HistoryList, ShiftWithNoStartTakesTheFightOnView) {
    const auto history = two_runs();
    const auto order = list_order(history, meter::group_by_visit(history));
    HistorySelection s;
    TEST_ASSERT_FALSE(click(s, order, "a", false, true, "c"));
    TEST_ASSERT(s.marked == (std::set<std::string>{"a", "b", "c"}));
    HistorySelection alone;
    TEST_ASSERT_FALSE(click(alone, order, "b", false, true, ""));
    TEST_ASSERT(alone.marked == (std::set<std::string>{"b"}));
}

TEST_CASE(HistoryList, SelectAllAndDropWhatLeft) {
    auto history = two_runs();
    HistorySelection s;
    select_all(s, history);
    TEST_ASSERT_EQ(s.marked.size(), size_t{5});
    s.anchor = "c";
    history.erase(history.begin() + 2);  // c deleted
    drop_missing(s, history);
    TEST_ASSERT(s.marked == (std::set<std::string>{"a", "b", "d", "e"}));
    TEST_ASSERT(s.anchor.empty());
}

TEST_CASE(HistoryList, DayLabels) {
    const auto today = common::civil_day(2026, 11, 24);
    TEST_ASSERT_EQ(day_label(today, today), std::string("Today"));
    TEST_ASSERT_EQ(day_label(common::civil_day(2026, 11, 23), today), std::string("Yesterday"));
    TEST_ASSERT_EQ(day_label(common::civil_day(2026, 11, 21), today), std::string("Sat 21 Nov"));
    // By the days' serials: 31 Dec is yesterday on 1 Jan, and the year shows when
    // it is another one.
    const auto new_year = common::civil_day(2026, 1, 1);
    TEST_ASSERT_EQ(day_label(common::civil_day(2025, 12, 31), new_year), std::string("Yesterday"));
    TEST_ASSERT_EQ(day_label(common::civil_day(2025, 12, 30), new_year), std::string("Tue 30 Dec 2025"));
    // No clock: every day by its date.
    TEST_ASSERT_EQ(day_label(today, std::nullopt), std::string("Tue 24 Nov"));
}

TEST_CASE(HistoryList, RunTitlesPutTheTierUnderTheName) {
    RunTitle t = run_title("Krao Cave (Conquest)", 3);
    TEST_ASSERT_EQ(t.name, std::string("Krao Cave"));
    TEST_ASSERT_EQ(t.detail, std::string("Conquest \xC2\xB7 3 fights"));
    t = run_title("Abyssal Forge: Ludra (Conquest [Hard])", 1);
    TEST_ASSERT_EQ(t.name, std::string("Abyssal Forge: Ludra"));
    TEST_ASSERT_EQ(t.detail, std::string("Conquest [Hard] \xC2\xB7 1 fight"));
    // No tier: the count alone.
    t = run_title("Map 1010", 2);
    TEST_ASSERT_EQ(t.name, std::string("Map 1010"));
    TEST_ASSERT_EQ(t.detail, std::string("2 fights"));
    t = run_title("Impetusium Arena", 1);
    TEST_ASSERT_EQ(t.name, std::string("Impetusium Arena"));
    TEST_ASSERT_EQ(t.detail, std::string("1 fight"));
    // A label that is all parentheses, or an empty tier, keeps its name whole.
    TEST_ASSERT_EQ(run_title("(Conquest)", 1).name, std::string("(Conquest)"));
    TEST_ASSERT_EQ(run_title("Krao Cave ()", 1).name, std::string("Krao Cave ()"));
    TEST_ASSERT_EQ(run_title("Krao Cave ()", 1).detail, std::string("1 fight"));
}

TEST_CASE(HistoryList, DeletePromptsSayWhatGoesAndWhatStays) {
    auto history = two_runs();
    history[3].kept = true;  // d
    // Picked by hand, a kept fight goes too, and the dialog says so.
    auto p = delete_picked(history, {"d"});
    TEST_ASSERT(p.files == (std::vector<std::string>{"d"}));
    TEST_ASSERT_EQ(p.title, std::string("Delete this fight?"));
    TEST_ASSERT_EQ(p.body, std::string("Its file goes too, though you keep it."));
    p = delete_picked(history, {"a", "d", "gone"});
    TEST_ASSERT(p.files == (std::vector<std::string>{"a", "d"}));
    TEST_ASSERT_EQ(p.title, std::string("Delete 2 fights?"));
    TEST_ASSERT_EQ(p.body, std::string("Their files go too, the one you keep among them."));
    // A run spares its kept fights.
    const auto groups = meter::group_by_visit(history);
    p = delete_run(history, groups[0], "Krao Cave (Conquest), Today 12:10");
    TEST_ASSERT(p.files == (std::vector<std::string>{"c", "e"}));
    TEST_ASSERT_EQ(p.title, std::string("Delete this run's 2 fights?"));
    TEST_ASSERT_EQ(p.body,
                   std::string("Krao Cave (Conquest), Today 12:10. Their files go too. The one you keep stays."));
    // So does Clear.
    p = delete_all(history);
    TEST_ASSERT(p.everything);
    TEST_ASSERT(p.files.empty());
    TEST_ASSERT_EQ(p.title, std::string("Delete the 4 fights you don't keep?"));
    TEST_ASSERT_EQ(p.body, std::string("Their files go too. The one you keep stays, and so does the live fight."));
    // With none kept, Clear asks as it always did.
    history[3].kept = false;
    p = delete_all(history);
    TEST_ASSERT_EQ(p.title, std::string("Delete all 5 saved fights?"));
    TEST_ASSERT_EQ(p.body, std::string("Their files go too. The live fight stays."));
    // Every fight kept: neither Clear nor a run has anything to delete.
    for (auto& e : history) e.kept = true;
    TEST_ASSERT_EQ(kept_count(history), size_t{5});
    TEST_ASSERT_FALSE(delete_all(history).everything);
    TEST_ASSERT(delete_run(history, groups[0], "").files.empty());
}
