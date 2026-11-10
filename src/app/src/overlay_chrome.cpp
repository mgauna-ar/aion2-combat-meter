#include "app/overlay_chrome.hpp"

#ifdef HAVE_IMGUI

#include "app/game_icons.hpp"
#include "app/ui/icon_font.hpp"
#include "app/ui/icons.hpp"

#include "imgui_internal.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace aion2::app::chrome {

namespace {

/// The bold font: the atlas's second, where build_overlay_fonts put one.
ImFont* bold_font() {
    const ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    return atlas->Fonts.Size > 1 ? atlas->Fonts[1] : ImGui::GetFont();
}

float button_width(const char* label) {
    return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

/// A header button with no frame until the mouse is over it.
bool icon_button(const char* label, const char* tooltip) {
    ImGui::PushStyleColor(ImGuiCol_Button, rgba(colors::Text, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgba(colors::Text, 0.12f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgba(colors::Text, 0.20f));
    ImGui::PushStyleColor(ImGuiCol_Text, rgba(colors::TextDim));
    const bool pressed = ImGui::SmallButton(label);
    ImGui::PopStyleColor(4);
    if (tooltip != nullptr && tooltip[0] != '\0' && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

} // namespace

ImVec4 rgba(Rgb c, float a) { return ImVec4(c.r, c.g, c.b, a); }

ImU32 u32(Rgb c, float a) { return ImGui::GetColorU32(rgba(c, a)); }

Rgb lighten(Rgb c, float t) noexcept {
    return Rgb{c.r + (1.0f - c.r) * t, c.g + (1.0f - c.g) * t, c.b + (1.0f - c.b) * t};
}

bool build_overlay_fonts(const std::string& regular_ttf, const std::string& bold_ttf, float font_px, float icon_px) {
    ImFontAtlas& atlas = *ImGui::GetIO().Fonts;
    atlas.Clear();
    const float size = std::round(font_px);
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    const bool regular = !regular_ttf.empty() && atlas.AddFontFromFileTTF(regular_ttf.c_str(), size, &cfg) != nullptr;
    if (!regular) atlas.AddFontDefault();
    // The icons merge into the font added last. ImGui's own font is 13 px, whatever
    // the size asked for.
    ui::load_icon_font(regular ? size : 13.0f);
    if (regular && !bold_ttf.empty() && atlas.AddFontFromFileTTF(bold_ttf.c_str(), size, &cfg) != nullptr) {
        ui::load_icon_font(size);
    }
    build_atlas_with_icons(atlas, icon_px);
    return regular;
}

float bold_text_width(const std::string& s) {
    const ImFont* font = bold_font();
    return font->CalcTextSizeA(font->FontSize * ImGui::GetIO().FontGlobalScale, FLT_MAX, 0.0f, s.c_str()).x;
}

BoldScope::BoldScope() { ImGui::PushFont(bold_font()); }

BoldScope::~BoldScope() { ImGui::PopFont(); }

void text_right(ImDrawList* dl, float x_right, float y, ImU32 col, const std::string& s) {
    const ImVec2 size = ImGui::CalcTextSize(s.c_str());
    dl->AddText(ImVec2(x_right - size.x, y), col, s.c_str());
}

void shadowed_text(ImDrawList* dl, ImVec2 at, const std::string& text, float scale) {
    dl->AddText(ImVec2(at.x + 1.0f * scale, at.y + 1.0f * scale), u32(Rgb{0.0f, 0.0f, 0.0f}, 0.9f), text.c_str());
    dl->AddText(at, u32(colors::Text), text.c_str());
}

std::string fit(const std::string& s, float width) {
    if (ImGui::CalcTextSize(s.c_str()).x <= width) return s;
    std::string out = s;
    while (!out.empty() && ImGui::CalcTextSize((out + "...").c_str()).x > width) {
        // Continuation bytes (10xxxxxx) go with the lead byte before them.
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) out.pop_back();
        if (!out.empty()) out.pop_back();
    }
    return out + "...";
}

ImVec2 window_padding(float scale) noexcept { return ImVec2(10.0f * scale, 8.0f * scale); }

ImVec2 item_spacing(float scale) noexcept { return ImVec2(7.0f * scale, 4.0f * scale); }

float header_height(float scale) {
    // The title line (its buttons are no taller), and the rule with the spacing
    // above and below it.
    return ImGui::GetTextLineHeight() + 1.0f + 2.0f * item_spacing(scale).y;
}

float dot_width(float scale) noexcept { return 8.0f * scale; }

bool begin(const char* id, float opacity, float scale, bool interactive) {
    return begin(id, opacity, scale, interactive, ImVec2(0.0f, 0.0f), ImGui::GetIO().DisplaySize);
}

bool begin(const char* id, float opacity, float scale, bool interactive, ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, window_padding(scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    // A panel fitted to one line is under ImGui's own least window size.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, item_spacing(scale));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f * scale);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, rgba(colors::Background, opacity));
    ImGui::PushStyleColor(ImGuiCol_Border, rgba(colors::Border, interactive ? 1.0f : std::min(1.0f, opacity + 0.2f)));
    ImGui::PushStyleColor(ImGuiCol_Text, rgba(colors::Text));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus;
    return ImGui::Begin(id, nullptr, flags);
}

void end() {
    ImGui::End();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(6);
}

void dot(Rgb color, float scale, float alpha) {
    const float line = ImGui::GetTextLineHeight();
    const float d = dot_width(scale);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(d, line));
    const ImVec2 center(p.x + d * 0.5f, p.y + line * 0.5f + 0.5f * scale);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // A soft halo, then the light itself.
    dl->AddCircleFilled(center, d * 0.5f + 1.5f * scale, u32(color, 0.22f * alpha));
    dl->AddCircleFilled(center, d * 0.5f - 0.5f * scale, u32(color, alpha));
}

void hairline() {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + width, p.y), u32(colors::Text, 0.10f), 1.0f);
    ImGui::Dummy(ImVec2(width, 1.0f));
}

void section_title(const std::string& title, float scale) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::TextColored(rgba(colors::TextDim), "%s", title.c_str());
    const float x0 = ImGui::GetItemRectMax().x + 7.0f * scale;
    const float y = std::round(p.y + ImGui::GetTextLineHeight() * 0.5f) + 1.0f;
    if (x0 < p.x + width) {
        ImGui::GetWindowDrawList()->AddLine(ImVec2(x0, y), ImVec2(p.x + width, y), u32(colors::Text, 0.10f), 1.0f);
    }
}

void finish_header(const HeaderButtons& b, OverlayActions& out) {
    const float text_end_x = ImGui::GetItemRectMax().x;
    const char* app_label = ICON_SETTINGS "##app";
    const char* lock_label = ICON_LOCK "##lock";
    // The two icons sit closer together than items do.
    const float spacing = 2.0f * b.scale;
    const float buttons_w = button_width(app_label) + button_width(lock_label) + spacing;
    const float buttons_x = ImGui::GetWindowContentRegionMax().x - buttons_w;
    if (b.interactive && buttons_x > text_end_x + ImGui::GetStyle().ItemSpacing.x) {
        ImGui::SameLine(buttons_x);
        if (icon_button(app_label, b.app_tooltip)) out.open_app = true;
        ImGui::SameLine(0.0f, spacing);
        if (icon_button(lock_label, b.lock_tooltip)) out.lock = true;
    }
    out.drag_area =
        ClientRect{0.0f, 0.0f, b.interactive ? std::max(buttons_x, text_end_x) : 0.0f, ImGui::GetCursorPosY()};
    hairline();
}

ClientRect resize_grip(float scale) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float g = 13.0f * scale;
    // Inside the rounded corner, so the strokes do not run over the border.
    const ImVec2 br(display.x - 3.0f * scale, display.y - 3.0f * scale);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 1; i <= 3; ++i) {
        const float k = static_cast<float>(i) * 3.5f * scale;
        dl->AddLine(ImVec2(br.x - k, br.y), ImVec2(br.x, br.y - k), u32(colors::TextDim, 0.75f), std::max(1.0f, scale));
    }
    return ClientRect{display.x - g - 3.0f * scale, display.y - g - 3.0f * scale, display.x, display.y};
}

