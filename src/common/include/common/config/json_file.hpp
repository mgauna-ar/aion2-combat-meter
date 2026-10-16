#pragma once

#include "common/config/json.hpp"
#include <filesystem>
#include <optional>
#include <string>

namespace aion2::config {

/// The JSON document in `path`, or nullopt when the file is missing, unreadable or
/// not JSON.
[[nodiscard]] std::optional<JsonValue> read_json_file(const std::filesystem::path& path);

/// Writes `text` to `path` through a temp file next to it and a rename, so a crash
/// or a full disk never leaves a half-written file where a whole one was. Creates
/// the parent folder. Empty on success, else what went wrong.
[[nodiscard]] std::string write_file_atomic(const std::filesystem::path& path, const std::string& text);

/// write_file_atomic of `doc` stringified.
[[nodiscard]] std::string write_json_atomic(const std::filesystem::path& path, const JsonValue& doc);

} // namespace aion2::config
