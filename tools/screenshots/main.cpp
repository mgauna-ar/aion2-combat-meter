// Renders the README's pictures from the overlay's and the app window's own
// drawing code.
//
//   aion2_screenshots --font FILE --bold FILE --out DIR [--all]
//
// The made-up boss fight (tests/synthetic_session.hpp) is replayed through the
// real pipeline, and draw_overlay and draw_app draw each scene into a software
// rasterizer (canvas.cpp, from ffxiv-hub). Nothing here needs Windows or the game.
// The fonts stand in for Segoe UI and Segoe UI Bold; `make screenshots` passes
// Arial from macOS. --all also draws the app's other pages and the overlays
// unlocked, for a look at them; the README does not use those.

#include "canvas.hpp"
#include "synthetic_session.hpp"

#include "app/app_frame.hpp"
#include "app/app_settings.hpp"
#include "app/aura_view.hpp"
#include "app/ping_view.hpp"
#include "app/power_view.hpp"
#include "app/game_icons.hpp"
#include "app/overlay_chrome.hpp"
#include "app/overlay_view.hpp"
#include "app/ui/theme.hpp"
#include "capture/capture_file.hpp"
#include "meter/fight_detail.hpp"
#include "meter/history_store.hpp"
#include "pipeline/pipeline.hpp"

#include "imgui.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
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

/// The overlay's fonts and icons, as overlay_window.cpp's rebuild_fonts builds them:
/// icons at `icon_px` before the scale (the meter's emblems, or the aura overlay's
/// bar icons or tiles).
bool build_overlay_fonts(const std::string& font, const std::string& bold, float icon_px) {
    if (!app::chrome::build_overlay_fonts(font, bold, kBaseFontSize * kScale, std::round(icon_px * kScale))) {
        std::fprintf(stderr, "cannot load font %s\n", font.c_str());
        return false;
    }
    g_font_texture = atlas_texture();
    return true;
}

