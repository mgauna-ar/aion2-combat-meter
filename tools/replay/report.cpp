#include "report.hpp"

#include "hex_dump.hpp"
#include "meter/fight_detail.hpp"

#include "game/map_names.hpp"
#include "game/npc_names.hpp"
#include "game/skill_names.hpp"
#include "protocol/opcodes.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <string_view>
#include <type_traits>
#include <variant>

namespace aion2::replay {

namespace {

const char* role_name(pipeline::FlowRole role) {
    switch (role) {
        case pipeline::FlowRole::Game: return "game";
        case pipeline::FlowRole::Candidate: return "candidate";
        case pipeline::FlowRole::Rejected: return "not game";
    }
    return "?";
}

/// " <name>" after an id a table names, else nothing: the id stays first, since a
/// capture investigation works from it.
std::string name_suffix(std::string_view name) { return name.empty() ? std::string() : " " + std::string(name); }

std::string map_suffix(uint32_t map_id) { return name_suffix(game::map_name(map_id)); }

std::string modifiers(uint8_t m, bool crit, uint8_t direction) {
    std::string out;
    const auto add = [&out](const char* s) {
        if (!out.empty()) out += ',';
        out += s;
    };
    if (crit) add("crit");
    if (direction == proto::hit_direction::Back) add("back");
    if (direction == proto::hit_direction::Front) add("front");
    if ((m & proto::hit_modifier::Block) != 0) add("block");
    if ((m & proto::hit_modifier::Perfect) != 0) add("perfect");
    if ((m & proto::hit_modifier::Double) != 0) add("double");
    if ((m & proto::hit_modifier::Heavy) != 0) add("heavy");
    return out.empty() ? out : " [" + out + "]";
}

} // namespace

double Report::rel(Micros ts) const { return ts >= m_first_ts ? to_seconds(ts - m_first_ts) : 0.0; }

std::string Report::name(const std::string& real, uint32_t id) const {
    if (m_opt.anonymize || real.empty()) return "#" + std::to_string(id);
    return real;
}

std::string Report::who(uint32_t id, const meter::CombatantRegistry& registry) const {
    const meter::Actor* a = registry.find(id);
    const meter::Character* c = registry.character(registry.character_of(id));
    std::string label;
    if (c != nullptr) {
        label = name(c->name, id);
    } else if (a != nullptr && a->npc_code != 0) {
        label = game::npc_label(a->npc_code);
    } else {
        label = "#" + std::to_string(id);
    }
    if (label != "#" + std::to_string(id)) label += "(" + std::to_string(id) + ")";
    const meter::EntityId owner = registry.resolve(id);
    if (owner != id) label += "<-" + name(registry.display_name(owner), owner);
    return label;
}

void Report::dump_frame(const proto::Frame& f, const char* why) {
    size_t& dumped = m_dumped[f.opcode];
    if (m_opt.dump_limit != 0 && dumped >= m_opt.dump_limit) return;
    ++dumped;
    std::printf("%+.3f  %02x %02x %-14s %s depth %u flag %02x  offset %" PRIu64 "  body %zu bytes\n", rel(f.ts),
                unsigned{f.opcode} >> 8, unsigned{f.opcode} & 0xFFu, std::string(proto::opcode_name(f.opcode)).c_str(),
                why, unsigned{f.depth}, unsigned{f.flag}, f.stream_offset, f.body.size());
    std::fputs(hex_dump(f.body).c_str(), stdout);
}

void Report::on_frame(const proto::Frame& f, pipeline::FrameOutcome outcome) {
    if (m_first_ts == 0) m_first_ts = f.ts;
    m_last_ts = f.ts;
    auto& s = m_stats[f.opcode];
    ++s.count;
    s.min = s.count == 1 ? f.body.size() : std::min(s.min, f.body.size());
    s.max = std::max(s.max, f.body.size());
    s.total += f.body.size();
    if (f.depth > 0) ++s.in_bundle;
    if (f.flag != 0) ++s.flagged;
    if (outcome == pipeline::FrameOutcome::Failed) ++s.failed;

    if (m_opt.dump_all || m_opt.dump.count(f.opcode) != 0) {
        dump_frame(f, "");
    } else if (m_opt.unknown &&
               (outcome == pipeline::FrameOutcome::Failed || outcome == pipeline::FrameOutcome::Unknown)) {
        dump_frame(f, outcome == pipeline::FrameOutcome::Failed ? "FAILED" : "");
    }
}

void Report::on_event(const proto::ParsedEvent& pe, const meter::CombatantRegistry& reg) {
    if (!m_opt.events && !m_opt.auras) return;
    const double t = rel(pe.ts);
    const bool aura = std::holds_alternative<proto::BuffApplied>(pe.event) ||
                      std::holds_alternative<proto::BuffRemoved>(pe.event) ||
                      std::holds_alternative<proto::SkillUsed>(pe.event) ||
                      std::holds_alternative<proto::CooldownSet>(pe.event);
    if (aura ? !m_opt.auras : !m_opt.events) return;
    std::visit(
        [&](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, proto::BuffApplied>) {
                std::printf("%+.3f  %s %s <- %s  %s (effect %u", t, e.renewed ? "buff~    " : "buff+    ",
                            who(e.target, reg).c_str(), who(e.caster, reg).c_str(),
                            game::effect_label(e.buff_id, e.source_skill).c_str(), e.buff_id);
                if (e.source_skill != 0) std::printf(", skill %u lv %u", e.source_skill, unsigned{e.level});
                std::printf(") instance %u ", e.instance);
                if (e.permanent) {
                    std::printf("permanent\n");
                } else {
                    std::printf("%.1f s\n", static_cast<double>(e.duration_ms) / 1000.0);
                }
            } else if constexpr (std::is_same_v<T, proto::BuffRemoved>) {
                std::printf("%+.3f  buff-     %s  %s (effect %u)\n", t, who(e.target, reg).c_str(),
                            game::effect_label(e.buff_id).c_str(), e.buff_id);
            } else if constexpr (std::is_same_v<T, proto::SkillUsed>) {
                std::printf("%+.3f  use       %s (%u)\n", t, game::skill_label(e.skill).c_str(), e.skill);
            } else if constexpr (std::is_same_v<T, proto::CooldownSet>) {
                std::printf("%+.3f  cooldown  %s (%u) %.2f s left\n", t, game::skill_label(e.skill).c_str(), e.skill,
                            static_cast<double>(e.remaining_ms) / 1000.0);
            } else if constexpr (std::is_same_v<T, proto::SkillCast>) {
                std::printf("%+.3f  cast      %s -> %s  %s (%u) %s\n", t, who(e.caster, reg).c_str(),
                            who(e.target, reg).c_str(), game::skill_label(e.skill).c_str(), e.skill,
                            e.pressed ? "pressed" : "proc");
            } else if constexpr (std::is_same_v<T, proto::Heartbeat>) {
                // Twenty a second: the flow summary counts them.
            } else if constexpr (std::is_same_v<T, proto::Identity>) {
                std::printf("%+.3f  identity  %s%s %u \"%s\" server %u class %u\n", t, e.own ? "own " : "",
                            e.scanned ? "scanned" : "", e.id, name(e.name, e.id).c_str(), unsigned{e.server},
                            e.class_code);
            } else if constexpr (std::is_same_v<T, proto::Spawn>) {
                std::printf("%+.3f  spawn     %u kind %02x npc %u%s", t, e.id, unsigned{e.kind}, e.npc_code,
                            name_suffix(game::npc_name(e.npc_code)).c_str());
                if (e.has_hp) std::printf(" hp %" PRIu64 "/%" PRIu64, e.cur_hp, e.max_hp);
                if (e.owner != 0) std::printf(" owner %u", e.owner);
                if (!e.aimed_at.empty()) {
                    std::printf(" aimed at %s", m_opt.anonymize ? "a player" : ("\"" + e.aimed_at + "\"").c_str());
                }
                std::printf("\n");
            } else if constexpr (std::is_same_v<T, proto::Death>) {
                std::printf("%+.3f  death     %s\n", t, who(e.id, reg).c_str());
            } else if constexpr (std::is_same_v<T, proto::MapLoad>) {
                std::printf("%+.3f  map       %u%s (load %u)%s connection %u\n", t, e.map_id,
                            map_suffix(e.map_id).c_str(), e.load_count, e.is_instance() ? " instance" : "",
                            pe.connection);
            } else if constexpr (std::is_same_v<T, proto::Damage>) {
                std::printf("%+.3f  damage    %s -> %s %" PRIu64, t, who(e.actor, reg).c_str(),
                            who(e.target, reg).c_str(), e.amount);
                if (!e.extra_hits.empty()) std::printf(" (%zu extra)", e.extra_hits.size());
                std::printf("%s skill %u layout %u\n", modifiers(e.modifiers, e.crit, e.direction).c_str(), e.skill,
                            unsigned{e.layout});
            } else if constexpr (std::is_same_v<T, proto::Tick>) {
                std::printf("%+.3f  %s %s -> %s %" PRIu64 " skill %u effect %02x\n", t, e.heal ? "hot      " : "dot      ",
                            who(e.actor, reg).c_str(), who(e.target, reg).c_str(), e.amount, e.skill, unsigned{e.effect});
            } else if constexpr (std::is_same_v<T, proto::PartyRoster>) {
                std::printf("%+.3f  party    ", t);
                for (size_t i = 0; i < e.members.size(); ++i) {
                    std::printf(" %s", m_opt.anonymize ? ("member" + std::to_string(i + 1)).c_str()
                                                       : e.members[i].name.c_str());
                }
                std::printf("\n");
            } else if constexpr (std::is_same_v<T, proto::RemainingHp>) {
                if (e.self) {
                    std::printf("%+.3f  self      %s\n", t, who(e.id, reg).c_str());
                } else if (e.has_hp) {
                    std::printf("%+.3f  hp        %s %" PRIu64 "\n", t, who(e.id, reg).c_str(), e.hp);
                }
            }
        },
        pe.event);
}

