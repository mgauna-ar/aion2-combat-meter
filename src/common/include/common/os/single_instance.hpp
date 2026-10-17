#pragma once

#include "common/os/unique_handle.hpp"
#include <chrono>
#include <string>
#include <cstdint>

namespace aion2::os {

/**
 * @brief Ensures single instance execution via a named Win32 mutex and wakes existing instance if duplicate.
 */
class SingleInstance {
public:
    static constexpr const char* DEFAULT_MUTEX_NAME = "Aion2CombatMeterSingleInstanceMutex";
    static constexpr const wchar_t* ACTIVATION_MESSAGE_NAME = L"AION2_COMBAT_METER_ACTIVATE";
    /// The running instance's tray window class (TrayIcon): the one window the
    /// activation message goes to.
    static constexpr const wchar_t* ACTIVATION_WINDOW_CLASS = L"Aion2CombatMeterTray";

    explicit SingleInstance(std::string mutex_name = DEFAULT_MUTEX_NAME);
    ~SingleInstance();

    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;
    SingleInstance(SingleInstance&&) noexcept;
    SingleInstance& operator=(SingleInstance&&) noexcept;

    /// Attempts to acquire the single-instance lock. Returns true if this is the primary instance.
    bool try_acquire();

    /// try_acquire() every 100 ms until it succeeds or `timeout` has passed. An
    /// updated meter waits this way for the one that launched it to exit: the mutex
    /// frees when it does, and waiting on the process itself would open a handle to
    /// another process (AGENTS.md, "Passive only").
    bool acquire_within(std::chrono::milliseconds timeout);

    /// Returns whether this instance holds the primary lock.
    [[nodiscard]] bool is_primary() const noexcept { return m_is_primary; }

    /// Posts the activation message to the running instance's tray window, which
    /// opens its window. Posted to that window alone, never broadcast: a broadcast
    /// reaches every top-level window, the game's too.
    void notify_existing_instance();

    /// Releases the mutex handle.
    void release();

    /// Returns the registered Win32 window message ID for instance activation.
    [[nodiscard]] uint32_t activation_message_id() const noexcept { return m_activation_msg_id; }

private:
    std::string m_name;
    UniqueHandle m_handle;
    bool m_is_primary{false};
    uint32_t m_activation_msg_id{0};
};

} // namespace aion2::os
