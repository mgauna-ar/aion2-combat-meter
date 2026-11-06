#include "app/aura_view.hpp"

#include "app/display_format.hpp"
#include "app/imgui_guard.hpp"
#include "meter/skill_names.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace aion2::app {

namespace {

[[nodiscard]] bool running(const meter::AuraEntry& e, Micros now) noexcept {
    return e.permanent || e.expires_at > now;
}

} // namespace

Micros aura_display_time(Micros snapshot_now, Micros since_arrival) noexcept {
    return snapshot_now + std::min(since_arrival, kAuraDisplayLead);
}

std::vector<const meter::AuraEntry*> shown_auras(const std::vector<meter::AuraEntry>& entries,
                                                 const AuraSettings& s, Micros now, bool self) {
    std::vector<const meter::AuraEntry*> out;
    const auto min_ms = static_cast<uint64_t>(std::max(0.0f, s.min_duration_s) * 1000.0f);
    for (const meter::AuraEntry& e : entries) {
        if (!running(e, now)) continue;
        if (e.permanent ? !s.show_permanent : e.duration_ms < min_ms) continue;
        if (self && e.hostile && !s.show_debuffs) continue;
        out.push_back(&e);
    }
    return out;
}

std::vector<const meter::CooldownEntry*> shown_cooldowns(const std::vector<meter::CooldownEntry>& entries,
                                                         const AuraSettings& s, Micros now) {
    std::vector<const meter::CooldownEntry*> out;
    if (!s.show_cooldowns) return out;
    const auto min_ms = static_cast<uint64_t>(std::max(0.0f, s.min_cooldown_s) * 1000.0f);
    for (const meter::CooldownEntry& c : entries) {
        if (c.ready_at > now && c.duration_ms >= min_ms) out.push_back(&c);
    }
    return out;
}

std::vector<MissingEffect> missing_effects(const meter::AuraSnapshot& auras, const std::vector<uint32_t>& watch,
                                           Micros now) {
    std::vector<MissingEffect> out;
    for (const uint32_t key : watch) {
        const bool on = std::any_of(auras.on_self.begin(), auras.on_self.end(),
                                    [&](const meter::AuraEntry& e) { return e.key == key && running(e, now); });
        if (on) continue;
        MissingEffect m{key, {}};
        const auto seen = std::find_if(auras.seen.begin(), auras.seen.end(),
                                       [&](const meter::SeenEffect& s) { return s.key == key; });
        if (seen != auras.seen.end()) {
            m.label = seen->label;
        } else if (const std::string_view name = meter::skill_name(key); !name.empty()) {
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
Rgb aura_color(const meter::AuraEntry& e) {
    if (e.hostile) return colors::BossHp;
    if (e.mine) return colors::Accent;
    return colors::Good;
}

/// One bar per effect: its share of its duration left, its name and stacks, and the
/// time left on the right.
void draw_entries(const std::vector<const meter::AuraEntry*>& entries, Micros now, float scale, bool interactive) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float line = ImGui::GetTextLineHeight();
    const float row_h = line + 5.0f * scale;
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float time_w = ImGui::CalcTextSize("00m00").x + 6.0f * scale;
    float y = origin.y;
    for (const meter::AuraEntry* e : entries) {
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
        std::string label = e->label;
        if (e->stacks > 1) label += " x" + std::to_string(e->stacks);
        dl->AddText(ImVec2(origin.x + 4.0f * scale, ty), u32(colors::Text),
                    fit(label, width - time_w - 8.0f * scale).c_str());
        const ImVec2 size = ImGui::CalcTextSize(left.c_str());
        dl->AddText(ImVec2(row_max.x - 4.0f * scale - size.x, ty), u32(colors::Text), left.c_str());

        if (interactive && ImGui::IsMouseHoveringRect(row_min, row_max)) {
            ImGui::SetTooltip("%s\neffect %u%s%s", e->label.c_str(), e->buff_id,
                              e->source_skill != 0 ? (", skill " + std::to_string(e->source_skill)).c_str() : "",
                              e->hostile ? "\nan enemy's" : (e->mine ? "\nyours" : "\nan ally's"));
        }
        y += row_h;
    }
    ImGui::Dummy(ImVec2(width, std::max(0.0f, y - origin.y)));
}

void section_title(const std::string& title) { ImGui::TextColored(rgba(colors::TextDim), "%s", title.c_str()); }

/// One bar per cooldown: the share of it left, the skill, and the time to ready,
/// "~" before an estimate.
void draw_cooldowns(const std::vector<const meter::CooldownEntry*>& entries, Micros now, float scale,
                    bool interactive) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float line = ImGui::GetTextLineHeight();
    const float row_h = line + 5.0f * scale;
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float time_w = ImGui::CalcTextSize("~00m00").x + 6.0f * scale;
    float y = origin.y;
    for (const meter::CooldownEntry* c : entries) {
        const ImVec2 row_min(origin.x, y);
        const ImVec2 row_max(origin.x + width, y + row_h - 1.0f * scale);
        dl->AddRectFilled(row_min, row_max, u32(colors::Text, 0.04f), 2.0f * scale);
        const Micros left = c->ready_at > now ? c->ready_at - now : 0;
        const double total = static_cast<double>(c->duration_ms) * static_cast<double>(kMicrosPerMilli);
        const float fraction =
            total > 0.0 ? static_cast<float>(std::clamp(static_cast<double>(left) / total, 0.0, 1.0)) : 0.0f;
        dl->AddRectFilled(row_min, ImVec2(row_min.x + width * fraction, row_max.y), u32(colors::Warn, 0.40f),
                          2.0f * scale);
        const float ty = y + (row_h - line) * 0.5f - 0.5f * scale;
        dl->AddText(ImVec2(origin.x + 4.0f * scale, ty), u32(colors::Text),
                    fit(c->label, width - time_w - 8.0f * scale).c_str());
        const std::string time = (c->exact ? "" : "~") + format_time_left(left);
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

        const meter::AuraSnapshot* auras = in.snapshot != nullptr ? &in.snapshot->auras : nullptr;
        bool drew = false;
        if (auras != nullptr) {
            const auto missing = missing_effects(*auras, s.watch, in.now);
            if (!missing.empty()) {
                // A slow pulse, so a missing effect catches the eye without flashing.
                const float pulse = 0.55f + 0.45f * static_cast<float>(std::sin(in.pulse_s * 4.0));
                for (const MissingEffect& m : missing) {
                    ImGui::TextColored(rgba(colors::Live, pulse), "Missing: %s", m.label.c_str());
                }
                drew = true;
            }
            const auto self = shown_auras(auras->on_self, s, in.now, true);
            if (!self.empty()) {
                draw_entries(self, in.now, scale, interactive);
                drew = true;
            }
            const auto cooldowns = shown_cooldowns(auras->cooldowns, s, in.now);
            if (!cooldowns.empty()) {
                section_title("Cooldowns");
                draw_cooldowns(cooldowns, in.now, scale, interactive);
                drew = true;
            }
            if (s.show_target && auras->target != 0) {
                const auto target = shown_auras(auras->on_target, s, in.now, false);
                if (!target.empty()) {
                    section_title("On " + auras->target_label);
                    draw_entries(target, in.now, scale, interactive);
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