void Report::on_fight(const meter::FightSummary& f) {
    ++m_fights;
    if (!m_opt.fights) return;
    std::printf("\nfight %" PRIu64 "  map %u%s  at %+.2f s  %.2f s  end %s\n", f.id, f.map_id,
                map_suffix(f.map_id).c_str(), rel(f.start_ts), f.duration_s,
                std::string(meter::end_reason_name(f.end_reason)).c_str());
    if (f.boss.present()) {
        std::printf("  target %s  max hp %" PRIu64 "%s%s", f.boss.label.c_str(), f.boss.max_hp,
                    f.boss.is_boss ? "  boss" : "", f.boss.killed ? "  killed" : "");
        if (f.boss.hp_known && !f.boss.killed) std::printf("  hp %s%" PRIu64, f.boss.hp_exact ? "" : "~", f.boss.hp);
        std::printf("\n");
    }
    std::printf("  total %" PRIu64 " damage, %.1f dps\n", f.total_damage, f.total_dps);
    std::printf("  %-2s %-16s %-4s %12s %10s %6s %6s %6s %6s %9s\n", "#", "name", "cls", "damage", "dps", "share",
                "hits", "crit%", "back%", "max hit");
    int rank = 0;
    for (const auto& c : f.combatants) {
        std::printf("  %-2d %-16s %-4s %12" PRIu64 " %10.1f %5.1f%% %6" PRIu64 " %6.1f %6.1f %9" PRIu64 "%s\n", ++rank,
                    name(c.name.starts_with('#') ? std::string() : c.name, c.id).c_str(),
                    std::string(game::class_short(c.cls)).c_str(), c.damage, c.dps, 100.0 * c.share, c.hits.hits,
                    100.0 * c.crit_rate(), 100.0 * c.back_rate(), c.max_hit, c.is_local ? "  (you)" : "");
    }
    if (m_opt.skills) {
        for (const auto& c : f.combatants) print_breakdown(c);
    }
    print_detail(f);
    print_casts(f);
    const auto& q = f.quality;
    std::printf("  capture %s: %" PRIu64 " gap bytes, %" PRIu64 " resyncs, %" PRIu64 " bundle errors%s\n",
                q.clean() ? "clean" : "lossy", q.gap_bytes, q.resyncs, q.bundle_errors,
                q.partial ? ", partial (server sent only some players' hits)" : "");
}

