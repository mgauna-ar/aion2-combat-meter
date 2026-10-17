#pragma once

#include <string>

namespace aion2::app {

/// One tray balloon.
struct TrayNotice {
    std::string title;
    std::string message;
};

} // namespace aion2::app
