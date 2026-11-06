#pragma once

// Windows only: the overlay's own top-level window and the DX11 device it draws
// with. Built by CMake on Windows; the Makefile never compiles src/app/src/win32.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include "app/app_settings.hpp"
#include "app/overlay_view.hpp"

#include <functional>
#include <string>

struct ImGuiContext;

namespace aion2::app::win32 {

/// The global hotkeys, registered on the overlay window.
enum class Hotkey : int {
    ToggleLock = 1,
    ToggleVisible = 2,
    EndFight = 3,
};

/// Where an overlay window starts, and how: from OverlaySettings or AuraSettings.
struct OverlayPlacement {
    bool locked{false};
    bool visible{true};
    float scale{1.0f};
    /// Screen position and size; x and y of -1 mean the default spot.
    int x{-1};
    int y{-1};
    int width{430};
    int height{250};
    /// Where it goes with no saved position, and on reset_position.
    int default_x{40};
    int default_y{160};
};

[[nodiscard]] OverlayPlacement placement_of(const OverlaySettings& s);
/// Under the meter overlay's default spot.
[[nodiscard]] OverlayPlacement placement_of(const AuraSettings& s);

/**
 * @brief A transparent, always-on-top, non-activating window for the meter.
 *
 * WS_EX_LAYERED with full layer alpha and DwmExtendFrameIntoClientArea over the
 * whole client area, so DWM composes the swap chain's own alpha: whatever ImGui
 * leaves at alpha 0 is see-through. WS_EX_NOACTIVATE keeps keyboard focus with
 * the game when the overlay is clicked; WS_EX_TRANSPARENT, set while locked, lets
 * clicks through to the game. Moving and resizing are Windows' own, through the
 * hit test: the header's empty space answers HTCAPTION and the corner grip
 * HTBOTTOMRIGHT. It is a separate window: nothing is drawn inside the game, which
 * needs borderless windowed mode for the overlay to show on top of it. It has its
 * own ImGui context, made current before every message and frame, beside the app
 * window's. The meter overlay and the aura overlay are two of these; the meter's
 * holds the global hotkeys.
 */
class OverlayWindow {
public:
    static constexpr int kIconResource = 101;
    static constexpr float kBaseFontSize = 15.0f;

    OverlayWindow() = default;
    ~OverlayWindow();
    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    /// `title` is the window's caption, which only window tools show. With `hotkeys`
    /// it registers Ctrl+Alt+L, H and R, which only one window may.
    bool create(const OverlayPlacement& placement, const wchar_t* title, bool hotkeys, std::string& error);
    void destroy();

    void set_locked(bool locked);
    void set_visible(bool visible);
    [[nodiscard]] bool locked() const noexcept { return m_locked; }
    [[nodiscard]] bool visible() const noexcept { return m_visible; }
    /// Re-asserts topmost: a game going fullscreen-borderless can push it down.
    void keep_on_top();
    /// Back to its default spot near the main monitor's top-left corner.
    void reset_position();
    /// The window in screen pixels.
    [[nodiscard]] RECT screen_rect() const;
    /// The monitor's DPI over 96.
    [[nodiscard]] float dpi_scale() const;

    /// Draws one frame with `draw` (draw_overlay or draw_auras), which gets the
    /// settings' `scale` times the monitor's DPI scale.
    OverlayActions render(float scale, const std::function<OverlayActions(float scale)>& draw);

    std::function<void(Hotkey)> on_hotkey;
    /// The user finished moving or resizing the window.
    std::function<void()> on_geometry_changed;

    /// The window procedure's body.
    LRESULT handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

private:
    bool create_device();
    void create_render_target();
    void rebuild_fonts(float scale);
    void register_hotkeys();

    HWND m_hwnd{nullptr};
    ImGuiContext* m_imgui{nullptr};
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_swap_chain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_render_target;
    bool m_locked{false};
    bool m_visible{true};
    bool m_hotkeys{false};
    POINT m_default{40, 160};
    float m_font_scale{0.0f};
    UINT m_resize_width{0};
    UINT m_resize_height{0};
    ClientRect m_drag_area;
    ClientRect m_resize_grip;
};

} // namespace aion2::app::win32
