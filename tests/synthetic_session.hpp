#pragma once

// A made-up dungeon boss fight as a whole pcapng capture, wire bytes and all: the
// end-to-end tests run it through the pipeline, and tools/synth writes it to disk
// for trying aion2meter-replay and drawing the overlay without the game. Every
// name in it is invented.

#include "capture/packet_headers.hpp"
#include "capture/pcapng_writer.hpp"
#include "protocol/entity_id.hpp"
#include "protocol/opcodes.hpp"
#include "wire_builder.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace aion2::test::synthetic {

using namespace aion2::test::wire;

inline constexpr uint64_t kStartMs = 1'792'152'000'000ULL;  // 2026-10-16 12:00:00 UTC
inline constexpr uint64_t kMe = 1001;
inline constexpr uint64_t kAlly = 1002;
inline constexpr uint64_t kAlly2 = 1003;
inline constexpr uint64_t kBossRaw = 2'000'123;
inline constexpr uint64_t kSpiritRaw = 3'000'001;
inline constexpr uint32_t kBossNpc = 0x0A1B2C;
inline constexpr uint64_t kBossHp = 3'000'000;
inline constexpr uint32_t kDungeon = 610010;
inline constexpr uint32_t kTown = 200100;

/// What the capture holds, to check the meter against.
struct Truth {
    std::map<uint64_t, uint64_t> damage;  // by player, summon folded into its owner
    double first_hit_s{0};
    double last_hit_s{0};
    double death_s{0};
    uint64_t boss_damage{0};
};

struct Session {
    Bytes pcapng;
    Truth truth;
};

/// Frames queued per timestamp, sent as segments when the clock moves on.
class SessionWriter {
public:
    SessionWriter() {
        TcpSpec syn;
        syn.seq = m_seq - 1;
        syn.flags = capture::tcp_flag::Syn | capture::tcp_flag::Ack;
        m_file.add_packet(capture::link_type::Ethernet, ts(0), ethernet_tcp(syn, Bytes{}));
    }

    void at(double seconds, const Bytes& frame) {
        if (seconds != m_pending_s) flush();
        m_pending_s = seconds;
        put_bytes(m_pending, frame);
    }

    Bytes finish() {
        flush();
        return m_file.bytes();
    }

private:
    static Micros ts(double seconds) { return kStartMs * kMicrosPerMilli + from_seconds(seconds); }

    void flush() {
        constexpr size_t kMss = 1400;
        for (size_t at = 0; at < m_pending.size(); at += kMss) {
            const size_t n = std::min(kMss, m_pending.size() - at);
            TcpSpec spec;
            spec.seq = m_seq;
            const std::span<const uint8_t> part(m_pending.data() + at, n);
            m_file.add_packet(capture::link_type::Ethernet, ts(m_pending_s), ethernet_tcp(spec, part));
            m_seq += static_cast<uint32_t>(n);
        }
        m_pending.clear();
    }

    capture::PcapngBuilder m_file;
    uint32_t m_seq{1000};
    double m_pending_s{0};
    Bytes m_pending;
};

