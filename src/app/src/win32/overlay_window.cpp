#include "overlay_window.hpp"

#include "common/os/logger.hpp"

#include <dwmapi.h>
#include <dxgi.h>
#include <windowsx.h>

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace aion2::app::win32 {

namespace {

constexpr const wchar_t* kWindowClass = L"Aion2CombatMeterOverlay";
constexpr int kDefaultX = 40;
constexpr int kDefaultY = 160;
constexpr int kMinWidth = 200;
constexpr int kMinHeight = 90;

LRESULT CALLBACK overlay_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    auto* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) return DefWindowProcW(hwnd, msg, wparam, lparam);
    return self->handle_message(hwnd, msg, wparam, lparam);
}

DWORD ex_style(bool locked) {
    DWORD ex = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED;
    if (locked) ex |= WS_EX_TRANSPARENT;
    return ex;
}

/// The saved position if any monitor still shows its top-left corner, else the
/// default. Only -1, -1 means unset: a monitor left of or above the main one has
/// negative coordinates.
POINT start_position(const OverlaySettings& s) {
    POINT pt{s.x, s.y};
    if ((s.x == -1 && s.y == -1) || MonitorFromPoint(pt, MONITOR_DEFAULTTONULL) == nullptr) {
        pt = POINT{kDefaultX, kDefaultY};
    }
    return pt;
}

} // namespace

OverlayWindow::~OverlayWindow() { destroy(); }

bool OverlayWindow::create(const OverlaySettings& settings, std::string& error) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = overlay_wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(kIconResource));
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    m_locked = settings.locked;
    m_visible = settings.visible;
    const POINT pos = start_position(settings);
    m_hwnd = CreateWindowExW(ex_style(m_locked), kWindowClass, L"AION 2 Combat Meter", WS_POPUP, pos.x, pos.y,
                             std::max(settings.width, kMinWidth), std::max(settings.height, kMinHeight), nullptr,
                             nullptr, inst, this);
    if (m_hwnd == nullptr) {
        error = "cannot create the overlay window";
        return false;
    }

    // Full layer alpha, then let DWM use the swap chain's per-pixel alpha.
    SetLayeredWindowAttributes(m_hwnd, RGB(0, 0, 0), 255, LWA_ALPHA);
    const MARGINS margins{-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(m_hwnd, &margins);

    if (!create_device()) {
        error = "cannot create a Direct3D 11 device for the overlay";
        destroy();
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // the meter keeps its own geometry in config.json
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_device.Get(), m_context.Get());
    m_imgui_ready = true;
    rebuild_fonts(settings.scale * dpi_scale());

    register_hotkeys();
    ShowWindow(m_hwnd, m_visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    return true;
}

void OverlayWindow::destroy() {
    if (m_imgui_ready) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_imgui_ready = false;
    }
    m_render_target.Reset();
    m_swap_chain.Reset();
    m_context.Reset();
    m_device.Reset();
    if (m_hwnd != nullptr) {
        for (const Hotkey h : {Hotkey::ToggleLock, Hotkey::ToggleVisible, Hotkey::EndFight}) {
            UnregisterHotKey(m_hwnd, static_cast<int>(h));
        }
        SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr));
    }
}

bool OverlayWindow::create_device() {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = m_hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    // The blt model: DWM composes its alpha with the extended frame, which a flip
    // model swap chain on a plain HWND does not.
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &sd, m_swap_chain.ReleaseAndGetAddressOf(),
                                               m_device.ReleaseAndGetAddressOf(), &got,
                                               m_context.ReleaseAndGetAddressOf());
    if (hr == DXGI_ERROR_UNSUPPORTED) {
        // No usable GPU (a VM, a remote session): software rendering is plenty here.
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                           &sd, m_swap_chain.ReleaseAndGetAddressOf(),
                                           m_device.ReleaseAndGetAddressOf(), &got,
                                           m_context.ReleaseAndGetAddressOf());
    }
    if (FAILED(hr)) return false;
    // The overlay never goes fullscreen; keep DXGI's Alt+Enter away from it.
    Microsoft::WRL::ComPtr<IDXGIFactory> factory;
    if (SUCCEEDED(m_swap_chain->GetParent(IID_PPV_ARGS(factory.GetAddressOf())))) {
        factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);
    }
    create_render_target();
    return true;
}

void OverlayWindow::create_render_target() {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer;
    if (SUCCEEDED(m_swap_chain->GetBuffer(0, IID_PPV_ARGS(back_buffer.GetAddressOf())))) {
        m_device->CreateRenderTargetView(back_buffer.Get(), nullptr, m_render_target.ReleaseAndGetAddressOf());
    }
}