void Report::print_breakdown(const meter::CombatantTotals& c) const {
    const std::string who = name(c.name.starts_with('#') ? std::string() : c.name, c.id);
    std::printf("  %s: %s\n", who.c_str(), std::string(game::class_name(c.cls)).c_str());
    if (!c.skills.empty()) {
        std::printf("    %-26s %9s %6s %5s %5s %6s %6s %6s %6s %6s %6s %6s %8s\n", "skill", "damage", "share", "hits",
                    "ticks", "crit%", "perf%", "dbl%", "back%", "frnt%", "mult%", "blk%", "max");
    }
    for (const auto& s : c.skills) {
        const double share = c.damage == 0 ? 0.0 : 100.0 * static_cast<double>(s.damage) / static_cast<double>(c.damage);
        std::printf("    %-26.26s %9" PRIu64 " %5.1f%% %5" PRIu64 " %5" PRIu64
                    " %6.1f %6.1f %6.1f %6.1f %6.1f %6.1f %6.1f %8" PRIu64 "\n",
                    game::skill_label(s.code).c_str(), s.damage, share, s.hits.hits, s.ticks,
                    100.0 * s.hits.rate(&meter::HitCounts::crits), 100.0 * s.hits.rate(&meter::HitCounts::perfect),
                    100.0 * s.hits.rate(&meter::HitCounts::doubles), 100.0 * s.hits.rate(&meter::HitCounts::back),
                    100.0 * s.hits.rate(&meter::HitCounts::front), 100.0 * s.hits.rate(&meter::HitCounts::multi),
                    100.0 * s.hits.rate(&meter::HitCounts::blocks), s.max_hit);
    }
    if (c.healing > 0) {
        std::printf("    healing %" PRIu64 " (%.1f hps, %" PRIu64 " heals)\n", c.healing, c.hps, c.heal_hits);
        for (const auto& s : c.heal_skills) {
            std::printf("      %-26.26s %9" PRIu64 " %5" PRIu64 " heals\n", game::skill_label(s.code).c_str(), s.amount,
                        s.hits);
        }
    }
    if (c.damage_taken > 0) {
        std::printf("    taken %" PRIu64 " (%" PRIu64 " hits, %.1f%% crit)\n", c.damage_taken, c.taken.hits,
                    100.0 * c.taken.rate(&meter::HitCounts::crits));
        for (const auto& s : c.taken_by) {
            const std::string from = s.source_npc != 0 ? game::npc_label(s.source_npc) : std::string("?");
            std::printf("      %-26.26s %-24s %9" PRIu64 " %5" PRIu64 " hits\n", game::skill_label(s.code).c_str(),
                        from.c_str(), s.amount, s.hits);
        }
    }
}

