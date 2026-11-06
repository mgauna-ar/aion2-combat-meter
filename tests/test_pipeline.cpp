#include "test_framework.hpp"
#include "synthetic_session.hpp"

#include "capture/capture_file.hpp"
#include "pipeline/pipeline.hpp"

#include <functional>

using namespace aion2;
using namespace aion2::pipeline;
using namespace aion2::test::synthetic;

namespace {

/// Runs `bytes` (a capture file) through a pipeline, as aion2meter-replay does.
/// `before` sees each packet's time first.
std::vector<meter::FightSummary> replay(const Bytes& bytes, Pipeline& p,
                                        const std::function<void(Micros)>& before = {}) {
    std::string error;
    auto reader = capture::CaptureFileReader::from_bytes(bytes, error);
    if (!reader) throw std::runtime_error(error);
    capture::CapturedPacket pkt;
    Micros last = 0;
    while (reader->next(pkt)) {
        if (before) before(pkt.ts);
        p.on_packet(pkt.link_type, pkt.data, pkt.ts);
        last = pkt.ts;
    }
    p.tick(last + 60 * kMicrosPerSecond);
    return p.take_finished();
}

PipelineOptions only(bool meter, bool auras) {
    PipelineOptions o;
    o.plugins.combat_meter = meter;
    o.plugins.auras = auras;
    return o;
}

} // namespace

TEST_CASE(Pipeline, SyntheticBossFightEndToEnd) {
    const Session session = boss_session();
    Pipeline p;
    const auto fights = replay(session.pcapng, p);

    TEST_ASSERT_EQ(fights.size(), size_t{1});
    const meter::FightSummary& f = fights[0];
    TEST_ASSERT(f.end_reason == meter::EndReason::BossKilled);
    TEST_ASSERT_EQ(f.map_id, kDungeon);
    TEST_ASSERT(f.boss.is_boss);
    TEST_ASSERT(f.boss.killed);
    TEST_ASSERT_EQ(f.boss.npc_code, kBossNpc);
    TEST_ASSERT_EQ(f.boss.label, std::string("Ultimate Berk"));
    TEST_ASSERT_EQ(f.boss.max_hp, kBossHp);
    TEST_ASSERT_FALSE(f.quality.partial);
    TEST_ASSERT(f.quality.clean());
    TEST_ASSERT_NEAR(f.duration_s, session.truth.last_hit_s - session.truth.first_hit_s, 1e-6);

    TEST_ASSERT_EQ(f.combatants.size(), size_t{3});
    uint64_t total = 0;
    for (const auto& c : f.combatants) {
        TEST_ASSERT_EQ(c.damage, session.truth.damage.at(c.id));
        total += c.damage;
        TEST_ASSERT(c.in_party);
    }
    TEST_ASSERT_EQ(f.total_damage, total);
    TEST_ASSERT_EQ(total, session.truth.boss_damage);

    const auto names = [&f] {
        std::vector<std::string> out;
        for (const auto& c : f.combatants) out.push_back(c.name);
        return out;
    }();
    TEST_ASSERT(std::find(names.begin(), names.end(), "Ariel") != names.end());
    TEST_ASSERT(std::find(names.begin(), names.end(), "Zikel") != names.end());

    const auto& stats = p.parser().stats();
    TEST_ASSERT_EQ(stats.failed, uint64_t{0});
    TEST_ASSERT_EQ(p.demux().game_resyncs(), uint64_t{0});
}

TEST_CASE(Pipeline, SnapshotReportsCaptureState) {
    const Session session = boss_session();
    Pipeline p;
    TEST_ASSERT(p.snapshot(0).capture == CaptureState::Waiting);
    (void)replay(session.pcapng, p);
    const Micros end = kStartMs * kMicrosPerMilli + 80 * kMicrosPerSecond;
    const LiveSnapshot live = p.snapshot(end);
    TEST_ASSERT(live.capture == CaptureState::Live);
    TEST_ASSERT_EQ(live.local_name, std::string("Ariel"));
    TEST_ASSERT_EQ(live.map_id, kTown);
    TEST_ASSERT_EQ(live.recent.size(), size_t{1});
    TEST_ASSERT(p.snapshot(end + Pipeline::kQuietAfter).capture == CaptureState::Quiet);
}

TEST_CASE(Pipeline, ReplayIsDeterministic) {
    const Session session = boss_session();
    Pipeline a;
    Pipeline b;
    const auto fa = replay(session.pcapng, a);
    const auto fb = replay(session.pcapng, b);
    TEST_ASSERT_EQ(fa.size(), fb.size());
    TEST_ASSERT_EQ(fa[0].total_damage, fb[0].total_damage);
    TEST_ASSERT_EQ(fa[0].end_ts, fb[0].end_ts);
}

TEST_CASE(Pipeline, APluginSwitchedOffGetsNothing) {
    const Session session = boss_session();
    const Micros end = kStartMs * kMicrosPerMilli + 80 * kMicrosPerSecond;

    Pipeline both;
    const auto fights = replay(session.pcapng, both);
    TEST_ASSERT_EQ(fights.size(), size_t{1});
    TEST_ASSERT(!both.snapshot(end).auras.seen.empty());

    Pipeline meter_only(only(true, false));
    const auto same = replay(session.pcapng, meter_only);
    TEST_ASSERT_EQ(same.size(), size_t{1});
    TEST_ASSERT_EQ(same[0].total_damage, fights[0].total_damage);
    TEST_ASSERT(meter_only.snapshot(end).auras == auras::AuraSnapshot{});

    Pipeline auras_only(only(false, true));
    TEST_ASSERT(replay(session.pcapng, auras_only).empty());
    TEST_ASSERT(auras_only.snapshot(end).fight.id == 0);
    TEST_ASSERT(auras_only.snapshot(end).auras.seen == both.snapshot(end).auras.seen);
}

TEST_CASE(Pipeline, SwitchingTheMeterOffEndsItsFightAndTheAurasForget) {
    const Session session = boss_session();
    const Micros cut = kStartMs * kMicrosPerMilli + from_seconds(session.truth.first_hit_s + 1.0);
    Pipeline p;
    bool switched = false;
    const auto fights = replay(session.pcapng, p, [&](Micros ts) {
        if (switched || ts < cut) return;
        switched = true;
        TEST_ASSERT(p.snapshot(ts).fight.live);
        TEST_ASSERT(!p.snapshot(ts).auras.seen.empty());
        p.set_plugins(aion2::plugins::PluginSwitches{false, false}, ts);
        TEST_ASSERT(p.snapshot(ts).auras == auras::AuraSnapshot{});
    });
    TEST_ASSERT(switched);
    TEST_ASSERT_EQ(fights.size(), size_t{1});
    TEST_ASSERT(fights[0].end_reason == meter::EndReason::Manual);
    TEST_ASSERT(fights[0].total_damage < session.truth.boss_damage);
}
