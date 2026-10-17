#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace aion2::os {

/// Starts `exe` with `args`, in the executable's folder, and does not wait for it.
/// False when it could not be started, and always on the mock. Only ever our own
/// executable: the updater's relaunch.
[[nodiscard]] bool launch_detached(const std::filesystem::path& exe, const std::vector<std::string>& args);

} // namespace aion2::os
