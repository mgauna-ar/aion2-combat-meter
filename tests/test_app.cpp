#include "test_framework.hpp"
#include "file_test_support.hpp"
#include "synthetic_session.hpp"

#include "app/app_runtime.hpp"
#include "app/app_settings.hpp"
#include "app/overlay_visibility.hpp"
#include "app/display_format.hpp"
#include "capture/capture_file.hpp"
#include "capture/file_source.hpp"
#include "capture/os/npcap_source.hpp"
#include "common/time_format.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
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
    runtime.update_settings(meter, HistorySettings{});
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
    OverlaySettings s;
    s.locked = true;
    s.show_in_combat_only = true;
    s.hide_after_combat_s = 5.0f;
    OverlayConditions now;
    TEST_ASSERT_FALSE(overlay_shown(s, now));  // no fight yet
    now.fight_live = true;
    TEST_ASSERT(overlay_shown(s, now));
    now.fight_live = false;
    now.seconds_since_fight = 4.9;
    TEST_ASSERT(overlay_shown(s, now));
    now.seconds_since_fight = 5.0;
    TEST_ASSERT_FALSE(overlay_shown(s, now));
}

TEST_CASE(OverlayVisibility, DungeonsOnly) {
    OverlaySettings s;
    s.show_in_dungeons_only = true;
    OverlayConditions now;
    TEST_ASSERT(overlay_shown(s, now));  // unlocked: always findable
    s.locked = true;
    TEST_ASSERT_FALSE(overlay_shown(s, now));  // no map load yet, or the open world
    now.in_dungeon = true;
    TEST_ASSERT(overlay_shown(s, now));
    // Both conditions must allow it.
    s.show_in_combat_only = true;
    TEST_ASSERT_FALSE(overlay_shown(s, now));
    now.fight_live = true;
    TEST_ASSERT(overlay_shown(s, now));
}

TEST_CASE(OverlayVisibility, ConditionsWaitForTheLock) {
    OverlaySettings s;
    s.show_in_combat_only = true;
    s.hide_when_game_unfocused = true;
    OverlayConditions now;
    now.game_in_front = false;
    TEST_ASSERT(overlay_shown(s, now));  // unlocked: always findable
    s.locked = true;
    TEST_ASSERT_FALSE(overlay_shown(s, now));
    now.fight_live = true;
    TEST_ASSERT_FALSE(overlay_shown(s, now));  // in a fight, but the game is not in front
    now.game_in_front = true;
    TEST_ASSERT(overlay_shown(s, now));
    s.visible = false;
    TEST_ASSERT_FALSE(overlay_shown(s, now));  // the hotkey's hide rules over all
}
