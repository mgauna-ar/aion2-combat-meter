#include "app/ui/table.hpp"

#include "app/game_icons.hpp"
#include "app/ui/page.hpp"
#include "app/ui/widgets.hpp"
#include "game/skill_names.hpp"

#ifdef HAVE_IMGUI
#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <vector>
#endif

namespace aion2::app::ui {

#ifdef HAVE_IMGUI

bool begin_table(const char* id, const Column* cols, int count, float height) {
    // What a column costs beyond its content: its cell padding and its border.
    const float chrome = ImGui::GetStyle().CellPadding.x * 2.0f + 1.0f;
    const float avail = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize;
    std::vector<int> order(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) order[static_cast<size_t>(i)] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return cols[a].priority < cols[b].priority; });
    std::vector<bool> shown(static_cast<size_t>(count), false);
    float used = 0.0f;
    float natural = 0.0f;
    int shown_count = 0;
    for (const int i : order) {
        const float w = m(cols[i].width > 0.0f ? cols[i].width : 110.0f) + chrome;
        if (used > 0.0f && used + w > avail) continue;
        used += w;
        natural += cols[i].width > 0.0f ? cols[i].width : 110.0f;
        shown[static_cast<size_t>(i)] = true;
        ++shown_count;
    }
    // The chrome of the columns shown only: counting the dropped ones made a table
    // that fits look cramped, and its stretching column shrank to its minimum.
    const auto sizing = table_sizing(natural, shown_count, kTableFlags);
    if (!ImGui::BeginTable(id, count, sizing.flags, ImVec2(0.0f, height))) return false;
    for (int i = 0; i < count; ++i) {
        const ImGuiTableColumnFlags hide = shown[static_cast<size_t>(i)] ? 0 : ImGuiTableColumnFlags_Disabled;
        if (cols[i].width > 0.0f) {
            ImGui::TableSetupColumn(cols[i].name, ImGuiTableColumnFlags_WidthFixed | hide, m(cols[i].width));
        } else {
            ImGui::TableSetupColumn(cols[i].name, sizing.flex_flags() | hide, sizing.flex_width(110.0f, 1.0f));
        }
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    ImGui::PushStyleColor(ImGuiCol_Text, v4(colors::TextDim));
    for (int i = 0; i < count; ++i) {
        if (!ImGui::TableSetColumnIndex(i)) continue;
        ImGui::TableHeader(cols[i].name);
        if (cols[i].tip != nullptr && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", cols[i].tip);
    }
    ImGui::PopStyleColor();
    return true;
}

void cell(int column, uint32_t color, const std::string& text) {
    if (!ImGui::TableSetColumnIndex(column)) return;
    text_colored_u32(color, "%s", text.c_str());
}

namespace {

/// An icon a text line high, then the text, with the icon's room kept when it has none.
void icon_text(int icon, uint32_t color, const std::string& text) {
    const float side = ImGui::GetTextLineHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    draw_icon(ImGui::GetWindowDrawList(), icon, p.x, p.y, side);
    ImGui::SetCursorScreenPos(ImVec2(p.x + side + m(6.0f), p.y));
    text_colored_u32(color, "%s", text.c_str());
}

} // namespace

void skill_cell(int column, uint32_t code, uint32_t color) {
    if (!ImGui::TableSetColumnIndex(column)) return;
    icon_text(skill_icon(code), color, game::skill_label(code));
}

void effect_cell(int column, uint32_t buff_id, uint32_t source_skill, uint32_t color) {
    if (!ImGui::TableSetColumnIndex(column)) return;
    icon_text(effect_icon(buff_id, source_skill), color, game::effect_label(buff_id, source_skill));
}

void player_cell(int column, const std::string& name, game::AionClass cls) {
    if (!ImGui::TableSetColumnIndex(column)) return;
    icon_text(class_icon(cls), class_or(cls, colors::TextBody), name);
}

uint32_t row_color_u32(const meter::CombatantTotals& c) {
    if (c.cls != game::AionClass::Unknown) return class_color_u32(c.cls);
    return c.is_local ? colors::Accent : colors::TextMuted;
}

uint32_t class_or(game::AionClass cls, uint32_t fallback) {
    return cls != game::AionClass::Unknown ? class_color_u32(cls) : fallback;
}

std::string number(uint64_t n) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%" PRIu64, n);
    return buf;
}

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
