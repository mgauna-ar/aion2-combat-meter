#include "test_framework.hpp"
#include "file_test_support.hpp"
#include "synthetic_session.hpp"

#include "app/app_runtime.hpp"
#include "app/app_settings.hpp"
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
    const AppSettings back = settings_from_json(settings_to_json(s));
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
}

TEST_CASE(TimeFormat, StampsAndClocks) {
    const Micros t = 1'792'152'000'000'000ULL + 5 * kMicrosPerSecond;
    TEST_ASSERT_EQ(common::utc_file_stamp(t), std::string("20261016-120005Z"));
    TEST_ASSERT_EQ(common::clock_duration(95.4), std::string("1:35"));
    TEST_ASSERT_EQ(common::clock_duration(3723), std::string("1:02:03"));
    const auto leap = common::utc_time(1'709'208'000'000'000ULL);  // 2024-02-29 12:00:00Z
    TEST_ASSERT_EQ(leap.month, 2u);
    TEST_ASSERT_EQ(leap.day, 29u);
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

TEST_CASE(AppRuntime, MissingCaptureFileFailsToStart) {
    ScratchDir dir("aion2_test_runtime_missing");
    AppRuntime runtime(AppSettings{}, dir.path);
    std::string error;
    TEST_ASSERT_FALSE(runtime.start(std::make_unique<capture::FileSource>(dir.path / "none.pcapng", 1.0), error));
    TEST_ASSERT_FALSE(runtime.source_status().empty());
}
