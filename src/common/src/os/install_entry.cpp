#include "common/os/install_entry.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "common/os/paths.hpp"

#include <filesystem>
#include <string>
#endif

namespace aion2::os {

#ifdef _WIN32

namespace {

constexpr const wchar_t* kUninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AION2CombatMeter";

/// A REG_SZ value, or empty when it is missing or of another type.
std::wstring read_string(HKEY key, const wchar_t* name) {
    DWORD size = 0;
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS || size == 0) {
        return {};
    }
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, value.data(), &size) != ERROR_SUCCESS) return {};
    // RegGetValueW counts the terminating null.
    if (const size_t end = value.find(L'\0'); end != std::wstring::npos) value.resize(end);
    return value;
}

} // namespace

bool refresh_install_entry(std::string_view version) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kUninstallKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    bool ok = false;
    const std::wstring location = read_string(key, L"InstallLocation");
    const std::filesystem::path exe_dir = executable_dir();
    std::error_code ec;
    // The same folder however it is spelt: case, a trailing separator, a short name.
    if (!location.empty() && !exe_dir.empty() && std::filesystem::equivalent(location, exe_dir, ec) && !ec) {
        const std::wstring wanted = to_wide(version);
        if (read_string(key, L"DisplayVersion") == wanted) {
            ok = true;
        } else {
            ok = RegSetValueExW(key, L"DisplayVersion", 0, REG_SZ, reinterpret_cast<const BYTE*>(wanted.c_str()),
                                static_cast<DWORD>((wanted.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
        }
    }
    RegCloseKey(key);
    return ok;
}

#else

bool refresh_install_entry(std::string_view) { return false; }

#endif

} // namespace aion2::os
