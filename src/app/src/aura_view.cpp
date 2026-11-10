#include "app/aura_view.hpp"

#include "app/display_format.hpp"
#include "app/game_icons.hpp"
#include "app/imgui_guard.hpp"
#include "game/skill_names.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace aion2::app {

namespace {

[[nodiscard]] bool running(const auras::AuraEntry& e, Micros now) noexcept {
    return e.permanent || e.expires_at > now;
}

} // namespace

Micros aura_display_time(Micros snapshot_now, Micros since_arrival) noexcept {
    return snapshot_now + std::min(since_arrival, kAuraDisplayLead);
}

std::vector<const auras::AuraEntry*> shown_auras(const std::vector<auras::AuraEntry>& entries,
                                                 const AuraSettings& s, Micros now, bool self) {
    std::vector<const auras::AuraEntry*> out;
    const auto min_ms = static_cast<uint64_t>(std::max(0.0f, s.min_duration_s) * 1000.0f);
    for (const auras::AuraEntry& e : entries) {
        if (!running(e, now)) continue;
        if (e.permanent ? !s.show_permanent : e.duration_ms < min_ms) continue;
        if (self && e.hostile && !s.show_debuffs) continue;
        out.push_back(&e);
    }
    return out;
}

bool just_ready(const auras::CooldownEntry& c, Micros now) noexcept {
    return c.ready_at <= now && now < c.ready_at + kReadyFlash;
}

std::vector<const auras::CooldownEntry*> shown_cooldowns(const std::vector<auras::CooldownEntry>& entries,
                                                         const AuraSettings& s, Micros now) {
    std::vector<const auras::CooldownEntry*> out;
    if (!s.show_cooldowns) return out;
    const auto min_ms = static_cast<uint64_t>(std::max(0.0f, s.min_cooldown_s) * 1000.0f);
    for (const auras::CooldownEntry& c : entries) {
        // A reset sent for a skill never seen used has no length to show.
        if (c.duration_ms == 0 || c.duration_ms < min_ms) continue;
        const bool ready_shown = s.show_ready && (s.icons || just_ready(c, now));
        if (c.ready_at > now || ready_shown) out.push_back(&c);
    }
    // The snapshot comes soonest ready first, which bars keep; icons keep their place.
    if (s.icons) {
        std::sort(out.begin(), out.end(),
                  [](const auras::CooldownEntry* a, const auras::CooldownEntry* b) { return a->skill < b->skill; });
    }
    return out;
}

std::optional<int> low_hp_percent(const auras::AuraSnapshot& a, const AuraSettings& s) {
    if (!s.low_hp_alert || a.max_hp == 0 || a.hp == 0) return std::nullopt;
    const double share = static_cast<double>(a.hp) / static_cast<double>(a.max_hp);
    if (share * 100.0 >= static_cast<double>(s.low_hp_percent)) return std::nullopt;
    return std::max(1, static_cast<int>(share * 100.0));
}

TileLayout tile_layout(float width, float size, float gap, size_t count) noexcept {
    TileLayout t;
    if (size > 0.0f) t.columns = std::max(1, static_cast<int>(std::floor((width + gap) / (size + gap))));
    t.rows = static_cast<int>((count + static_cast<size_t>(t.columns) - 1) / static_cast<size_t>(t.columns));
    return t;
}

std::string tile_initials(const std::string& label) {
    // Words of ASCII letters and digits: the first letter of the first two, or the
    // first two of a single word.
    std::vector<std::string> words;
    std::string word;
    for (const char ch : label) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x80 && std::isalnum(c) != 0) {
            word += ch;
        } else if (!word.empty()) {
            words.push_back(std::move(word));
            word.clear();
        }
    }
    if (!word.empty()) words.push_back(std::move(word));
    std::string out;
    if (words.size() >= 2) {
        out = {words[0][0], words[1][0]};
    } else if (!words.empty()) {
        out = words[0].substr(0, 2);
    } else {
        return "?";
    }
    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

