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

#include <string>

namespace aion2::app {

namespace {

ImVec4 rgba(Rgb c, float a = 1.0f) { return ImVec4(c.r, c.g, c.b, a); }

ImU32 u32(Rgb c, float a = 1.0f) { return ImGui::GetColorU32(rgba(c, a)); }

float button_width(const char* label) {
    return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

/// `s` cut with "..." to fit `width`, by whole UTF-8 code points.
std::string fit(const std::string& s, float width) {
    if (ImGui::CalcTextSize(s.c_str()).x <= width) return s;
    std::string out = s;
    while (!out.empty() && ImGui::CalcTextSize((out + "...").c_str()).x > width) {
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) out.pop_back();
        if (!out.empty()) out.pop_back();
    }
    return out + "...";
}

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

/// One bar per effect: its share of its duration left, its name and stacks, and the
/// time left on the right.
void draw_entries(const std::vector<const auras::AuraEntry*>& entries, Micros now, float scale, bool interactive) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float line = ImGui::GetTextLineHeight();
    const float row_h = line + 5.0f * scale;
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float time_w = ImGui::CalcTextSize("00m00").x + 6.0f * scale;
    float y = origin.y;
    for (const auras::AuraEntry* e : entries) {
        const ImVec2 row_min(origin.x, y);
        const ImVec2 row_max(origin.x + width, y + row_h - 1.0f * scale);
        dl->AddRectFilled(row_min, row_max, u32(colors::Text, 0.04f), 2.0f * scale);
        float fraction = 1.0f;
        std::string left = "-";
        if (!e->permanent) {
            const Micros remaining = e->expires_at > now ? e->expires_at - now : 0;
            const double total = static_cast<double>(e->duration_ms) * static_cast<double>(kMicrosPerMilli);
            fraction = total > 0.0 ? static_cast<float>(std::clamp(static_cast<double>(remaining) / total, 0.0, 1.0)) : 0.0f;
            left = format_time_left(remaining);
        }
        dl->AddRectFilled(row_min, ImVec2(row_min.x + width * fraction, row_max.y), u32(aura_color(*e), 0.45f),
                          2.0f * scale);

        const float ty = y + (row_h - line) * 0.5f - 0.5f * scale;
        // The skill's icon, then the name; an effect without one (a monster's) keeps
        // the space, so the names line up.
        draw_icon(dl, effect_icon(e->buff_id, e->source_skill), origin.x + 2.0f * scale,
                  y + (row_h - 1.0f * scale - line) * 0.5f, line);
        const float label_x = origin.x + line + 6.0f * scale;
        std::string label = e->label;
        if (e->stacks > 1) label += " x" + std::to_string(e->stacks);
        dl->AddText(ImVec2(label_x, ty), u32(colors::Text),
                    fit(label, row_max.x - time_w - label_x - 4.0f * scale).c_str());
        const ImVec2 size = ImGui::CalcTextSize(left.c_str());
        dl->AddText(ImVec2(row_max.x - 4.0f * scale - size.x, ty), u32(colors::Text), left.c_str());

        if (interactive && ImGui::IsMouseHoveringRect(row_min, row_max)) {
            ImGui::SetTooltip("%s", effect_tooltip(*e).c_str());
        }
        y += row_h;
    }
    ImGui::Dummy(ImVec2(width, std::max(0.0f, y - origin.y)));
}

void section_title(const std::string& title) { ImGui::TextColored(rgba(colors::TextDim), "%s", title.c_str()); }

/// One bar per cooldown: the share of it left, the skill, and the time to ready,
/// "~" before an estimate. One that just came back is a full bar reading "Ready".
void draw_cooldowns(const std::vector<const auras::CooldownEntry*>& entries, Micros now, float scale, bool interactive,
                    float pulse) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float line = ImGui::GetTextLineHeight();
    const float row_h = line + 5.0f * scale;
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float time_w = ImGui::CalcTextSize("~00m00").x + 6.0f * scale;
    float y = origin.y;
    for (const auras::CooldownEntry* c : entries) {
        const ImVec2 row_min(origin.x, y);
        const ImVec2 row_max(origin.x + width, y + row_h - 1.0f * scale);
        dl->AddRectFilled(row_min, row_max, u32(colors::Text, 0.04f), 2.0f * scale);
        const bool ready = c->ready_at <= now;
        const Micros left = ready ? 0 : c->ready_at - now;
        const double total = static_cast<double>(c->duration_ms) * static_cast<double>(kMicrosPerMilli);
        const float fraction =
            ready ? 1.0f
                  : (total > 0.0 ? static_cast<float>(std::clamp(static_cast<double>(left) / total, 0.0, 1.0)) : 0.0f);
        dl->AddRectFilled(row_min, ImVec2(row_min.x + width * fraction, row_max.y),
                          ready ? u32(colors::Good, 0.30f + 0.30f * pulse) : u32(colors::Warn, 0.40f), 2.0f * scale);
        const float ty = y + (row_h - line) * 0.5f - 0.5f * scale;
        draw_icon(dl, skill_icon(c->skill), origin.x + 2.0f * scale, y + (row_h - 1.0f * scale - line) * 0.5f, line);
        const float label_x = origin.x + line + 6.0f * scale;
        dl->AddText(ImVec2(label_x, ty), u32(colors::Text),
                    fit(c->label, row_max.x - time_w - label_x - 4.0f * scale).c_str());
        const std::string time = ready ? "Ready" : (c->exact ? "" : "~") + format_time_left(left);
        const ImVec2 size = ImGui::CalcTextSize(time.c_str());
        dl->AddText(ImVec2(row_max.x - 4.0f * scale - size.x, ty), u32(colors::Text), time.c_str());
        if (interactive && ImGui::IsMouseHoveringRect(row_min, row_max)) {
            ImGui::SetTooltip("%s\n%s", c->label.c_str(),
                              c->exact ? "what the server says is left"
                                       : "~ an estimate: the listed cooldown less your specialties,\n"
                                         "shortened to the quickest reuse seen this session");
        }
        y += row_h;
    }
    ImGui::Dummy(ImVec2(width, std::max(0.0f, y - origin.y)));
}

