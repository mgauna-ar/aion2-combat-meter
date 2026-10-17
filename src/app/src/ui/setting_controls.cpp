#include "app/ui/views.hpp"

#include "app/ui/widgets.hpp"

#ifdef HAVE_IMGUI
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
    if (applies == Applies::Meter) out.meter_changed = true;
}

template <typename Slide>
bool slider_row(const char* label, const char* help, AppActions& out, Applies applies, Slide slide) {
    begin_setting_row(label, help);
    char id[80];
    std::snprintf(id, sizeof(id), "##sld_%s", label);
    const bool changed = slide(id);
    if (changed) on_edit(out, applies);
    if (ImGui::IsItemDeactivatedAfterEdit()) on_release(out, applies);
    end_setting_row();
    return changed;
}

} // namespace

bool setting_switch(const char* label, const char* help, bool* value, AppActions& out, Applies applies) {
    if (!setting_toggle(label, help, value)) return false;
    on_edit(out, applies);
    on_release(out, applies);
    return true;
}

bool setting_slider(const char* label, const char* help, float* value, float min, float max, const char* format,
                    AppActions& out, Applies applies) {
    return slider_row(label, help, out, applies, [&](const char* id) {
        return ImGui::SliderFloat(id, value, min, max, format, ImGuiSliderFlags_AlwaysClamp);
    });
}

bool setting_slider(const char* label, const char* help, double* value, double min, double max, const char* format,
                    AppActions& out, Applies applies) {
    return slider_row(label, help, out, applies, [&](const char* id) {
        return ImGui::SliderScalar(id, ImGuiDataType_Double, value, &min, &max, format, ImGuiSliderFlags_AlwaysClamp);
    });
}

bool setting_slider(const char* label, const char* help, int* value, int min, int max, const char* format,
                    AppActions& out, Applies applies) {
    return slider_row(label, help, out, applies, [&](const char* id) {
        return ImGui::SliderInt(id, value, min, max, format, ImGuiSliderFlags_AlwaysClamp);
    });
}

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
