#include "app/ui/meter_tabs.hpp"

#include "app/display_format.hpp"
#include "app/game_icons.hpp"
#include "app/timeline_chart.hpp"
#include "app/ui/icons.hpp"
#include "app/ui/table.hpp"
#include "app/ui/theme.hpp"
#include "app/ui/widgets.hpp"
#include "common/time_format.hpp"
#include "game/skill_names.hpp"
#include "meter/fight_detail.hpp"

#ifdef HAVE_IMGUI
#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <vector>
#endif

namespace aion2::app::ui {

#ifdef HAVE_IMGUI

namespace {

/// The boss's line: its own colour, apart from every class's.
constexpr uint32_t kBossColor = colors::DangerLight;

void metric_button(TimelineView& view, const char* label, meter::TimelineMetric metric) {
    if (button(label, view.metric == metric ? ButtonKind::Primary : ButtonKind::Secondary, ButtonSize::Fit)) {
        view.metric = metric;
    }
}

void smoothing_button(TimelineView& view, const char* label, size_t seconds) {
    if (button(label, view.smoothing_s == seconds ? ButtonKind::Primary : ButtonKind::Secondary, ButtonSize::Fit)) {
        view.smoothing_s = seconds;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Each point averages the %zu seconds around it.", seconds);
}

/// What the lines show on the left, how smooth they are and what else is drawn on
/// the right.
void render_controls(TimelineView& view) {
    const char* damage = ICON_SWORDS "  DPS##TimelineDamage";
    const char* healing = ICON_HEART "  HPS##TimelineHealing";
    const char* taken = ICON_SHIELD "  Damage taken##TimelineTaken";
    metric_button(view, damage, meter::TimelineMetric::Damage);
    same_line_if_room(button_width(healing, ButtonSize::Fit));
    metric_button(view, healing, meter::TimelineMetric::Healing);
    same_line_if_room(button_width(taken, ButtonSize::Fit));
    metric_button(view, taken, meter::TimelineMetric::Taken);

    const char* buffs = ICON_SPARKLE "  Buffs##TimelineBuffs";
    constexpr const char* kLabels[] = {"5 s##TimelineSmooth5", "15 s##TimelineSmooth15", "30 s##TimelineSmooth30"};
    constexpr size_t kSeconds[] = {5, 15, 30};
    const float gap = m(4.0f);
    float width = button_width(buffs, ButtonSize::Fit) + m(12.0f) + gap * 2.0f;
    for (const char* label : kLabels) width += button_width(label, ButtonSize::Fit);
    same_line_if_room(width, 24.0f);
    right_align(width);
    if (button(buffs, view.show_buffs ? ButtonKind::Primary : ButtonKind::Secondary, ButtonSize::Fit)) {
        view.show_buffs = !view.show_buffs;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Bands where one player's buff was on two or more party members.");
    }
    ImGui::SameLine(0.0f, m(12.0f));
    for (size_t i = 0; i < std::size(kLabels); ++i) {
        if (i > 0) ImGui::SameLine(0.0f, gap);
        smoothing_button(view, kLabels[i], kSeconds[i]);
    }
}

bool is_shown(const TimelineView& view, const TimelineLine& line) { return !view.hidden.contains(line.name); }

uint32_t line_color(const TimelineLine& line) {
    return class_or(line.cls, line.is_local ? colors::Accent : colors::TextMuted);
}

bool boss_shown(const AppViewState& state) { return state.timeline.show_boss && !state.timeline_chart.boss_hp.empty(); }

/// What every shown line read at the hovered point, and what was going on then.
void render_readout(const AppViewState& state, const meter::FightDetail& detail, size_t point, double at_s,
                    double span_s, ImVec2 anchor) {
    const TimelineChart& chart = state.timeline_chart;
    std::vector<const TimelineLine*> shown;
    for (const TimelineLine& line : chart.lines) {
        if (is_shown(state.timeline, line) && point < line.values.size()) shown.push_back(&line);
    }
    std::sort(shown.begin(), shown.end(),
              [point](const TimelineLine* a, const TimelineLine* b) { return a->values[point] > b->values[point]; });

    // Drawn for a picture, with no mouse to follow: beside the crosshair instead.
    if (state.timeline_hover_at >= 0.0f) ImGui::SetNextWindowPos(anchor);
    ImGui::BeginTooltip();
    ImGui::PushFont(bold_font());
    text_colored_u32(colors::TextPrimary, "%s", common::clock_duration(at_s).c_str());
    ImGui::PopFont();
    for (const TimelineLine* line : shown) {
        text_colored_u32(line_color(*line), "%s", line->name.c_str());
        ImGui::SameLine(m(150.0f));
        text_colored_u32(colors::TextPrimary, "%s", format_rate(line->values[point]).c_str());
    }
    if (boss_shown(state) && point < chart.boss_hp.size() && chart.boss_hp[point] >= 0.0f) {
        text_colored_u32(kBossColor, "Boss HP");
        ImGui::SameLine(m(150.0f));
        text_colored_u32(colors::TextPrimary, "%s", format_percent(chart.boss_hp[point]).c_str());
    }

    bool header = false;
    if (state.timeline.show_buffs) {
        for (const meter::BuffWindow& w : detail.timeline.buffs) {
            if (at_s < w.begin_s || at_s >= w.end_s) continue;
            if (!header) {
                ImGui::Separator();
                header = true;
            }
            text_colored_u32(colors::WarningLight, "%s", game::effect_label(w.buff_id, w.source_skill).c_str());
            if (!w.caster.empty()) {
                ImGui::SameLine();
                text_colored_u32(colors::TextDim, "from %s", w.caster.c_str());
            }
        }
    }
    header = false;
    for (const meter::DeathRecord& death : detail.deaths) {
        if (std::abs(death.time_s - at_s) > span_s * 0.5 + 1.0) continue;
        if (!header) {
            ImGui::Separator();
            header = true;
        }
        text_colored_u32(colors::DangerLight, ICON_SKULL "  %s died at %s", death.name.c_str(),
                         common::clock_duration(death.time_s).c_str());
    }
    ImGui::EndTooltip();
}

/// Width of the value labels left of the plot.
float value_gutter(double top) { return ImGui::CalcTextSize(format_rate(top).c_str()).x + m(10.0f); }

void render_chart(AppViewState& state, const meter::FightDetail& detail, float width, float height) {
    const TimelineChart& chart = state.timeline_chart;
    const TimelineView& view = state.timeline;
    const float line_h = ImGui::GetTextLineHeight();
    float peak = 0.0f;
    for (const TimelineLine& line : chart.lines) {
        if (is_shown(view, line)) peak = std::max(peak, line.peak);
    }
    const double top = chart_top(peak);

    const ImVec2 p_min = ImGui::GetCursorScreenPos();
    const ImVec2 p_max(p_min.x + width, p_min.y + height);
    // Values to the left of the plot, deaths above it, the time along its bottom.
    const ImVec2 plot_min(p_min.x + value_gutter(top), p_min.y + line_h + m(6.0f));
    const ImVec2 plot_max(p_max.x, p_max.y - line_h - m(4.0f));
    const float plot_w = plot_max.x - plot_min.x;
    const float plot_h = plot_max.y - plot_min.y;
    const double length = static_cast<double>(std::max<size_t>(chart.length_s, 1));
    const auto x_of = [&](double s) {
        return plot_min.x + static_cast<float>(std::clamp(s / length, 0.0, 1.0)) * plot_w;
    };
    const auto y_of = [&](double v) {
        return plot_max.y - static_cast<float>(std::clamp(v / top, 0.0, 1.0)) * plot_h;
    };
    // The boss's HP has a scale of its own, 0 to 100% of the plot's height.
    const auto y_boss = [&](float fraction) { return plot_max.y - std::clamp(fraction, 0.0f, 1.0f) * plot_h; };

    ImGui::InvisibleButton("##TimelineCanvas", ImVec2(width, height));
    float mouse_x = ImGui::GetMousePos().x;
    bool hovered = ImGui::IsItemHovered() && mouse_x >= plot_min.x;
    if (state.timeline_hover_at >= 0.0f) {
        mouse_x = plot_min.x + state.timeline_hover_at * plot_w;
        hovered = true;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilledMultiColor(plot_min, plot_max, colors::SurfaceSunken, colors::SurfaceSunken, colors::SurfaceLow,
                                colors::SurfaceLow);
    dl->AddRect(plot_min, plot_max, colors::Border, m(6.0f), 0, m(1.0f));

    dl->PushClipRect(plot_min, plot_max, true);
    // Party-wide buffs behind everything, darker where more of them overlap.
    if (view.show_buffs) {
        const float icon = line_h * 0.9f;
        for (const meter::BuffWindow& w : detail.timeline.buffs) {
            const float x0 = x_of(w.begin_s);
            const float x1 = x_of(w.end_s);
            const uint32_t tint = class_or(w.caster_cls, colors::WarningLight);
            dl->AddRectFilled(ImVec2(x0, plot_min.y), ImVec2(x1, plot_max.y), colors::with_alpha(tint, 0.06f));
            dl->AddLine(ImVec2(x0, plot_min.y + m(1.0f)), ImVec2(x1, plot_min.y + m(1.0f)),
                        colors::with_alpha(tint, 0.6f), m(2.0f));
            if (x1 - x0 > icon + m(4.0f)) {
                draw_icon(dl, effect_icon(w.buff_id, w.source_skill), x0 + m(2.0f), plot_min.y + m(4.0f), icon, 0.8f);
            }
        }
    }
    for (int i = 1; i <= 3; ++i) {
        const float y = plot_max.y - plot_h * static_cast<float>(i) / 4.0f;
        dl->AddLine(ImVec2(plot_min.x + m(1.0f), y), ImVec2(plot_max.x - m(1.0f), y),
                    colors::with_alpha(colors::Border, 0.45f));
    }
    const double step = time_step(length, plot_w, m(70.0f));
    for (double t = step; t < length; t += step) {
        const float x = x_of(t);
        dl->AddLine(ImVec2(x, plot_min.y + m(1.0f)), ImVec2(x, plot_max.y - m(1.0f)),
                    colors::with_alpha(colors::Border, 0.30f));
    }

    std::vector<ImVec2> points;
    // The boss's HP under the players' lines, dimmer and thinner.
    if (boss_shown(state)) {
        const double span = length / static_cast<double>(chart.boss_hp.size());
        const auto flush = [&] {
            if (points.size() >= 2) {
                dl->AddPolyline(points.data(), static_cast<int>(points.size()), colors::with_alpha(kBossColor, 0.7f),
                                ImDrawFlags_None, std::max(1.0f, std::round(m(1.5f))));
            }
            points.clear();
        };
        for (size_t i = 0; i < chart.boss_hp.size(); ++i) {
            if (chart.boss_hp[i] < 0.0f) {
                flush();
                continue;
            }
            points.emplace_back(x_of((static_cast<double>(i) + 0.5) * span), y_boss(chart.boss_hp[i]));
        }
        flush();
    }
    // A whole-pixel width lets ImGui draw the line from its font texture, with half
    // the vertices.
    const float thickness = std::max(1.0f, std::round(m(2.0f)));
    for (const TimelineLine& line : chart.lines) {
        if (!is_shown(view, line) || line.values.size() < 2) continue;
        points.clear();
        const double span = length / static_cast<double>(line.values.size());
        for (size_t i = 0; i < line.values.size(); ++i) {
            points.emplace_back(x_of((static_cast<double>(i) + 0.5) * span), y_of(line.values[i]));
        }
        dl->AddPolyline(points.data(), static_cast<int>(points.size()), line_color(line), ImDrawFlags_None, thickness);
    }
    dl->PopClipRect();

    for (int i = 1; i <= 4; ++i) {
        const float y = plot_max.y - plot_h * static_cast<float>(i) / 4.0f;
        const std::string label = format_rate(top * i / 4.0);
        const float w = ImGui::CalcTextSize(label.c_str()).x;
        dl->AddText(ImVec2(plot_min.x - m(6.0f) - w, y - line_h * 0.5f), colors::TextDim, label.c_str());
    }
    if (boss_shown(state)) {
        // The boss's scale, inside the plot's right edge.
        for (const int pct : {50, 100}) {
            const std::string label = std::to_string(pct) + "%";
            const float w = ImGui::CalcTextSize(label.c_str()).x;
            const float y = y_boss(static_cast<float>(pct) / 100.0f);
            dl->AddText(ImVec2(plot_max.x - w - m(6.0f), y + m(2.0f)), colors::with_alpha(kBossColor, 0.8f),
                        label.c_str());
        }
    }
    for (double t = 0.0; t <= length; t += step) {
        const std::string label = common::clock_duration(t);
        const float w = ImGui::CalcTextSize(label.c_str()).x;
        const float x = std::clamp(x_of(t) - w * 0.5f, plot_min.x, p_max.x - w);
        dl->AddText(ImVec2(x, plot_max.y + m(3.0f)), colors::TextDim, label.c_str());
    }

    // Each death: a skull over the plot, a bar for as long as they stayed down.
    const float skull_w = ImGui::CalcTextSize(ICON_SKULL).x;
    for (const meter::DeathRecord& death : detail.deaths) {
        if (view.hidden.contains(death.name) || death.time_s > length) continue;
        const float x = x_of(death.time_s);
        const uint32_t color = class_or(death.cls, colors::DangerLight);
        const double up_at = death.up() ? death.time_s + death.up_after_s : length;
        dl->AddLine(ImVec2(x, plot_min.y), ImVec2(x, plot_max.y), colors::with_alpha(colors::Danger, 0.45f), m(1.0f));
        dl->AddLine(ImVec2(x, plot_min.y - m(2.0f)), ImVec2(std::max(x_of(up_at), x + m(2.0f)), plot_min.y - m(2.0f)),
                    colors::with_alpha(color, 0.8f), m(2.0f));
        dl->AddText(ImVec2(x - skull_w * 0.5f, p_min.y), color, ICON_SKULL);
    }

    if (!hovered || chart.lines.empty()) return;
    const size_t n = chart.lines.front().values.size();
    const auto point = static_cast<size_t>(
        std::clamp((mouse_x - plot_min.x) / plot_w * static_cast<float>(n), 0.0f, static_cast<float>(n - 1)));
    const double span = length / static_cast<double>(n);
    const double at_s = (static_cast<double>(point) + 0.5) * span;
    const float x = x_of(at_s);
    dl->AddLine(ImVec2(x, plot_min.y), ImVec2(x, plot_max.y), colors::with_alpha(colors::White, 0.35f));
    for (const TimelineLine& line : chart.lines) {
        if (is_shown(view, line) && point < line.values.size()) {
            dl->AddCircleFilled(ImVec2(x, y_of(line.values[point])), m(3.5f), line_color(line));
        }
    }
    if (boss_shown(state) && point < chart.boss_hp.size() && chart.boss_hp[point] >= 0.0f) {
        dl->AddCircleFilled(ImVec2(x, y_boss(chart.boss_hp[point])), m(3.0f), kBossColor);
    }
    render_readout(state, detail, point, at_s, span, ImVec2(x + m(14.0f), plot_min.y + m(16.0f)));
}

float legend_entry_width(const std::string& name) {
    return m(16.0f) + m(6.0f) + ImGui::CalcTextSize(name.c_str()).x;
}

/// The legend's keys: a line each, and the boss's.
std::vector<std::string> legend_names(const AppViewState& state) {
    std::vector<std::string> names;
    for (const TimelineLine& line : state.timeline_chart.lines) names.push_back(line.name);
    if (!state.timeline_chart.boss_hp.empty()) names.emplace_back("Boss HP");
    return names;
}

/// Height the legend's entries take once they wrap to `width`.
float legend_height(const AppViewState& state, float width) {
    const float gap = m(14.0f);
    int rows = 1;
    float x = 0.0f;
    for (const std::string& name : legend_names(state)) {
        const float w = legend_entry_width(name);
        if (x > 0.0f && x + gap + w > width) {
            ++rows;
            x = 0.0f;
        }
        x += (x > 0.0f ? gap : 0.0f) + w;
    }
    const float row_h = ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y + m(4.0f);
    return static_cast<float>(rows) * row_h;
}

/// One key per line, which turns the line off and on; the boss's last.
void render_legend(AppViewState& state) {
    TimelineView& view = state.timeline;
    const auto key = [&](size_t i, const std::string& name, uint32_t color, bool shown) {
        const float w = legend_entry_width(name);
        if (i > 0) same_line_if_room(w, 14.0f);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushID(static_cast<int>(i));
        const bool clicked = ImGui::InvisibleButton("##TimelineLegend", ImVec2(w, ImGui::GetTextLineHeight()));
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        if (hovered) ImGui::SetTooltip(shown ? "Hide this line" : "Show this line");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float mid = p.y + ImGui::GetTextLineHeight() * 0.5f;
        dl->AddLine(ImVec2(p.x, mid), ImVec2(p.x + m(16.0f), mid), shown ? color : colors::with_alpha(color, 0.25f),
                    m(2.5f));
        dl->AddText(ImVec2(p.x + m(22.0f), p.y),
                    !shown ? colors::TextFaint : (hovered ? colors::TextPrimary : colors::TextBody), name.c_str());
        return clicked;
    };
    size_t i = 0;
    for (const TimelineLine& line : state.timeline_chart.lines) {
        const bool shown = is_shown(view, line);
        if (key(i++, line.name, line_color(line), shown)) {
            if (shown) {
                view.hidden.insert(line.name);
            } else {
                view.hidden.erase(line.name);
            }
        }
    }
    if (!state.timeline_chart.boss_hp.empty() && key(i, "Boss HP", kBossColor, view.show_boss)) {
        view.show_boss = !view.show_boss;
    }
}

const char* empty_title(meter::TimelineMetric metric) {
    switch (metric) {
        case meter::TimelineMetric::Healing: return "No healing this fight";
        case meter::TimelineMetric::Taken: return "No damage taken this fight";
        case meter::TimelineMetric::Damage: break;
    }
    return "No damage this fight";
}

} // namespace

void no_detail_state(const char* icon) {
    empty_state(icon, "Not kept for this fight",
                "It was saved before the meter kept each fight's seconds, deaths and effects.");
}

void timeline_tab(const meter::FightSummary& f, AppViewState& state) {
    if (f.detail == nullptr) {
        no_detail_state(ICON_TRENDING);
        return;
    }
    render_controls(state.timeline);
    ImGui::Dummy(ImVec2(0.0f, m(4.0f)));

    const float width = ImGui::GetContentRegionAvail().x;
    // One point per pixel across the plot, whose value labels are rarely wider than these.
    const auto points = static_cast<size_t>(std::max(width - value_gutter(999'999.0), 2.0f));
    update_timeline_chart(state.timeline_chart, f, state.timeline, points);
    if (state.timeline_chart.lines.empty()) {
        empty_state(ICON_TRENDING, empty_title(state.timeline.metric),
                    "Each party member's fight fills in here second by second as it runs.");
        return;
    }
    const float chart_h = std::max(fill_h(0.0f) - legend_height(state, width) - m(8.0f), m(140.0f));
    render_chart(state, *f.detail, width, chart_h);
    ImGui::Dummy(ImVec2(0.0f, m(2.0f)));
    render_legend(state);
}

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
