#include "common/os/tray_icon.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include "common/os/paths.hpp"
#include "common/os/single_instance.hpp"

namespace aion2::os {

namespace {

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTrayId = 1;
constexpr const wchar_t* kTrayClass = SingleInstance::ACTIVATION_WINDOW_CLASS;

TrayIcon* tray_of(HWND hwnd) { return reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)); }

LRESULT CALLBACK tray_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    TrayIcon* tray = tray_of(hwnd);
    if (tray == nullptr) return DefWindowProcW(hwnd, msg, wparam, lparam);

    if (tray->activation_message() != 0 && msg == tray->activation_message()) {
        tray->handle_activate();
        return 0;
    }
    if (msg == kTrayMessage) {
        switch (LOWORD(lparam)) {
            case WM_LBUTTONUP:
                tray->handle_activate();
                return 0;
            case WM_RBUTTONUP:
            case WM_CONTEXTMENU:
                tray->show_menu();
                return 0;
            default:
                break;
        }
    } else if (msg == WM_COMMAND) {
        tray->handle_command(LOWORD(wparam));
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void copy_wide(wchar_t* dst, size_t capacity, const std::string& utf8) {
    const std::wstring w = to_wide(utf8);
    wcsncpy_s(dst, capacity, w.c_str(), _TRUNCATE);
}

/// The build number Windows really is: GetVersionEx is deprecated and can lie.
DWORD windows_build() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) return 0;
    const auto get_version = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    return get_version != nullptr && get_version(&info) == 0 ? info.dwBuildNumber : 0;
}

/// Popup menus are light unless the app asks for dark ones, as the app is dark
/// whatever the system's theme (its title bar too). The only way to ask is uxtheme's
/// SetPreferredAppMode, which Windows exports by ordinal alone: 135, with
/// FlushMenuThemes at 136, since Windows 10 1903 (build 18362). Widely used, and
/// stable since. Before that build 135 was something else, so it is not called.
/// The setting is the process's: the app window's system menu turns dark as well.
void prefer_dark_menus() {
    if (windows_build() < 18362) return;
    // Kept loaded: the preference lives in it.
    const HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (uxtheme == nullptr) return;
    using SetPreferredAppModeFn = int(WINAPI*)(int);
    using FlushMenuThemesFn = void(WINAPI*)();
    const auto set_mode = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
    const auto flush = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
    if (set_mode == nullptr) return;
    constexpr int kForceDark = 2;  // 0 default, 1 follow the system, 2 dark, 3 light
    set_mode(kForceDark);
    if (flush != nullptr) flush();
}

} // namespace

TrayIcon::~TrayIcon() { destroy(); }

bool TrayIcon::create(uint32_t icon_resource, uint32_t activation_message) {
    if (m_created) return true;
    m_activation_message = activation_message;
    prefer_dark_menus();
    HINSTANCE inst = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = tray_wnd_proc;
    wc.hInstance = inst;
    wc.lpszClassName = kTrayClass;
    RegisterClassExW(&wc);

    // A hidden top-level window rather than a message-only one: a second instance
    // finds it by class with FindWindowW, which sees top-level windows only.
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kTrayClass, L"AION 2 Combat Meter", WS_POPUP, 0, 0, 0, 0,
                                nullptr, nullptr, inst, this);
    if (hwnd == nullptr) return false;
    m_hwnd = hwnd;

    HICON icon = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(icon_resource), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                               LR_DEFAULTCOLOR));
    if (icon == nullptr) icon = LoadIconW(nullptr, IDI_APPLICATION);

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = kTrayMessage;
    nid.hIcon = icon;
    copy_wide(nid.szTip, ARRAYSIZE(nid.szTip), m_tooltip.empty() ? "AION 2 Combat Meter" : m_tooltip);
    if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
        DestroyWindow(hwnd);
        m_hwnd = nullptr;
        return false;
    }
    m_created = true;
    return true;
}

void TrayIcon::destroy() {
    if (!m_created) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = static_cast<HWND>(m_hwnd);
    nid.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    SetWindowLongPtrW(static_cast<HWND>(m_hwnd), GWLP_USERDATA, 0);
    DestroyWindow(static_cast<HWND>(m_hwnd));
    UnregisterClassW(kTrayClass, GetModuleHandleW(nullptr));
    m_hwnd = nullptr;
    m_created = false;
}

void TrayIcon::set_tooltip(const std::string& text) {
    if (text == m_tooltip) return;
    m_tooltip = text;
    if (!m_created) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = static_cast<HWND>(m_hwnd);
    nid.uID = kTrayId;
    nid.uFlags = NIF_TIP;
    copy_wide(nid.szTip, ARRAYSIZE(nid.szTip), text);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayIcon::notify(const std::string& title, const std::string& message) {
    if (!m_created) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = static_cast<HWND>(m_hwnd);
    nid.uID = kTrayId;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    copy_wide(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), title);
    copy_wide(nid.szInfo, ARRAYSIZE(nid.szInfo), message);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayIcon::show_menu() {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;
    for (const TrayMenuItem& item : this->menu()) {
        if (item.separator) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            continue;
        }
        UINT flags = MF_STRING;
        if (item.checked) flags |= MF_CHECKED;
        if (!item.enabled) flags |= MF_GRAYED;
        AppendMenuW(menu, flags, item.id, to_wide(item.label).c_str());
    }
    POINT pt;
    GetCursorPos(&pt);
    // The documented dance so the menu closes when the user clicks elsewhere.
    SetForegroundWindow(static_cast<HWND>(m_hwnd));
    TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, static_cast<HWND>(m_hwnd), nullptr);
    PostMessageW(static_cast<HWND>(m_hwnd), WM_NULL, 0, 0);
    DestroyMenu(menu);
}

} // namespace aion2::os

#else // !_WIN32

namespace aion2::os {

TrayIcon::~TrayIcon() { destroy(); }

bool TrayIcon::create(uint32_t, uint32_t activation_message) {
    m_activation_message = activation_message;
    m_created = true;
    return true;
}

void TrayIcon::destroy() { m_created = false; }

void TrayIcon::set_tooltip(const std::string& text) { m_tooltip = text; }

void TrayIcon::notify(const std::string&, const std::string&) {}

void TrayIcon::show_menu() {}

} // namespace aion2::os

#endif
