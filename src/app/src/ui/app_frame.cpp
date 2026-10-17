#include "app/app_frame.hpp"

#include "aion2/version.hpp"
#include "app/imgui_guard.hpp"
#include "app/ui/icons.hpp"
#include "app/ui/theme.hpp"
#include "app/ui/views.hpp"
#include "app/ui/widgets.hpp"

namespace aion2::app {

#ifdef HAVE_IMGUI

namespace {

using namespace ui;

struct NavItem {
    const char* label;
    const char* icon;
    AppPage page;
};

constexpr NavItem kNavItems[] = {
    {"Dashboard", ICON_DASHBOARD, AppPage::Dashboard},
    {"Combat Meter", ICON_SWORDS, AppPage::Meter},
    {"Settings", ICON_SETTINGS, AppPage::Settings},
};

bool nav_item(const NavItem& item, bool selected) {
    const float height = m(metrics::NavItemH);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;

    ImGui::PushStyleColor(ImGuiCol_Button, v4(colors::with_alpha(colors::Canvas, 0.0f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, v4(colors::with_alpha(colors::White, 0.05f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, v4(colors::with_alpha(colors::White, 0.09f)));
    ImGui::PushStyleColor(ImGuiCol_Border, v4(colors::with_alpha(colors::Canvas, 0.0f)));
    // Label-less: the button is the hit box and the hover fill; the icon and the
    // text are drawn below so they can differ in colour.
    ImGui::PushID(item.label);
    const bool clicked = ImGui::Button("##nav", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    ImGui::PopStyleColor(4);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p_max(p.x + width, p.y + height);
    if (selected) {
        dl->AddRectFilledMultiColor(p, p_max, colors::with_alpha(colors::Accent, 0.20f),
                                    colors::with_alpha(colors::Accent, 0.04f), colors::with_alpha(colors::Accent, 0.04f),
                                    colors::with_alpha(colors::Accent, 0.20f));
        dl->AddRectFilled(ImVec2(p.x, p.y + m(6.0f)), ImVec2(p.x + m(3.0f), p_max.y - m(6.0f)), colors::Accent,
                          m(2.0f));
    }
    const uint32_t icon_color = selected ? colors::AccentHover : (hovered ? colors::TextMuted : colors::TextDim);
    const uint32_t text_color = selected ? colors::TextPrimary : (hovered ? colors::TextBody : colors::TextMuted);
    const float text_y = p.y + (height - ImGui::GetTextLineHeight()) * 0.5f;
    dl->AddText(ImVec2(p.x + m(13.0f), text_y), icon_color, item.icon);
    dl->AddText(ImVec2(p.x + m(41.0f), text_y), text_color, item.label);
    return clicked;
}

struct Badge {
    const char* label;
    uint32_t color;
};

Badge capture_badge(const AppFrameInput& in) {
    if (in.snapshot == nullptr) return {"Starting", colors::TextDim};
    switch (in.snapshot->capture) {
        case pipeline::CaptureState::Live:
            return in.snapshot->fight.live ? Badge{"In combat", colors::SuccessLight}
                                           : Badge{"Game connected", colors::SuccessLight};
        case pipeline::CaptureState::Quiet: return {"No game traffic", colors::WarningLight};
        case pipeline::CaptureState::Waiting: break;
    }
    return {"Waiting for the game", colors::TextDim};
}

void render_sidebar(const AppFrameInput& in, AppViewState& state) {
    const float sidebar_width = m(metrics::SidebarW);
    const ImVec2 panel_min = ImGui::GetCursorScreenPos();

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(m(8.0f), 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, v4(colors::with_alpha(colors::Canvas, 0.0f)));
    ImGui::BeginChild("##Sidebar", ImVec2(sidebar_width, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);

    // A gradient and a hairline seam: the sidebar reads as its own surface.
    const ImVec2 panel_max(panel_min.x + sidebar_width, panel_min.y + ImGui::GetWindowSize().y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilledMultiColor(panel_min, panel_max, colors::SidebarTop, colors::SidebarTop, colors::SidebarBottom,
                                colors::SidebarBottom);
    dl->AddLine(ImVec2(panel_max.x - m(1.0f), panel_min.y), ImVec2(panel_max.x - m(1.0f), panel_max.y),
                colors::BorderSubtle, m(1.0f));

    // Branding.
    ImGui::Dummy(ImVec2(0.0f, m(10.0f)));
    ImGui::Indent(m(6.0f));
    const float brand_y = ImGui::GetCursorPosY();
    icon_chip(ICON_GAUGE, colors::Accent, metrics::ChipSizeLg);
    ImGui::SameLine(0.0f, m(10.0f));
    ImGui::BeginGroup();
    ImGui::SetCursorPosY(brand_y + m(1.0f));
    ImGui::PushFont(bold_font());
    text_colored_u32(colors::TextPrimary, "AION 2 METER");
    ImGui::PopFont();
    text_colored_u32(colors::TextDim, "v%s", VERSION_STRING);
    ImGui::EndGroup();
    ImGui::Unindent(m(6.0f));
    ImGui::Dummy(ImVec2(0.0f, m(12.0f)));

    for (const NavItem& item : kNavItems) {
        if (nav_item(item, state.page == item.page)) state.page = item.page;
    }

    // Footer: the capture's state, measured from the font so it never clips.
    const float line_h = ImGui::GetTextLineHeight();
    const float footer_height = m(11.0f) + pill_height() + ImGui::GetStyle().ItemSpacing.y + line_h + m(10.0f);
    const float footer_top = ImGui::GetWindowHeight() - footer_height;
    if (ImGui::GetCursorPosY() < footer_top) ImGui::SetCursorPosY(footer_top);
    const ImVec2 rule = ImGui::GetCursorScreenPos();
    dl->AddLine(rule, ImVec2(rule.x + ImGui::GetContentRegionAvail().x, rule.y), colors::BorderSubtle, m(1.0f));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + m(11.0f));

    ImGui::Indent(m(6.0f));
    const Badge badge = in.recording ? Badge{"Recording", colors::DangerLight} : capture_badge(in);
    pill(badge.label, badge.color);
    if (ImGui::IsItemHovered() && !in.source_status.empty()) ImGui::SetTooltip("%s", in.source_status.c_str());
    if (in.snapshot != nullptr && !in.snapshot->local_name.empty()) {
        text_colored_u32(colors::TextFaint, "%s", in.snapshot->local_name.c_str());
    } else {
        text_colored_u32(colors::TextFaint, "passive capture only");
    }
    ImGui::Unindent(m(6.0f));

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

} // namespace

AppActions draw_app(const AppFrameInput& in, AppViewState& state) {
    AppActions out;
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##AppRoot", nullptr, flags);

    render_sidebar(in, state);
    ImGui::SameLine(0.0f, 0.0f);

    // Equal padding on all four edges, and a scrollbar for a page taller than the window.
    const float page_pad = m(metrics::PagePad);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(page_pad, page_pad));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, v4(colors::with_alpha(colors::Canvas, 0.0f)));
    ImGui::BeginChild("##Page", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
    switch (state.page) {
        case AppPage::Dashboard: render_dashboard(in, state, out); break;
        case AppPage::Meter: render_meter_page(in, state, out); break;
        case AppPage::Settings: render_settings_page(in, state, out); break;
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImGui::End();
    ImGui::PopStyleVar(3);
    return out;
}

#else // !HAVE_IMGUI

AppActions draw_app(const AppFrameInput&, AppViewState&) { return {}; }

#endif

} // namespace aion2::app