void OverlayWindow::rebuild_fonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    char windows_dir[MAX_PATH] = {};
    const UINT len = GetWindowsDirectoryA(windows_dir, MAX_PATH);
    const std::string font = (len > 0 && len < MAX_PATH ? std::string(windows_dir) : std::string("C:\\Windows")) +
                             "\\Fonts\\segoeui.ttf";
    ImFont* loaded = nullptr;
    if (GetFileAttributesA(font.c_str()) != INVALID_FILE_ATTRIBUTES) {
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        loaded = io.Fonts->AddFontFromFileTTF(font.c_str(), std::round(kBaseFontSize * scale), &cfg);
    }
    if (loaded == nullptr) {
        io.Fonts->AddFontDefault();
        io.FontGlobalScale = scale;
    } else {
        io.FontGlobalScale = 1.0f;
    }
    // The atlas texture is rebuilt by the next NewFrame.
    ImGui_ImplDX11_InvalidateDeviceObjects();

    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    style.ScaleAllSizes(scale);
    ImGui::GetStyle() = style;
    m_font_scale = scale;
}

void OverlayWindow::register_hotkeys() {
    const struct {
        Hotkey id;
        UINT key;
        const char* name;
    } keys[] = {
        {Hotkey::ToggleLock, 'L', "Ctrl+Alt+L"},
        {Hotkey::ToggleVisible, 'H', "Ctrl+Alt+H"},
        {Hotkey::EndFight, 'R', "Ctrl+Alt+R"},
    };
    for (const auto& k : keys) {
        if (!RegisterHotKey(m_hwnd, static_cast<int>(k.id), MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, k.key)) {
            os::Logger::warn(std::string("hotkey ") + k.name + " is taken by another program");
        }
    }
}

void OverlayWindow::set_locked(bool locked) {
    m_locked = locked;
    if (m_hwnd == nullptr) return;
    // A click-through window gets no more mouse messages, so ImGui would keep the
    // last position and its hover (a tooltip) for good.
    if (locked && m_imgui_ready) ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, static_cast<LONG_PTR>(ex_style(locked)));
    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
}

void OverlayWindow::set_visible(bool visible) {
    m_visible = visible;
    if (m_hwnd != nullptr) ShowWindow(m_hwnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
}

void OverlayWindow::keep_on_top() {
    if (m_hwnd == nullptr || !m_visible) return;
    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

RECT OverlayWindow::screen_rect() const {
    RECT r{};
    if (m_hwnd != nullptr) GetWindowRect(m_hwnd, &r);
    return r;
}

float OverlayWindow::dpi_scale() const {
    const UINT dpi = m_hwnd != nullptr ? GetDpiForWindow(m_hwnd) : 96;
    return dpi == 0 ? 1.0f : static_cast<float>(dpi) / 96.0f;
}

OverlayActions OverlayWindow::render(OverlayFrameInput input, OverlayViewState& state) {
    if (!m_imgui_ready) return {};
    if (m_resize_width != 0 && m_resize_height != 0) {
        m_render_target.Reset();
        m_swap_chain->ResizeBuffers(0, m_resize_width, m_resize_height, DXGI_FORMAT_UNKNOWN, 0);
        m_resize_width = m_resize_height = 0;
        create_render_target();
    }
    // A failed resize (the device was lost) leaves none: try again next frame.
    if (!m_render_target) {
        create_render_target();
        if (!m_render_target) return {};
    }
    const float scale = input.scale * dpi_scale();
    if (std::fabs(scale - m_font_scale) > 0.01f) rebuild_fonts(scale);
    input.scale = scale;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    const OverlayActions actions = draw_overlay(input, state);
    m_drag_area = actions.drag_area;
    m_resize_grip = actions.resize_grip;
    ImGui::Render();

    const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    m_context->OMSetRenderTargets(1, m_render_target.GetAddressOf(), nullptr);
    m_context->ClearRenderTargetView(m_render_target.Get(), clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    m_swap_chain->Present(1, 0);
    return actions;
}

LRESULT OverlayWindow::handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (m_imgui_ready && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) return 1;

    switch (msg) {
        case WM_NCHITTEST: {
            if (m_locked) return HTTRANSPARENT;
            POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(hwnd, &pt);
            const auto x = static_cast<float>(pt.x);
            const auto y = static_cast<float>(pt.y);
            if (m_resize_grip.contains(x, y)) return HTBOTTOMRIGHT;
            if (m_drag_area.contains(x, y)) return HTCAPTION;
            return HTCLIENT;
        }
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED) {
                m_resize_width = LOWORD(lparam);
                m_resize_height = HIWORD(lparam);
            }
            return 0;
        case WM_EXITSIZEMOVE:
            if (on_geometry_changed) on_geometry_changed();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize.x = static_cast<LONG>(kMinWidth * dpi_scale());
            info->ptMinTrackSize.y = static_cast<LONG>(kMinHeight * dpi_scale());
            return 0;
        }
        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_HOTKEY:
            if (on_hotkey) on_hotkey(static_cast<Hotkey>(wparam));
            return 0;
        case WM_CLOSE:
            set_visible(false);  // the tray's Exit quits; closing only hides
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SYSCOMMAND:
            if ((wparam & 0xFFF0) == SC_KEYMENU) return 0;
            break;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace aion2::app::win32
