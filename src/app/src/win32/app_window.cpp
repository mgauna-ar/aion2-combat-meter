#include "app_window.hpp"

#include "app/ui/theme.hpp"

#include <dxgi.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace aion2::app::win32 {

namespace {

LRESULT CALLBACK app_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    auto* self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) return DefWindowProcW(hwnd, msg, wparam, lparam);
    return self->handle_message(hwnd, msg, wparam, lparam);
}

std::string windows_font(const char* file) {
    char dir[MAX_PATH] = {};
    const UINT len = GetWindowsDirectoryA(dir, MAX_PATH);
    const std::string root = len > 0 && len < MAX_PATH ? std::string(dir) : std::string("C:\\Windows");
    const std::string path = root + "\\Fonts\\" + file;
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES ? path : std::string();
}

/// The saved rectangle if a monitor still shows its top-left corner; else the
/// default size, centred on the main monitor. Only -1, -1 means unset.
RECT start_rect(const WindowSettings& s) {
    const POINT corner{s.x, s.y};
    if (!(s.x == -1 && s.y == -1) && MonitorFromPoint(corner, MONITOR_DEFAULTTONULL) != nullptr) {
        return RECT{s.x, s.y, s.x + s.width, s.y + s.height};
    }
    const UINT dpi = GetDpiForSystem();
    const int w = MulDiv(static_cast<int>(ui::metrics::WindowDefaultW), static_cast<int>(dpi), 96);
    const int h = MulDiv(static_cast<int>(ui::metrics::WindowDefaultH), static_cast<int>(dpi), 96);
    const int x = std::max(0, (GetSystemMetrics(SM_CXSCREEN) - w) / 2);
    const int y = std::max(0, (GetSystemMetrics(SM_CYSCREEN) - h) / 2);
    return RECT{x, y, x + w, y + h};
}

} // namespace

AppWindow::~AppWindow() { destroy(); }

bool AppWindow::create(const WindowSettings& settings, int icon_resource, std::string& error) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = app_wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(icon_resource));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    const RECT r = start_rect(settings);
    m_hwnd = CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, L"AION 2 Combat Meter", WS_OVERLAPPEDWINDOW, r.left, r.top,
                             r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, this);
    if (m_hwnd == nullptr) {
        error = "cannot create the app window";
        return false;
    }
    if (!create_device()) {
        error = "cannot create a Direct3D 11 device for the app window";
        destroy();
        return false;
    }

    // Its own context: the overlay keeps the one it made, and each window makes its
    // own current before touching ImGui.
    ImGuiContext* previous = ImGui::GetCurrentContext();
    m_context_imgui = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_context_imgui);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // the meter keeps its own geometry in config.json
    io.LogFilename = nullptr;
    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_device.Get(), m_context.Get());
    rebuild_fonts();
    ImGui::SetCurrentContext(previous);
    return true;
}

void AppWindow::destroy() {
    if (m_context_imgui != nullptr) {
        ImGuiContext* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(m_context_imgui);
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(m_context_imgui);
        ImGui::SetCurrentContext(previous == m_context_imgui ? nullptr : previous);
        m_context_imgui = nullptr;
    }
    m_render_target.Reset();
    m_swap_chain.Reset();
    m_context.Reset();
    m_device.Reset();
    if (m_hwnd != nullptr) {
        SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr));
    }
}

bool AppWindow::create_device() {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = m_hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
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
    Microsoft::WRL::ComPtr<IDXGIFactory> factory;
    if (SUCCEEDED(m_swap_chain->GetParent(IID_PPV_ARGS(factory.GetAddressOf())))) {
        factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);
    }
    create_render_target();
    return true;
}

void AppWindow::create_render_target() {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer;
    if (SUCCEEDED(m_swap_chain->GetBuffer(0, IID_PPV_ARGS(back_buffer.GetAddressOf())))) {
        m_device->CreateRenderTargetView(back_buffer.Get(), nullptr, m_render_target.ReleaseAndGetAddressOf());
    }
}

float AppWindow::dpi_scale() const {
    const UINT dpi = m_hwnd != nullptr ? GetDpiForWindow(m_hwnd) : 96;
    return dpi == 0 ? 1.0f : static_cast<float>(dpi) / 96.0f;
}

void AppWindow::rebuild_fonts() {
    // Called with this window's context current.
    ImGui::GetIO().Fonts->Clear();
    ImGui::GetStyle() = ImGuiStyle();
    m_font_scale = dpi_scale();
    ui::setup_fonts_and_theme(windows_font("segoeui.ttf"), windows_font("segoeuib.ttf"), m_font_scale);
    // The atlas texture is rebuilt by the next NewFrame.
    ImGui_ImplDX11_InvalidateDeviceObjects();
}

void AppWindow::show() {
    if (m_hwnd == nullptr) return;
    ShowWindow(m_hwnd, IsIconic(m_hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(m_hwnd);
}

void AppWindow::hide() {
    if (m_hwnd != nullptr) ShowWindow(m_hwnd, SW_HIDE);
}

bool AppWindow::visible() const { return m_hwnd != nullptr && IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd); }

RECT AppWindow::screen_rect() const {
    RECT r{};
    if (m_hwnd != nullptr) GetWindowRect(m_hwnd, &r);
    return r;
}

AppActions AppWindow::render(const AppFrameInput& input, AppViewState& state) {
    if (m_context_imgui == nullptr || !visible()) return {};
    ImGui::SetCurrentContext(m_context_imgui);
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
    if (std::fabs(dpi_scale() - m_font_scale) > 0.01f) rebuild_fonts();

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    const AppActions actions = draw_app(input, state);
    ImGui::Render();

    const uint32_t canvas = ui::colors::Canvas;
    const float clear[4] = {static_cast<float>(canvas & 0xFF) / 255.0f, static_cast<float>((canvas >> 8) & 0xFF) / 255.0f,
                            static_cast<float>((canvas >> 16) & 0xFF) / 255.0f, 1.0f};
    m_context->OMSetRenderTargets(1, m_render_target.GetAddressOf(), nullptr);
    m_context->ClearRenderTargetView(m_render_target.Get(), clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    // No vsync wait: the frame loop paces itself, and the overlay already waits.
    m_swap_chain->Present(0, 0);
    return actions;
}

LRESULT AppWindow::handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (m_context_imgui != nullptr) {
        ImGui::SetCurrentContext(m_context_imgui);
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) return 1;
    }
    switch (msg) {
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
            const float s = dpi_scale();
            info->ptMinTrackSize.x = static_cast<LONG>(ui::metrics::WindowMinW * s);
            info->ptMinTrackSize.y = static_cast<LONG>(ui::metrics::WindowMinH * s);
            return 0;
        }
        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_CLOSE:
            if (on_close) {
                on_close();
            } else {
                hide();
            }
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
