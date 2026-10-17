#include "meter/pull_grouping.hpp"

#include <algorithm>

namespace aion2::meter {

std::vector<PullGroup> group_by_visit(const std::vector<HistoryEntry>& entries) {
    std::vector<PullGroup> groups;
    for (size_t i = 0; i < entries.size(); ++i) {
        const HistoryEntry& e = entries[i];
        const bool same_visit = !groups.empty() && groups.back().visit_ts == e.visit_ts &&
                                (e.visit_ts != 0 || groups.back().map_id == e.map_id);
        if (!same_visit) {
            PullGroup g;
            g.map_id = e.map_id;
            g.visit_ts = e.visit_ts;
            groups.push_back(std::move(g));
        }
        PullGroup& g = groups.back();
        g.pulls.push_back(PullRef{i, static_cast<int>(g.pulls.size()) + 1});
    }
    for (auto& g : groups) std::reverse(g.pulls.begin(), g.pulls.end());
    std::reverse(groups.begin(), groups.end());
    return groups;
}

} // namespace aion2::meter