void Report::print_detail(const meter::FightSummary& f) const {
    if (!m_opt.deaths && !m_opt.active && !m_opt.effects) return;
    if (f.detail == nullptr) {
        std::printf("  no detail\n");
        return;
    }
    const meter::FightDetail& d = *f.detail;
    if (m_opt.active) {
        for (const auto& c : f.combatants) {
            std::printf("  active  %-16s %7.1f s  %5.1f%%\n",
                        name(c.name.starts_with('#') ? std::string() : c.name, c.id).c_str(), c.active_s,
                        100.0 * c.active_share(f.duration_s));
        }
    }
    if (m_opt.deaths) {
        std::printf("  deaths %zu\n", d.deaths.size());
        for (const meter::DeathRecord& death : d.deaths) {
            const meter::RecapEvent* blow = death.killing_blow();
            std::printf("    %7.2f s  %-16s", death.time_s, name(death.name, death.player).c_str());
            if (blow != nullptr) {
                std::printf("  by %s%s %" PRIu64 "%s", game::skill_label(blow->skill).c_str(),
                            blow->source_npc != 0 ? (" (" + game::npc_label(blow->source_npc) + ")").c_str() : "",
                            blow->amount, blow->crit ? " crit" : "");
            } else {
                std::printf("  no hit seen");
            }
            if (death.up()) {
                std::printf("  up after %.2f s\n", death.up_after_s);
            } else {
                std::printf("  still down\n");
            }
            for (const meter::RecapEvent& e : death.recap) {
                const std::string from = !e.damage()      ? (m_opt.anonymize ? std::string("a healer") : e.healer)
                                         : e.source_npc != 0 ? game::npc_label(e.source_npc)
                                                             : std::string("?");
                std::printf("      -%5.2f s  %-4s %-26.26s %-20.20s %c%" PRIu64 "%s\n", e.before_s,
                            std::string(meter::recap_kind_name(e.kind)).c_str(), game::skill_label(e.skill).c_str(),
                            from.c_str(), e.damage() ? '-' : '+', e.amount, e.crit ? " crit" : "");
            }
        }
    }
    if (m_opt.effects) {
        std::printf("  effects %zu rows, %zu party-wide buff windows\n", d.effects.size(), d.timeline.buffs.size());
        for (const meter::EffectUptimeRow& r : d.effects) {
            const std::string target =
                r.side == meter::EffectSide::OnEnemy ? r.target_name : name(r.target_name, r.target);
            const std::string caster = r.side == meter::EffectSide::PartyDebuff || r.caster.empty()
                                           ? (r.caster.empty() ? std::string("?") : r.caster)
                                           : (m_opt.anonymize ? std::string("a player") : r.caster);
            std::printf("    %-6s %-28.28s on %-20.20s from %-20.20s %4u x %7.1f s %5.1f%%\n",
                        std::string(meter::effect_side_name(r.side)).c_str(),
                        game::effect_label(r.buff_id, r.source_skill).c_str(), target.c_str(), caster.c_str(),
                        r.applications, r.active_s, 100.0 * r.uptime(f.duration_s));
        }
    }
}

