#include "pipeline/pipeline.hpp"

namespace aion2::pipeline {

Pipeline::Pipeline(PipelineOptions options)
    : m_demux(options.demux), m_engine(options.meter), m_plugins(options.plugins) {}

void Pipeline::on_packet(uint32_t link, std::span<const uint8_t> data, Micros ts) {
    m_demux.on_packet(link, data, ts, *this);
    if (ts >= m_last_tick + kTickEvery) tick(ts);
}

void Pipeline::on_frame(const proto::Frame& frame) {
    const proto::ParseStats& stats = m_parser.stats();
    const uint64_t decoded = stats.decoded;
    const uint64_t failed = stats.failed;
    const uint64_t unknown = stats.unknown;
    m_events.clear();
    m_parser.parse(frame, m_events);
    if (m_frame_observer) {
        FrameOutcome outcome = FrameOutcome::Ignored;
        if (stats.decoded > decoded) outcome = FrameOutcome::Decoded;
        else if (stats.failed > failed) outcome = FrameOutcome::Failed;
        else if (stats.unknown > unknown) outcome = FrameOutcome::Unknown;
        m_frame_observer(frame, outcome);
    }
    if (m_events.empty()) return;
    if (m_plugins.combat_meter) m_engine.set_capture_counters(capture_totals());
    for (const auto& e : m_events) {
        if (m_event_observer) m_event_observer(e);
        if (m_plugins.combat_meter) m_engine.on_event(e);
        if (m_plugins.auras) m_auras.on_event(e);
        if (m_plugins.ping) m_ping.on_event(e);
        if (m_plugins.combat_power) m_power.on_event(e);
    }
}

void Pipeline::tick(Micros now) {
    m_last_tick = now;
    m_demux.tick(now, *this);
    if (m_plugins.combat_meter) {
        m_engine.set_capture_counters(capture_totals());
        m_engine.tick(now);
    }
    if (m_plugins.auras) m_auras.tick(now);
    if (m_plugins.ping) m_ping.tick(now);
    if (m_plugins.combat_power) m_power.tick(now);
}

void Pipeline::end_fight(meter::EndReason reason, Micros now) {
    m_engine.set_capture_counters(capture_totals());
    m_engine.end_fight(reason, now);
}

void Pipeline::set_plugins(const aion2::plugins::PluginSwitches& on, Micros now) {
    if (m_plugins.combat_meter && !on.combat_meter) {
        end_fight(meter::EndReason::Manual, now);
        // It will miss what is put on and taken off meanwhile.
        m_engine.forget_effects();
    }
    if (m_plugins.auras && !on.auras) m_auras.clear();
    if (m_plugins.ping && !on.ping) m_ping.clear();
    if (m_plugins.combat_power && !on.combat_power) m_power.clear();
    m_plugins = on;
}

meter::CaptureQuality Pipeline::capture_totals() const {
    meter::CaptureQuality q;
    q.gap_bytes = m_demux.game_gap_bytes();
    q.resyncs = m_demux.game_resyncs();
    q.bundle_errors = m_demux.game_bundle_errors();
    q.dropped_packets = m_dropped;
    return q;
}

LiveSnapshot Pipeline::snapshot(Micros now) const {
    LiveSnapshot s;
    s.fight = m_engine.current();
    s.recent.assign(m_engine.recent().begin(), m_engine.recent().end());
    s.recent_revision = m_engine.recent_revision();
    const Micros last = m_demux.last_game_ts();
    if (last == 0) {
        s.capture = CaptureState::Waiting;
    } else if (now > last && now - last >= kQuietAfter) {
        s.capture = CaptureState::Quiet;
    } else {
        s.capture = CaptureState::Live;
    }
    const auto& reg = m_engine.registry();
    if (reg.local_player() != 0) s.local_name = reg.display_name(reg.local_player());
    s.map_id = m_engine.map_id();
    s.now = now;
    s.parse = m_parser.stats();
    s.capture_totals = capture_totals();
    if (m_plugins.auras) s.auras = m_auras.snapshot(now);
    if (m_plugins.ping) s.ping = m_ping.snapshot(now);
    if (m_plugins.combat_power) s.power = m_power.snapshot(now);
    return s;
}

} // namespace aion2::pipeline
