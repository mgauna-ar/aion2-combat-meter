#pragma once

#include "common/micros.hpp"
#include "meter/combatant_registry.hpp"
#include "meter/types.hpp"
#include "pipeline/pipeline.hpp"
#include "pipeline/stream_demux.hpp"
#include "protocol/events.hpp"
#include "protocol/frame_decoder.hpp"
#include "protocol/packet_parser.hpp"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace aion2::replay {

struct ReportOptions {
    bool opcodes{false};
    bool events{false};
    bool fights{false};
    /// Each player's skills, healing and damage taken under their fight's table.
    bool skills{false};
    bool unknown{false};
    bool anonymize{false};
    bool dump_all{false};
    std::set<uint16_t> dump;
    size_t dump_limit{20};
};

/// Prints what aion2meter-replay asked for as the pipeline runs, and the summaries
/// at the end. Times are seconds from the capture's first game frame, so the output
/// of a capture is the same on every run and machine: `make fixtures` diffs it.
class Report {
public:
    explicit Report(const ReportOptions& options) : m_opt(options) {}

    void on_frame(const proto::Frame& frame, pipeline::FrameOutcome outcome);
    void on_event(const proto::ParsedEvent& event, const meter::CombatantRegistry& registry);
    void on_fight(const meter::FightSummary& fight);
    /// One player's skills, healing and damage taken (--skills).
    void print_breakdown(const meter::CombatantTotals& c) const;

    void print_flows(const pipeline::StreamDemux& demux) const;
    void print_parse(const proto::ParseStats& stats) const;
    void print_opcodes() const;

    [[nodiscard]] uint64_t frames() const;
    [[nodiscard]] size_t distinct_opcodes() const { return m_stats.size(); }
    [[nodiscard]] Micros span() const { return m_last_ts > m_first_ts ? m_last_ts - m_first_ts : 0; }
    [[nodiscard]] size_t fights() const noexcept { return m_fights; }

private:
    struct OpcodeStats {
        uint64_t count{0};
        size_t min{0};
        size_t max{0};
        uint64_t total{0};
        uint64_t in_bundle{0};
        uint64_t flagged{0};
        uint64_t failed{0};
    };

    [[nodiscard]] double rel(Micros ts) const;
    [[nodiscard]] std::string who(uint32_t id, const meter::CombatantRegistry& registry) const;
    [[nodiscard]] std::string name(const std::string& real, uint32_t id) const;
    void dump_frame(const proto::Frame& frame, const char* why);

    const ReportOptions& m_opt;
    std::map<uint16_t, OpcodeStats> m_stats;
    std::map<uint16_t, size_t> m_dumped;
    Micros m_first_ts{0};
    Micros m_last_ts{0};
    size_t m_fights{0};
};

} // namespace aion2::replay
