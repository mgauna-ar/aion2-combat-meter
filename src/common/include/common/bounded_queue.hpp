#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace aion2::common {

/// A multi-producer, single-consumer queue that never blocks a producer.
///
/// The capture threads push and the engine thread pops. A full queue drops the new
/// item and counts it rather than stalling a capture thread: Npcap keeps filling its
/// kernel buffer while we wait, so a stall would lose packets anyway, and a dropped
/// packet shows up downstream as a TCP gap the decoder already recovers from.
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t capacity) : m_capacity(capacity == 0 ? 1 : capacity) {}

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    /// False, with the item dropped and counted, when the queue is full or closed.
    bool try_push(T item) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_closed || m_items.size() >= m_capacity) {
                ++m_dropped;
                return false;
            }
            m_items.push_back(std::move(item));
        }
        m_ready.notify_one();
        return true;
    }

    /// The oldest item, waiting up to `timeout` for one. nullopt on a timeout, and
    /// at once when the queue is closed and empty.
    template <typename Rep, typename Period>
    std::optional<T> pop_for(std::chrono::duration<Rep, Period> timeout) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (!m_ready.wait_for(lock, timeout, [this] { return !m_items.empty() || m_closed; })) {
            return std::nullopt;
        }
        if (m_items.empty()) return std::nullopt;
        T item = std::move(m_items.front());
        m_items.pop_front();
        return item;
    }

    /// Wakes the consumer and refuses every later push.
    void close() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_closed = true;
        }
        m_ready.notify_all();
    }

    [[nodiscard]] bool closed() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_closed;
    }

    [[nodiscard]] size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_items.size();
    }

    /// Items refused since the queue was made.
    [[nodiscard]] uint64_t dropped() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_dropped;
    }

    [[nodiscard]] size_t capacity() const noexcept { return m_capacity; }

private:
    const size_t m_capacity;
    mutable std::mutex m_mutex;
    std::condition_variable m_ready;
    std::deque<T> m_items;
    uint64_t m_dropped{0};
    bool m_closed{false};
};

} // namespace aion2::common
