#pragma once

#include "common/micros.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace aion2::proto {

/// One decoded game frame. The spans view the decoder's buffer and are valid only
/// during the on_frame call.
struct Frame {
    uint16_t opcode{0};
    /// The optional byte in 0xF0..0xFE between the length and the opcode; 0 when absent.
    uint8_t flag{0};
    /// The bytes after the opcode.
    std::span<const uint8_t> body;
    /// The whole frame as it was framed: length, flag, opcode and body.
    std::span<const uint8_t> raw;
    Micros ts{0};
    /// 0 at the top of the stream; 1 inside an LZ4 bundle, 2 inside one in that.
    uint8_t depth{0};
    /// Byte offset of the enclosing top-level frame in the stream.
    uint64_t stream_offset{0};
};

class FrameSink {
public:
    virtual ~FrameSink() = default;
    virtual void on_frame(const Frame& frame) = 0;
};

struct DecoderStats {
    uint64_t frames{0};
    uint64_t bundles{0};
    uint64_t bundle_errors{0};
    /// Times the decoder lost the frame boundaries and searched for a heartbeat.
    uint64_t resyncs{0};
    /// Bytes thrown away while searching.
    uint64_t skipped_bytes{0};
    uint64_t heartbeats{0};
    uint64_t gaps{0};
    /// The server clock from the latest heartbeat, Unix milliseconds; 0 before one.
    uint64_t last_server_ms{0};
};

/**
 * @brief Splits one server-to-client byte stream into game frames.
 *
 * A frame is `varint len, [flag 0xF0..0xFE], opcode (2 bytes) body`, and takes
 * len + varint_bytes - 4 bytes on the wire. `FF FF u32 raw_size` in place of an
 * opcode opens an LZ4 bundle that expands into more frames. 0x00 bytes between
 * frames are padding.
 *
 * Aligned, it reads frame after frame. A length out of range, or no heartbeat for
 * kHeartbeatSilence while bytes keep coming, means the boundaries are lost (a gap,
 * or a capture that joined mid-stream): it goes Unaligned and searches for the next
 * heartbeat, `0E 00 36` and a u64 that reads as a Unix millisecond time near the
 * capture's. The server sends about twenty a second, so a resync costs ~50 ms.
 */
class FrameDecoder {
public:
    static constexpr size_t kMaxFrameBytes = 1u << 20;
    static constexpr int kMaxBundleDepth = 2;
    static constexpr Micros kHeartbeatSilence = 3 * kMicrosPerSecond;
    /// How far a heartbeat's server time may sit from the capture's clock.
    static constexpr uint64_t kClockToleranceMs = 24ull * 60 * 60 * 1000;

    /// A new stream: aligned when it began at a frame boundary (a SYN), else not.
    void start(bool at_boundary, Micros ts);
    void feed(std::span<const uint8_t> bytes, Micros ts, FrameSink& sink);
    /// Bytes were lost: drop what is buffered and search for a heartbeat.
    void on_gap();

    [[nodiscard]] bool aligned() const noexcept { return m_aligned; }
    [[nodiscard]] const DecoderStats& stats() const noexcept { return m_stats; }
    /// Capture time of the latest heartbeat; 0 before one.
    [[nodiscard]] Micros last_heartbeat_ts() const noexcept { return m_last_heartbeat_ts; }

    /// True when `bytes` (11 or more) start with a heartbeat whose server time is
    /// within kClockToleranceMs of `ts`, or of any time this century when `ts` is 0.
    [[nodiscard]] static bool is_heartbeat_at(std::span<const uint8_t> bytes, Micros ts) noexcept;

private:
    enum class Step : uint8_t { Frame, NeedMore, Lost };

    void lose_alignment();
    void search(Micros ts);
    /// Decodes every whole frame buffered, searching first when unaligned.
    void decode_all(Micros ts, FrameSink& sink);
    /// Decodes one frame at m_pos.
    Step decode_one(Micros ts, FrameSink& sink);
    /// Emits `payload` (flag stripped) as one frame, expanding a bundle.
    void emit(std::span<const uint8_t> raw, std::span<const uint8_t> payload, uint8_t flag, Micros ts,
              uint8_t depth, uint64_t offset, FrameSink& sink);
    /// Decodes every frame of a decompressed bundle. False when the bytes do not
    /// split into whole frames.
    bool decode_bundle(std::span<const uint8_t> bytes, Micros ts, uint8_t depth, uint64_t offset, FrameSink& sink);
    void compact();

    std::vector<uint8_t> m_buf;
    size_t m_pos{0};
    /// Stream offset of m_buf[0].
    uint64_t m_buf_offset{0};
    bool m_aligned{false};
    Micros m_last_heartbeat_ts{0};
    /// Capture time the decoder went aligned, for the heartbeat watchdog.
    Micros m_aligned_since{0};
    DecoderStats m_stats;
};

} // namespace aion2::proto
