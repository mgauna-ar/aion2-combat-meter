// Renders the README's overlay pictures from the overlay's own drawing code.
//
//   aion2_screenshots --font FILE --out DIR
//
// The made-up boss fight (tests/synthetic_session.hpp) is replayed through the
// real pipeline, and draw_overlay draws each scene into a software rasterizer
// (canvas.cpp, from ffxiv-hub) over a plain backdrop. Nothing here needs Windows or
// the game. FILE is the TrueType font in place of Segoe UI; `make screenshots` passes
// Arial from macOS.

#include "canvas.hpp"
#include "synthetic_session.hpp"

#include "app/app_settings.hpp"
#include "app/overlay_view.hpp"
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
    app::OverlayViewState view;
    std::vector<meter::HistoryEntry> history;
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
    unsigned char* pixels = nullptr;
    int w = 0;
    int h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    g_font_texture = std::make_unique<shots::Texture>();
    g_font_texture->width = w;
    g_font_texture->height = h;
    g_font_texture->rgba.assign(pixels, pixels + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    io.Fonts->SetTexID(g_font_texture.get());
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
    input.history = &scene.history;
    input.settings = &settings;
    input.scale = kScale;
    input.source_status = "capturing on 2 adapters; game connected";
    // A few frames, so every SameLine and child settles before the one captured.
    for (int frame = 0; frame < 3; ++frame) {
        io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
        io.DeltaTime = 1.0f / 60.0f;
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        ImGui::NewFrame();
        const app::OverlayActions actions = app::draw_overlay(input, scene.view);
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

} // namespace

int main(int argc, char** argv) {
    std::string font;
    std::filesystem::path out_dir;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string arg = argv[i];
        if (arg == "--font") font = argv[i + 1];
        else if (arg == "--out") out_dir = argv[i + 1];
    }
    if (font.empty() || out_dir.empty()) {
        std::fprintf(stderr, "usage: aion2_screenshots --font FILE --out DIR\n");
        return 2;
    }
    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    if (!setup_imgui(font)) return 1;

    const auto session = test::synthetic::boss_session();
    app::OverlaySettings settings;

    Scene live{"overlay-live.png", 430.0f, 215.0f, replay_until(session, 30.0, nullptr), {}, {}};

    std::vector<meter::FightSummary> finished;
    Scene done{"overlay-killed.png", 430.0f, 215.0f, replay_until(session, 0, &finished), {}, {}};

    Scene history{"overlay-history.png", 430.0f, 300.0f, done.snapshot, {}, {}};
    history.view.tab = app::OverlayViewState::Tab::History;
    for (const auto& fight : finished) {
        meter::HistoryEntry e;
        e.file = meter::fight_file_name(fight);
        e.id = fight.id;
        e.map_id = fight.map_id;
        e.start_ts = fight.start_ts;
        e.duration_s = fight.duration_s;
        e.boss_label = fight.boss.label;
        e.is_boss = fight.boss.is_boss;
        e.killed = fight.boss.killed;
        e.total_damage = fight.total_damage;
        e.total_dps = fight.total_dps;
        e.end_reason = fight.end_reason;
        e.players = fight.combatants.size();
        history.history.push_back(e);
        history.view.selected_file = e.file;
        history.view.selected_fight = fight;
    }

    bool ok = render(live, settings, out_dir);
    ok = render(done, settings, out_dir) && ok;
    ok = render(history, settings, out_dir) && ok;
    ImGui::DestroyContext();
    return ok ? 0 : 1;
}
