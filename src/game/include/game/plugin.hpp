#pragma once

#include "common/micros.hpp"
#include "protocol/events.hpp"

namespace aion2::game {

/**
 * @brief A feature the capture feeds: the combat meter, the auras.
 *
 * The pipeline hands every plugin that is switched on each parsed event and the
 * passing capture time, on the engine thread (or the replay tool's only one). A
 * plugin keeps its own record of whatever it needs, and never reads another's:
 * plugins depend on the protocol and the game data, not on each other.
 */
class Plugin {
public:
    virtual ~Plugin() = default;

    virtual void on_event(const proto::ParsedEvent& event) = 0;
    /// Capture time has reached `now`, with or without packets.
    virtual void tick(Micros now) = 0;
};

} // namespace aion2::game
