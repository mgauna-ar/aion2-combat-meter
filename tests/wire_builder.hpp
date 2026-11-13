#pragma once

// Byte builders for the tests: network frames around a TCP payload, and the game's
// own framing. Real captures hold character names and never enter the repository
// (AGENTS.md), so every committed test builds its bytes here.

#include "capture/packet_headers.hpp"
#include "common/micros.hpp"
#include "lz4_test_encoder.hpp"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

namespace aion2::test::wire {

using Bytes = std::vector<uint8_t>;

inline void put_be16(Bytes& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}

inline void put_be32(Bytes& b, uint32_t v) {
    for (int i = 3; i >= 0; --i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

inline void put_le16(Bytes& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
}

inline void put_le32(Bytes& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

inline void put_le64(Bytes& b, uint64_t v) {
    for (int i = 0; i < 8; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

inline void put_bytes(Bytes& b, std::span<const uint8_t> more) { b.insert(b.end(), more.begin(), more.end()); }

inline void put_bytes(Bytes& b, std::initializer_list<uint8_t> more) { b.insert(b.end(), more.begin(), more.end()); }

inline Bytes bytes_of(std::string_view text) { return Bytes(text.begin(), text.end()); }

constexpr uint32_t ip4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(c) << 8) | d;
}

/// The server and the client of the tests' one game connection.
inline constexpr uint32_t kServerIp = ip4(104, 18, 0, 10);
inline constexpr uint32_t kClientIp = ip4(192, 168, 1, 50);
inline constexpr uint16_t kServerPort = 13328;
inline constexpr uint16_t kClientPort = 51000;

struct TcpSpec {
    uint32_t src_ip{kServerIp};
    uint32_t dst_ip{kClientIp};
    uint16_t src_port{kServerPort};
    uint16_t dst_port{kClientPort};
    uint32_t seq{1000};
    uint32_t ack{1};
    uint8_t flags{static_cast<uint8_t>(capture::tcp_flag::Ack | capture::tcp_flag::Psh)};
};

/// An IPv4 packet holding one TCP segment.
inline Bytes ipv4_tcp(const TcpSpec& spec, std::span<const uint8_t> payload) {
    Bytes ip;
    const auto total = static_cast<uint16_t>(20 + 20 + payload.size());
    put_bytes(ip, {0x45, 0x00});
    put_be16(ip, total);
    put_be16(ip, 0x1234);   // id
    put_be16(ip, 0x4000);   // DF
    put_bytes(ip, {64, 6});  // ttl, TCP
    put_be16(ip, 0);        // checksum: never checked
    put_be32(ip, spec.src_ip);
    put_be32(ip, spec.dst_ip);
    put_be16(ip, spec.src_port);
    put_be16(ip, spec.dst_port);
    put_be32(ip, spec.seq);
    put_be32(ip, spec.ack);
    ip.push_back(0x50);  // header length 20
    ip.push_back(spec.flags);
    put_be16(ip, 65535);  // window
    put_be16(ip, 0);      // checksum
    put_be16(ip, 0);      // urgent
    put_bytes(ip, payload);
    return ip;
}

/// An Ethernet II frame around ipv4_tcp.
inline Bytes ethernet_tcp(const TcpSpec& spec, std::span<const uint8_t> payload) {
    Bytes frame = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0x08, 0x00};
    put_bytes(frame, ipv4_tcp(spec, payload));
    return frame;
}

/// A Null (BSD loopback) frame around ipv4_tcp, as Npcap's loopback adapter writes.
inline Bytes loopback_tcp(const TcpSpec& spec, std::span<const uint8_t> payload) {
    Bytes frame;
    put_le32(frame, 2);  // AF_INET, host order
    put_bytes(frame, ipv4_tcp(spec, payload));
    return frame;
}

/// An unsigned LEB128 varint.
inline void put_varint(Bytes& b, uint64_t v) {
    do {
        const auto low = static_cast<uint8_t>(v & 0x7F);
        v >>= 7;
        b.push_back(static_cast<uint8_t>(v != 0 ? (low | 0x80) : low));
    } while (v != 0);
}

inline size_t varint_size(uint64_t v) {
    size_t n = 1;
    while (v >= 0x80) {
        v >>= 7;
        ++n;
    }
    return n;
}

/// One game frame: varint length, optional flag byte, payload. The length counts
/// the payload and flag plus 4, so wire size = length + varint bytes - 4.
inline Bytes game_frame(std::span<const uint8_t> payload, uint8_t flag = 0) {
    const size_t content = payload.size() + (flag != 0 ? 1 : 0);
    const uint64_t len = content + 4;
    Bytes out;
    put_varint(out, len);
    if (flag != 0) out.push_back(flag);
    put_bytes(out, payload);
    return out;
}

/// A frame of `opcode` (wire order: first byte, second byte) and `body`.
inline Bytes opcode_frame(uint16_t opcode, std::span<const uint8_t> body, uint8_t flag = 0) {
    Bytes payload;
    put_be16(payload, opcode);
    put_bytes(payload, body);
    return game_frame(payload, flag);
}

inline Bytes opcode_frame(uint16_t opcode, std::initializer_list<uint8_t> body, uint8_t flag = 0) {
    const Bytes b(body);
    return opcode_frame(opcode, b, flag);
}

/// The heartbeat frame: 0E 00 36 and the server's Unix milliseconds.
inline Bytes heartbeat(uint64_t server_ms) {
    Bytes body;
    put_le64(body, server_ms);
    return opcode_frame(0x0036, body);
}

/// The pong frame: 18 03 36, 00 00, the client's clock it echoes and the server's
/// Unix milliseconds.
inline Bytes pong_body(uint64_t client_ms, uint64_t server_ms) {
    Bytes body = {0x00, 0x00};
    put_le64(body, client_ms);
    put_le64(body, server_ms);
    return body;
}

inline Bytes pong(uint64_t client_ms, uint64_t server_ms) { return opcode_frame(0x0336, pong_body(client_ms, server_ms)); }

/// An LZ4 bundle frame: FF FF, the raw size, and `inner` (whole frames) compressed.
inline Bytes bundle_frame(std::span<const uint8_t> inner) {
    Bytes payload = {0xFF, 0xFF};
    put_le32(payload, static_cast<uint32_t>(inner.size()));
    put_bytes(payload, lz4::compress(inner));
    return game_frame(payload);
}

/// Concatenates byte strings.
inline Bytes concat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const auto& p : parts) put_bytes(out, p);
    return out;
}

// ---------------------------------------------------------------------------
// Packet bodies, laid out as protocol/packet_parser.cpp reads them.
// ---------------------------------------------------------------------------

inline void put_f32(Bytes& b, float f) {
    uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(bits));
    put_le32(b, bits);
}

