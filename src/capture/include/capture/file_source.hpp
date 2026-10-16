#pragma once

#include "capture/packet_source.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace aion2::capture {

/**
 * @brief Plays a capture file back as if it were live.
 *
 * With `speed` > 0 packets are released at their recorded pace times `speed`, and
 * now() runs on the recording's clock, so the overlay shows a recorded fight as
 * it happened (aion2-combat-meter.exe --replay). With `speed` 0 everything is
 * pushed at once, for tests.
 */
class FileSource final : public PacketSource {
public:
    FileSource(std::filesystem::path path, double speed);
    ~FileSource() override;
    FileSource(const FileSource&) = delete;
    FileSource& operator=(const FileSource&) = delete;

    bool start(Push push, std::string& error) override;
    void stop() override;
    [[nodiscard]] Micros now() const override;
    [[nodiscard]] uint64_t dropped() const override { return 0; }
    [[nodiscard]] std::string status() const override;
    [[nodiscard]] bool finished() const override { return m_finished.load(); }

private:
    void run(Push push);

    std::filesystem::path m_path;
    double m_speed;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_finished{false};
    /// The recording's time at m_wall_start; both set once the first packet is read.
    std::atomic<Micros> m_file_start{0};
    std::chrono::steady_clock::time_point m_wall_start{};
    /// The latest packet's time, for now() at full speed.
    std::atomic<Micros> m_last_ts{0};
    mutable std::mutex m_mutex;
    std::string m_status;
};

} // namespace aion2::capture
