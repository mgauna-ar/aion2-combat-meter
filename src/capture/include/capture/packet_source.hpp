#pragma once

#include "common/micros.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace aion2::capture {

/// A captured packet that owns its bytes, to cross from a capture thread to the
/// engine thread.
struct OwnedPacket {
    uint32_t link_type{0};
    Micros ts{0};
    std::vector<uint8_t> data;
};

/**
 * @brief Where packets come from: Npcap on the game PC, or a capture file.
 *
 * A source runs its own threads and hands every packet to `push`, which must not
 * block (the app's push is a BoundedQueue::try_push). The meter only ever reads
 * from a source; nothing is sent.
 */
class PacketSource {
public:
    using Push = std::function<void(OwnedPacket&&)>;

    virtual ~PacketSource() = default;

    /// Starts capturing. False, with `error` saying why, when nothing could start.
    virtual bool start(Push push, std::string& error) = 0;
    /// Stops and joins every thread. Safe to call twice.
    virtual void stop() = 0;
    /// The clock packets are stamped with, now: wall time for live capture, the
    /// recording's time for a file played back.
    [[nodiscard]] virtual Micros now() const = 0;
    /// Packets the driver dropped before we read them.
    [[nodiscard]] virtual uint64_t dropped() const = 0;
    /// One line for the overlay and the log: what is being captured.
    [[nodiscard]] virtual std::string status() const = 0;
    /// A file source that has played everything; never true for live capture.
    [[nodiscard]] virtual bool finished() const { return false; }
};

/// The system clock as Micros: what Npcap stamps packets with.
[[nodiscard]] Micros wall_clock_now() noexcept;

} // namespace aion2::capture