/// One bar per chain skill on offer, glowing: the skill and what set it off. No
/// timer: the server does not send the window, only the offer and its end.
void draw_chains(const std::vector<auras::ChainEntry>& chains, float scale, bool interactive, float pulse) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float line = ImGui::GetTextLineHeight();
    const float row_h = line + 5.0f * scale;
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    float y = origin.y;
    for (const auras::ChainEntry& c : chains) {
        const ImVec2 row_min(origin.x, y);
        const ImVec2 row_max(origin.x + width, y + row_h - 1.0f * scale);
        dl->AddRectFilled(row_min, row_max, u32(colors::Warn, 0.30f + 0.35f * pulse), 2.0f * scale);
        dl->AddRect(row_min, row_max, u32(colors::Warn, 0.6f + 0.4f * pulse), 2.0f * scale, 0, 1.5f * scale);
        draw_icon(dl, skill_icon(c.skill), origin.x + 2.0f * scale, y + (row_h - 1.0f * scale - line) * 0.5f, line);
        const float label_x = origin.x + line + 6.0f * scale;
        const float ty = y + (row_h - line) * 0.5f - 0.5f * scale;
        const char* tag = "Chain";
        const float tag_w = ImGui::CalcTextSize(tag).x;
        dl->AddText(ImVec2(label_x, ty), u32(colors::Text),
                    fit(c.label, row_max.x - tag_w - label_x - 10.0f * scale).c_str());
        dl->AddText(ImVec2(row_max.x - 4.0f * scale - tag_w, ty), u32(colors::Text), tag);
        if (interactive && ImGui::IsMouseHoveringRect(row_min, row_max)) {
            ImGui::SetTooltip("%s\non offer after %s", c.label.c_str(), game::skill_label(c.trigger).c_str());
        }
        y += row_h;
    }
    ImGui::Dummy(ImVec2(width, std::max(0.0f, y - origin.y)));
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

