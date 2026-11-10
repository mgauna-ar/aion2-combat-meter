#include "app/ping_view.hpp"

#include "app/display_format.hpp"
#include "app/imgui_guard.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace aion2::app {

PingLevel ping_level(float ms, const PingSettings& s) noexcept {
    if (ms >= static_cast<float>(s.bad_ms)) return PingLevel::Bad;
    if (ms >= static_cast<float>(s.warn_ms)) return PingLevel::Warn;
    return PingLevel::Good;
}

namespace {

[[nodiscard]] bool stale(const ping::PingSnapshot& p, Micros now) noexcept {
    return now > p.ping_at && now - p.ping_at > ping::PingTracker::kPingStale;
}

} // namespace

std::optional<float> shown_ping(const ping::PingSnapshot& p, Micros now) noexcept {
    if (p.state != ping::PingState::Measured || p.ping_at == 0 || stale(p, now)) return std::nullopt;
    return p.ping_ms;
}

const char* ping_status(const ping::PingSnapshot& p, Micros now) noexcept {
    switch (p.state) {
        case ping::PingState::Waiting: return "Waiting for the game's ping";
        case ping::PingState::NoCounter: return "A replay has no ping";
        case ping::PingState::ClockRejected: return "The game's clock is not the one expected";
        case ping::PingState::Measured: break;
    }
    return p.ping_at == 0 || stale(p, now) ? "No answer to the game's ping lately" : "";
}

std::string format_ms(float ms) {
    char buf[32];
    if (ms < 9.95f) {
        std::snprintf(buf, sizeof(buf), "%.1f ms", static_cast<double>(std::max(ms, 0.0f)));
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f ms", static_cast<double>(ms));
    }
    return buf;
}

float ping_graph_top(float highest) noexcept {
    constexpr float kSteps[] = {50.0f, 100.0f, 150.0f, 200.0f, 300.0f, 500.0f, 750.0f, 1000.0f, 1500.0f, 2000.0f, 3000.0f};
    for (const float step : kSteps) {
        if (highest <= step) return step;
    }
    return std::ceil(highest / 1000.0f) * 1000.0f;
}

PingGraph ping_graph(const ping::PingSnapshot& p, Micros now) {
    PingGraph g;
    g.columns.resize(kPingGraphColumns);
    const Micros now_second = now - now % kMicrosPerSecond;
    float highest = 0.0f;
    for (const ping::PingSecond& second : p.seconds) {
        // A second past `now` (the display running a moment behind) is the newest.
        const size_t age = second.ts >= now_second ? 0 : static_cast<size_t>((now_second - second.ts) / kMicrosPerSecond);
        if (age >= kPingGraphColumns) continue;
        PingGraphColumn& column = g.columns[kPingGraphColumns - 1 - age];
        column.ping_ms = second.ping_ms;
        column.late_ms = second.late_ms;
        highest = std::max({highest, second.ping_ms, second.late_ms});
    }
    g.top_ms = ping_graph_top(highest);
    return g;
}

} // namespace aion2::app

#ifdef HAVE_IMGUI

namespace aion2::app {

namespace {

ImVec4 rgba(Rgb c, float a = 1.0f) { return ImVec4(c.r, c.g, c.b, a); }

ImU32 u32(Rgb c, float a = 1.0f) { return ImGui::GetColorU32(rgba(c, a)); }

float button_width(const char* label) {
    return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

Rgb level_color(PingLevel level) noexcept {
    switch (level) {
        case PingLevel::Good: return colors::Good;
        case PingLevel::Warn: return colors::Warn;
        case PingLevel::Bad: return colors::Live;
    }
    return colors::Text;
}

/// The graph in the rectangle from `p0`, `w` by `h`: each second's lateness as a bar
/// from the bottom, the ping as a line over them.
void draw_graph(const PingGraph& g, ImVec2 p0, float w, float h, float scale) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + w, p0.y + h);
    dl->AddRectFilled(p0, p1, u32(colors::Text, 0.04f), 2.0f * scale);
    const size_t n = g.columns.size();
    if (n == 0 || g.top_ms <= 0.0f) return;
    const float step = w / static_cast<float>(n);
    const auto y_of = [&](float ms) { return p1.y - h * std::clamp(ms / g.top_ms, 0.0f, 1.0f); };

    for (size_t i = 0; i < n; ++i) {
        const float late = g.columns[i].late_ms;
        // Under a millisecond is the heartbeat's own resolution.
        if (late < 1.0f) continue;
        const float x0 = p0.x + step * static_cast<float>(i);
        dl->AddRectFilled(ImVec2(x0, y_of(late)), ImVec2(std::max(x0 + step, x0 + 1.0f), p1.y), u32(colors::Warn, 0.55f));
    }