/// 45 36: id, mask, flags (has name), name, server, class, then a level.
inline Bytes identity_body(uint64_t id, std::string_view name, uint16_t server, uint32_t class_code) {
    Bytes b;
    put_varint(b, id);
    put_le32(b, 0x00000101);
    b.push_back(0x01);
    b.push_back(static_cast<uint8_t>(name.size()));
    put_bytes(b, bytes_of(name));
    put_le16(b, server);
    put_le32(b, class_code);
    put_le32(b, 45);  // level
    return b;
}

/// Where player_body and own_player_body put a player unless told otherwise: a
/// spot in the second capture's town.
inline constexpr float kTownX = -110'250.0f;
inline constexpr float kTownY = -103'100.0f;
inline constexpr float kTownZ = 21'912.0f;

/// A movement packet: id, the bytes its opcode puts before the position
/// (`header`: 00 00 for 1a 37; 05 03 or 04 for 1b 37, 1c 37 and 29 37; six for
/// 2e 37; one for 2f 37 and 3c 37), x, y, z, then `tail` bytes not read.
inline Bytes position_body(uint64_t id, const Bytes& header, float x, float y, float z, size_t tail = 4) {
    Bytes b;
    put_varint(b, id);
    put_bytes(b, header);
    put_f32(b, x);
    put_f32(b, y);
    put_f32(b, z);
    b.resize(b.size() + tail, 0x5A);
    return b;
}

