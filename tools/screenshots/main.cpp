// Renders the README's pictures from the overlay's and the app window's own
// drawing code.
//
//   aion2_screenshots --font FILE --bold FILE --out DIR [--all]
//
// The made-up boss fight (tests/synthetic_session.hpp) is replayed through the
// real pipeline, and draw_overlay and draw_app draw each scene into a software
// rasterizer (canvas.cpp, from ffxiv-hub). Nothing here needs Windows or the game.
// The fonts stand in for Segoe UI and Segoe UI Bold; `make screenshots` passes
// Arial from macOS. --all also draws the app's other pages, for a look at them;
// the README does not use those.

#include "canvas.hpp"
#include "synthetic_session.hpp"

#include "app/app_frame.hpp"
#include "app/app_settings.hpp"
#include "app/overlay_view.hpp"
#include "app/ui/theme.hpp"
#include "capture/capture_file.hpp"
#include "meter/history_store.hpp"
#include "pipeline/pipeline.hpp"

#include "imgui.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace aion2;

/// A 150% monitor, the overlay at scale 1: what the pictures are drawn at.
constexpr float kScale = 1.5f;
constexpr float kBaseFontSize = 15.0f;
constexpr int kMargin = 36;

struct Scene {
    std::string file;
    float width;
    float height;
    pipeline::LiveSnapshot snapshot;
};

/// The synthetic capture replayed up to `until` (or to the end with 0), as the
/// overlay would have seen it then.
pipeline::LiveSnapshot replay_until(const test::synthetic::Session& session, double until_s,
                                    std::vector<meter::FightSummary>* finished) {
    std::string error;
    auto reader = capture::CaptureFileReader::from_bytes(session.pcapng, error);
    pipeline::Pipeline pipe;
    capture::CapturedPacket pkt;
    const Micros start = test::synthetic::kStartMs * kMicrosPerMilli;
    const Micros stop = until_s > 0 ? start + from_seconds(until_s) : ~Micros{0};
    Micros last = start;
    while (reader && reader->next(pkt) && pkt.ts <= stop) {
        pipe.on_packet(pkt.link_type, pkt.data, pkt.ts);
        last = pkt.ts;
    }
    pipe.tick(last);
    if (finished != nullptr) *finished = pipe.take_finished();
    return pipe.snapshot(last);
}

std::unique_ptr<shots::Texture> g_font_texture;
std::unique_ptr<shots::Texture> g_app_font_texture;