std::vector<MissingEffect> missing_effects(const auras::AuraSnapshot& auras, const std::vector<uint32_t>& watch,
                                           Micros now) {
    std::vector<MissingEffect> out;
    for (const uint32_t key : watch) {
        const bool on = std::any_of(auras.on_self.begin(), auras.on_self.end(),
                                    [&](const auras::AuraEntry& e) { return e.key == key && running(e, now); });
        if (on) continue;
        MissingEffect m{key, {}};
        const auto seen = std::find_if(auras.seen.begin(), auras.seen.end(),
                                       [&](const auras::SeenEffect& s) { return s.key == key; });
        if (seen != auras.seen.end()) {
            m.label = seen->label;
        } else if (const std::string_view name = game::skill_name(key); !name.empty()) {
            m.label = std::string(name);
        } else {
            m.label = "Effect " + std::to_string(key);
        }
        out.push_back(std::move(m));
    }
    return out;
}

std::string format_time_left(Micros left) {
    const double s = to_seconds(left);
    char buf[24];
    if (s >= 60.0) {
        const auto whole = static_cast<unsigned>(s);
        std::snprintf(buf, sizeof(buf), "%um%02u", whole / 60, whole % 60);
    } else if (s >= 10.0) {
        std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(s));
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f", std::floor(s * 10.0) / 10.0);
    }
    return buf;
}

std::string format_tile_time(Micros left) {
    if (to_seconds(left) < 60.0) return format_time_left(left);
    return std::to_string(static_cast<unsigned>(to_seconds(left) / 60.0)) + "m";
}

} // namespace aion2::app

#ifdef HAVE_IMGUI

#include "app/overlay_chrome.hpp"
#include "app/ui/icons.hpp"

#include <string>

