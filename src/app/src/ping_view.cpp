#include "app/ping_view.hpp"

#include "app/display_format.hpp"
#include "app/imgui_guard.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <vector>

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

PingRowLayout ping_row_layout(float ping_w, float jitter_w, float gap, float max_w) noexcept {
    if (jitter_w <= 0.0f) return PingRowLayout{1, ping_w};
    const float both = ping_w + gap + jitter_w;
    if (both <= max_w) return PingRowLayout{1, both};
    return PingRowLayout{2, std::max(ping_w, jitter_w)};
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

#include "app/overlay_chrome.hpp"

namespace aion2::app {

using chrome::rgba;
using chrome::u32;

namespace {

Rgb level_color(PingLevel level) noexcept {
    switch (level) {
        case PingLevel::Good: return colors::Good;
        case PingLevel::Warn: return colors::Warn;
        case PingLevel::Bad: return colors::Live;
    }
    return colors::Text;
}

/// A quad under the ping's line, from it down to the graph's floor, fading as it goes.
void fill_under(ImDrawList* dl, ImVec2 a, ImVec2 b, float floor_y, float top_y) {
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    const float span = std::max(1.0f, floor_y - top_y);
    // Brighter the higher the line runs, so a spike stands out.
    const auto shade = [&](float y) { return u32(colors::Accent, 0.04f + 0.30f * std::clamp((floor_y - y) / span, 0.0f, 1.0f)); };
    const ImU32 none = u32(colors::Accent, 0.02f);
    dl->PrimReserve(6, 4);
    const auto first = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
    dl->PrimWriteVtx(a, uv, shade(a.y));
    dl->PrimWriteVtx(b, uv, shade(b.y));
    dl->PrimWriteVtx(ImVec2(b.x, floor_y), uv, none);
    dl->PrimWriteVtx(ImVec2(a.x, floor_y), uv, none);
    for (const int i : {0, 1, 2, 0, 2, 3}) dl->PrimWriteIdx(static_cast<ImDrawIdx>(first + i));
}

/// The graph in the rectangle from `p0`, `w` by `h`: each second's lateness as a bar
/// from the bottom, the ping as a line over them with a soft fill under it.
void draw_graph(const PingGraph& g, ImVec2 p0, float w, float h, float scale) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + w, p0.y + h);
    dl->AddRectFilled(p0, p1, u32(colors::Text, 0.05f), 3.0f * scale);
    const size_t n = g.columns.size();
    if (n == 0 || g.top_ms <= 0.0f) return;
    const float step = w / static_cast<float>(n);
    const auto y_of = [&](float ms) { return p1.y - h * std::clamp(ms / g.top_ms, 0.0f, 1.0f); };
    // Half the scale, to read a height by.
    const float mid_y = std::round(p0.y + h * 0.5f);
    dl->AddLine(ImVec2(p0.x, mid_y), ImVec2(p1.x, mid_y), u32(colors::Text, 0.07f), 1.0f);

    // Each second's point, where there is one.
    std::vector<ImVec2> points(n);
    std::vector<bool> have(n, false);
    for (size_t i = 0; i < n; ++i) {
        if (g.columns[i].ping_ms < 0.0f) continue;
        points[i] = ImVec2(p0.x + step * (static_cast<float>(i) + 0.5f), y_of(g.columns[i].ping_ms));
        have[i] = true;
    }
    for (size_t i = 1; i < n; ++i) {
        if (have[i - 1] && have[i]) fill_under(dl, points[i - 1], points[i], p1.y, p0.y);
    }

    for (size_t i = 0; i < n; ++i) {
        const float late = g.columns[i].late_ms;
        // Under a millisecond is the heartbeat's own resolution.
        if (late < 1.0f) continue;
        const float x0 = p0.x + step * static_cast<float>(i);
        dl->AddRectFilled(ImVec2(x0, y_of(late)), ImVec2(std::max(x0 + step, x0 + 1.0f), p1.y), u32(colors::Warn, 0.60f));
    }

    const float thickness = std::max(1.0f, 1.5f * scale);
    size_t newest = n;
    for (size_t i = 0; i < n; ++i) {
        if (!have[i]) continue;
        newest = i;
        if (i > 0 && have[i - 1]) {
            dl->AddLine(points[i - 1], points[i], u32(colors::Accent), thickness);
        } else if (i + 1 >= n || !have[i + 1]) {
            // A second on its own still shows, as a dot.
            dl->AddCircleFilled(points[i], thickness, u32(colors::Accent));
        }
    }
    // The newest second, where the eye starts.
    if (newest < n) {
        dl->AddCircleFilled(points[newest], 3.5f * scale, u32(colors::Accent, 0.30f));
        dl->AddCircleFilled(points[newest], 2.0f * scale, u32(chrome::lighten(colors::Accent, 0.35f)));
    }

    char top[24];
    std::snprintf(top, sizeof(top), "%.0f ms", static_cast<double>(g.top_ms));
    dl->AddText(ImVec2(p0.x + 4.0f * scale, p0.y + 2.0f * scale), u32(colors::TextDim, 0.75f), top);
}

/// "min 64": a dim caption and its figure.
void labelled(const char* label, float value, float scale) {
    ImGui::TextColored(rgba(colors::TextDim), "%s", label);
    ImGui::SameLine(0.0f, 4.0f * scale);
    ImGui::Text("%.0f", static_cast<double>(value));
}

} // namespace

