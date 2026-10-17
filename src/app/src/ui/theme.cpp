#include "app/ui/theme.hpp"

#include "app/display_format.hpp"
#include "app/imgui_guard.hpp"
#include "app/ui/icon_font.hpp"

#include <algorithm>
#include <cmath>

namespace aion2::app::ui {

namespace {

float g_ui_scale = 1.0f;
ImFont* g_bold_font = nullptr;

#ifdef HAVE_IMGUI
/// An IM_COL32 token as the float colour ImGuiStyle stores.
ImVec4 v4(uint32_t col, float alpha_scale = 1.0f) {
    return ImVec4(static_cast<float>(col & 0xFF) / 255.0f, static_cast<float>((col >> 8) & 0xFF) / 255.0f,
                  static_cast<float>((col >> 16) & 0xFF) / 255.0f,
                  static_cast<float>((col >> 24) & 0xFF) / 255.0f * alpha_scale);
}
#endif

} // namespace

void set_ui_scale(float scale) { g_ui_scale = scale; }

float ui_scale() { return g_ui_scale; }

void set_bold_font(ImFont* font) { g_bold_font = font; }

ImFont* bold_font() { return g_bold_font; }

uint32_t class_color_u32(meter::AionClass cls, float alpha) {
    const Rgb c = class_color(cls);
    const auto channel = [](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return colors::rgb(channel(c.r), channel(c.g), channel(c.b), channel(alpha));
}

void apply_slate_theme() {
#ifdef HAVE_IMGUI
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowPadding = ImVec2(metrics::CardPad, metrics::CardPad);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.CellPadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(metrics::Gutter, 8.0f);
    style.ItemInnerSpacing = ImVec2(7.0f, 6.0f);
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 11.0f;

    style.WindowRounding = metrics::CardRadius;
    style.ChildRounding = metrics::CardRadius;
    style.FrameRounding = 6.0f;
    style.PopupRounding = metrics::CardRadius;
    style.ScrollbarRounding = 5.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    // Tabs read as an underlined strip rather than folders.
    style.TabBarBorderSize = 1.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = v4(colors::TextPrimary);
    c[ImGuiCol_TextDisabled] = v4(colors::TextDim);
    c[ImGuiCol_WindowBg] = v4(colors::Canvas);
    c[ImGuiCol_ChildBg] = v4(colors::Surface);
    c[ImGuiCol_PopupBg] = v4(colors::Surface, 0.98f);
    c[ImGuiCol_Border] = v4(colors::Border);
    c[ImGuiCol_BorderShadow] = v4(colors::with_alpha(colors::Canvas, 0.0f));
    c[ImGuiCol_FrameBg] = v4(colors::SurfaceLow);
    c[ImGuiCol_FrameBgHovered] = v4(colors::SurfaceRaised);
    c[ImGuiCol_FrameBgActive] = v4(colors::with_alpha(colors::Accent, 0.22f));
    c[ImGuiCol_TitleBg] = v4(colors::Canvas);
    c[ImGuiCol_TitleBgActive] = v4(colors::Surface);
    c[ImGuiCol_TitleBgCollapsed] = v4(colors::with_alpha(colors::Canvas, 0.75f));
    c[ImGuiCol_MenuBarBg] = v4(colors::Surface);
    c[ImGuiCol_ScrollbarBg] = v4(colors::with_alpha(colors::Canvas, 0.55f));
    c[ImGuiCol_ScrollbarGrab] = v4(colors::BorderStrong);
    c[ImGuiCol_ScrollbarGrabHovered] = v4(colors::with_alpha(colors::Accent, 0.75f));
    c[ImGuiCol_ScrollbarGrabActive] = v4(colors::Accent);
    c[ImGuiCol_CheckMark] = v4(colors::AccentHover);
    c[ImGuiCol_SliderGrab] = v4(colors::Accent);
    c[ImGuiCol_SliderGrabActive] = v4(colors::AccentHover);
    c[ImGuiCol_Button] = v4(colors::SurfaceRaised);
    c[ImGuiCol_ButtonHovered] = v4(colors::with_alpha(colors::Accent, 0.55f));
    c[ImGuiCol_ButtonActive] = v4(colors::Accent);
    c[ImGuiCol_Header] = v4(colors::with_alpha(colors::Accent, 0.18f));
    c[ImGuiCol_HeaderHovered] = v4(colors::with_alpha(colors::Accent, 0.32f));
    c[ImGuiCol_HeaderActive] = v4(colors::with_alpha(colors::Accent, 0.48f));
    c[ImGuiCol_Separator] = v4(colors::with_alpha(colors::Border, 0.70f));
    c[ImGuiCol_SeparatorHovered] = v4(colors::with_alpha(colors::Accent, 0.78f));
    c[ImGuiCol_SeparatorActive] = v4(colors::Accent);
    c[ImGuiCol_ResizeGrip] = v4(colors::with_alpha(colors::BorderStrong, 0.25f));
    c[ImGuiCol_ResizeGripHovered] = v4(colors::with_alpha(colors::Accent, 0.67f));
    c[ImGuiCol_ResizeGripActive] = v4(colors::with_alpha(colors::Accent, 0.95f));
    c[ImGuiCol_Tab] = v4(colors::with_alpha(colors::Canvas, 0.0f));
    c[ImGuiCol_TabHovered] = v4(colors::with_alpha(colors::Accent, 0.22f));
    c[ImGuiCol_TabActive] = v4(colors::with_alpha(colors::Accent, 0.16f));
    c[ImGuiCol_TabUnfocused] = v4(colors::with_alpha(colors::Canvas, 0.0f));
    c[ImGuiCol_TabUnfocusedActive] = v4(colors::with_alpha(colors::Accent, 0.10f));
    c[ImGuiCol_TableHeaderBg] = v4(colors::with_alpha(colors::SurfaceLow, 0.0f));
    c[ImGuiCol_TableBorderStrong] = v4(colors::Border);
    c[ImGuiCol_TableBorderLight] = v4(colors::with_alpha(colors::Border, 0.40f));
    c[ImGuiCol_TableRowBg] = v4(colors::with_alpha(colors::Canvas, 0.0f));
    c[ImGuiCol_TableRowBgAlt] = v4(colors::with_alpha(colors::White, 0.022f));
    c[ImGuiCol_NavHighlight] = v4(colors::with_alpha(colors::Accent, 0.85f));
    c[ImGuiCol_ModalWindowDimBg] = v4(colors::with_alpha(colors::Canvas, 0.65f));
#endif
}

void setup_fonts_and_theme(const std::string& regular_ttf, const std::string& bold_ttf, float dpi_scale) {
#ifdef HAVE_IMGUI
    ImGuiIO& io = ImGui::GetIO();
    const float size = std::round(16.0f * dpi_scale);
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    if (regular_ttf.empty() || io.Fonts->AddFontFromFileTTF(regular_ttf.c_str(), size, &cfg) == nullptr) {
        io.Fonts->AddFontDefault();
    }
    // Icons merge into the font added last, so each font that shows them gets its own pass.
    load_icon_font(size);
    ImFont* bold = bold_ttf.empty() ? nullptr : io.Fonts->AddFontFromFileTTF(bold_ttf.c_str(), size, &cfg);
    if (bold != nullptr) load_icon_font(size);
    set_bold_font(bold != nullptr ? bold : io.Fonts->Fonts[0]);

    set_ui_scale(dpi_scale);
    apply_slate_theme();
    ImGui::GetStyle().ScaleAllSizes(dpi_scale);
#else
    (void)regular_ttf;
    (void)bold_ttf;
    (void)dpi_scale;
#endif
}

} // namespace aion2::app::ui
