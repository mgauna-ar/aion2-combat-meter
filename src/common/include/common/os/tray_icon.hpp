#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace aion2::os {

struct TrayMenuItem {
    uint32_t id{0};
    std::string label;
    bool checked{false};
    bool enabled{true};
    /// A separator line; id and label are ignored.
    bool separator{false};
};

/**
 * @brief The notification-area icon and its right-click menu.
 *
 * The menu is built fresh each time it opens, by `menu_builder`, so check marks
 * always show the current state. Messages arrive through the thread's message
 * loop, which the app runs anyway for the overlay window. A mock off Windows:
 * create() succeeds and handle_command() still dispatches, for tests.
 */
class TrayIcon {
public:
    TrayIcon() = default;
    ~TrayIcon();
    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    /// `icon_resource` is the ICON id in app.rc. `activation_message` is the
    /// registered message a second instance posts to this icon's window; it acts as
    /// a click.
    bool create(uint32_t icon_resource, uint32_t activation_message = 0);
    void destroy();

    /// Re-drawn only when the text changes, so it can be called every frame.
    void set_tooltip(const std::string& text);
    void notify(const std::string& title, const std::string& message);

    void set_menu_builder(std::function<std::vector<TrayMenuItem>()> fn) { m_menu_builder = std::move(fn); }
    void set_on_command(std::function<void(uint32_t)> fn) { m_on_command = std::move(fn); }
    /// A left click, or a second instance starting.
    void set_on_activate(std::function<void()> fn) { m_on_activate = std::move(fn); }

    void handle_command(uint32_t id) {
        if (m_on_command) m_on_command(id);
    }
    void handle_activate() {
        if (m_on_activate) m_on_activate();
    }
    [[nodiscard]] std::vector<TrayMenuItem> menu() const {
        return m_menu_builder ? m_menu_builder() : std::vector<TrayMenuItem>{};
    }
    /// Pops the menu up at the cursor; the icon's window procedure calls it on a
    /// right click.
    void show_menu();
    [[nodiscard]] uint32_t activation_message() const noexcept { return m_activation_message; }

private:
    /// The icon's hidden window: an HWND, as void* so this header needs no <windows.h>.
    [[maybe_unused]] void* m_hwnd{nullptr};
    uint32_t m_activation_message{0};
    std::string m_tooltip;
    bool m_created{false};
    std::function<std::vector<TrayMenuItem>()> m_menu_builder;
    std::function<void(uint32_t)> m_on_command;
    std::function<void()> m_on_activate;
};

} // namespace aion2::os
