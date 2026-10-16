#include "app/display_format.hpp"

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
        case meter::AionClass::Unknown: break;
    }
    return colors::Unknown;
}

Rgb row_color(const meter::CombatantTotals& row) noexcept {
    if (row.cls != meter::AionClass::Unknown) return class_color(row.cls);
    return row.is_local ? colors::Local : colors::Unknown;
}

std::string format_amount(uint64_t value) {
    char buf[32];
    const auto v = static_cast<double>(value);
    if (value >= 1'000'000'000ULL) {
        std::snprintf(buf, sizeof(buf), "%.2fB", v / 1e9);
    } else if (value >= 1'000'000ULL) {
        std::snprintf(buf, sizeof(buf), "%.2fM", v / 1e6);
    } else if (value >= 1'000ULL) {
        std::snprintf(buf, sizeof(buf), "%.1fk", v / 1e3);
    } else {
        std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(value));
    }
    return buf;
}

std::string format_rate(double value) {
    if (value < 0) value = 0;
    char buf[32];
    if (value >= 1e9) {
        std::snprintf(buf, sizeof(buf), "%.2fB", value / 1e9);
    } else if (value >= 1e6) {
        std::snprintf(buf, sizeof(buf), "%.2fM", value / 1e6);
    } else if (value >= 1e3) {
        std::snprintf(buf, sizeof(buf), "%.1fk", value / 1e3);
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f", value);
    }
    return buf;
}

std::string format_percent(double fraction) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.1f%%", 100.0 * fraction);
    return buf;
}

} // namespace aion2::app