/// The atlas the context's fonts were added to, as a texture the canvas samples.
std::unique_ptr<shots::Texture> atlas_texture() {
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels = nullptr;
    int w = 0;
    int h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    auto tex = std::make_unique<shots::Texture>();
    tex->width = w;
    tex->height = h;
    tex->rgba.assign(pixels, pixels + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    io.Fonts->SetTexID(tex.get());
    return tex;
}

bool setup_imgui(const std::string& font) {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    if (io.Fonts->AddFontFromFileTTF(font.c_str(), std::round(kBaseFontSize * kScale), &cfg) == nullptr) {
        std::fprintf(stderr, "cannot load font %s\n", font.c_str());
        return false;
    }
    g_font_texture = atlas_texture();
    // The style overlay_window.cpp sets up.
    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    style.ScaleAllSizes(kScale);
    ImGui::GetStyle() = style;
    return true;
}

bool render(Scene& scene, const app::OverlaySettings& settings, const std::filesystem::path& out_dir) {
    ImGuiIO& io = ImGui::GetIO();
    const int w = static_cast<int>(std::lround(scene.width * kScale));
    const int h = static_cast<int>(std::lround(scene.height * kScale));
    app::OverlayFrameInput input;
    input.snapshot = &scene.snapshot;
    input.settings = &settings;
    input.scale = kScale;
    input.source_status = "capturing on 2 adapters; game connected";
    // A few frames, so every SameLine and child settles before the one captured.
    for (int frame = 0; frame < 3; ++frame) {
        io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
        io.DeltaTime = 1.0f / 60.0f;
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        ImGui::NewFrame();
        const app::OverlayActions actions = app::draw_overlay(input);
        ImGui::Render();
        (void)actions;
    }
    shots::Canvas overlay(w, h, shots::Rgba{0, 0, 0, 0});
    overlay.draw(*ImGui::GetDrawData());
    shots::Canvas picture = shots::make_backdrop(w + 2 * kMargin, h + 2 * kMargin);
    picture.paste(overlay, kMargin, kMargin);
    const std::filesystem::path path = out_dir / scene.file;
    if (!picture.write_png(path.string())) {
        std::fprintf(stderr, "cannot write %s\n", path.string().c_str());
        return false;
    }
    std::printf("wrote %s\n", path.string().c_str());
    return true;
}

/// The app window's context, as app_window.cpp sets it up: the fonts and the theme.
ImGuiContext* setup_app_imgui(const std::string& font, const std::string& bold) {
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    app::ui::setup_fonts_and_theme(font, bold, kScale);
    g_app_font_texture = atlas_texture();
    return ctx;
}

struct AppScene {
    std::string file;
    pipeline::LiveSnapshot snapshot;
    std::vector<meter::HistoryEntry> history;
    app::AppViewState view;
};

/// One page of the app window, in a window-sized picture with rounded corners and a
/// shadow, over the backdrop.
bool render_app(AppScene& scene, app::AppSettings& settings, const std::filesystem::path& out_dir) {
    ImGuiIO& io = ImGui::GetIO();
    const int w = static_cast<int>(std::lround(app::ui::metrics::WindowDefaultW * kScale));
    const int h = static_cast<int>(std::lround(app::ui::metrics::WindowDefaultH * kScale));
    app::AppFrameInput input;
    input.snapshot = &scene.snapshot;
    input.history = &scene.history;
    input.source_status = "Capturing on 2 adapters; the game's connection is on port 13328";
    input.settings = &settings;
    input.data_dir = "C:\\Users\\you\\AppData\\Roaming\\aion2-combat-meter";
    // Several frames, so tables, tabs and wrapped rows settle before the one captured.
    for (int frame = 0; frame < 4; ++frame) {
        io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
        io.DeltaTime = 1.0f / 60.0f;
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        ImGui::NewFrame();
        (void)app::draw_app(input, scene.view);
        ImGui::Render();
    }
    shots::Canvas window(w, h, shots::from_im_col32(app::ui::colors::Canvas));
    window.draw(*ImGui::GetDrawData());
    shots::Canvas picture = shots::make_backdrop(w + 2 * kMargin, h + 2 * kMargin);
    const float corner = 8.0f * kScale;
    picture.drop_shadow(shots::Rect{kMargin, kMargin, w, h}, corner, 18.0f * kScale, 0.45f, static_cast<int>(6 * kScale));
    picture.paste_rounded(window, kMargin, kMargin, corner);
    picture.outline(shots::Rect{kMargin, kMargin, w, h}, corner, shots::from_im_col32(app::ui::colors::BorderStrong));
    const std::filesystem::path path = out_dir / scene.file;
    if (!picture.write_png(path.string())) {
        std::fprintf(stderr, "cannot write %s\n", path.string().c_str());
        return false;
    }
    std::printf("wrote %s\n", path.string().c_str());
    return true;
}

meter::HistoryEntry entry_of(const meter::FightSummary& fight) {
    meter::HistoryEntry e;
    e.file = meter::fight_file_name(fight);
    e.id = fight.id;
    e.map_id = fight.map_id;
    e.visit_ts = fight.visit_ts;
    e.start_ts = fight.start_ts;
    e.duration_s = fight.duration_s;
    e.boss_label = fight.boss.label;
    e.is_boss = fight.boss.is_boss;
    e.killed = fight.boss.killed;
    e.partial = fight.quality.partial;
    e.total_damage = fight.total_damage;
    e.total_dps = fight.total_dps;
    e.end_reason = fight.end_reason;
    e.players = fight.combatants.size();
    return e;
}

/// A made-up evening around the synthetic fight: two earlier pulls of the same boss
/// in that visit, and a fight on another map before it. Invented, like the fight.
std::vector<meter::HistoryEntry> evening(const meter::FightSummary& kill) {
    std::vector<meter::HistoryEntry> out;
    meter::HistoryEntry earlier = entry_of(kill);
    earlier.file = "earlier-visit.json";
    earlier.map_id = 400001;
    earlier.visit_ts = kill.start_ts - 3'600 * kMicrosPerSecond;
    earlier.start_ts = earlier.visit_ts + 300 * kMicrosPerSecond;
    earlier.duration_s = 41.0;
    earlier.boss_label = "NPC #8206";
    earlier.is_boss = false;
    earlier.killed = true;
    earlier.end_reason = meter::EndReason::Idle;
    out.push_back(earlier);

    const Micros visit = kill.visit_ts != 0 ? kill.visit_ts : kill.start_ts - 900 * kMicrosPerSecond;
    meter::HistoryEntry wipe = entry_of(kill);
    wipe.file = "pull-1.json";
    wipe.visit_ts = visit;
    wipe.start_ts = kill.start_ts - 600 * kMicrosPerSecond;
    wipe.duration_s = 63.0;
    wipe.killed = false;
    wipe.boss_hp_known = true;
    wipe.boss_hp_left = 0.46;
    wipe.end_reason = meter::EndReason::Wipe;
    out.push_back(wipe);
    meter::HistoryEntry reset = wipe;
    reset.file = "pull-2.json";
    reset.start_ts = kill.start_ts - 300 * kMicrosPerSecond;
    reset.duration_s = 38.0;
    reset.boss_hp_left = 0.71;
    reset.end_reason = meter::EndReason::Idle;
    out.push_back(reset);
    meter::HistoryEntry last = entry_of(kill);
    last.visit_ts = visit;
    out.push_back(last);
    return out;
}

} // namespace

