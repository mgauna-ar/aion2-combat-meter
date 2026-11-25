#include "app/ui/views.hpp"

#include "app/ui/icons.hpp"
#include "app/ui/page.hpp"
#include "app/ui/widgets.hpp"

#ifdef HAVE_IMGUI
#include "imgui_internal.h"
#include <algorithm>
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

/// How far `value` sits from `min` to `max`, 0 to 1: how much of the track it fills.
template <typename T>
float fraction_of(T value, T min, T max) {
    const double span = static_cast<double>(max) - static_cast<double>(min);
    if (span <= 0.0) return 0.0f;
    return static_cast<float>(
        std::clamp((static_cast<double>(value) - static_cast<double>(min)) / span, 0.0, 1.0));
}

/// A slider that shows its value by filling its track up to it, with no grab: ImGui
/// centres the value's text, and its grab, drawn under the text, hid it wherever the
/// value sat mid-way. ImGui's slider still does the work (the drag, the keys, Ctrl+click
/// to type a value) on the upper of two channels, its frame and grab made see-through;
/// the frame and the fill go on the lower one after it, at the value it ended on.
template <typename T, typename Slide>
bool filled_slider(const char* id, const T* value, T min, T max, Slide slide) {
    // Typing a value is ImGui's own input box, drawn as it is.
    if (ImGui::GetCurrentWindowRead()->SkipItems || ImGui::TempInputIsActive(ImGui::GetID(id))) return slide(id);
    // A splitter of its own: the window's draw list may be split already, by a table.
    static ImDrawListSplitter splitter;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    splitter.Split(dl, 2);
    splitter.SetCurrentChannel(dl, 1);
    const ImVec4 clear(0.0f, 0.0f, 0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, clear);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, clear);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, clear);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, clear);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, clear);
    const bool changed = slide(id);
    ImGui::PopStyleColor(5);

    splitter.SetCurrentChannel(dl, 0);
    const ImVec2 frame_min = ImGui::GetItemRectMin();
    const ImVec2 frame_max = ImGui::GetItemRectMax();
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    const float rounding = ImGui::GetStyle().FrameRounding;
    // GetColorU32 applies the style's alpha, so a disabled row dims as ImGui's own do.
    dl->AddRectFilled(frame_min, frame_max,
                      ImGui::GetColorU32(active    ? ImGuiCol_FrameBgActive
                                         : hovered ? ImGuiCol_FrameBgHovered
                                                   : ImGuiCol_FrameBg),
                      rounding);
    const float fill_x = frame_min.x + (frame_max.x - frame_min.x) * fraction_of(*value, min, max);
    if (fill_x - frame_min.x >= 1.0f) {
        // The frame's own shape, cut at the value: rounded at the left, square where
        // it ends. No bright edge there: it would run through the text.
        dl->PushClipRect(frame_min, ImVec2(fill_x, frame_max.y), true);
        dl->AddRectFilled(
            frame_min, frame_max,
            ImGui::GetColorU32(colors::with_alpha(colors::Accent, active ? 0.55f : hovered ? 0.45f : 0.35f)),
            rounding);
        dl->PopClipRect();
    }
    splitter.Merge(dl);
    return changed;
}

/// A slider row. While the value is off its default, a reset button takes the left
/// of the control column, so the way back is on the row it belongs to.
template <typename T, typename Slide>
bool slider_row(const char* label, const char* help, T* value, T min, T max, T def, const char* format,
                AppActions& out, Applies applies, Slide slide) {
    begin_setting_row(label, help);
    ImGui::PushID(label);
    bool changed = false;
    // The button comes and goes as the slider is let go of, not mid-drag: the track
    // would shift under the pointer, and the value with it. ImGui keeps whether it
    // showed, under the slider's id, while the slider is held (or typed in).
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID slider_id = ImGui::GetID("##slider");
    bool show_reset = differs(*value, def);
    if (ImGui::GetActiveID() == slider_id) {
        show_reset = storage->GetBool(slider_id, show_reset);
    } else {
        storage->SetBool(slider_id, show_reset);
    }
    if (show_reset) {
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
    if (filled_slider("##slider", value, min, max, slide)) {
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
    return slider_row(label, help, value, min, max, def, format, out, applies, [&](const char* id) {
        return ImGui::SliderFloat(id, value, min, max, format, ImGuiSliderFlags_AlwaysClamp);
    });
}

bool setting_slider(const char* label, const char* help, double* value, double min, double max, double def,
                    const char* format, AppActions& out, Applies applies) {
    return slider_row(label, help, value, min, max, def, format, out, applies, [&](const char* id) {
        return ImGui::SliderScalar(id, ImGuiDataType_Double, value, &min, &max, format, ImGuiSliderFlags_AlwaysClamp);
    });
}

bool setting_slider(const char* label, const char* help, int* value, int min, int max, int def, const char* format,
                    AppActions& out, Applies applies) {
    return slider_row(label, help, value, min, max, def, format, out, applies, [&](const char* id) {
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
    setting_switch("Hide when the game is not in front", "Hidden while another program has the focus, and while the game is not running.",
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
