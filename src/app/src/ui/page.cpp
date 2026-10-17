#include "app/ui/page.hpp"

#include "app/ui/theme.hpp"
#include "app/ui/widgets.hpp"

#ifdef HAVE_IMGUI
#include "imgui_internal.h"

#include <cstdio>
#endif

namespace aion2::app::ui {

#ifdef HAVE_IMGUI

namespace {

/// A tab's label with or without its icon. The part after ### is its id, the same
/// either way.
void tab_label(char* out, size_t size, const PageTab& tab, bool with_icon) {
    if (with_icon && tab.icon != nullptr) {
        std::snprintf(out, size, "%s  %s###%s", tab.icon, tab.label, tab.label);
    } else {
        std::snprintf(out, size, "%s###%s", tab.label, tab.label);
    }
}

/// The width the tab bar lays its tabs out in, as TabBarLayout() adds them up.
float tabs_width(const PageTab* tabs, size_t count, bool with_icons) {
    float width = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        char label[128];
        tab_label(label, sizeof(label), tabs[i], with_icons);
        width += ImGui::TabItemCalcSize(label, false).x;
    }
    return width + ImGui::GetStyle().ItemInnerSpacing.x * static_cast<float>(count - 1);
}

} // namespace

void render_tabs(const char* id, const PageTab* tabs, size_t count, int select) {
    if (tabs == nullptr || count == 0) return;

    // ImGui's default fitting shrinks tabs that do not fit and clips their labels.
    // Drop the icons first, then scroll.
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool with_icons = tabs_width(tabs, count, true) <= avail;
    const bool fits = with_icons || tabs_width(tabs, count, false) <= avail;
    if (!ImGui::BeginTabBar(id, fits ? ImGuiTabBarFlags_None : ImGuiTabBarFlags_FittingPolicyScroll)) return;

    for (size_t i = 0; i < count; ++i) {
        char label[128];
        tab_label(label, sizeof(label), tabs[i], with_icons);
        const ImGuiTabItemFlags flags = static_cast<int>(i) == select ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem(label, nullptr, flags)) {
            ImGui::Dummy(ImVec2(0.0f, m(4.0f)));
            tabs[i].body();
            ImGui::EndTabItem();
        }
    }
    ImGui::EndTabBar();
}

void begin_settings_card(const char* id, const char* icon, const char* label, uint32_t accent) {
    CardOptions opts{};
    opts.auto_height = true;
    begin_card(id, ImVec2(0.0f, 0.0f), opts);
    section_header(icon, label, accent);
}

void end_settings_card() { end_card(); }

void render_settings_grid(const SettingsSection* sections, size_t count) {
    if (sections == nullptr || count == 0) return;

    const int columns = grid_columns(2, metrics::GridMinCol);
    if (columns == 1) {
        ImGui::BeginChild("##SettingsGridSingle", ImVec2(0.0f, fill_h(0.0f)), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground);
        for (size_t i = 0; i < count; ++i) {
            if (i > 0) ImGui::Dummy(ImVec2(0.0f, m(metrics::Gutter)));
            sections[i].body();
        }
        ImGui::EndChild();
        return;
    }

    // Sections alternate between the columns rather than filling the left one
    // first, so a long section does not leave the right column empty.
    const float col_w = split_w(columns);
    const float col_h = fill_h(0.0f);
    for (int col = 0; col < columns; ++col) {
        if (col > 0) ImGui::SameLine(0.0f, m(metrics::Gutter));
        char child_id[32];
        std::snprintf(child_id, sizeof(child_id), "##SettingsGridCol%d", col);
        ImGui::BeginChild(child_id, ImVec2(col_w, col_h), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        bool first = true;
        for (size_t i = static_cast<size_t>(col); i < count; i += static_cast<size_t>(columns)) {
            if (!first) ImGui::Dummy(ImVec2(0.0f, m(metrics::Gutter)));
            first = false;
            sections[i].body();
        }
        ImGui::EndChild();
    }
}

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
