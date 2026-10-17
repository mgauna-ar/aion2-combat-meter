#pragma once

// Windows only: the desktop window (history, settings, status) and the DX11 device
// it draws with. Built by CMake on Windows; the Makefile never compiles src/app/src/win32.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include "app/app_frame.hpp"
#include "app/app_settings.hpp"

#include <functional>
#include <string>

struct ImGuiContext;

namespace aion2::app::win32 {

/**
 * @brief An ordinary resizable window around draw_app().
 *
 * It has its own ImGui context and DX11 device beside the overlay's: ImGui keeps
 * its backend data per context, and each window makes its own context current
 * before it handles a message or draws a frame. Closing it asks the owner
 * (on_close), which hides it to the tray or quits.
 */
class AppWindow {
public:
    static constexpr const wchar_t* kWindowClass = L"Aion2CombatMeterApp";

    AppWindow() = default;
    ~AppWindow();
    AppWindow(const AppWindow&) = delete;
    AppWindow& operator=(const AppWindow&) = delete;

    /// Creates the window hidden, at the saved geometry when a monitor still shows it.
    bool create(const WindowSettings& settings, int icon_resource, std::string& error);
    void destroy();

    /// Shows it, restored if minimized, in front.
    void show();
    void hide();
    /// Shown and not minimized: worth drawing.
    [[nodiscard]] bool visible() const;
    /// The window in screen pixels, as create() takes it back. Read it after a move
    /// or resize, not while minimized.
    [[nodiscard]] RECT screen_rect() const;

    /// Draws one frame.
    AppActions render(const AppFrameInput& input, AppViewState& state);

    /// The close button, Alt+F4 or the system menu's Close.
    std::function<void()> on_close;
    /// The user finished moving or resizing the window.
    std::function<void()> on_geometry_changed;

    /// The window procedure's body.
    LRESULT handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

private:
    bool create_device();
    void create_render_target();
    /// Fonts and theme at the window's DPI; again whenever the DPI changes.
    void rebuild_fonts();
    [[nodiscard]] float dpi_scale() const;

    HWND m_hwnd{nullptr};
    ImGuiContext* m_context_imgui{nullptr};
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_swap_chain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_render_target;
    float m_font_scale{0.0f};
    UINT m_resize_width{0};
    UINT m_resize_height{0};
};

} // namespace aion2::app::win32