int main(int argc, char** argv) {
    std::string font;
    std::string bold;
    std::filesystem::path out_dir;
    bool all = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--all") {
            all = true;
        } else if (i + 1 < argc) {
            if (arg == "--font") font = argv[i + 1];
            else if (arg == "--bold") bold = argv[i + 1];
            else if (arg == "--out") out_dir = argv[i + 1];
            ++i;
        }
    }
    if (font.empty() || out_dir.empty()) {
        std::fprintf(stderr, "usage: aion2_screenshots --font FILE [--bold FILE] --out DIR\n");
        return 2;
    }
    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    if (!setup_imgui(font)) return 1;

    const auto session = test::synthetic::boss_session();
    app::OverlaySettings settings;

    Scene live{"overlay-live.png", 430.0f, 215.0f, replay_until(session, 30.0, nullptr)};

    std::vector<meter::FightSummary> finished;
    Scene done{"overlay-killed.png", 430.0f, 215.0f, replay_until(session, 0, &finished)};

    bool ok = render(live, settings, out_dir);
    ok = render(done, settings, out_dir) && ok;
    ImGuiContext* overlay_ctx = ImGui::GetCurrentContext();

    setup_app_imgui(font, bold);
    app::AppSettings app_settings;
    const meter::FightSummary& kill = finished.empty() ? done.snapshot.fight : finished.back();
    AppScene damage{"app-damage.png", done.snapshot, evening(kill), {}};
    damage.view.selected_file = damage.history.back().file;
    damage.view.selected_fight = kill;
    damage.view.damage_player = static_cast<meter::EntityId>(test::synthetic::kMe);
    ok = render_app(damage, app_settings, out_dir) && ok;
    AppScene settings_page{"app-settings.png", done.snapshot, damage.history, {}};
    settings_page.view.open_meter_tab = 3;
    // Locked, so the visibility conditions read as live rather than greyed out.
    app::AppSettings shown_settings = app_settings;
    shown_settings.overlay.locked = true;
    shown_settings.overlay.show_in_combat_only = true;
    ok = render_app(settings_page, shown_settings, out_dir) && ok;
    if (all) {
        AppScene healing = damage;
        healing.file = "app-healing.png";
        healing.view.open_meter_tab = 1;
        healing.view.heal_player = static_cast<meter::EntityId>(test::synthetic::kAlly2);
        ok = render_app(healing, app_settings, out_dir) && ok;
        AppScene taken = damage;
        taken.file = "app-taken.png";
        taken.view.open_meter_tab = 2;
        taken.view.taken_player = static_cast<meter::EntityId>(test::synthetic::kMe);
        ok = render_app(taken, app_settings, out_dir) && ok;
        AppScene dashboard{"app-dashboard.png", done.snapshot, damage.history, {}};
        dashboard.view.page = app::AppPage::Dashboard;
        ok = render_app(dashboard, app_settings, out_dir) && ok;
        AppScene app_page{"app-app-settings.png", done.snapshot, damage.history, {}};
        app_page.view.page = app::AppPage::Settings;
        ok = render_app(app_page, app_settings, out_dir) && ok;
    }
    ImGui::DestroyContext();

    ImGui::SetCurrentContext(overlay_ctx);
    ImGui::DestroyContext();
    return ok ? 0 : 1;
}
