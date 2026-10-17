#include "app/app_runtime.hpp"

#include "common/os/logger.hpp"
#include "common/time_format.hpp"

#include <chrono>

namespace aion2::app {

namespace {

constexpr auto kPopTimeout = std::chrono::milliseconds(20);

pipeline::PipelineOptions pipeline_options(const AppSettings& s) {
    pipeline::PipelineOptions o;
    o.demux.server_port = static_cast<uint16_t>(s.capture.server_port);
    o.demux.any_port = s.capture.detect_relays;
    o.meter = s.meter;
    return o;
}

} // namespace

AppRuntime::AppRuntime(AppSettings settings, std::filesystem::path data_dir)
    : m_settings(std::move(settings)),
      m_data_dir(std::move(data_dir)),
      m_pipeline(pipeline_options(m_settings)),
      m_store(m_data_dir / "history") {
    m_history = m_store.load_index();
    m_history_revision = 1;
}

AppRuntime::~AppRuntime() { stop(); }

bool AppRuntime::start(std::unique_ptr<capture::PacketSource> source, std::string& error) {
    m_source = std::move(source);
    const bool started = m_source->start(
        [this](capture::OwnedPacket&& pkt) { (void)m_queue.try_push(std::move(pkt)); }, error);
    if (!started) {
        set_status(error);
        os::Logger::error("capture failed to start: " + error);
        return false;
    }
    m_stop = false;
    m_thread = std::thread([this] { run(); });
    return true;
}

void AppRuntime::stop() {
    // The engine thread first: it reads the source (dropped(), now()), and stopping
    // the source frees what those read. Pushes into the closed queue just fail.
    m_stop = true;
    m_queue.close();
    if (m_thread.joinable()) m_thread.join();
    if (m_source) m_source->stop();
}

void AppRuntime::run() {
    Micros last_tick = 0;
    Micros last_publish = 0;
    Micros last_packet = 0;
    while (!m_stop.load()) {
        auto pkt = m_queue.pop_for(kPopTimeout);
        // While packets are queued, time is the packet's: the source's clock runs
        // ahead of a backlog, and ticking on it would end fights whose hits are
        // still waiting in the queue. Only an empty queue lets time move on.
        const Micros now = pkt ? std::max(last_packet, pkt->ts) : std::max(last_packet, m_source->now());

        // Before the packet, so a recording started now includes it.
        handle_recording(now);
        if (pkt) {
            if (m_recorder.is_open()) m_recorder.write(pkt->link_type, pkt->ts, pkt->data);
            m_pipeline.on_packet(pkt->link_type, pkt->data, pkt->ts);
            last_packet = now;
        }

        const bool ended = m_end_fight_requested.exchange(false);
        if (ended) m_pipeline.end_fight(meter::EndReason::Manual, now);

        if (now >= last_tick + pipeline::Pipeline::kTickEvery) {
            m_pipeline.set_dropped_packets(m_queue.dropped() + m_source->dropped());
            m_pipeline.tick(now);
            last_tick = now;
        }
        save_finished();

        // Published on capture time, once more whenever the queue runs dry so a
        // replay at full speed still ends on its final state, and after a manual end
        // even when the clock stands still (a finished replay).
        if (ended || now >= last_publish + kPublishEvery || (!pkt && last_publish != now)) {
            publish(now);
            last_publish = now;
        }
    }
    if (m_recorder.is_open()) m_recorder.close();
}

void AppRuntime::publish(Micros now) {
    m_snapshot.publish(m_pipeline.snapshot(now));
    const std::string status = m_source->status();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status = status;
}

void AppRuntime::handle_recording(Micros now) {
    const int request = m_recording_request.exchange(-1);
    if (request == 1 && !m_recorder.is_open()) {
        const std::filesystem::path path = captures_dir() / ("aion2-" + common::utc_file_stamp(now) + ".pcapng");
        std::string error;
        if (m_recorder.open(path, error)) {
            m_recording = true;
            std::lock_guard<std::mutex> lock(m_mutex);
            m_recording_path = path.string();
            os::Logger::info("recording started");
        } else {
            os::Logger::error("cannot start recording: " + error);
        }
    } else if (request == 0 && m_recorder.is_open()) {
        m_recorder.close();
        m_recording = false;
        os::Logger::info("recording stopped");
    }
}

void AppRuntime::save_finished() {
    bool saved_any = false;
    for (const auto& fight : m_pipeline.take_finished()) {
        if (!meter::worth_saving(fight, m_settings.meter)) continue;
        const std::string error = m_store.save(fight);
        if (!error.empty()) {
            os::Logger::error("cannot save fight " + std::to_string(fight.id) + ": " + error);
            continue;
        }
        saved_any = true;
    }
    if (!saved_any) return;

    auto index = m_store.load_index();
    const auto keep = static_cast<size_t>(m_settings.history.max_fights);
    if (index.size() > keep + static_cast<size_t>(kPruneSlack)) {
        m_store.prune(keep);
        index = m_store.load_index();
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_history = std::move(index);
    }
    ++m_history_revision;
}

std::vector<meter::HistoryEntry> AppRuntime::history() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_history;
}

std::optional<meter::FightSummary> AppRuntime::load_fight(const std::string& file) const {
    // Read-only, and HistoryStore keeps no state between calls: safe off the engine thread.
    return meter::HistoryStore(history_dir()).load(file);
}

std::string AppRuntime::source_status() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

void AppRuntime::set_status(std::string status) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status = std::move(status);
}

bool AppRuntime::source_finished() const { return m_source && m_source->finished(); }

std::string AppRuntime::recording_path() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_recording_path;
}

} // namespace aion2::app