void draw_bar(ImDrawList* dl, ImVec2 min, ImVec2 max, float fraction, Rgb color, float scale, const BarStyle& style) {
    const float rounding = style.rounding * scale;
    if (style.track) dl->AddRectFilled(min, max, u32(colors::Text, 0.06f), rounding);
    const float x1 = min.x + (max.x - min.x) * std::clamp(fraction, 0.0f, 1.0f);
    if (x1 - min.x < 1.0f) return;
    // One rounded rectangle, its vertices recoloured from a darker shade at the left
    // to the colour itself at the fill's end.
    const int first = dl->VtxBuffer.Size;
    dl->AddRectFilled(min, ImVec2(x1, max.y), u32(color, style.alpha), rounding);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, first, dl->VtxBuffer.Size, min, ImVec2(x1, min.y),
                                                  u32(Rgb{color.r * 0.55f, color.g * 0.55f, color.b * 0.55f}),
                                                  u32(color));
    // The edge the eye follows as the bar moves; a full bar has none.
    const float edge = 2.0f * scale;
    if (fraction < 0.995f && x1 - min.x > rounding + edge) {
        dl->AddRectFilled(ImVec2(x1 - edge, min.y), ImVec2(x1, max.y), u32(lighten(color, 0.45f), 0.90f), rounding,
                          ImDrawFlags_RoundCornersRight);
    }
}

} // namespace aion2::app::chrome

#endif // HAVE_IMGUI