namespace aion2::app {

using chrome::fit;
using chrome::rgba;
using chrome::shadowed_text;
using chrome::u32;

namespace {

/// A bar's colour: an enemy's effect red, the local player's own blue, an ally's green.
Rgb aura_color(const auras::AuraEntry& e) {
    if (e.hostile) return colors::BossHp;
    if (e.mine) return colors::Accent;
    return colors::Good;
}

/// What hovering an effect says: its name, ids, and whose it is.
std::string effect_tooltip(const auras::AuraEntry& e) {
    std::string out = e.label + "\neffect " + std::to_string(e.buff_id);
    if (e.source_skill != 0) out += ", skill " + std::to_string(e.source_skill);
    out += e.hostile ? "\nan enemy's" : (e.mine ? "\nyours" : "\nan ally's");
    return out;
}

/// One row of the overlay as bars: an icon in its own square, then a bar with a
/// label in it, a dim note after the label, and a figure at its right end.
struct BarRow {
    int icon{kNoIcon};
    std::string label;
    /// After the label, dim: an effect's stacks.
    std::string note;
    /// At the right, bold: the time left, "Ready", "Chain".
    std::string value;
    Rgb color{colors::Accent};
    float fraction{1.0f};
    float fill_alpha{0.60f};
    /// Above 0, a border in the bar's colour at this opacity: an alert.
    float border_alpha{0.0f};
    std::string tooltip;
};

/// Where the rows start, and how far apart: an icon high, and the gap under it.
struct BarMetrics {
    float icon;
    float gap;
    float pitch;
};

BarMetrics bar_metrics(float scale) {
    const float icon = std::round(kAuraBarIconPx * scale);
    const float gap = 2.0f * scale;
    return BarMetrics{icon, gap, icon + gap};
}

void draw_bar_rows(const std::vector<BarRow>& rows, float scale, bool interactive) {
    if (rows.empty()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const BarMetrics m = bar_metrics(scale);
    const float line = ImGui::GetTextLineHeight();
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float rounding = 3.0f * scale;
    const float pad = 6.0f * scale;
    float y = origin.y;
    for (const BarRow& r : rows) {
        const ImVec2 row_min(origin.x, y);
        const ImVec2 row_max(origin.x + width, y + m.icon);
        // The icon's square: dark under an icon with see-through corners, tinted for
        // an effect with none (a monster's), so the labels still line up.
        const ImVec2 icon_max(row_min.x + m.icon, row_max.y);
        dl->AddRectFilled(row_min, icon_max, u32(colors::Background, 0.85f), rounding);
        if (r.icon != kNoIcon) {
            draw_icon(dl, r.icon, row_min.x, row_min.y, m.icon, 1.0f, rounding);
        } else {
            dl->AddRectFilled(row_min, icon_max, u32(r.color, 0.30f), rounding);
        }
        const ImVec2 bar_min(icon_max.x + 3.0f * scale, y);
        chrome::BarStyle style;
        style.alpha = r.fill_alpha;
        chrome::draw_bar(dl, bar_min, row_max, r.fraction, r.color, scale, style);
        if (r.border_alpha > 0.0f) {
            dl->AddRect(bar_min, row_max, u32(r.color, r.border_alpha), rounding, 0, 1.5f * scale);
        }

        const float ty = y + (m.icon - line) * 0.5f - 0.5f * scale;
        float value_w = 0.0f;
        if (!r.value.empty()) {
            const chrome::BoldScope bold;
            value_w = ImGui::CalcTextSize(r.value.c_str()).x;
            dl->AddText(ImVec2(row_max.x - pad - value_w, ty), u32(colors::Text), r.value.c_str());
        }
        const float note_w = r.note.empty() ? 0.0f : ImGui::CalcTextSize(r.note.c_str()).x + 5.0f * scale;
        const float label_x = bar_min.x + pad;
        const std::string label = fit(r.label, row_max.x - pad - value_w - pad - note_w - label_x);
        dl->AddText(ImVec2(label_x, ty), u32(colors::Text), label.c_str());
        if (!r.note.empty()) {
            dl->AddText(ImVec2(label_x + ImGui::CalcTextSize(label.c_str()).x + 5.0f * scale, ty),
                        u32(colors::Text, 0.65f), r.note.c_str());
        }
        if (interactive && !r.tooltip.empty() && ImGui::IsMouseHoveringRect(row_min, row_max)) {
            ImGui::SetTooltip("%s", r.tooltip.c_str());
        }
        y += m.pitch;
    }
    ImGui::Dummy(ImVec2(width, y - origin.y - m.gap));
}

/// An effect: the share of its duration left, its name and stacks, the time left.
BarRow effect_row(const auras::AuraEntry& e, Micros now) {
    BarRow r;
    r.icon = effect_icon(e.buff_id, e.source_skill);
    r.label = e.label;
    if (e.stacks > 1) r.note = "x" + std::to_string(e.stacks);
    r.color = aura_color(e);
    if (!e.permanent) {
        const Micros remaining = e.expires_at > now ? e.expires_at - now : 0;
        const double total = static_cast<double>(e.duration_ms) * static_cast<double>(kMicrosPerMilli);
        r.fraction =
            total > 0.0 ? static_cast<float>(std::clamp(static_cast<double>(remaining) / total, 0.0, 1.0)) : 0.0f;
        r.value = format_time_left(remaining);
    }
    r.tooltip = effect_tooltip(e);
    return r;
}

/// A cooldown: the share of it left, the skill, and the time to ready, "~" before an
/// estimate. One that just came back is a full bar reading "Ready".
BarRow cooldown_row(const auras::CooldownEntry& c, Micros now, float pulse) {
    BarRow r;
    r.icon = skill_icon(c.skill);
    r.label = c.label;
    const bool ready = c.ready_at <= now;
    if (ready) {
        r.color = colors::Good;
        r.fill_alpha = 0.40f + 0.35f * pulse;
        r.value = "Ready";
    } else {
        const Micros left = c.ready_at - now;
        const double total = static_cast<double>(c.duration_ms) * static_cast<double>(kMicrosPerMilli);
        r.color = colors::Warn;
        r.fraction = total > 0.0 ? static_cast<float>(std::clamp(static_cast<double>(left) / total, 0.0, 1.0)) : 0.0f;
        r.value = (c.exact ? "" : "~") + format_time_left(left);
    }
    r.tooltip = c.label + "\n" +
                (c.exact ? "what the server says is left"
                         : "~ an estimate: the listed cooldown less your specialties,\n"
                           "shortened to the quickest reuse seen this session");
    return r;
}

/// A chain skill on offer, glowing: the skill and what set it off. No timer: the
/// server does not send the window, only the offer and its end.
BarRow chain_row(const auras::ChainEntry& c, float pulse) {
    BarRow r;
    r.icon = skill_icon(c.skill);
    r.label = c.label;
    r.value = "Chain";
    r.color = colors::Warn;
    r.fill_alpha = 0.45f + 0.40f * pulse;
    r.border_alpha = 0.6f + 0.4f * pulse;
    r.tooltip = c.label + "\non offer after " + game::skill_label(c.trigger);
    return r;
}

/// A warning across the window, in a red that pulses: low HP, a watched effect gone.
void draw_alert(const std::string& text, float scale, float pulse) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float line = ImGui::GetTextLineHeight();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + ImGui::GetContentRegionAvail().x, min.y + bar_metrics(scale).icon);
    ImGui::Dummy(ImVec2(max.x - min.x, max.y - min.y));
    const float rounding = 3.0f * scale;
    dl->AddRectFilled(min, max, u32(colors::Live, 0.12f + 0.14f * pulse), rounding);
    dl->AddRect(min, max, u32(colors::Live, 0.30f + 0.40f * pulse), rounding, 0, 1.0f);
    const float ty = min.y + (max.y - min.y - line) * 0.5f - 0.5f * scale;
    const float pad = 6.0f * scale;
    dl->AddText(ImVec2(min.x + pad, ty), u32(colors::Live), ICON_WARNING);
    const float text_x = min.x + pad + ImGui::CalcTextSize(ICON_WARNING).x + 5.0f * scale;
    const chrome::BoldScope bold;
    dl->AddText(ImVec2(text_x, ty), u32(colors::Text), fit(text, max.x - pad - text_x).c_str());
}

/// A square in icons mode: an icon (or a tinted square with initials), a shade over
/// part of it, a time in the middle and a count in the corner, and a border.
struct Tile {
    int icon{kNoIcon};
    std::string initials;
    /// The border's colour, and the fill of a tile with no icon.
    Rgb tint{colors::Accent};
    float border_alpha{0.9f};
    float border_px{1.5f};
    float icon_alpha{1.0f};
    /// The share darkened, from the top: an effect's time gone, a cooldown's time left.
    float shade{0.0f};
    std::string center;
    std::string corner;
    std::string tooltip;
};

/// Tiles of `size` in rows across the window, wrapping.
void draw_tiles(const std::vector<Tile>& tiles, float size, float scale, bool interactive) {
    if (tiles.empty()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float gap = 4.0f * scale;
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const TileLayout layout = tile_layout(width, size, gap, tiles.size());
    const float rounding = 4.0f * scale;
    const chrome::BoldScope bold;
    for (size_t i = 0; i < tiles.size(); ++i) {
        const Tile& t = tiles[i];
        const auto col = static_cast<float>(i % static_cast<size_t>(layout.columns));
        const auto row = static_cast<float>(i / static_cast<size_t>(layout.columns));
        const ImVec2 min(origin.x + col * (size + gap), origin.y + row * (size + gap));
        const ImVec2 max(min.x + size, min.y + size);
        dl->AddRectFilled(min, max, u32(colors::Background, 0.85f), rounding);
        if (t.icon != kNoIcon) {
            draw_icon(dl, t.icon, min.x, min.y, size, t.icon_alpha, rounding);
        } else {
            dl->AddRectFilled(min, max, u32(t.tint, 0.35f * t.icon_alpha), rounding);
            dl->AddText(ImVec2(min.x + 4.0f * scale, min.y + 3.0f * scale), u32(colors::Text, t.icon_alpha),
                        t.initials.c_str());
        }
        if (t.shade > 0.0f) {
            const float shade = std::clamp(t.shade, 0.0f, 1.0f);
            dl->AddRectFilled(min, ImVec2(max.x, min.y + size * shade), u32(colors::Background, 0.65f), rounding,
                              shade >= 1.0f ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersTop);
        }
        if (!t.center.empty()) {
            const ImVec2 ts = ImGui::CalcTextSize(t.center.c_str());
            shadowed_text(dl, ImVec2(min.x + (size - ts.x) * 0.5f, min.y + (size - ts.y) * 0.5f), t.center, scale);
        }
        if (!t.corner.empty()) {
            const ImVec2 ts = ImGui::CalcTextSize(t.corner.c_str());
            shadowed_text(dl, ImVec2(max.x - ts.x - 3.0f * scale, max.y - ts.y - 1.0f * scale), t.corner, scale);
        }
        // A dark line inside the coloured border, which keeps it apart from a bright icon.
        const float border = t.border_px * scale;
        dl->AddRect(ImVec2(min.x + border, min.y + border), ImVec2(max.x - border, max.y - border),
                    u32(Rgb{0.0f, 0.0f, 0.0f}, 0.45f), std::max(0.0f, rounding - border), 0, 1.0f);
        dl->AddRect(min, max, u32(t.tint, t.border_alpha), rounding, 0, border);
        if (interactive && !t.tooltip.empty() && ImGui::IsMouseHoveringRect(min, max)) {
            ImGui::SetTooltip("%s", t.tooltip.c_str());
        }
    }
    ImGui::Dummy(ImVec2(width, static_cast<float>(layout.rows) * (size + gap) - gap));
}

Tile effect_tile(const auras::AuraEntry& e, Micros now) {
    Tile t;
    t.icon = effect_icon(e.buff_id, e.source_skill);
    t.initials = tile_initials(e.label);
    t.tint = aura_color(e);
    if (!e.permanent) {
        const Micros left = e.expires_at > now ? e.expires_at - now : 0;
        const double total = static_cast<double>(e.duration_ms) * static_cast<double>(kMicrosPerMilli);
        const double share_left = total > 0.0 ? std::clamp(static_cast<double>(left) / total, 0.0, 1.0) : 0.0;
        t.shade = static_cast<float>(1.0 - share_left);
        t.center = format_tile_time(left);
    }
    if (e.stacks > 1) t.corner = std::to_string(e.stacks);
    t.tooltip = effect_tooltip(e);
    return t;
}

/// Cooling: dim, shaded by the share left, with the time. Ready: lit, glowing as
/// it comes back.
Tile cooldown_tile(const auras::CooldownEntry& c, Micros now, float pulse) {
    Tile t;
    t.icon = skill_icon(c.skill);
    t.initials = tile_initials(c.label);
    if (c.ready_at > now) {
        const Micros left = c.ready_at - now;
        const double total = static_cast<double>(c.duration_ms) * static_cast<double>(kMicrosPerMilli);
        t.shade = total > 0.0 ? static_cast<float>(std::clamp(static_cast<double>(left) / total, 0.0, 1.0)) : 0.0f;
        t.icon_alpha = 0.45f;
        t.tint = colors::Warn;
        t.border_alpha = 0.5f;
        t.border_px = 1.0f;
        t.center = (c.exact ? "" : "~") + format_tile_time(left);
        t.tooltip = c.label + (c.exact ? "\nwhat the server says is left" : "\n~ an estimate");
    } else {
        t.tint = colors::Good;
        const bool flash = just_ready(c, now);
        t.border_alpha = flash ? 0.5f + 0.5f * pulse : 0.6f;
        t.border_px = flash ? 3.0f : 1.0f;
        t.tooltip = c.label + "\nready";
    }
    return t;
}

Tile chain_tile(const auras::ChainEntry& c, float pulse) {
    Tile t;
    t.icon = skill_icon(c.skill);
    t.initials = tile_initials(c.label);
    t.tint = colors::Warn;
    t.border_alpha = 0.5f + 0.5f * pulse;
    t.border_px = 3.0f;
    t.tooltip = c.label + "\non offer after " + game::skill_label(c.trigger);
    return t;
}

/// A watched effect that is missing: its icon dimmed, in a pulsing red border.
Tile missing_tile(const MissingEffect& m, int icon, float pulse) {
    Tile t;
    t.icon = icon;
    t.tint = colors::Live;
    t.icon_alpha = 0.35f;
    t.border_alpha = 0.4f + 0.6f * pulse;
    t.border_px = 2.5f;
    t.tooltip = "Missing: " + m.label;
    return t;
}

} // namespace

OverlayActions draw_auras(const AuraFrameInput& in) {
    OverlayActions out;
    const AuraSettings defaults{};
    const AuraSettings& s = in.settings != nullptr ? *in.settings : defaults;
    const float scale = in.scale;
    const bool interactive = !s.locked;

    if (chrome::begin("##aion2auras", s.opacity, scale, interactive)) {
        // Header, while it can be moved: a title and the buttons. Locked, the window
        // is only its bars.
        if (interactive) {
            ImGui::TextColored(rgba(colors::TextDim), "AURAS");
            chrome::HeaderButtons buttons;
            buttons.scale = scale;
            buttons.app_tooltip = "Open the app: the aura settings are under Settings.";
            buttons.lock_tooltip = "Click-through and fixed in place. Unlock it in the app.";
            chrome::finish_header(buttons, out);
        }

        const auras::AuraSnapshot* auras = in.snapshot != nullptr ? &in.snapshot->auras : nullptr;
        bool drew = false;
        if (auras != nullptr) {
            // Slow pulses, so an alert catches the eye without flashing.
            const float pulse = 0.55f + 0.45f * static_cast<float>(std::sin(in.pulse_s * 4.0));
            const float glow = 0.5f + 0.5f * static_cast<float>(std::sin(in.pulse_s * 6.0));
            const float tile = static_cast<float>(s.icon_size) * scale;
            if (const auto low = low_hp_percent(*auras, s)) {
                draw_alert("Low HP " + std::to_string(*low) + "%", scale, pulse);
                drew = true;
            }
            // Missing watched effects: in icons, a tile for each that has an icon.
            std::vector<Tile> alert_tiles;
            for (const MissingEffect& m : missing_effects(*auras, s.watch, in.now)) {
                const int icon = s.icons ? skill_icon(m.key) : kNoIcon;
                if (icon != kNoIcon) {
                    alert_tiles.push_back(missing_tile(m, icon, pulse));
                } else {
                    draw_alert("Missing: " + m.label, scale, pulse);
                }
                drew = true;
            }
            if (s.show_chains && !auras->chains.empty()) {
                if (s.icons) {
                    for (const auras::ChainEntry& c : auras->chains) alert_tiles.push_back(chain_tile(c, glow));
                } else {
                    std::vector<BarRow> rows;
                    for (const auras::ChainEntry& c : auras->chains) rows.push_back(chain_row(c, glow));
                    draw_bar_rows(rows, scale, interactive);
                }
                drew = true;
            }
            draw_tiles(alert_tiles, tile, scale, interactive);

            // One list of effects, as tiles or as bars.
            const auto draw_effects = [&](const std::vector<const auras::AuraEntry*>& entries) {
                if (s.icons) {
                    std::vector<Tile> tiles;
                    for (const auras::AuraEntry* e : entries) tiles.push_back(effect_tile(*e, in.now));
                    draw_tiles(tiles, tile, scale, interactive);
                } else {
                    std::vector<BarRow> rows;
                    for (const auras::AuraEntry* e : entries) rows.push_back(effect_row(*e, in.now));
                    draw_bar_rows(rows, scale, interactive);
                }
            };
            const auto self = shown_auras(auras->on_self, s, in.now, true);
            if (!self.empty()) {
                draw_effects(self);
                drew = true;
            }
            const auto cooldowns = shown_cooldowns(auras->cooldowns, s, in.now);
            if (!cooldowns.empty()) {
                chrome::section_title("Cooldowns", scale);
                if (s.icons) {
                    std::vector<Tile> tiles;
                    for (const auras::CooldownEntry* c : cooldowns) tiles.push_back(cooldown_tile(*c, in.now, glow));
                    draw_tiles(tiles, tile, scale, interactive);
                } else {
                    std::vector<BarRow> rows;
                    for (const auras::CooldownEntry* c : cooldowns) rows.push_back(cooldown_row(*c, in.now, glow));
                    draw_bar_rows(rows, scale, interactive);
                }
                drew = true;
            }
            if (s.show_target && auras->target != 0) {
                const auto target = shown_auras(auras->on_target, s, in.now, false);
                if (!target.empty()) {
                    chrome::section_title("On " + auras->target_label, scale);
                    draw_effects(target);
                    drew = true;
                }
            }
        }
        if (!drew && interactive) {
            ImGui::TextColored(rgba(colors::TextDim), "%s",
                               in.snapshot == nullptr || in.snapshot->capture == pipeline::CaptureState::Waiting
                                   ? "Waiting for the game."
                                   : "No effects on you.");
        }

        if (interactive) out.resize_grip = chrome::resize_grip(scale);
    }
    chrome::end();
    return out;
}

} // namespace aion2::app

#else // !HAVE_IMGUI

namespace aion2::app {

OverlayActions draw_auras(const AuraFrameInput&) { return {}; }

} // namespace aion2::app

#endif