OverlayActions draw_ping(const PingFrameInput& in) {
    OverlayActions out;
    const PingSettings defaults{};
    const PingSettings& s = in.settings != nullptr ? *in.settings : defaults;
    const float scale = in.scale;
    const bool interactive = !s.locked;

    const ping::PingSnapshot none{};
    const ping::PingSnapshot& p = in.snapshot != nullptr ? in.snapshot->ping : none;
    const Micros now = in.snapshot != nullptr ? in.snapshot->now : 0;
    const std::optional<float> ping = shown_ping(p, now);
    const Rgb level = ping ? level_color(ping_level(*ping, s)) : colors::TextDim;
    const std::string ping_text = ping ? format_ms(*ping) : std::string("-");
    const std::string jitter_text = p.has_jitter ? format_ms(p.jitter_ms) : std::string("-");

    // The line of figures, measured first: without the graph the panel is as large
    // as that line.
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImVec2 pad = chrome::window_padding(scale);
    const ImVec2 spacing = chrome::item_spacing(scale);
    const float dot_gap = 6.0f * scale;
    const float ping_w = chrome::dot_width(scale) + dot_gap + ImGui::CalcTextSize("Ping").x + spacing.x +
                         (ping ? chrome::bold_text_width(ping_text) : ImGui::CalcTextSize(ping_text.c_str()).x);
    const float jitter_w =
        s.show_jitter ? ImGui::CalcTextSize("Jitter").x + spacing.x + ImGui::CalcTextSize(jitter_text.c_str()).x : 0.0f;
    const float inner_max = std::max(0.0f, display.x - 2.0f * pad.x);
    const PingRowLayout row = ping_row_layout(ping_w, jitter_w, 14.0f * scale, inner_max);
    const float line = ImGui::GetTextLineHeight();

    // The figures' panel: under where the header is while unlocked, so locking moves
    // and resizes nothing. With the graph it takes the rest of the window.
    const float header_h = chrome::header_height(scale);
    const ImVec2 body_pos(0.0f, header_h);
    const ImVec2 body_size =
        s.show_graph ? ImVec2(display.x, std::max(0.0f, display.y - header_h))
                     : ImVec2(std::min(display.x, row.width + 2.0f * pad.x),
                              static_cast<float>(row.rows) * line + static_cast<float>(row.rows - 1) * spacing.y + 2.0f * pad.y);
    const float inner_w = std::max(0.0f, body_size.x - 2.0f * pad.x);

    const bool open = interactive ? chrome::begin("##aion2ping", s.opacity, scale, true)
                                  : chrome::begin("##aion2ping", s.opacity, scale, false, body_pos, body_size);
    if (open) {
        if (interactive) {
            ImGui::TextColored(rgba(colors::TextDim), "PING");
            chrome::HeaderButtons buttons;
            buttons.scale = scale;
            buttons.app_tooltip = "Open the app: the ping's settings are on its page.";
            buttons.lock_tooltip = "Click-through and fixed in place. Unlock it in the app.";
            chrome::finish_header(buttons, out);
            if (!s.show_graph) {
                // What stays once locked, marked out in the window it is moved by.
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 p1(body_pos.x + body_size.x, body_pos.y + body_size.y);
                dl->AddRectFilled(body_pos, p1, u32(colors::Text, 0.08f), 8.0f * scale);
            }
            // Exactly where the locked panel's content starts, however tall the
            // header came out.
            ImGui::SetCursorPos(ImVec2(pad.x, header_h + pad.y));
        }
        const float left = ImGui::GetCursorPosX();

        // A light in the ping's colour, the ping, and the jitter at the panel's right
        // edge, or under the ping where there is no room beside it.
        chrome::dot(level, scale, ping ? 1.0f : 0.6f);
        ImGui::SameLine(0.0f, dot_gap);
        ImGui::TextColored(rgba(colors::TextDim), "Ping");
        ImGui::SameLine();
        if (ping) {
            const chrome::BoldScope bold;
            ImGui::TextColored(rgba(level), "%s", ping_text.c_str());
        } else {
            ImGui::TextColored(rgba(colors::TextDim), "%s", ping_text.c_str());
            if (interactive && ImGui::IsItemHovered()) ImGui::SetTooltip("%s.", ping_status(p, now));
        }
        if (s.show_jitter) {
            if (row.rows == 1) ImGui::SameLine(left + inner_w - jitter_w);
            ImGui::TextColored(rgba(colors::TextDim), "Jitter");
            ImGui::SameLine();
            ImGui::TextUnformatted(jitter_text.c_str());
        }

        if (s.show_graph) {
            // The graph takes what is left above the line of figures under it: the
            // same locked or not, the grip drawn over the corner.
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const float graph_h = avail.y - ImGui::GetTextLineHeightWithSpacing();
            if (graph_h >= 14.0f * scale && avail.x > 0.0f) {
                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(avail.x, graph_h));
                draw_graph(ping_graph(p, now), p0, avail.x, graph_h, scale);
            }
            if (p.samples > 0) {
                labelled("min", p.min_ms, scale);
                ImGui::SameLine(0.0f, 10.0f * scale);
                labelled("avg", p.avg_ms, scale);
                ImGui::SameLine(0.0f, 10.0f * scale);
                labelled("max", p.max_ms, scale);
            } else {
                ImGui::TextColored(rgba(colors::TextDim), "%s", ping_status(p, now));
            }
        }

        if (interactive && !s.show_graph) {
            // The rest of the window is only there to move it by: say so, where
            // there is room under the panel.
            ImGui::SetCursorPos(ImVec2(pad.x, body_pos.y + body_size.y + spacing.y + 2.0f * scale));
            const char* hint = "Locked, only this stays.";
            ImGui::PushTextWrapPos(display.x - pad.x);
            const float hint_h = ImGui::CalcTextSize(hint, nullptr, false, inner_max).y;
            if (ImGui::GetCursorPosY() + hint_h <= display.y - pad.y) {
                ImGui::TextColored(rgba(colors::TextDim, 0.8f), "%s", hint);
            }
            ImGui::PopTextWrapPos();
        }

        if (interactive) out.resize_grip = chrome::resize_grip(scale);
    }
    chrome::end();
    return out;
}

} // namespace aion2::app

#else // !HAVE_IMGUI

namespace aion2::app {

OverlayActions draw_ping(const PingFrameInput&) { return {}; }

} // namespace aion2::app

#endif