/// Text with a dark shadow, readable over any icon.
void shadowed_text(ImDrawList* dl, ImVec2 at, const std::string& text, float scale) {
    dl->AddText(ImVec2(at.x + 1.0f * scale, at.y + 1.0f * scale), u32(Rgb{0.0f, 0.0f, 0.0f}, 0.9f), text.c_str());
    dl->AddText(at, u32(colors::Text), text.c_str());
}

/// Tiles of `size` in rows across the window, wrapping.
void draw_tiles(const std::vector<Tile>& tiles, float size, float scale, bool interactive) {
    if (tiles.empty()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float gap = 4.0f * scale;
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const TileLayout layout = tile_layout(width, size, gap, tiles.size());
    const float rounding = 3.0f * scale;
    for (size_t i = 0; i < tiles.size(); ++i) {
        const Tile& t = tiles[i];
        const auto col = static_cast<float>(i % static_cast<size_t>(layout.columns));
        const auto row = static_cast<float>(i / static_cast<size_t>(layout.columns));
        const ImVec2 min(origin.x + col * (size + gap), origin.y + row * (size + gap));
        const ImVec2 max(min.x + size, min.y + size);
        dl->AddRectFilled(min, max, u32(colors::Background, 0.85f), rounding);
        if (t.icon != kNoIcon) {
            draw_icon(dl, t.icon, min.x, min.y, size, t.icon_alpha);
        } else {
            dl->AddRectFilled(min, max, u32(t.tint, 0.35f * t.icon_alpha), rounding);
            dl->AddText(ImVec2(min.x + 3.0f * scale, min.y + 2.0f * scale), u32(colors::Text, t.icon_alpha),
                        t.initials.c_str());
        }
        if (t.shade > 0.0f) {
            dl->AddRectFilled(min, ImVec2(max.x, min.y + size * std::clamp(t.shade, 0.0f, 1.0f)),
                              u32(colors::Background, 0.65f));
        }
        if (!t.center.empty()) {
            const ImVec2 ts = ImGui::CalcTextSize(t.center.c_str());
            shadowed_text(dl, ImVec2(min.x + (size - ts.x) * 0.5f, min.y + (size - ts.y) * 0.5f), t.center, scale);
        }
        if (!t.corner.empty()) {
            const ImVec2 ts = ImGui::CalcTextSize(t.corner.c_str());
            shadowed_text(dl, ImVec2(max.x - ts.x - 2.0f * scale, max.y - ts.y), t.corner, scale);
        }
        dl->AddRect(min, max, u32(t.tint, t.border_alpha), rounding, 0, t.border_px * scale);
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
    const ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * scale, 6.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, rgba(colors::Background, s.opacity));
    ImGui::PushStyleColor(ImGuiCol_Border, rgba(colors::Border, interactive ? 1.0f : std::min(1.0f, s.opacity + 0.2f)));
    ImGui::PushStyleColor(ImGuiCol_Text, rgba(colors::Text));

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("##aion2auras", nullptr, flags)) {
        // Header, while it can be moved: a title and the buttons. Locked, the window
        // is only its bars.
        if (interactive) {
            ImGui::TextColored(rgba(colors::TextDim), "AURAS");
            const float text_end_x = ImGui::GetItemRectMax().x;
            const char* app_label = "App";
            const char* lock_label = "Lock";
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float buttons_x =
                ImGui::GetWindowContentRegionMax().x - (button_width(app_label) + button_width(lock_label) + spacing);
            if (buttons_x > text_end_x + spacing) {
                ImGui::SameLine(buttons_x);
                if (ImGui::SmallButton(app_label)) out.open_app = true;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open the app: the aura settings are under Settings.");
                ImGui::SameLine();
                if (ImGui::SmallButton(lock_label)) out.lock = true;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click-through and fixed in place. Unlock it in the app.");
            }
            out.drag_area = ClientRect{0.0f, 0.0f, std::max(buttons_x, text_end_x), ImGui::GetCursorPosY()};
            ImGui::Separator();
        }

        const auras::AuraSnapshot* auras = in.snapshot != nullptr ? &in.snapshot->auras : nullptr;
        bool drew = false;
        if (auras != nullptr) {
            // Slow pulses, so an alert catches the eye without flashing.
            const float pulse = 0.55f + 0.45f * static_cast<float>(std::sin(in.pulse_s * 4.0));
            const float glow = 0.5f + 0.5f * static_cast<float>(std::sin(in.pulse_s * 6.0));
            const float tile = static_cast<float>(s.icon_size) * scale;
            if (const auto low = low_hp_percent(*auras, s)) {
                ImGui::TextColored(rgba(colors::Live, pulse), "Low HP %d%%", *low);
                drew = true;
            }
            // Missing watched effects: in icons, a tile for each that has an icon.
            std::vector<Tile> alert_tiles;
            for (const MissingEffect& m : missing_effects(*auras, s.watch, in.now)) {
                const int icon = s.icons ? skill_icon(m.key) : kNoIcon;
                if (icon != kNoIcon) {
                    alert_tiles.push_back(missing_tile(m, icon, pulse));
                } else {
                    ImGui::TextColored(rgba(colors::Live, pulse), "Missing: %s", m.label.c_str());
                }
                drew = true;
            }
            if (s.show_chains && !auras->chains.empty()) {
                if (s.icons) {
                    for (const auras::ChainEntry& c : auras->chains) alert_tiles.push_back(chain_tile(c, glow));
                } else {
                    draw_chains(auras->chains, scale, interactive, glow);
                }
                drew = true;
            }
            draw_tiles(alert_tiles, tile, scale, interactive);

            const auto self = shown_auras(auras->on_self, s, in.now, true);
            if (!self.empty()) {
                if (s.icons) {
                    std::vector<Tile> tiles;
                    for (const auras::AuraEntry* e : self) tiles.push_back(effect_tile(*e, in.now));
                    draw_tiles(tiles, tile, scale, interactive);
                } else {
                    draw_entries(self, in.now, scale, interactive);
                }
                drew = true;
            }
            const auto cooldowns = shown_cooldowns(auras->cooldowns, s, in.now);
            if (!cooldowns.empty()) {
                section_title("Cooldowns");
                if (s.icons) {
                    std::vector<Tile> tiles;
                    for (const auras::CooldownEntry* c : cooldowns) tiles.push_back(cooldown_tile(*c, in.now, glow));
                    draw_tiles(tiles, tile, scale, interactive);
                } else {
                    draw_cooldowns(cooldowns, in.now, scale, interactive, glow);
                }
                drew = true;
            }
            if (s.show_target && auras->target != 0) {
                const auto target = shown_auras(auras->on_target, s, in.now, false);
                if (!target.empty()) {
                    section_title("On " + auras->target_label);
                    if (s.icons) {
                        std::vector<Tile> tiles;
                        for (const auras::AuraEntry* e : target) tiles.push_back(effect_tile(*e, in.now));
                        draw_tiles(tiles, tile, scale, interactive);
                    } else {
                        draw_entries(target, in.now, scale, interactive);
                    }
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

        if (interactive) {
            const float g = 12.0f * scale;
            const ImVec2 br(io.DisplaySize.x - 2.0f, io.DisplaySize.y - 2.0f);
            ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(br.x - g, br.y), br, ImVec2(br.x, br.y - g),
                                                          u32(colors::TextDim, 0.6f));
            out.resize_grip = ClientRect{br.x - g, br.y - g, br.x + 2.0f, br.y + 2.0f};
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);
    return out;
}

} // namespace aion2::app

#else // !HAVE_IMGUI

namespace aion2::app {

OverlayActions draw_auras(const AuraFrameInput&) { return {}; }

} // namespace aion2::app

#endif
