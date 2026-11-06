#pragma once

#include "common/micros.hpp"
#include "meter/history_store.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace aion2::meter {

/// One saved fight in a visit: where it is in the history, and its number there.
struct PullRef {
    /// Into the entries given to group_by_visit().
    size_t index{0};
    /// 1 for the visit's first fight.
    int number{0};
};

/// The fights of one map visit: entering a map, or starting the meter, until the next.
struct PullGroup {
    uint32_t map_id{0};
    Micros visit_ts{0};
    /// Newest first.
    std::vector<PullRef> pulls;
};

/// The history by map visit, newest visit first. `entries` are oldest first, as
/// HistoryStore::load_index() returns them. A version 1 record has no visit, so
/// consecutive ones on the same map stand in for one.
[[nodiscard]] std::vector<PullGroup> group_by_visit(const std::vector<HistoryEntry>& entries);

} // namespace aion2::meter
