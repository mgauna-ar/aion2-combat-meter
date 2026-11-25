#include "app/history_list.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string_view>
#include <unordered_set>

namespace aion2::app {

namespace {

/// Where `file` sits in `order`; -1 when it is not there (or empty).
[[nodiscard]] std::ptrdiff_t position(const std::vector<std::string>& order, const std::string& file) {
    if (file.empty()) return -1;
    const auto it = std::find(order.begin(), order.end(), file);
    return it == order.end() ? -1 : std::distance(order.begin(), it);
}

/// "Its file goes too." for one, "Their files go too." for more.
[[nodiscard]] std::string files_go(size_t n) { return n == 1 ? "Its file goes too." : "Their files go too."; }

/// " The one you keep stays", " The 2 you keep stay": the kept fights a delete spares.
[[nodiscard]] std::string kept_stay(size_t kept) {
    if (kept == 0) return {};
    return kept == 1 ? " The one you keep stays" : " The " + std::to_string(kept) + " you keep stay";
}

} // namespace

std::vector<std::string> list_order(const std::vector<meter::HistoryEntry>& history,
                                    const std::vector<meter::PullGroup>& groups) {
    std::vector<std::string> order;
    order.reserve(history.size());
    for (const auto& g : groups) {
        for (const auto& p : g.pulls) {
            if (p.index < history.size()) order.push_back(history[p.index].file);
        }
    }
    return order;
}

bool click(HistorySelection& selection, const std::vector<std::string>& order, const std::string& file, bool ctrl,
           bool shift, const std::string& on_view) {
    if (!ctrl && !shift) {
        selection.marked.clear();
        selection.anchor = file;
        selection.taken_view.clear();
        return true;
    }
    if (shift) {
        const std::ptrdiff_t to = position(order, file);
        if (to < 0) return false;
        std::ptrdiff_t from = position(order, selection.anchor);
        if (from < 0) from = position(order, on_view);
        if (from < 0) from = to;
        if (!ctrl) selection.marked.clear();
        for (std::ptrdiff_t i = std::min(from, to); i <= std::max(from, to); ++i) {
            selection.marked.insert(order[static_cast<size_t>(i)]);
        }
        // The range's start stays put: another Shift-click picks again from it.
        selection.anchor = order[static_cast<size_t>(from)];
        return false;
    }
    if (!on_view.empty() && selection.taken_view != on_view) {
        selection.taken_view = on_view;
        if (on_view != file) selection.marked.insert(on_view);
    }
    if (selection.marked.erase(file) == 0) selection.marked.insert(file);
    selection.anchor = file;
    return false;
}

void drop_missing(HistorySelection& selection, const std::vector<meter::HistoryEntry>& history) {
    if (selection.marked.empty() && selection.anchor.empty()) return;
    std::unordered_set<std::string_view> present;
    present.reserve(history.size());
    for (const auto& e : history) present.insert(e.file);
    for (auto it = selection.marked.begin(); it != selection.marked.end();) {
        it = present.count(*it) == 0 ? selection.marked.erase(it) : std::next(it);
    }
    if (!selection.anchor.empty() && present.count(selection.anchor) == 0) selection.anchor.clear();
}

void select_all(HistorySelection& selection, const std::vector<meter::HistoryEntry>& history) {
    for (const auto& e : history) selection.marked.insert(e.file);
}

std::string day_label(const common::LocalDay& day, const std::optional<common::LocalDay>& today) {
    if (today) {
        if (day.serial == today->serial) return "Today";
        if (day.serial + 1 == today->serial) return "Yesterday";
    }
    static constexpr const char* kWeekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static constexpr const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const unsigned month = day.month >= 1 && day.month <= 12 ? day.month - 1 : 0;
    std::string label =
        std::string(kWeekdays[day.weekday % 7]) + " " + std::to_string(day.day) + " " + kMonths[month];
    if (today && day.year != today->year) label += " " + std::to_string(day.year);
    return label;
}

RunTitle run_title(const std::string& map_label, size_t fights) {
    RunTitle title{map_label, std::to_string(fights) + (fights == 1 ? " fight" : " fights")};
    // The tier: the last " (...)", closing the name. A label that is nothing else
    // keeps it as its name.
    if (map_label.empty() || map_label.back() != ')') return title;
    const size_t open = map_label.rfind(" (");
    if (open == std::string::npos || open == 0 || open + 3 >= map_label.size()) return title;
    title.name = map_label.substr(0, open);
    // " · ", as the list's rows put it between two facts. Escaped: the source stays ASCII.
    title.detail = map_label.substr(open + 2, map_label.size() - open - 3) + " \xC2\xB7 " + title.detail;
    return title;
}

DeletePrompt delete_picked(const std::vector<meter::HistoryEntry>& history, const std::set<std::string>& files) {
    DeletePrompt p;
    size_t kept = 0;
    for (const auto& e : history) {
        if (files.count(e.file) == 0) continue;
        p.files.push_back(e.file);
        if (e.kept) ++kept;
    }
    const size_t n = p.files.size();
    if (n == 1) {
        p.title = "Delete this fight?";
        p.body = kept == 1 ? "Its file goes too, though you keep it." : files_go(1);
    } else {
        p.title = "Delete " + std::to_string(n) + " fights?";
        p.body = files_go(n);
        if (kept == 1) {
            p.body = "Their files go too, the one you keep among them.";
        } else if (kept > 1) {
            p.body = "Their files go too, the " + std::to_string(kept) + " you keep among them.";
        }
    }
    return p;
}

DeletePrompt delete_run(const std::vector<meter::HistoryEntry>& history, const meter::PullGroup& run,
                        const std::string& where) {
    DeletePrompt p;
    size_t kept = 0;
    // Oldest first, as the history holds them.
    for (auto it = run.pulls.rbegin(); it != run.pulls.rend(); ++it) {
        if (it->index >= history.size()) continue;
        const auto& e = history[it->index];
        if (e.kept) {
            ++kept;
        } else {
            p.files.push_back(e.file);
        }
    }
    const size_t n = p.files.size();
    p.title = n == 1 ? std::string("Delete this run's fight?") : "Delete this run's " + std::to_string(n) + " fights?";
    p.body = (where.empty() ? std::string() : where + ". ") + files_go(n);
    if (kept > 0) p.body += kept_stay(kept) + ".";
    return p;
}

DeletePrompt delete_all(const std::vector<meter::HistoryEntry>& history) {
    DeletePrompt p;
    const size_t kept = kept_count(history);
    const size_t n = history.size() - kept;
    if (n == 0) return p;
    p.everything = true;
    if (kept == 0) {
        p.title = n == 1 ? std::string("Delete the saved fight?") : "Delete all " + std::to_string(n) + " saved fights?";
        p.body = files_go(n) + " The live fight stays.";
    } else {
        p.title = n == 1 ? std::string("Delete the fight you don't keep?")
                         : "Delete the " + std::to_string(n) + " fights you don't keep?";
        p.body = files_go(n) + kept_stay(kept) + ", and so does the live fight.";
    }
    return p;
}

size_t kept_count(const std::vector<meter::HistoryEntry>& history) noexcept {
    return static_cast<size_t>(
        std::count_if(history.begin(), history.end(), [](const meter::HistoryEntry& e) { return e.kept; }));
}

} // namespace aion2::app
