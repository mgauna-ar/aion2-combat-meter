#include "app/ui/views.hpp"

#include "app/ui/icons.hpp"
#include "app/ui/page.hpp"
#include "app/ui/widgets.hpp"

#ifdef HAVE_IMGUI
#include <cmath>
#include <cstdio>
#endif

namespace aion2::app::ui {

#ifdef HAVE_IMGUI

namespace {

/// A drag in progress: preview an overlay edit, and remember to save.
void on_edit(AppActions& out, Applies applies) {
    out.settings_changed = true;
    if (applies == Applies::Overlay) out.overlay_changed = true;
}

/// The slider was let go of: save now, and hand a meter edit to the engine.
void on_release(AppActions& out, Applies applies) {
    out.save_settings = true;
    if (applies == Applies::Overlay) out.overlay_changed = true;
    if (applies == Applies::Engine) out.engine_changed = true;
}

bool differs(float a, float b) { return std::fabs(a - b) > 1e-4f; }
bool differs(double a, double b) { return std::fabs(a - b) > 1e-6; }
bool differs(int a, int b) { return a != b; }

/// A slider row. While the value is off its default, a reset button takes the left
/// of the control column, so the way back is on the row it belongs to.
template <typename T, typename Slide>
bool slider_row(const char* label, const char* help, T* value, T def, const char* format, AppActions& out,
                Applies applies, Slide slide) {
    begin_setting_row(label, help);
    ImGui::PushID(label);
    bool changed = false;
    if (differs(*value, def)) {
        const float control_w = ImGui::CalcItemWidth();
        const float gap = m(6.0f);
        if (button(ICON_RESET "##reset", ButtonKind::Secondary, ButtonSize::Icon)) {
            *value = def;
            changed = true;
            on_edit(out, applies);
            on_release(out, applies);
        }
        if (ImGui::IsItemHovered()) {
            char text[48];
            std::snprintf(text, sizeof(text), format, def);
            ImGui::SetTooltip("Back to the default, %s", text);
        }
        ImGui::SameLine(0.0f, gap);
        ImGui::SetNextItemWidth(control_w - ImGui::GetFrameHeight() - gap);
    }
    if (slide("##slider")) {
        changed = true;
        on_edit(out, applies);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) on_release(out, applies);
    ImGui::PopID();
    end_setting_row();
    return changed;
}

/// A dim caption between two runs of rows in one card.
void sub_label(const char* text) {
    ImGui::Dummy(ImVec2(0.0f, m(4.0f)));
    text_colored_u32(colors::TextDim, "%s", text);
    ImGui::Dummy(ImVec2(0.0f, m(2.0f)));
}

} // namespace

bool setting_switch(const char* label, const char* help, bool* value, AppActions& out, Applies applies) {
    if (!setting_toggle(label, help, value)) return false;
    on_edit(out, applies);
    on_release(out, applies);
    return true;
}

bool setting_slider(const char* label, const char* help, float* value, float min, float max, float def,
                    const char* format, AppActions& out, Applies applies) {
    return slider_row(label, help, value, def, format, out, applies, [&](const char* id) {
        return ImGui::SliderFloat(id, value, min, max, format, ImGuiSliderFlags_AlwaysClamp);
    });
}

bool setting_slider(const char* label, const char* help, double* value, double min, double max, double def,
                    const char* format, AppActions& out, Applies applies) {
    return slider_row(label, help, value, def, format, out, applies, [&](const char* id) {
        return ImGui::SliderScalar(id, ImGuiDataType_Double, value, &min, &max, format, ImGuiSliderFlags_AlwaysClamp);
    });
}

bool setting_slider(const char* label, const char* help, int* value, int min, int max, int def, const char* format,
                    AppActions& out, Applies applies) {
    return slider_row(label, help, value, def, format, out, applies, [&](const char* id) {
        return ImGui::SliderInt(id, value, min, max, format, ImGuiSliderFlags_AlwaysClamp);
    });
}

bool advanced_section(const char* id) {
    ImGui::Dummy(ImVec2(0.0f, m(2.0f)));
    ImGui::PushStyleColor(ImGuiCol_Text, v4(colors::TextMuted));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, v4(colors::with_alpha(colors::White, 0.05f)));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, v4(colors::with_alpha(colors::White, 0.09f)));
    const bool open = ImGui::TreeNodeEx(id, ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen,
                                        "Advanced");
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fine tuning. The defaults suit most players.");
    if (open) ImGui::Dummy(ImVec2(0.0f, m(4.0f)));
    return open;
}

void overlay_settings_card(const OverlayControls& c, AppActions& out) {
    begin_settings_card(c.id, ICON_MONITOR, "IN-GAME OVERLAY", colors::Accent);
    ImGui::PushID(c.id);
    if (c.before) c.before();
    setting_switch("Lock", "Click-through and fixed in place. Ctrl+Alt+L locks or unlocks every overlay.", c.locked,
                   out, Applies::Overlay);
    if (c.look) c.look();
    // Stored as 0..1; shown as the percentage it is.
    int opacity = static_cast<int>(std::lround(*c.opacity * 100.0f));
    if (setting_slider("Opacity", "Of its background; the text stays solid.", &opacity, 0, 100,
                       static_cast<int>(std::lround(c.default_opacity * 100.0f)), "%d%%", out, Applies::Overlay)) {
        *c.opacity = static_cast<float>(opacity) / 100.0f;
    }
    setting_slider("Scale", "On top of the monitor's own scaling.", c.scale, 0.6f, 2.5f, 1.0f, "%.2fx", out,
                   Applies::Overlay);
    begin_setting_row("Position", c.position_help);
    if (button(ICON_RESET "  Reset position", ButtonKind::Secondary, ButtonSize::Fit)) *c.reset_position = true;
    end_setting_row();

    sub_label("WHEN IT SHOWS");
    // Conditions apply only while locked, so the overlay can always be dragged back.
    const bool locked = *c.locked;
    if (!locked) ImGui::BeginDisabled();
    if (c.conditions) c.conditions();
    setting_switch("Hide when the game is not in front", "Hidden while another program has the focus.",
                   c.hide_when_game_unfocused, out);
    setting_switch("Only in dungeons", "Hidden in the open world. A new dungeon counts before the meter can name it.",
                   c.show_in_dungeons_only, out);
    if (!locked) ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0.0f, m(2.0f)));
    ImGui::PushTextWrapPos(0.0f);
    if (!locked) {
        text_colored_u32(colors::WarningLight, ICON_WARNING "  Lock the overlay for these to apply.");
    } else {
        text_dim("They apply while the overlay is locked; unlocked, it always shows.");
    }
    ImGui::PopTextWrapPos();
    ImGui::PopID();
    end_settings_card();
}

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
