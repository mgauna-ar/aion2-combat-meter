#include "app/display_format.hpp"

#include <cmath>
#include <cstdio>

namespace aion2::app {

namespace {

constexpr Rgb hex(uint32_t rgb) noexcept {
    return {static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
            static_cast<float>(rgb & 0xFF) / 255.0f};
}

} // namespace

/// The game's own: each class's party emblem (game_icons), in the bright tone it
/// reads in on a dark background, as tools/gen_icon_sheet.py samples it. The
/// Brawler has no such emblem yet and keeps a red of its own.
Rgb class_color(game::AionClass cls) noexcept {
    switch (cls) {
        case game::AionClass::Gladiator: return hex(0x6EB7C9);
        case game::AionClass::Templar: return hex(0x7EA0F9);
        case game::AionClass::Assassin: return hex(0x7EB775);
        case game::AionClass::Ranger: return hex(0x5CAC8A);
        case game::AionClass::Sorcerer: return hex(0xAE84E8);
        case game::AionClass::Spiritmaster: return hex(0xC060C1);
        case game::AionClass::Cleric: return hex(0xE3CA77);
        case game::AionClass::Chanter: return hex(0xE1A359);
        case game::AionClass::Brawler: return {0.88f, 0.36f, 0.32f};
        case game::AionClass::Unknown: break;
    }
    return colors::Unknown;
}

Rgb row_color(const meter::CombatantTotals& row) noexcept {
    if (row.cls != game::AionClass::Unknown) return class_color(row.cls);
    return row.is_local ? colors::Local : colors::Unknown;
}

namespace {

/// `v` with k, M or B, chosen by how it prints: a unit starts where the one below
/// would round up to 1000 (999,960 reads 1.00M, not 1000.0k).
std::string with_unit(double v, const char* below_k) {
    char buf[32];
    if (v >= 1e9 - 5e3) {
        std::snprintf(buf, sizeof(buf), "%.2fB", v / 1e9);
    } else if (v >= 1e6 - 50.0) {
        std::snprintf(buf, sizeof(buf), "%.2fM", v / 1e6);
    } else if (v >= 1e3 - 0.05) {
        std::snprintf(buf, sizeof(buf), "%.1fk", v / 1e3);
    } else {
        std::snprintf(buf, sizeof(buf), below_k, v);
    }
    return buf;
}

} // namespace

std::string format_amount(uint64_t value) { return with_unit(static_cast<double>(value), "%.0f"); }

std::string format_rate(double value) {
    if (!std::isfinite(value) || value < 0.0) value = 0.0;
    return with_unit(value, "%.1f");
}

std::string format_percent(double fraction) {
    if (!std::isfinite(fraction)) fraction = 0.0;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.1f%%", 100.0 * fraction);
    return buf;
}

std::vector<ShownRow> shown_rows(const std::vector<meter::CombatantTotals>& rows, int max_rows,
                                 bool with_healers) {
    std::vector<ShownRow> out;
    if (max_rows <= 0) return out;
    const auto limit = static_cast<size_t>(max_rows);
    int rank = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        // Players who only healed or were hit rank below everyone who dealt damage.
        if (rows[i].damage == 0 && !with_healers) continue;
        ++rank;
        if (out.size() < limit) {
            out.push_back({i, rank});
        } else if (rows[i].is_local) {
            out.back() = {i, rank};
            break;
        }
    }
    return out;
}

} // namespace aion2::app