/// The shapes the end of a real 45 36 took in the arena capture.
enum class PlayerTrailer : uint8_t {
    /// The tag, a title, a rank, the combat power, five zeros: 63 of its 77.
    Plain,
    /// One byte more after the zeros, a 01: 6 of them.
    ExtraOne,
    /// One byte more after the zeros, a 00: 5. Read from the end, the figure would
    /// come out 256 times too small.
    ExtraZero,
    /// A 01 between the rank and the combat power: 2.
    Inserted,
    /// No title and no rank: one, a level-11 character.
    Untitled,
    /// No tag before the title: none seen; what another patch might send.
    NoTag,
};

/// 45 36 as the game sends it: the identity (its class's third byte the faction), a
/// byte, a flags byte, where the player stands, some 1,200 bytes of gear and looks
/// (filler here), then the tag's u32, a title and a rank (u32 each), the combat
/// power and five zeros, as `trailer` varies them. `zone_byte` puts one byte more
/// (the faction again) after the class, as the fourth capture's map does.
inline Bytes player_body(uint64_t id, std::string_view name, uint16_t server, uint32_t combat_power,
                         PlayerTrailer trailer = PlayerTrailer::Plain, float x = kTownX, float y = kTownY,
                         float z = kTownZ, uint8_t faction = 1, bool zone_byte = false) {
    Bytes b;
    put_varint(b, id);
    put_le32(b, 0x00000101);
    b.push_back(0x01);
    b.push_back(static_cast<uint8_t>(name.size()));
    put_bytes(b, bytes_of(name));
    put_le16(b, server);
    put_le32(b, 0x02000000u | (uint32_t{faction} << 16));
    if (zone_byte) b.push_back(faction);
    put_le16(b, 0xD288);
    put_f32(b, x);
    put_f32(b, y);
    put_f32(b, z);
    b.resize(b.size() + 1200, 0x64);
    if (trailer != PlayerTrailer::NoTag) put_le32(b, 0x1FCBF421);
    if (trailer != PlayerTrailer::Untitled) {
        put_le32(b, 1172);
        put_le32(b, 14);
    }
    if (trailer == PlayerTrailer::Inserted) b.push_back(0x01);
    put_le32(b, combat_power);
    b.resize(b.size() + 5, 0x00);
    if (trailer == PlayerTrailer::ExtraOne) b.push_back(0x01);
    if (trailer == PlayerTrailer::ExtraZero) b.push_back(0x00);
    return b;
}

/// 33 36: the same record, with the u16 the real ones carry between the name and
/// the server (1406 in all of the second capture's).
inline Bytes own_identity_body(uint64_t id, std::string_view name, uint16_t server, uint32_t class_code) {
    Bytes b;
    put_varint(b, id);
    put_le32(b, 0x00000101);
    b.push_back(0x01);
    b.push_back(static_cast<uint8_t>(name.size()));
    put_bytes(b, bytes_of(name));
    put_le16(b, 1406);
    put_le16(b, server);
    put_le32(b, class_code);
    put_le32(b, 45);  // level
    return b;
}

/// 33 36 as the game sends it, as far as it is read: the identity (its class's
/// third byte the faction), 155 bytes of filler, a flags byte and where the local
/// player stands, then the rest of the record (filler). `zone_byte` puts one byte
/// more before the flags, as the fourth capture's map does.
inline Bytes own_player_body(uint64_t id, std::string_view name, uint16_t server, float x = kTownX,
                             float y = kTownY, float z = kTownZ, uint8_t faction = 1, bool zone_byte = false) {
    Bytes b = own_identity_body(id, name, server, 0x2D000000u | (uint32_t{faction} << 16));
    // own_identity_body's level is the first four of the 155.
    b.resize(b.size() + 151, 0x64);
    if (zone_byte) b.push_back(faction);
    b.push_back(0xD2);
    put_f32(b, x);
    put_f32(b, y);
    put_f32(b, z);
    b.resize(b.size() + 400, 0x64);
    return b;
}

