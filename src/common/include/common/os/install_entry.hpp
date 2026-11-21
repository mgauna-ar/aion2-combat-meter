#pragma once

#include <string_view>

namespace aion2::os {

/// The installer's entry among Windows' installed apps: HKCU's
/// Software\Microsoft\Windows\CurrentVersion\Uninstall\AION2CombatMeter
/// (packaging/windows/installer.nsi). Update & restart swaps the exe without the
/// installer, so the entry's DisplayVersion would keep the version first installed.
///
/// Sets it to `version` when the entry is this copy's, its InstallLocation the running
/// exe's folder. A copy from the zip has no entry, and a copy elsewhere leaves the
/// installed one's alone. True when the entry now shows `version`; the mock has no
/// entry and returns false.
bool refresh_install_entry(std::string_view version);

} // namespace aion2::os