/// The party (Ariel, Lumiel, Zikel and Zikel's spirit) kills a boss in a dungeon,
/// then goes back to town. Heartbeats every 50 ms throughout; some hits arrive in
/// LZ4 bundles, as the server sends them.
inline Session boss_session() {
    using proto::opcode::Damage;
    SessionWriter w;
    Truth truth;
    const auto boss = static_cast<uint64_t>(proto::normalize_entity_id(kBossRaw));
    const auto spirit = static_cast<uint64_t>(proto::normalize_entity_id(kSpiritRaw));

    // Heartbeats are interleaved by time below; the script runs to 80 s.
    std::vector<std::pair<double, Bytes>> script;
    for (int i = 0; i <= 80 * 20; ++i) script.emplace_back(i * 0.05, heartbeat(kStartMs + static_cast<uint64_t>(i) * 50));

    script.emplace_back(0.001, opcode_frame(proto::opcode::OwnCharacter, identity_body(kMe, "Ariel", 0x0123, 7)));
    script.emplace_back(0.002, opcode_frame(proto::opcode::MapLoad, map_load_body(1, kDungeon)));
    script.emplace_back(0.003, opcode_frame(proto::opcode::OtherPlayer, identity_body(kAlly, "Lumiel", 0x0123, 3)));
    script.emplace_back(0.004, opcode_frame(proto::opcode::OtherPlayer, identity_body(kAlly2, "Zikel", 0x0123, 5)));
    script.emplace_back(0.005, opcode_frame(proto::opcode::PartyRoster, party_body({"Ariel", "Lumiel", "Zikel"})));
    SpawnSpec b;
    b.raw_id = kBossRaw;
    b.npc_code = kBossNpc;
    b.cur_hp = kBossHp;
    b.max_hp = kBossHp;
    script.emplace_back(0.006, opcode_frame(proto::opcode::Spawn, spawn_body(b)));
    SpawnSpec s;
    s.raw_id = kSpiritRaw;
    s.kind = proto::spawn_kind::Summon;
    s.owner = static_cast<uint32_t>(kAlly2);
    s.cur_hp = 20'000;
    s.max_hp = 20'000;
    script.emplace_back(0.007, opcode_frame(proto::opcode::Spawn, spawn_body(s)));

    uint64_t boss_taken = 0;
    bool dead = false;
    const auto land = [&](double t, uint64_t actor, uint64_t credit_to, uint64_t amount, bool crit, bool back,
                          bool bundled) {
        if (dead) return;
        DamageSpec d;
        d.target = boss;
        d.actor = actor;
        d.amount = amount;
        d.crit = crit;
        d.direction = back ? 1 : 2;
        // Only the long layouts carry a direction, as in a real capture.
        d.layout = back ? 6 : crit ? 5 : 4;
        const Bytes frame = opcode_frame(Damage, damage_body({d}));
        script.emplace_back(t, bundled ? bundle_frame(frame) : frame);
        truth.damage[credit_to] += amount;
        if (truth.first_hit_s == 0) truth.first_hit_s = t;
        truth.last_hit_s = t;
        boss_taken += amount;
        // The server sends the boss's HP after each hit; 0 is the kill, and the
        // despawn (42 36) follows about 4 s later.
        const uint64_t hp = boss_taken >= kBossHp ? 0 : kBossHp - boss_taken;
        script.emplace_back(t + 0.001, opcode_frame(proto::opcode::RemainingHp, hp_body(boss, hp)));
        if (hp == 0) {
            dead = true;
            truth.death_s = t;
            script.emplace_back(t + 4.1, opcode_frame(proto::opcode::Death, death_body(boss)));
        }
    };

    for (int i = 0; !dead && i < 200; ++i) {
        const double t = 5.0 + 0.5 * i;
        land(t, kMe, kMe, 9'000 + static_cast<uint64_t>(i * 37 % 2000), i % 4 == 0, i % 3 == 0, i % 10 == 5);
        land(t + 0.1, kAlly, kAlly, 11'000 + static_cast<uint64_t>(i * 53 % 3000), i % 5 == 0, false, false);
        land(t + 0.2, kAlly2, kAlly2, 7'000 + static_cast<uint64_t>(i * 29 % 1500), false, i % 2 == 0, false);
        if (i % 2 == 0) land(t + 0.3, spirit, kAlly2, 3'000, false, false, false);
        if (i % 6 == 0) {
            // The boss hits back; it starts nothing and counts for nobody.
            DamageSpec back;
            back.target = kMe;
            back.actor = boss;
            back.amount = 4'000;
            script.emplace_back(t + 0.4, opcode_frame(Damage, damage_body({back})));
        }
    }
    truth.boss_damage = boss_taken;
    script.emplace_back(75.0, opcode_frame(proto::opcode::MapLoad, map_load_body(2, kTown)));

    std::stable_sort(script.begin(), script.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [t, frame] : script) w.at(t, frame);
    return Session{w.finish(), truth};
}

} // namespace aion2::test::synthetic