struct SpawnSpec {
    uint64_t raw_id{2'000'123};
    uint8_t kind{0x10};
    /// Seven digits, like the game's (src/game/data/npcs.tsv); this one is invented.
    uint32_t npc_code{2'345'678};
    uint64_t cur_hp{1'000'000};
    uint64_t max_hp{1'000'000};
    uint32_t owner{0};
    /// 00 for a corpse coming into view.
    uint8_t alive{0x01};
    /// Fields between the npc code and the position marker. The first capture had
    /// none, but the parser finds the marker wherever they end.
    Bytes before_marker{};
    /// A boss mechanic's target: the mask becomes (kind, 00, 01) and the name, length
    /// first, goes before the npc code, as the second capture's traps had it.
    std::string aimed_at{};
};

/// 41 36, as the first capture showed it: id, the mask (kind, then two bytes), the
/// npc code (u32), then 00 02, which with the code's top byte 00 makes the 00 00 02
/// marker, position, heading, a u16, the alive byte, HP, and for a summon the owner
/// link after them.
inline Bytes spawn_body(const SpawnSpec& s) {
    Bytes b;
    put_varint(b, s.raw_id);
    if (s.aimed_at.empty()) {
        put_bytes(b, {s.kind, 0x20, 0x00});
    } else {
        put_bytes(b, {s.kind, 0x00, 0x01});
        b.push_back(static_cast<uint8_t>(s.aimed_at.size()));
        put_bytes(b, bytes_of(s.aimed_at));
    }
    put_le32(b, s.npc_code);
    if (!s.before_marker.empty()) {
        put_bytes(b, s.before_marker);
        b.push_back(0x00);  // the marker's own first byte, with no code just before it
    }
    put_bytes(b, {0x00, 0x02});
    put_f32(b, 1024.5f);
    put_f32(b, -310.25f);
    put_f32(b, 88.0f);
    put_f32(b, 258.1f);  // heading
    put_le16(b, 0xB786);
    b.push_back(s.alive);
    put_varint(b, s.cur_hp);
    put_varint(b, s.max_hp);
    if (s.owner != 0) {
        put_bytes(b, {0x07, 0x02, 0x06});
        put_le32(b, s.owner);
    }
    put_bytes(b, {0x99, 0x98});
    return b;
}

struct DamageSpec {
    uint64_t target{0};
    uint64_t actor{0};
    uint32_t skill{11000};
    uint8_t layout{4};
    bool crit{false};
    uint8_t modifiers{0};
    /// 1 back, 2 front.
    uint8_t direction{2};
    uint64_t amount{0};
    std::vector<uint64_t> extra_hits;
    bool pad{false};
    /// Switch bit 0x08: a u32 follows the damage.
    bool trailer{false};
};

inline void put_damage_record(Bytes& b, const DamageSpec& d) {
    put_varint(b, d.target);
    put_varint(b, d.layout | (d.extra_hits.empty() ? 0u : 0x20u) | (d.trailer ? 0x08u : 0u));
    put_varint(b, 0);  // flag
    put_varint(b, d.actor);
    put_le32(b, d.skill);
    b.push_back(0x07);  // hit uid
    put_varint(b, d.crit ? 3 : 1);
    // Layouts 4..7. The filler is not zero, so a parser reading the wrong size
    // takes it for the pad, the scalar or the damage and gets the amount wrong.
    constexpr size_t kBlockSize[] = {8, 12, 10, 14};
    const size_t block = kBlockSize[d.layout - 4];
    b.push_back(d.modifiers);
    b.push_back(0x00);
    b.push_back(d.direction);
    for (size_t i = 3; i < block; ++i) b.push_back(0x5A);
    if (d.pad) b.push_back(0x00);
    put_varint(b, 100);  // scalar
    put_varint(b, d.amount);
    if (d.trailer) put_le32(b, 100);
    if (!d.extra_hits.empty()) {
        put_varint(b, d.extra_hits.size());
        for (const uint64_t h : d.extra_hits) put_varint(b, h);
    } else if (d.layout == 4) {
        put_varint(b, 0);  // tail
    }
}

/// 04 38: records joined by 01 00.
inline Bytes damage_body(std::initializer_list<DamageSpec> records) {
    Bytes b;
    bool first = true;
    for (const auto& d : records) {
        if (!first) put_bytes(b, {0x01, 0x00});
        first = false;
        put_damage_record(b, d);
    }
    return b;
}

