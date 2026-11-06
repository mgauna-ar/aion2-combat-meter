#include "app/app_settings.hpp"

#include "common/config/json_file.hpp"

#include <algorithm>

namespace aion2::app {

namespace {

constexpr const char* kWindow = "app";
constexpr const char* kCapture = "capture";
constexpr const char* kOverlay = "overlay";
constexpr const char* kAuras = "auras";
constexpr const char* kMeter = "meter";
constexpr const char* kHistory = "history";

/// An executable name: letters, digits, dot, dash, underscore and space only, so a
/// hand-edited value can only ever name a process, never a path.
bool plausible_exe_name(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' ||
               c == '_' || c == ' ';
    });
}

/// The watch list's codes: whole numbers that fit a u32, each once, at most
/// kMaxWatchedEffects. Anything else in a hand-edited file is dropped.
std::vector<uint32_t> read_watch(const config::JsonValue& json) {
    std::vector<uint32_t> out;
    if (!json.is_array()) return out;
    for (const config::JsonValue& v : json.as_array()) {
        if (!v.is_number()) continue;
        const double d = v.as_double();
        if (d < 1.0 || d > 4294967295.0 || d != static_cast<double>(static_cast<uint64_t>(d))) continue;
        const auto code = static_cast<uint32_t>(d);
        if (std::find(out.begin(), out.end(), code) == out.end()) out.push_back(code);
        if (out.size() == kMaxWatchedEffects) break;
    }
    return out;
}

} // namespace

void sanitize(AppSettings& s) {
    s.capture.server_port = std::clamp(s.capture.server_port, 1, 65535);
    if (!plausible_exe_name(s.capture.game_process)) s.capture.game_process = CaptureSettings{}.game_process;
    s.overlay.opacity = std::clamp(s.overlay.opacity, 0.0f, 1.0f);
    s.overlay.scale = std::clamp(s.overlay.scale, 0.6f, 2.5f);
    s.overlay.width = std::clamp(s.overlay.width, 200, 4000);
    s.overlay.height = std::clamp(s.overlay.height, 90, 4000);
    s.overlay.max_rows = std::clamp(s.overlay.max_rows, 1, 24);
    s.overlay.hide_after_combat_s = std::clamp(s.overlay.hide_after_combat_s, 0.0f, 600.0f);
    s.auras.opacity = std::clamp(s.auras.opacity, 0.0f, 1.0f);
    s.auras.scale = std::clamp(s.auras.scale, 0.6f, 2.5f);
    s.auras.width = std::clamp(s.auras.width, 160, 4000);
    s.auras.height = std::clamp(s.auras.height, 90, 4000);
    s.auras.min_duration_s = std::clamp(s.auras.min_duration_s, 0.0f, 60.0f);
    s.auras.min_cooldown_s = std::clamp(s.auras.min_cooldown_s, 0.0f, 120.0f);
    if (s.auras.watch.size() > kMaxWatchedEffects) s.auras.watch.resize(kMaxWatchedEffects);
    s.meter.idle_timeout_s = std::clamp(s.meter.idle_timeout_s, 2.0, 600.0);
    s.meter.boss_idle_timeout_s = std::clamp(s.meter.boss_idle_timeout_s, s.meter.idle_timeout_s, 1800.0);
    s.meter.capture_lost_s = std::clamp(s.meter.capture_lost_s, 5.0, 600.0);
    s.meter.kill_grace_s = std::clamp(s.meter.kill_grace_s, 0.0, 30.0);
    s.meter.min_saved_duration_s = std::clamp(s.meter.min_saved_duration_s, 0.0, 3600.0);
    s.meter.partial_damage_ratio = std::clamp(s.meter.partial_damage_ratio, 0.0, 1.0);
    s.meter.memory_history = std::clamp(s.meter.memory_history, 1, 100);
    s.history.max_fights = std::clamp(s.history.max_fights, 1, 100000);
    s.window.width = std::clamp(s.window.width, 640, 8000);
    s.window.height = std::clamp(s.window.height, 400, 8000);
}

config::JsonValue settings_to_json(const AppSettings& s) {
    config::JsonValue root;
    config::JsonValue window;
    config::write_keys<WindowSettings>(kWindowKeys, s.window, window);
    root[kWindow] = std::move(window);
    config::JsonValue capture;
    config::write_keys<CaptureSettings>(kCaptureKeys, s.capture, capture);
    capture["game_process"] = s.capture.game_process;
    root[kCapture] = std::move(capture);
    config::JsonValue overlay;
    config::write_keys<OverlaySettings>(kOverlayKeys, s.overlay, overlay);
    root[kOverlay] = std::move(overlay);
    config::JsonValue auras;
    config::write_keys<AuraSettings>(kAuraKeys, s.auras, auras);
    config::JsonValue::ArrayType watch;
    for (const uint32_t code : s.auras.watch) watch.emplace_back(static_cast<double>(code));
    auras["watch"] = config::JsonValue(std::move(watch));
    root[kAuras] = std::move(auras);
    config::JsonValue meter;
    config::write_keys<meter::MeterSettings>(meter::kMeterSettingKeys, s.meter, meter);
    root[kMeter] = std::move(meter);
    config::JsonValue history;
    config::write_keys<HistorySettings>(kHistoryKeys, s.history, history);
    root[kHistory] = std::move(history);
    return root;
}

AppSettings settings_from_json(const config::JsonValue& json) {
    AppSettings s;
    config::read_keys<WindowSettings>(kWindowKeys, json[kWindow], s.window);
    config::read_keys<CaptureSettings>(kCaptureKeys, json[kCapture], s.capture);
    if (json[kCapture]["game_process"].is_string()) s.capture.game_process = json[kCapture]["game_process"].as_string();
    config::read_keys<OverlaySettings>(kOverlayKeys, json[kOverlay], s.overlay);
    config::read_keys<AuraSettings>(kAuraKeys, json[kAuras], s.auras);
    s.auras.watch = read_watch(json[kAuras]["watch"]);
    config::read_keys<meter::MeterSettings>(meter::kMeterSettingKeys, json[kMeter], s.meter);
    config::read_keys<HistorySettings>(kHistoryKeys, json[kHistory], s.history);
    sanitize(s);
    return s;
}

AppSettings load_settings(const std::filesystem::path& file) {
    const auto json = config::read_json_file(file);
    if (!json) return AppSettings{};
    return settings_from_json(*json);
}

std::string save_settings(const std::filesystem::path& file, const AppSettings& settings) {
    return config::write_json_atomic(file, settings_to_json(settings));
}

} // namespace aion2::app
