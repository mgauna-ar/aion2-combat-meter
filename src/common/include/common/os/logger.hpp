#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <cstdint>

namespace aion2::os {

enum class LogLevel : uint8_t {
    Debug,
    Info,
    Warning,
    Error
};

/**
 * @brief Thread-safe diagnostic file logger with session rotation (meter.log / meter.prev.log).
 *
 * Logs are shared when people ask for help, so callers log entity ids, never
 * character names.
 */
class Logger {
public:
    /// Paths are std::filesystem::path end to end, so a user folder outside the
    /// ANSI code page is opened as itself on Windows.
    static bool init(const std::filesystem::path& custom_path = {}, bool rotate = true);
    static void log(LogLevel level, std::string_view message);

    static void debug(std::string_view message) { log(LogLevel::Debug, message); }
    static void info(std::string_view message)  { log(LogLevel::Info, message); }
    static void warn(std::string_view message)  { log(LogLevel::Warning, message); }
    static void error(std::string_view message) { log(LogLevel::Error, message); }

    [[nodiscard]] static std::filesystem::path log_file_path();
    [[nodiscard]] static std::filesystem::path previous_log_path(const std::filesystem::path& current_path = {});
    /// meter.log in app_data_dir().
    [[nodiscard]] static std::filesystem::path default_log_path();

    static void shutdown();
};

} // namespace aion2::os
