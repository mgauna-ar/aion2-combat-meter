#include "app/ui/views.hpp"

#include "app/ui/icons.hpp"
#include "app/ui/widgets.hpp"

namespace aion2::app::ui {

#ifdef HAVE_IMGUI

void render_meter_page(const AppFrameInput&, AppViewState&, AppActions&) {
    page_header(ICON_SWORDS, "Combat Meter", "Every pull, live and saved");
    empty_state(ICON_SWORDS, "No fight yet", "Hit something in game.");
}

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
