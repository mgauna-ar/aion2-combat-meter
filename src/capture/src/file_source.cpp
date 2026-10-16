#include "capture/file_source.hpp"

#include "capture/capture_file.hpp"

#include <algorithm>

namespace aion2::capture {

Micros wall_clock_now() noexcept {
    const auto since_epoch = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<Micros>(std::chrono::duration_cast<std::chrono::microseconds>(since_epoch).count());
}

FileSource::FileSource(std::filesystem::path path, double speed) : m_path(std::move(path)), m_speed(speed) {}

FileSource::~FileSource() { stop(); }

bool FileSource::start(Push push, std::string& error) {
    // Opened here once to fail early with a reason; run() opens its own.
    std::string why;
    if (!CaptureFileReader::open(m_path, why)) {
        error = m_path.filename().string() + ": " + why;
        return false;
    }
    m_stop = false;
    m_finished = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status = "replaying " + m_path.filename().string();
    }
    m_thread = std::thread([this, push = std::move(push)]() mutable { run(std::move(push)); });
    return true;
}

void FileSource::stop() {
    m_stop = true;
    if (m_thread.joinable()) m_thread.join();
}

Micros FileSource::now() const {
    const Micros start = m_file_start.load();
    if (start == 0) return 0;
    if (m_speed <= 0.0 || m_finished.load()) return m_last_ts.load();
    const auto elapsed = std::chrono::steady_clock::now() - m_wall_start;
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    return start + static_cast<Micros>(static_cast<double>(us) * m_speed);
}

std::string FileSource::status() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

void FileSource::run(Push push) {
    std::string error;
    auto reader = CaptureFileReader::open(m_path, error);
    if (!reader) {
        m_finished = true;
        return;
    }
    CapturedPacket pkt;
    while (!m_stop.load() && reader->next(pkt)) {
        if (pkt.ts == 0) continue;  // simple packet blocks carry no time
        if (m_file_start.load() == 0) {
            m_wall_start = std::chrono::steady_clock::now();
            m_file_start = pkt.ts;
        }
        if (m_speed > 0.0) {
            // Sleep in short steps so stop() is never kept waiting by a long pause.
            while (!m_stop.load() && pkt.ts > now()) {
                const Micros wait = std::min<Micros>(pkt.ts - now(), 50 * kMicrosPerMilli);
                std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(wait)));
            }
        }
        m_last_ts = std::max(m_last_ts.load(), pkt.ts);
        push(OwnedPacket{pkt.link_type, pkt.ts, std::vector<uint8_t>(pkt.data.begin(), pkt.data.end())});
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status = reader->error().empty() ? "replay finished: " + m_path.filename().string()
                                           : "replay stopped: " + reader->error();
    }
    m_finished = true;
}

} // namespace aion2::capture