void Report::print_casts(const meter::FightSummary& f) const {
    if (!m_opt.casts) return;
    if (!f.has_casts) {
        std::printf("  no casts\n");
        return;
    }
    const double minutes = f.duration_s / 60.0;
    for (const auto& c : f.combatants) {
        std::printf("  casts  %-16s %5" PRIu64 "  %5.1f a minute\n",
                    name(c.name.starts_with('#') ? std::string() : c.name, c.id).c_str(), c.casts,
                    minutes > 0.0 ? static_cast<double>(c.casts) / minutes : 0.0);
        for (const meter::SkillCasts& s : c.cast_skills) {
            std::printf("    %-26.26s %5" PRIu64 "  %5.1f a minute", game::skill_label(s.code).c_str(), s.casts,
                        minutes > 0.0 ? static_cast<double>(s.casts) / minutes : 0.0);
            if (s.casts >= 2) std::printf("  every %.2f s", s.mean_gap_s());
            std::printf("  first %+.2f s\n", s.first_s);
        }
    }
}

void Report::print_flows(const pipeline::StreamDemux& demux) const {
    auto flows = demux.flows();
    std::sort(flows.begin(), flows.end(), [](const auto& a, const auto& b) { return a.first_ts < b.first_ts; });
    size_t shown = 0;
    for (const auto& f : flows) {
        if (f.role != pipeline::FlowRole::Game) continue;
        ++shown;
        // Packets from two adapters can arrive a little out of order.
        const double secs = f.last_ts > f.first_ts ? to_seconds(f.last_ts - f.first_ts) : 0.0;
        const auto& d = f.decoder;
        std::printf("flow %s  [%s]\n", m_opt.anonymize ? "(hidden)" : f.key.to_string().c_str(), role_name(f.role));
        std::printf("  %.1f s, %" PRIu64 " segments, %" PRIu64 " bytes, %" PRIu64 " gap bytes\n", secs, f.segments,
                    f.bytes, f.gap_bytes);
        std::printf("  frames %" PRIu64 ", bundles %" PRIu64 " (%" PRIu64 " bad), resyncs %" PRIu64
                    ", skipped %" PRIu64 " bytes\n",
                    d.frames, d.bundles, d.bundle_errors, d.resyncs, d.skipped_bytes);
        std::printf("  heartbeats %" PRIu64 " (%.1f/s)", d.heartbeats,
                    secs > 0 ? static_cast<double>(d.heartbeats) / secs : 0.0);
        if (d.last_server_ms != 0 && f.last_ts != 0) {
            const double skew = static_cast<double>(d.last_server_ms) / 1000.0 - to_seconds(f.last_ts);
            std::printf(", server clock %+.3f s from capture", skew);
        }
        std::printf("\n");
    }
    if (shown == 0) std::printf("no game flow found: is the port right (--port), or was the capture filtered?\n");
    if (flows.size() > shown) std::printf("(%zu other TCP flows ignored)\n", flows.size() - shown);
}