bool setup_imgui(const std::string& font, const std::string& bold) {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    if (!build_overlay_fonts(font, bold, app::kMeterEmblemPx)) return false;
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

/// What the Ping plugin would hold two minutes into a calm evening with one lag
/// spike: made up here, since the synthetic session has no pongs and a replay has
/// no performance counter to make a ping of them.
ping::PingSnapshot ping_demo(Micros now) {
    ping::PingSnapshot p;
    p.state = ping::PingState::Measured;
    p.pongs = 30;
    p.has_jitter = true;
    p.jitter_ms = 2.4f;
    p.heartbeat_at = now;
    const Micros now_second = now - now % kMicrosPerSecond;
    uint32_t seed = 12345;
    const auto noise = [&] {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>((seed >> 16) & 0xFF) / 255.0f;
    };
    float ping_ms = 68.0f;
    float sum = 0.0f;
    for (size_t age = app::kPingGraphColumns; age-- > 0;) {
        // A pong every ten seconds; the one 40 s ago came back slowly.
        if (age % 10 == 3) {
            ping_ms = age == 43 ? 143.0f : 62.0f + 12.0f * noise();
            p.min_ms = p.samples == 0 ? ping_ms : std::min(p.min_ms, ping_ms);
            p.max_ms = std::max(p.max_ms, ping_ms);
            sum += ping_ms;
            ++p.samples;
            p.ping_ms = ping_ms;
            p.ping_at = now_second - age * kMicrosPerSecond;
        }
        float late_ms = 5.0f * noise();
        if (age >= 44 && age <= 47) late_ms = 55.0f + 60.0f * noise();
        p.seconds.push_back(ping::PingSecond{now_second - age * kMicrosPerSecond, ping_ms, late_ms});
    }
    p.avg_ms = sum / static_cast<float>(p.samples);
    return p;
}

/// What of `canvas` was drawn on: the box around every pixel that is not see-through.
/// A locked ping overlay draws its panel only, wherever in the window that is.
shots::Rect drawn_bounds(const shots::Canvas& canvas) {
    int x0 = canvas.width();
    int y0 = canvas.height();
    int x1 = -1;
    int y1 = -1;
    for (int y = 0; y < canvas.height(); ++y) {
        for (int x = 0; x < canvas.width(); ++x) {
            if (canvas.pixel(x, y).a <= 0.0f) continue;
            x0 = std::min(x0, x);
            y0 = std::min(y0, y);
            x1 = std::max(x1, x);
            y1 = std::max(y1, y);
        }
    }
    if (x1 < x0) return shots::Rect{0, 0, canvas.width(), canvas.height()};
    return shots::Rect{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

/// An overlay over the same backdrop, cut to what `draw` draws of its window, so a
/// locked one shows as the panel it is.
template <typename Draw>
bool render_drawn(Scene& scene, const std::filesystem::path& out_dir, Draw&& draw) {
    ImGuiIO& io = ImGui::GetIO();
    const int w = static_cast<int>(std::lround(scene.width * kScale));
    const int h = static_cast<int>(std::lround(scene.height * kScale));
    for (int frame = 0; frame < 3; ++frame) {
        io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
        io.DeltaTime = 1.0f / 60.0f;
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        ImGui::NewFrame();
        draw();
        ImGui::Render();
    }
    shots::Canvas window(w, h, shots::Rgba{0, 0, 0, 0});
    window.draw(*ImGui::GetDrawData());
    const shots::Canvas overlay = window.crop(drawn_bounds(window));
    shots::Canvas picture = shots::make_backdrop(overlay.width() + 2 * kMargin, overlay.height() + 2 * kMargin);
    picture.paste(overlay, kMargin, kMargin);
    const std::filesystem::path path = out_dir / scene.file;
    if (!picture.write_png(path.string())) {
        std::fprintf(stderr, "cannot write %s\n", path.string().c_str());
        return false;
    }
    std::printf("wrote %s\n", path.string().c_str());
    return true;
}

/// The ping overlay.
bool render_ping(Scene& scene, const app::PingSettings& settings, const std::filesystem::path& out_dir) {
    app::PingFrameInput input;
    input.snapshot = &scene.snapshot;
    input.settings = &settings;
    input.scale = kScale;
    return render_drawn(scene, out_dir, [&] { (void)app::draw_ping(input); });
}

/// The combat power overlay.
bool render_power(Scene& scene, const app::PowerSettings& settings, const std::filesystem::path& out_dir) {
    app::PowerFrameInput input;
    input.snapshot = &scene.snapshot;
    input.settings = &settings;
    input.scale = kScale;
    return render_drawn(scene, out_dir, [&] { (void)app::draw_power(input); });
}

/// An arena's two opponents, the one being fought marked: what the Combat Power
/// plugin holds a minute into a two-on-two match.
combat_power::PowerSnapshot power_demo() {
    combat_power::PowerSnapshot p;
    p.map_id = 61;
    p.players = {
        {227, "Nezekan", 9, game::AionClass::Templar, 134'667},
        {301, "Triniel", 9, game::AionClass::Assassin, 121'480},
    };
    p.in_view = static_cast<uint32_t>(p.players.size());
    p.target = p.players.front();
    return p;
}

/// The aura overlay over the same backdrop, at the scene's own capture time.
bool render_auras(Scene& scene, const app::AuraSettings& settings, const std::filesystem::path& out_dir) {
    ImGuiIO& io = ImGui::GetIO();
    const int w = static_cast<int>(std::lround(scene.width * kScale));
    const int h = static_cast<int>(std::lround(scene.height * kScale));
    app::AuraFrameInput input;
    input.snapshot = &scene.snapshot;
    input.settings = &settings;
    input.scale = kScale;
    input.now = scene.snapshot.now;
    input.pulse_s = 0.4;  // a missing effect near its brightest
    for (int frame = 0; frame < 3; ++frame) {
        io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
        io.DeltaTime = 1.0f / 60.0f;
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        ImGui::NewFrame();
        (void)app::draw_auras(input);
        ImGui::Render();
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
/// `update` is the updater's state; unset on the README's pictures, which show none.
bool render_app(AppScene& scene, app::AppSettings& settings, const std::filesystem::path& out_dir,
                const std::optional<app::UpdateStatus>& update = std::nullopt) {
    ImGuiIO& io = ImGui::GetIO();
    const int w = static_cast<int>(std::lround(app::ui::metrics::WindowDefaultW * kScale));
    const int h = static_cast<int>(std::lround(app::ui::metrics::WindowDefaultH * kScale));
    app::AppFrameInput input;
    input.snapshot = &scene.snapshot;
    input.history = &scene.history;
    input.source_status = "Capturing on 2 adapters; the game's connection is on port 13328";
    input.settings = &settings;
    input.data_dir = "C:\\Users\\you\\AppData\\Roaming\\aion2-combat-meter";
    input.update = update;
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
    earlier.boss_label = "Highland Odyle Sparkie";
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
    if (!setup_imgui(font, bold)) return 1;

    const auto session = test::synthetic::boss_session();
    // Locked, as it is in play: no header buttons and no resize grip, which only an
    // unlocked overlay draws.
    app::OverlaySettings settings;
    settings.locked = true;

    Scene live{"overlay-live.png", 430.0f, 215.0f, replay_until(session, 30.0, nullptr)};

    std::vector<meter::FightSummary> finished;
    Scene done{"overlay-killed.png", 430.0f, 215.0f, replay_until(session, 0, &finished)};

    bool ok = render(live, settings, out_dir);
    ok = render(done, settings, out_dir) && ok;
    // Unlocked, as an overlay is while it is being placed: the header's buttons and
    // the grip. Under --all, like the pages the README does not show.
    if (all) {
        app::OverlaySettings unlocked = settings;
        unlocked.locked = false;
        Scene scene{"overlay-live-unlocked.png", 430.0f, 215.0f, live.snapshot};
        ok = render(scene, unlocked, out_dir) && ok;
    }
    // Ariel's buffs, cooldowns and curse on the boss mid-fight, a buff she watches for
    // missing (Wish of Concentration), a chain skill on offer, and her HP low.
    app::AuraSettings aura_settings;
    aura_settings.locked = true;
    aura_settings.watch = {15'310'000};
    ok = build_overlay_fonts(font, bold, app::kAuraBarIconPx) && ok;
    Scene auras{"overlay-auras.png", 280.0f, 286.0f, live.snapshot};
    ok = render_auras(auras, aura_settings, out_dir) && ok;
    if (all) {
        app::AuraSettings unlocked = aura_settings;
        unlocked.locked = false;
        Scene scene{"overlay-auras-unlocked.png", 280.0f, 320.0f, live.snapshot};
        ok = render_auras(scene, unlocked, out_dir) && ok;
    }
    // The same as icons, the atlas holding them at that size as the overlay's does.
    app::AuraSettings icon_settings = aura_settings;
    icon_settings.icons = true;
    ok = build_overlay_fonts(font, bold, static_cast<float>(icon_settings.icon_size)) && ok;
    Scene aura_icons{"overlay-auras-icons.png", 280.0f, 236.0f, live.snapshot};
    ok = render_auras(aura_icons, icon_settings, out_dir) && ok;
    ok = build_overlay_fonts(font, bold, kBaseFontSize) && ok;
    // The ping overlay, locked: its figures, the graph and the range under it.
    app::PingSettings ping_settings;
    ping_settings.locked = true;
    pipeline::LiveSnapshot ping_snapshot = live.snapshot;
    ping_snapshot.ping = ping_demo(ping_snapshot.now);
    Scene ping_scene{"overlay-ping.png", static_cast<float>(ping_settings.width),
                     static_cast<float>(ping_settings.height), ping_snapshot};
    ok = render_ping(ping_scene, ping_settings, out_dir) && ok;
    // Only the ping: the panel is as large as its line, whatever the window's size.
    app::PingSettings ping_only = ping_settings;
    ping_only.show_jitter = false;
    ping_only.show_graph = false;
    Scene ping_only_scene = ping_scene;
    ping_only_scene.file = "overlay-ping-only.png";
    ok = render_ping(ping_only_scene, ping_only, out_dir) && ok;
    if (all) {
        // Unlocked, the same window with its header: the figures keep their place and size.
        app::PingSettings unlocked = ping_settings;
        unlocked.locked = false;
        Scene scene = ping_scene;
        scene.file = "overlay-ping-unlocked.png";
        ok = render_ping(scene, unlocked, out_dir) && ok;
        app::PingSettings only_unlocked = ping_only;
        only_unlocked.locked = false;
        scene.file = "overlay-ping-only-unlocked.png";
        ok = render_ping(scene, only_unlocked, out_dir) && ok;
        app::PingSettings no_graph = ping_settings;
        no_graph.show_graph = false;
        scene.file = "overlay-ping-no-graph.png";
        ok = render_ping(scene, no_graph, out_dir) && ok;
        // Too narrow for both: the jitter goes under the ping.
        scene.file = "overlay-ping-no-graph-narrow.png";
        scene.width = 150.0f;
        ok = render_ping(scene, no_graph, out_dir) && ok;
    }
    // The combat power overlay, locked, in an arena: both opponents, the target marked.
    app::PowerSettings power_settings;
    power_settings.locked = true;
    pipeline::LiveSnapshot power_snapshot = live.snapshot;
    power_snapshot.power = power_demo();
    Scene power_scene{"overlay-combat-power.png", static_cast<float>(power_settings.width), 78.0f, power_snapshot};
    ok = render_power(power_scene, power_settings, out_dir) && ok;
    if (all) {
        // Unlocked, with its header; and a crowd: the target above its strongest.
        app::PowerSettings unlocked = power_settings;
        unlocked.locked = false;
        Scene scene = power_scene;
        scene.file = "overlay-combat-power-unlocked.png";
        scene.height = 112.0f;
        ok = render_power(scene, unlocked, out_dir) && ok;
        scene.file = "overlay-combat-power-crowd.png";
        scene.height = 132.0f;
        scene.snapshot.power.target = {512, "Vaizel", 9, game::AionClass::Ranger, 98'300};
        scene.snapshot.power.in_view = 40;
        ok = render_power(scene, power_settings, out_dir) && ok;
        scene.file = "overlay-combat-power-empty.png";
        scene.height = 112.0f;
        scene.snapshot.power = combat_power::PowerSnapshot{};
        ok = render_power(scene, unlocked, out_dir) && ok;
    }
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
    settings_page.view.open_meter_tab = app::MeterTab::Settings;
    // Locked, so the visibility conditions read as live rather than greyed out.
    app::AppSettings shown_settings = app_settings;
    shown_settings.overlay.locked = true;
    shown_settings.overlay.show_in_combat_only = true;
    ok = render_app(settings_page, shown_settings, out_dir) && ok;
    // The fight second by second, read at the moment Lumiel dies, under Spirit
    // Communion's band.
    AppScene timeline = damage;
    timeline.file = "app-timeline.png";
    timeline.view.damage_player = 0;
    timeline.view.open_meter_tab = app::MeterTab::Timeline;
    timeline.view.timeline_hover_at = 0.52f;
    ok = render_app(timeline, app_settings, out_dir) && ok;
    // Lumiel's death, with the hits and heals before it.
    AppScene deaths = damage;
    deaths.file = "app-deaths.png";
    deaths.view.damage_player = 0;
    deaths.view.open_meter_tab = app::MeterTab::Deaths;
    if (kill.detail != nullptr && !kill.detail->deaths.empty()) {
        deaths.view.death_player = kill.detail->deaths.front().player;
        deaths.view.death_time_s = kill.detail->deaths.front().time_s;
    }
    ok = render_app(deaths, app_settings, out_dir) && ok;
    // The party's effects on the boss.
    AppScene buffs = damage;
    buffs.file = "app-buffs.png";
    buffs.view.damage_player = 0;
    buffs.view.open_meter_tab = app::MeterTab::Buffs;
    buffs.view.effect_view = meter::EffectSide::PartyBuff;
    ok = render_app(buffs, app_settings, out_dir) && ok;
    // What the party pressed, and Ariel's skills.
    AppScene casts = damage;
    casts.file = "app-casts.png";
    casts.view.damage_player = 0;
    casts.view.open_meter_tab = app::MeterTab::Casts;
    casts.view.casts_player = static_cast<meter::EntityId>(test::synthetic::kMe);
    ok = render_app(casts, app_settings, out_dir) && ok;
    // The Auras plugin's page, switched on, with the overlay's settings above.
    AppScene auras_page{"app-auras.png", done.snapshot, damage.history, {}};
    auras_page.view.page = app::AppPage::Auras;
    app::AppSettings auras_settings = app_settings;
    auras_settings.plugins.auras = true;
    auras_settings.auras = aura_settings;
    ok = render_app(auras_page, auras_settings, out_dir) && ok;
    // The Ping plugin's page, switched on.
    pipeline::LiveSnapshot ping_page_snapshot = done.snapshot;
    ping_page_snapshot.ping = ping_demo(ping_page_snapshot.now);
    AppScene ping_page{"app-ping.png", ping_page_snapshot, damage.history, {}};
    ping_page.view.page = app::AppPage::Ping;
    app::AppSettings ping_app_settings = app_settings;
    ping_app_settings.plugins.ping = true;
    ping_app_settings.ping = ping_settings;
    ok = render_app(ping_page, ping_app_settings, out_dir) && ok;
    // The Combat Power plugin's page, switched on.
    pipeline::LiveSnapshot power_page_snapshot = done.snapshot;
    power_page_snapshot.power = power_demo();
    AppScene power_page{"app-combat-power.png", power_page_snapshot, damage.history, {}};
    power_page.view.page = app::AppPage::Power;
    app::AppSettings power_app_settings = app_settings;
    power_app_settings.plugins.combat_power = true;
    ok = render_app(power_page, power_app_settings, out_dir) && ok;
    if (all) {
        AppScene healing = damage;
        healing.file = "app-healing.png";
        healing.view.open_meter_tab = app::MeterTab::Healing;
        healing.view.heal_player = static_cast<meter::EntityId>(test::synthetic::kAlly2);
        ok = render_app(healing, app_settings, out_dir) && ok;
        AppScene taken = damage;
        taken.file = "app-taken.png";
        taken.view.open_meter_tab = app::MeterTab::Taken;
        taken.view.taken_player = static_cast<meter::EntityId>(test::synthetic::kMe);
        ok = render_app(taken, app_settings, out_dir) && ok;
        AppScene dashboard{"app-dashboard.png", done.snapshot, damage.history, {}};
        dashboard.view.page = app::AppPage::Dashboard;
        ok = render_app(dashboard, app_settings, out_dir) && ok;
        // Before the game is found: what the dashboard and the meter page say to do.
        AppScene waiting{"app-dashboard-waiting.png", pipeline::LiveSnapshot{}, {}, {}};
        waiting.view.page = app::AppPage::Dashboard;
        ok = render_app(waiting, app_settings, out_dir) && ok;
        AppScene no_fight{"app-meter-waiting.png", pipeline::LiveSnapshot{}, {}, {}};
        no_fight.view.open_meter_tab = app::MeterTab::Damage;
        ok = render_app(no_fight, app_settings, out_dir) && ok;
        // No player selected: the hint under the ranking.
        AppScene unselected = damage;
        unselected.file = "app-damage-unselected.png";
        unselected.view.damage_player = 0;
        unselected.view.open_meter_tab = app::MeterTab::Damage;
        ok = render_app(unselected, app_settings, out_dir) && ok;
        AppScene app_page{"app-app-settings.png", done.snapshot, damage.history, {}};
        app_page.view.page = app::AppPage::Settings;
        // A newer release found, so the Updates card and the sidebar pill show.
        app::ReleaseInfo next;
        next.version = {app::CURRENT_VERSION.major, app::CURRENT_VERSION.minor,
                        static_cast<uint16_t>(app::CURRENT_VERSION.patch + 1)};
        next.page_url = std::string(app::RELEASE_PAGE_PREFIX) + "tag/v" + next.version.to_string();
        next.download_url = std::string(app::RELEASE_DOWNLOAD_PREFIX) + "v" + next.version.to_string() + "/" +
                            std::string(app::RELEASE_ASSET_NAME);
        next.sha256 = std::string(64, '0');
        const app::UpdateStatus available{app::UpdateState::Available, next, 0.0f, {}};
        ok = render_app(app_page, app_settings, out_dir, available) && ok;
    }
    ImGui::DestroyContext();

    ImGui::SetCurrentContext(overlay_ctx);
    ImGui::DestroyContext();
    return ok ? 0 : 1;
}
