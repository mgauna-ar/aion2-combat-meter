#pragma once

namespace aion2::os {

/// Start with Windows: the meter's value under HKCU's
/// Software\Microsoft\Windows\CurrentVersion\Run, which is its only record. The
/// mock keeps it in memory.
[[nodiscard]] bool auto_start_enabled();

/// Writes the value (the running executable, quoted) or deletes it. False when the
/// registry refused.
bool set_auto_start(bool enabled);

} // namespace aion2::os