    const float thickness = std::max(1.0f, 1.5f * scale);
    bool have_prev = false;
    ImVec2 prev;
    for (size_t i = 0; i < n; ++i) {
        const float ping = g.columns[i].ping_ms;
        if (ping < 0.0f) {
            have_prev = false;
            continue;
        }
        const ImVec2 at(p0.x + step * (static_cast<float>(i) + 0.5f), y_of(ping));
        if (have_prev) {
            dl->AddLine(prev, at, u32(colors::Accent), thickness);
        } else {
            // A second on its own still shows, as a dot.
            dl->AddRectFilled(ImVec2(at.x - thickness, at.y - thickness), ImVec2(at.x + thickness, at.y + thickness),
                              u32(colors::Accent));
        }
        prev = at;
        have_prev = true;
    }

    char top[24];
    std::snprintf(top, sizeof(top), "%.0f ms", static_cast<double>(g.top_ms));
    dl->AddText(ImVec2(p0.x + 3.0f * scale, p0.y + 1.0f * scale), u32(colors::TextDim, 0.75f), top);
}

} // namespace

OverlayActions draw_ping(const PingFrameInput& in) {
    OverlayActions out;
    const PingSettings defaults{};
    const PingSettings& s = in.settings != nullptr ? *in.settings : defaults;
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
    if (ImGui::Begin("##aion2ping", nullptr, flags)) {
        // Header, while it can be moved: a title and the buttons. Locked, the window
        // is only its figures.
        if (interactive) {
            ImGui::TextColored(rgba(colors::TextDim), "PING");
            const float text_end_x = ImGui::GetItemRectMax().x;
            const char* app_label = "App";
            const char* lock_label = "Lock";
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float buttons_x =
                ImGui::GetWindowContentRegionMax().x - (button_width(app_label) + button_width(lock_label) + spacing);
            if (buttons_x > text_end_x + spacing) {
                ImGui::SameLine(buttons_x);
                if (ImGui::SmallButton(app_label)) out.open_app = true;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open the app: the ping's settings are on its page.");
                ImGui::SameLine();
                if (ImGui::SmallButton(lock_label)) out.lock = true;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click-through and fixed in place. Unlock it in the app.");
            }
            out.drag_area = ClientRect{0.0f, 0.0f, std::max(buttons_x, text_end_x), ImGui::GetCursorPosY()};
            ImGui::Separator();
        }

        const ping::PingSnapshot none{};
        const ping::PingSnapshot& p = in.snapshot != nullptr ? in.snapshot->ping : none;
        const Micros now = in.snapshot != nullptr ? in.snapshot->now : 0;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;

        // The ping, and the jitter at the right edge while there is room beside it.
        ImGui::TextColored(rgba(colors::TextDim), "Ping");
        ImGui::SameLine();
        const std::optional<float> ping = shown_ping(p, now);
        if (ping) {
            ImGui::TextColored(rgba(level_color(ping_level(*ping, s))), "%s", format_ms(*ping).c_str());
        } else {
            ImGui::TextColored(rgba(colors::TextDim), "-");
            if (interactive && ImGui::IsItemHovered()) ImGui::SetTooltip("%s.", ping_status(p, now));
        }
        if (s.show_jitter) {
            const std::string jitter = p.has_jitter ? format_ms(p.jitter_ms) : std::string("-");
            const float label_w = ImGui::CalcTextSize("Jitter").x;
            const float width = label_w + spacing + ImGui::CalcTextSize(jitter.c_str()).x;
            const float x = ImGui::GetWindowContentRegionMax().x - width;
            if (x > ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + spacing) ImGui::SameLine(x);
            ImGui::TextColored(rgba(colors::TextDim), "Jitter");
            ImGui::SameLine();
            ImGui::TextUnformatted(jitter.c_str());
        }

        if (s.show_graph) {
            // The graph takes what is left above the line of figures under it.
            const float line = ImGui::GetTextLineHeightWithSpacing();
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const float graph_h = avail.y - line - (interactive ? 10.0f * scale : 0.0f);
            if (graph_h >= 14.0f * scale && avail.x > 0.0f) {
                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(avail.x, graph_h));
                draw_graph(ping_graph(p, now), p0, avail.x, graph_h, scale);
            }
            if (p.samples > 0) {
                ImGui::TextColored(rgba(colors::TextDim), "min %.0f  avg %.0f  max %.0f", static_cast<double>(p.min_ms),
                                   static_cast<double>(p.avg_ms), static_cast<double>(p.max_ms));
            } else {
                ImGui::TextColored(rgba(colors::TextDim), "%s", ping_status(p, now));
            }
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

OverlayActions draw_ping(const PingFrameInput&) { return {}; }

} // namespace aion2::app

#endif
