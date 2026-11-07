#include "meter/death_log.hpp"

namespace aion2::meter {

std::string_view recap_kind_name(RecapKind kind) noexcept {
    switch (kind) {
        case RecapKind::Hit: return "hit";
        case RecapKind::Dot: return "dot";
        case RecapKind::Heal: return "heal";
        case RecapKind::HealTick: return "hot";
    }
    return "hit";
}

RecapKind recap_kind_from_name(std::string_view name) noexcept {
    if (name == "dot") return RecapKind::Dot;
    if (name == "heal") return RecapKind::Heal;
    if (name == "hot") return RecapKind::HealTick;
    return RecapKind::Hit;
}

const RecapEvent* DeathRecord::killing_blow() const noexcept {
    for (auto it = recap.rbegin(); it != recap.rend(); ++it) {
        if (it->damage()) return &*it;
    }
    return nullptr;
}

std::vector<RecapSample> recap_before(const RecapRing& ring, Micros death) {
    const Micros from = death > kRecapWindow ? death - kRecapWindow : 0;
    std::vector<RecapSample> eligible;
    for (size_t i = 0; i < ring.size(); ++i) {
        const RecapSample& s = ring.at(i);
        if (s.ts <= death && s.ts >= from) eligible.push_back(s);
    }
    // The newest ones, still oldest first.
    if (eligible.size() > kRecapShown) {
        eligible.erase(eligible.begin(), eligible.end() - static_cast<std::ptrdiff_t>(kRecapShown));
    }
    return eligible;
}

} // namespace aion2::meter
