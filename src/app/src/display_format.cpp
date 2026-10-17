#include "app/display_format.hpp"

#include <cmath>
#include <cstdio>

namespace aion2::app {

Rgb class_color(meter::AionClass cls) noexcept {
    switch (cls) {
        case meter::AionClass::Gladiator: return {0.80f, 0.52f, 0.30f};
        case meter::AionClass::Templar: return {0.62f, 0.68f, 0.78f};
        case meter::AionClass::Assassin: return {0.68f, 0.40f, 0.85f};
        case meter::AionClass::Ranger: return {0.45f, 0.78f, 0.35f};
        case meter::AionClass::Sorcerer: return {0.35f, 0.55f, 0.95f};
        case meter::AionClass::Spiritmaster: return {0.80f, 0.45f, 0.75f};
        case meter::AionClass::Cleric: return {0.95f, 0.88f, 0.55f};
        case meter::AionClass::Chanter: return {0.30f, 0.80f, 0.75f};
        case meter::AionClass::Brawler: return {0.88f, 0.36f, 0.32f};
        case meter::AionClass::Unknown: break;
    }
    return colors::Unknown;
}

Rgb row_color(const meter::CombatantTotals& row) noexcept {
    if (row.cls != meter::AionClass::Unknown) return class_color(row.cls);
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
