#pragma once

#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

namespace aion2::replay {

/// `bytes` as offset, 16 hex bytes and their printable ASCII per line, each line
/// starting with `indent`.
inline std::string hex_dump(std::span<const uint8_t> bytes, const std::string& indent = "    ") {
    std::string out;
    char buf[32];
    for (size_t line = 0; line < bytes.size(); line += 16) {
        std::snprintf(buf, sizeof(buf), "%04zx  ", line);
        out += indent;
        out += buf;
        std::string ascii;
        for (size_t i = 0; i < 16; ++i) {
            if (line + i < bytes.size()) {
                const uint8_t b = bytes[line + i];
                std::snprintf(buf, sizeof(buf), "%02x ", unsigned{b});
                out += buf;
                ascii += (b >= 0x20 && b < 0x7F) ? static_cast<char>(b) : '.';
            } else {
                out += "   ";
            }
            if (i == 7) out += ' ';
        }
        out += ' ';
        out += ascii;
        out += '\n';
    }
    return out;
}

/// `bytes` as one line of space-separated hex.
inline std::string hex_line(std::span<const uint8_t> bytes) {
    std::string out;
    char buf[16];
    for (size_t i = 0; i < bytes.size(); ++i) {
        std::snprintf(buf, sizeof(buf), i == 0 ? "%02x" : " %02x", unsigned{bytes[i]});
        out += buf;
    }
    return out;
}

} // namespace aion2::replay