void Report::print_parse(const proto::ParseStats& s) const {
    std::printf("parsed %" PRIu64 " frames: %" PRIu64 " decoded, %" PRIu64 " failed (%.1f%% coverage), %" PRIu64
                " unknown, %" PRIu64 " identities found by scan\n",
                s.frames, s.decoded, s.failed, s.coverage_percent(), s.unknown, s.scanned_identities);
    for (const auto& [op, n] : s.failures_by_opcode) {
        const unsigned hi = unsigned{op} >> 8;
        const unsigned lo = unsigned{op} & 0xFFu;
        std::printf("  %02x %02x %-14s failed %" PRIu64 " times (--dump %02x%02x to see them)\n", hi, lo,
                    std::string(proto::opcode_name(op)).c_str(), n, hi, lo);
    }
}

void Report::print_opcodes() const {
    std::printf("\n%-6s %-14s %9s %8s %8s %8s %9s %7s %7s\n", "opcode", "name", "count", "min", "avg", "max",
                "in_bundle", "flagged", "failed");
    std::vector<std::pair<uint16_t, OpcodeStats>> rows(m_stats.begin(), m_stats.end());
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        if (a.second.count != b.second.count) return a.second.count > b.second.count;
        return a.first < b.first;
    });
    for (const auto& [op, s] : rows) {
        std::printf("%02x %02x  %-14s %9" PRIu64 " %8zu %8.1f %8zu %9" PRIu64 " %7" PRIu64 " %7" PRIu64 "\n",
                    unsigned{op} >> 8, unsigned{op} & 0xFFu, std::string(proto::opcode_name(op)).c_str(), s.count, s.min,
                    static_cast<double>(s.total) / static_cast<double>(s.count), s.max, s.in_bundle, s.flagged,
                    s.failed);
    }
}

uint64_t Report::frames() const {
    uint64_t n = 0;
    for (const auto& [op, s] : m_stats) n += s.count;
    return n;
}

} // namespace aion2::replay
