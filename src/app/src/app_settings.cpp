#include "app/app_settings.hpp"

#include "common/config/json_file.hpp"

#include <algorithm>

namespace aion2::app {

namespace {

constexpr const char* kCapture = "capture";
constexpr const char* kOverlay = "overlay";
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

} // namespace

void sanitize(AppSettings& s) {
    s.capture.server_port = std::clamp(s.capture.server_port, 1, 65535);
    if (!plausible_exe_name(s.capture.game_process)) s.capture.game_process = CaptureSettings{}.game_process;
    s.overlay.opacity = std::clamp(s.overlay.opacity, 0.0f, 1.0f);
    s.overlay.scale = std::clamp(s.overlay.scale, 0.6f, 2.5f);
    s.overlay.width = std::clamp(s.overlay.width, 200, 4000);
    s.overlay.height = std::clamp(s.overlay.height, 90, 4000);
    s.meter.idle_timeout_s = std::clamp(s.meter.idle_timeout_s, 2.0, 600.0);
    s.meter.boss_idle_timeout_s = std::clamp(s.meter.boss_idle_timeout_s, s.meter.idle_timeout_s, 1800.0);
    s.meter.capture_lost_s = std::clamp(s.meter.capture_lost_s, 5.0, 600.0);
    s.meter.kill_grace_s = std::clamp(s.meter.kill_grace_s, 0.0, 30.0);
    s.meter.min_saved_duration_s = std::clamp(s.meter.min_saved_duration_s, 0.0, 3600.0);
    s.meter.partial_damage_ratio = std::clamp(s.meter.partial_damage_ratio, 0.0, 1.0);
    s.meter.memory_history = std::clamp(s.meter.memory_history, 1, 100);
    s.history.max_fights = std::clamp(s.history.max_fights, 1, 100000);
}

config::JsonValue settings_to_json(const AppSettings& s) {
    config::JsonValue root;
    config::JsonValue capture;
    config::write_keys<CaptureSettings>(kCaptureKeys, s.capture, capture);
    capture["game_process"] = s.capture.game_process;
    root[kCapture] = std::move(capture);
    config::JsonValue overlay;
    config::write_keys<OverlaySettings>(kOverlayKeys, s.overlay, overlay);
    root[kOverlay] = std::move(overlay);
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
    config::read_keys<CaptureSettings>(kCaptureKeys, json[kCapture], s.capture);
    if (json[kCapture]["game_process"].is_string()) s.capture.game_process = json[kCapture]["game_process"].as_string();
    config::read_keys<OverlaySettings>(kOverlayKeys, json[kOverlay], s.overlay);
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
