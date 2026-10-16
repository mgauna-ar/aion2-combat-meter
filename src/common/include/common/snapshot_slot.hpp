#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

namespace aion2::common {

/// The latest immutable snapshot one thread publishes and another reads.
///
/// The engine thread builds a whole new T and swaps it in; the UI thread takes a
/// shared_ptr to whichever is current and draws from it with no lock held, so a
/// slow frame never holds up the engine and the engine never mutates what a frame
/// is drawing.
template <typename T>
class SnapshotSlot {
public:
    void publish(T value) {
        auto next = std::make_shared<const T>(std::move(value));
        std::lock_guard<std::mutex> lock(m_mutex);
        m_current = std::move(next);
        ++m_revision;
    }

    /// The current snapshot, or nullptr before the first publish.
    [[nodiscard]] std::shared_ptr<const T> get() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_current;
    }

    /// Moves on every publish, so a reader can skip work when nothing changed.
    [[nodiscard]] uint64_t revision() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_revision;
    }

private:
    mutable std::mutex m_mutex;
    std::shared_ptr<const T> m_current;
    uint64_t m_revision{0};
};

} // namespace aion2::common
