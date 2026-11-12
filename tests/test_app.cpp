#include "test_framework.hpp"
#include "file_test_support.hpp"
#include "synthetic_session.hpp"

#include "app/app_runtime.hpp"
#include "app/app_settings.hpp"
#include "app/aura_view.hpp"
#include "app/overlay_visibility.hpp"
#include "app/display_format.hpp"
#include "app/timeline_chart.hpp"
#include "capture/capture_file.hpp"
#include "capture/file_source.hpp"
#include "capture/os/npcap_source.hpp"
#include "common/time_format.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <optional>
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
    s.meter.boss_min_max_hp = 2'500'000;
    s.history.max_fights = 42;
    s.window.close_to_tray = false;
    s.window.check_for_updates = false;
    s.window.x = -1200;
    s.window.width = 1280;
    const AppSettings back = settings_from_json(settings_to_json(s));
    TEST_ASSERT_FALSE(back.window.close_to_tray);
    TEST_ASSERT_FALSE(back.window.check_for_updates);
    TEST_ASSERT_EQ(back.window.x, -1200);
    TEST_ASSERT_EQ(back.window.width, 1280);
    TEST_ASSERT_EQ(back.capture.game_process, std::string("Aion2Test.exe"));
    TEST_ASSERT_NEAR(back.overlay.opacity, 0.5f, 1e-6f);
    TEST_ASSERT_EQ(back.overlay.x, 120);
    TEST_ASSERT_EQ(back.meter.boss_min_max_hp, uint64_t{2'500'000});
    TEST_ASSERT_EQ(back.history.max_fights, 42);

    config::JsonValue bad = settings_to_json(s);
    bad["capture"]["game_process"] = "..\\..\\evil.exe";
    bad["overlay"]["scale"] = 99.0;
    bad["capture"]["server_port"] = 70000;
    const AppSettings clean = settings_from_json(bad);
    TEST_ASSERT_EQ(clean.capture.game_process, std::string("Aion2.exe"));
    TEST_ASSERT_NEAR(clean.overlay.scale, 2.5f, 1e-6f);
    TEST_ASSERT_EQ(clean.capture.server_port, 65535);
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
    s.power.hide_when_game_unfocused = true;
    const config::JsonValue json = settings_to_json(s);
    TEST_ASSERT(json["combat_power"]["plugin_enabled"].as_bool(false));
    const AppSettings back = settings_from_json(json);
    TEST_ASSERT(back.plugins.combat_power);
    TEST_ASSERT(back.power.locked);
    TEST_ASSERT_EQ(back.power.x, 700);
    TEST_ASSERT_EQ(back.power.width, 300);
    TEST_ASSERT_FALSE(back.power.show_list);
    TEST_ASSERT_EQ(back.power.list_max_players, 4);
    TEST_ASSERT(back.power.hide_when_game_unfocused);
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
    // A local player who dealt no damage is ranked with the healers.
    rows[5].damage = 0;
    TEST_ASSERT(ranks(shown_rows(rows, 3, false)) == (Ranks{{0, 1}, {1, 2}, {3, 3}}));
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
