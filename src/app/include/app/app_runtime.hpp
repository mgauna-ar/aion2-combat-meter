#pragma once

#include "app/app_settings.hpp"
#include "capture/packet_source.hpp"
#include "capture/pcapng_writer.hpp"
#include "common/bounded_queue.hpp"
#include "common/micros.hpp"
#include "common/snapshot_slot.hpp"
#include "meter/history_store.hpp"
#include "pipeline/pipeline.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace aion2::app {

/**
 * @brief The meter minus its window: capture source, engine thread, history.
 *
 * Threads: the source's capture threads push packets into a BoundedQueue; one
 * engine thread owns the Pipeline, the history store and the recorder, and
 * publishes a LiveSnapshot about five times a second; the UI thread reads that
 * snapshot and the history index, and posts requests (end the fight, start or stop
 * recording, clear the history) as atomics, and new settings under the mutex, for
 * the engine thread to pick up. Nothing here touches Win32, so
 * the whole runtime runs in the tests on the Mac with a FileSource.
 */
class AppRuntime {
public:
    static constexpr size_t kQueueCapacity = 8192;
    static constexpr Micros kPublishEvery = 200 * kMicrosPerMilli;
    /// Saved fights beyond max_fights are pruned in batches of this many.
    static constexpr int kPruneSlack = 50;

    AppRuntime(AppSettings settings, std::filesystem::path data_dir);
    ~AppRuntime();
    AppRuntime(const AppRuntime&) = delete;
    AppRuntime& operator=(const AppRuntime&) = delete;

    /// Starts `source` and the engine thread. False, with `error`, when the source
    /// cannot start; the runtime then still answers the UI (history, status).
    bool start(std::unique_ptr<capture::PacketSource> source, std::string& error);
    void stop();

    /// The latest snapshot; nullptr before the first one.
    [[nodiscard]] std::shared_ptr<const pipeline::LiveSnapshot> snapshot() const { return m_snapshot.get(); }
    [[nodiscard]] uint64_t snapshot_revision() const { return m_snapshot.revision(); }

    void request_end_fight() noexcept { m_end_fight_requested = true; }
    /// New fight-tracking and history settings, from the settings page. The engine
    /// thread applies them on its next pass; a fight under way keeps going under them.
    void update_settings(const meter::MeterSettings& meter, const HistorySettings& history);
    /// Deletes every saved fight. The history revision moves once it is done.
    void request_clear_history() noexcept { m_clear_history_requested = true; }
    /// Starts or stops writing everything captured to a pcapng in captures_dir().
    void request_recording(bool on) noexcept { m_recording_request = on ? 1 : 0; }
    [[nodiscard]] bool recording() const noexcept { return m_recording.load(); }
    [[nodiscard]] std::string recording_path() const;

    /// Saved fights, oldest first, as of the last save.
    [[nodiscard]] std::vector<meter::HistoryEntry> history() const;
    [[nodiscard]] uint64_t history_revision() const noexcept { return m_history_revision.load(); }
    [[nodiscard]] std::optional<meter::FightSummary> load_fight(const std::string& file) const;

    /// What the capture source says it is doing, or why it could not start.
    [[nodiscard]] std::string source_status() const;
    [[nodiscard]] bool source_finished() const;
    [[nodiscard]] std::filesystem::path history_dir() const { return m_data_dir / "history"; }
    [[nodiscard]] std::filesystem::path captures_dir() const { return m_data_dir / "captures"; }

private:
    void run();
    void handle_recording(Micros now);
    void save_finished();
    /// Engine thread: settings from update_settings(), and a clear request.
    void apply_requests();
    void reload_history();
    void publish(Micros now);
    void set_status(std::string status);

    /// Engine thread only, after the constructor.
    AppSettings m_settings;
    std::filesystem::path m_data_dir;
    common::BoundedQueue<capture::OwnedPacket> m_queue{kQueueCapacity};
    common::SnapshotSlot<pipeline::LiveSnapshot> m_snapshot;
    std::unique_ptr<capture::PacketSource> m_source;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};

    std::atomic<bool> m_end_fight_requested{false};
    std::atomic<bool> m_clear_history_requested{false};
    /// -1 none, 0 stop, 1 start.
    std::atomic<int> m_recording_request{-1};
    std::atomic<bool> m_recording{false};

    // Engine thread only.
    pipeline::Pipeline m_pipeline;
    capture::PcapngFileWriter m_recorder;
    meter::HistoryStore m_store;

    mutable std::mutex m_mutex;
    std::vector<meter::HistoryEntry> m_history;
    std::atomic<uint64_t> m_history_revision{0};
    std::string m_status;
    std::string m_recording_path;
    /// From update_settings(), until the engine thread takes them.
    std::optional<std::pair<meter::MeterSettings, HistorySettings>> m_pending_settings;
};

} // namespace aion2::app