/// 00 8D kind 2: id, a count, then (stat, u64): stat 0 (HP), and stat 7 (max HP)
/// when `max_hp` is not 0. Anyone's, the local player's too.
inline Bytes hp_body(uint64_t id, uint64_t hp, uint64_t max_hp = 0) {
    Bytes b;
    put_varint(b, id);
    put_bytes(b, {0x02, static_cast<uint8_t>(max_hp != 0 ? 2 : 1), 0x00});
    put_le64(b, hp);
    if (max_hp != 0) {
        b.push_back(0x07);
        put_le64(b, max_hp);
    }
    return b;
}

/// 00 8D for the local player: id, kind 1, then (stat, u32) pairs.
inline Bytes self_values_body(uint64_t id) {
    Bytes b;
    put_varint(b, id);
    put_bytes(b, {0x01, 0x02});
    b.push_back(0x03);
    put_le32(b, 127300);
    b.push_back(0x06);
    put_le32(b, 459500);
    return b;
}

/// 00 8D kind 3, the local player's: kind 1's (stat, u32) list, then a count and
/// kind 2's (stat, u64) list, here with their HP, and their max HP (stat 7) when
/// `max_hp` is not 0.
inline Bytes both_values_body(uint64_t id, uint64_t hp, uint64_t max_hp = 0) {
    Bytes b;
    put_varint(b, id);
    put_bytes(b, {0x03, 0x02});
    b.push_back(0x01);
    put_le32(b, 2096);
    b.push_back(0x03);
    put_le32(b, 92133);
    b.push_back(static_cast<uint8_t>(max_hp != 0 ? 2 : 1));
    b.push_back(0x00);
    put_le64(b, hp);
    if (max_hp != 0) {
        b.push_back(0x07);
        put_le64(b, max_hp);
    }
    return b;
}

/// 05 38 effect 0x30, on the player themself: the unscaled skill 2001, the
/// amount, then the enemy skill whose hit set it off.
inline Bytes heal_on_hit_body(uint64_t player, uint64_t amount, uint32_t cause_skill) {
    Bytes b;
    put_varint(b, player);
    b.push_back(0x30);
    put_varint(b, player);
    put_varint(b, 393);
    put_le32(b, 2001);
    put_varint(b, amount);
    put_le32(b, cause_skill);
    return b;
}

/// 05 38 effect 0x00, a boss's on itself: a u32 and no amount.
inline Bytes no_amount_tick_body(uint64_t id) {
    Bytes b;
    put_varint(b, id);
    b.push_back(0x00);
    put_varint(b, id);
    put_varint(b, 1);
    put_le32(b, 100'000'010);
    return b;
}

/// 05 38: target, effect, actor, unknown varint, skill x100, amount.
inline Bytes tick_body(uint64_t target, uint8_t effect, uint64_t actor, uint32_t skill, uint64_t amount) {
    Bytes b;
    put_varint(b, target);
    b.push_back(effect);
    put_varint(b, actor);
    put_varint(b, 0);
    put_le32(b, skill * 100);
    put_varint(b, amount);
    return b;
}

