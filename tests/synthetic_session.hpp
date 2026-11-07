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
/// Ariel's max HP, as her login's 00 8D states it (stat 7).
inline constexpr uint64_t kMeMaxHp = 25'373;
inline constexpr uint64_t kAlly = 1002;
inline constexpr uint64_t kAlly2 = 1003;
inline constexpr uint64_t kBossRaw = 2'000'123;
inline constexpr uint64_t kSpiritRaw = 3'000'001;
/// Ultimate Berk, Krao Cave's last boss, in npcs.tsv. The fight is still made up.
inline constexpr uint32_t kBossNpc = 2'300'171;
inline constexpr uint64_t kBossHp = 3'000'000;
/// A real dungeon tier's id (Krao Cave, Conquest, in maps.tsv), so the README pictures
/// show a map name. The fight in it is still made up.
inline constexpr uint32_t kDungeon = 600001;
inline constexpr uint32_t kTown = 200100;

/// A party member's death in the capture.
struct DeathTruth {
    uint64_t player{0};
    double at_s{0};
    /// When their HP came back.
    double up_s{0};
    /// The killing blow.
    uint32_t skill{0};
    uint64_t amount{0};
};

/// What the capture holds, to check the meter against.
struct Truth {
    std::map<uint64_t, uint64_t> damage;  // by player, summon folded into its owner
    /// Presses (02 38 mode 2) by player in the fight: their own, not the spirit's.
    std::map<uint64_t, uint64_t> casts;
    double first_hit_s{0};
    double last_hit_s{0};
    double death_s{0};
    uint64_t boss_damage{0};
    std::vector<DeathTruth> deaths;
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
/// LZ4 bundles, as the server sends them. Ariel bursts for five seconds, Zikel
/// stops hitting for five, and Lumiel dies to a crit and is up again 4 s later.
/// Each player presses a skill before most of their hits (every fifth hit is the
/// same press striking again), and a passive of Ariel's goes off now and then.
inline Session boss_session() {
    using proto::opcode::Damage;
    SessionWriter w;
    Truth truth;
    const auto boss = static_cast<uint64_t>(proto::normalize_entity_id(kBossRaw));
    const auto spirit = static_cast<uint64_t>(proto::normalize_entity_id(kSpiritRaw));

    // Heartbeats are interleaved by time below; the script runs to 80 s.
    std::vector<std::pair<double, Bytes>> script;
    for (int i = 0; i <= 80 * 20; ++i) script.emplace_back(i * 0.05, heartbeat(kStartMs + static_cast<uint64_t>(i) * 50));

    script.emplace_back(0.001, opcode_frame(proto::opcode::OwnCharacter, own_identity_body(kMe, "Ariel", 0x0123, 7)));
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

    // Effects, with the server's stated ends: Ariel's own buffs, Zikel's on her, and
    // her curse on the boss, renewed while it lasts. Real skill codes, so they show
    // with their names.
    const auto effect = [&](double t, uint64_t target, uint64_t caster, uint32_t instance, uint32_t skill,
                            int64_t duration_ms, bool renewed) {
        BuffSpec e;
        e.target = target;
        e.caster = caster;
        e.instance = instance;
        e.skill = skill;
        e.buff_id = skill * 10 + 1;
        e.level = 10;
        e.duration_ms = duration_ms;
        e.expires_server_ms = kStartMs + static_cast<uint64_t>(t * 1000.0) + static_cast<uint64_t>(duration_ms);
        script.emplace_back(t, opcode_frame(renewed ? proto::opcode::BuffRenew : proto::opcode::BuffAdd,
                                            buff_body(e, renewed)));
    };
    effect(4.0, kMe, kMe, 1, 15'180'001, 60'000, false);      // Vaizel's Wisdom
    effect(22.0, kMe, kMe, 2, 15'230'001, 15'000, false);     // Arctic Armor
    // Spirit Communion, twice on the whole party: a party-wide buff.
    effect(26.0, kMe, kAlly2, 3, 16'770'001, 8'000, false);
    effect(26.0, kAlly, kAlly2, 1, 16'770'001, 8'000, false);
    effect(26.0, kAlly2, kAlly2, 1, 16'770'001, 8'000, false);
    effect(40.0, kMe, kAlly2, 4, 16'770'001, 8'000, false);
    effect(40.0, kAlly, kAlly2, 2, 16'770'001, 8'000, false);
    effect(40.0, kAlly2, kAlly2, 2, 16'770'001, 8'000, false);
    effect(5.0, boss, kMe, 1, 15'320'001, 10'000, false);     // Delayed Explosion
    for (int i = 1; i < 6; ++i) effect(5.0 + 10.0 * i, boss, kMe, 1, 15'320'001, 10'000, true);
    // Ariel's skill uses, whose cooldowns the aura overlay estimates, and one the
    // server cuts and so states.
    script.emplace_back(12.0, opcode_frame(proto::opcode::SkillUse, skill_use_body(15'060'230)));   // Hellfire, 45 s
    script.emplace_back(22.0, opcode_frame(proto::opcode::SkillUse, skill_use_body(15'230'010)));   // Arctic Armor, 90 s
    script.emplace_back(25.0, opcode_frame(proto::opcode::CooldownSet, cooldown_body(15'060'000, 22'000)));
    script.emplace_back(28.8, opcode_frame(proto::opcode::SkillUse, skill_use_body(15'040'030)));   // Firestorm, 5 s
    // Ariel's HP: her max with her login, then a hit taking her to 24% just before
    // the overlay's scene at 30 s, back to full at 32 s.
    script.emplace_back(0.008, opcode_frame(proto::opcode::RemainingHp, both_values_body(kMe, kMeMaxHp, kMeMaxHp)));
    script.emplace_back(29.0, opcode_frame(proto::opcode::RemainingHp, hp_body(kMe, 9'100)));
    script.emplace_back(29.5, opcode_frame(proto::opcode::RemainingHp, hp_body(kMe, 6'100)));
    script.emplace_back(32.0, opcode_frame(proto::opcode::RemainingHp, hp_body(kMe, kMeMaxHp)));
    // Two chain skills offered to Ariel: one withdrawn 0.8 s later, as a use would,
    // and one let run out, withdrawn 3 s after.
    script.emplace_back(29.6, opcode_frame(proto::opcode::ChainOffered, chain_offer_body(15'010'230, 15'030'230)));
    script.emplace_back(30.4, opcode_frame(proto::opcode::ChainWithdrawn, chain_end_body(15'030'230)));
    script.emplace_back(41.0, opcode_frame(proto::opcode::ChainOffered, chain_offer_body(15'080'230, 15'090'230)));
    script.emplace_back(44.0, opcode_frame(proto::opcode::ChainWithdrawn, chain_end_body(15'090'230)));
    // Presses that land no hit: Ariel's Arctic Armor (as its 01 38 above) and her
    // Dodges. And Fire Mark, a passive, going off: no press.
    const auto cast = [&](double t, uint64_t caster, uint32_t skill, uint64_t target, bool pressed) {
        script.emplace_back(t, opcode_frame(proto::opcode::Cast, cast_body(caster, skill, target, pressed)));
    };
    cast(22.0, kMe, 15'230'010, kMe, true);
    truth.casts[kMe] += 1;
    for (const double t : {18.2, 33.2, 47.2}) {
        cast(t, kMe, 15'000'100, kMe, true);
        truth.casts[kMe] += 1;
    }
    for (int i = 0; i < 12; ++i) cast(6.3 + 4.0 * i, kMe, 15'710'001, kMe, false);

    // Lumiel's death: two hits, a heal, and a crit that kills; her HP back 4 s later.
    // She does not hit while down.
    DeathTruth lumiel{kAlly, 30.15, 34.15, 900'102, 60'000};
    truth.deaths.push_back(lumiel);
    const auto boss_hits = [&](double t, uint64_t target, uint32_t skill, uint64_t amount, bool crit) {
        DamageSpec h;
        h.target = target;
        h.actor = boss;
        h.skill = skill;
        h.amount = amount;
        h.crit = crit;
        if (crit) h.layout = 5;
        script.emplace_back(t, opcode_frame(Damage, damage_body({h})));
    };
    boss_hits(27.4, kAlly, 900'101, 4'000, false);
    boss_hits(28.9, kAlly, 900'101, 4'000, false);
    script.emplace_back(29.45, opcode_frame(proto::opcode::Tick, tick_body(kAlly, 0x09, kAlly2, 16770000, 1'800)));
    boss_hits(lumiel.at_s, kAlly, lumiel.skill, lumiel.amount, true);
    script.emplace_back(lumiel.at_s + 0.0005, opcode_frame(proto::opcode::RemainingHp, hp_body(kAlly, 0)));
    script.emplace_back(lumiel.up_s, opcode_frame(proto::opcode::RemainingHp, hp_body(kAlly, 52'000)));

    uint64_t boss_taken = 0;
    bool dead = false;
    // Real skill codes, so classes and skill names come out as they would in game:
    // a Sorcerer, a Gladiator, and an Elementalist with a spirit.
    const uint32_t sorcerer[] = {15210000, 15040000, 15050000, 15060000};
    const uint32_t gladiator[] = {11010000, 11020000, 11040000, 11050000};
    const uint32_t elementalist[] = {16020030, 16030030, 16040020, 16060010};
    const auto land = [&](double t, uint64_t actor, uint64_t credit_to, uint64_t amount, bool crit, bool back,
                          bool bundled, uint32_t skill, uint8_t modifiers, bool pressed) {
        if (dead) return;
        if (actor == kAlly && t > lumiel.at_s && t < lumiel.up_s) return;
        // Zikel steps out for five seconds; the spirit goes on.
        if (actor == kAlly2 && t >= 36.0 && t < 41.0) return;
        // The press comes about 80 ms ahead of its hit, the first one before the
        // fight's first hit.
        if (pressed) {
            cast(t - 0.08, actor, skill, boss, true);
            truth.casts[credit_to] += 1;
        }
        DamageSpec d;
        d.target = boss;
        d.actor = actor;
        d.skill = skill;
        d.amount = amount;
        d.crit = crit;
        d.direction = back ? 1 : 2;
        // Only the long layouts carry a direction and modifiers, as in a real capture.
        d.layout = back ? 6 : crit ? 5 : 4;
        if (d.layout != 4) d.modifiers = modifiers;
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
        const auto pick = [i](const uint32_t (&skills)[4]) { return skills[(i * 7 + i / 3) % 4]; };
        const uint8_t mods = static_cast<uint8_t>((i % 7 == 0 ? proto::hit_modifier::Perfect : 0) |
                                                  (i % 9 == 0 ? proto::hit_modifier::Double : 0));
        // Ariel's burst: more than twice her usual for five seconds.
        const uint64_t ariel = 9'000 + static_cast<uint64_t>(i * 37 % 2000);
        const bool pressed = i % 5 != 4;
        land(t, kMe, kMe, t >= 15.0 && t < 20.0 ? ariel * 22 / 10 : ariel, i % 4 == 0, i % 3 == 0, i % 10 == 5,
             pick(sorcerer), mods, pressed);
        land(t + 0.1, kAlly, kAlly, 11'000 + static_cast<uint64_t>(i * 53 % 3000), i % 5 == 0, false, false,
             pick(gladiator), 0, pressed);
        land(t + 0.2, kAlly2, kAlly2, 7'000 + static_cast<uint64_t>(i * 29 % 1500), false, i % 2 == 0, false,
             pick(elementalist), mods, pressed);
        // A spirit's skills are its own: no press of its owner's.
        if (i % 2 == 0) land(t + 0.3, spirit, kAlly2, 3'000, false, false, false, 16120002, 0, false);
        if (i % 6 == 0) {
            // The boss hits back: damage taken, which starts nothing.
            DamageSpec back;
            back.target = kMe;
            back.actor = boss;
            back.skill = 900101;
            back.amount = 4'000;
            script.emplace_back(t + 0.4, opcode_frame(Damage, damage_body({back})));
        }
        if (i % 12 == 3) {
            // The Elementalist's heals over time on the other two.
            const auto heal = static_cast<uint64_t>(1'800 + i * 11 % 700);
            const uint32_t communion = 16770000;  // Spirit Communion
            script.emplace_back(t + 0.45, opcode_frame(proto::opcode::Tick, tick_body(kMe, 0x09, kAlly2, communion, heal)));
            script.emplace_back(t + 0.46,
                                opcode_frame(proto::opcode::Tick, tick_body(kAlly, 0x09, kAlly2, communion, heal / 2)));
        }
    }
    truth.boss_damage = boss_taken;
    script.emplace_back(75.0, opcode_frame(proto::opcode::MapLoad, map_load_body(2, kTown)));

    std::stable_sort(script.begin(), script.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [t, frame] : script) w.at(t, frame);
    return Session{w.finish(), truth};
}

} // namespace aion2::test::synthetic
