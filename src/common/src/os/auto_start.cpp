#include "common/os/auto_start.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#else
#include <atomic>
#endif

namespace aion2::os {

#ifdef _WIN32

namespace {

constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kValueName = L"AION2CombatMeter";

/// The running executable's full path, however long.
std::wstring executable_path() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (len == 0) return {};
        if (len < path.size()) {
            path.resize(len);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

} // namespace

bool auto_start_enabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0;
    const LONG res = RegQueryValueExW(key, kValueName, nullptr, &type, nullptr, nullptr);
    RegCloseKey(key);
    return res == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ);
}

bool set_auto_start(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        return false;
    }
    bool ok = false;
    if (enabled) {
        const std::wstring exe = executable_path();
        if (!exe.empty()) {
            // Quoted: the folder may hold spaces.
            const std::wstring value = L"\"" + exe + L"\"";
            ok = RegSetValueExW(key, kValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                                static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
        }
    } else {
        const LONG res = RegDeleteValueW(key, kValueName);
        ok = res == ERROR_SUCCESS || res == ERROR_FILE_NOT_FOUND;
    }
    RegCloseKey(key);
    return ok;
}

#else

namespace {
std::atomic<bool> g_mock_enabled{false};
} // namespace

bool auto_start_enabled() { return g_mock_enabled.load(); }

bool set_auto_start(bool enabled) {
    g_mock_enabled = enabled;
    return true;
}

#endif

} // namespace aion2::os