/// 2a 38 / 2b 38: an effect on `target`. With `skill` set, the record is kind 0x13
/// and names the skill that applied it, as a mantra's do.
struct BuffSpec {
    uint64_t target{0};
    uint64_t caster{0};
    uint32_t instance{1};
    uint32_t buff_id{0};
    uint32_t skill{0};
    uint8_t level{1};
    /// -1 for a permanent effect: the wire's all-ones duration.
    int64_t duration_ms{0};
    uint64_t expires_server_ms{0};
    /// Where the caster stands.
    float x{-111'445.2f};
    float y{-103'490.4f};
    float z{22'214.8f};
};

inline Bytes buff_body(const BuffSpec& s, bool renewed = false) {
    Bytes b;
    put_varint(b, s.target);
    if (!renewed) b.push_back(0x01);
    b.push_back(s.skill != 0 ? 0x13 : 0x11);
    put_varint(b, s.instance);
    put_le32(b, s.buff_id);
    put_le64(b, static_cast<uint64_t>(s.duration_ms));
    put_le64(b, s.expires_server_ms);
    put_varint(b, s.caster);
    b.push_back(s.level);
    if (s.skill != 0) put_le32(b, s.skill);
    b.push_back(renewed ? 0x02 : 0x00);
    put_f32(b, s.x);
    put_f32(b, s.y);
    put_f32(b, s.z);
    return b;
}

/// 1a 92: one instance of an effect left `target`.
inline Bytes buff_remove_body(uint64_t target, uint32_t buff_id) {
    Bytes b;
    put_varint(b, target);
    put_le32(b, buff_id);
    return b;
}

/// 01 38: the local player started `skill`.
inline Bytes skill_use_body(uint32_t skill) {
    Bytes b = {0x00, 0x00, 0x00};
    put_le32(b, skill);
    return b;
}

/// 18 38: `skill` on offer to the local player after `trigger`.
inline Bytes chain_offer_body(uint32_t trigger, uint32_t skill) {
    Bytes b;
    put_le32(b, trigger);
    b.push_back(0x01);
    put_le32(b, skill);
    return b;
}

/// 19 38: `skill` no longer on offer.
inline Bytes chain_end_body(uint32_t skill) {
    Bytes b = {0x01};
    put_le32(b, skill);
    return b;
}

/// 47 38: one cooldown and what is left of it.
inline Bytes cooldown_body(uint32_t skill, uint32_t remaining_ms) {
    Bytes b = {0x01};
    put_le32(b, skill);
    put_varint(b, remaining_ms);
    return b;
}

/// 02 38: `caster`'s `skill` going off at `target`. A press (mode 2) carries a
/// position; anything else (mode 0) one f32. The tails are the capture's shapes.
inline Bytes cast_body(uint64_t caster, uint32_t skill, uint64_t target, bool pressed, uint8_t sequence = 1) {
    Bytes b;
    put_varint(b, caster);
    b.push_back(0x00);
    put_le32(b, skill);
    b.push_back(sequence);
    b.push_back(pressed ? 0x02 : 0x00);
    put_varint(b, target);
    if (pressed) {
        put_f32(b, -83.0f);
        put_f32(b, 12.1f);
        put_f32(b, -99.3f);
        put_f32(b, 0.0f);
    } else {
        put_f32(b, 188.4f);
    }
    put_bytes(b, {0x90, 0x4e, 0x01, 0x00});
    return b;
}

inline Bytes death_body(uint64_t id) {
    Bytes b;
    put_varint(b, id);
    put_bytes(b, {0x00, 0x01});
    return b;
}

/// 47 36: an id, the reason byte, and the byte after it (00 in most).
inline Bytes left_view_body(uint64_t id, uint8_t reason = 1) {
    Bytes b;
    put_varint(b, id);
    put_bytes(b, {reason, 0x00});
    return b;
}

inline Bytes map_load_body(uint32_t load_count, uint32_t map_id) {
    Bytes b;
    put_le32(b, load_count);
    put_le32(b, map_id);
    return b;
}

/// 02 97: party header, then per member a prefix, the name and a tail whose length
/// varies (`tail_bytes` + member index).
inline Bytes party_body(std::initializer_list<std::string_view> names, size_t tail_bytes = 6) {
    Bytes b;
    put_le32(b, 0x5150);
    const std::string_view party = "Raid";
    b.push_back(static_cast<uint8_t>(party.size()));
    put_bytes(b, bytes_of(party));
    b.push_back(static_cast<uint8_t>(names.size()));
    put_le32(b, 0);
    put_bytes(b, {0, 0});
    put_le64(b, 0x0123'0000'0000'0042ULL);
    put_bytes(b, {0, 0, 0});
    put_varint(b, names.size());
    size_t index = 0;
    for (const auto name : names) {
        b.push_back(0x01);
        b.push_back(static_cast<uint8_t>(index));
        put_le64(b, 0x0123'0000'0000'1000ULL + index);
        b.push_back(static_cast<uint8_t>(name.size()));
        put_bytes(b, bytes_of(name));
        put_le32(b, 3);       // class
        put_le32(b, 45);      // level
        put_le32(b, 2100);    // gear score
        b.push_back(0x00);    // flags
        put_le16(b, 0x0123);  // server
        put_le64(b, 987654);  // combat power
        for (size_t i = 0; i < tail_bytes + index; ++i) b.push_back(0xEE);
        ++index;
    }
    return b;
}

} // namespace aion2::test::wire
