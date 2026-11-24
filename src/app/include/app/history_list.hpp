#pragma once

// The Combat Meter page's list of saved fights, as data: the fights picked with Ctrl
// or Shift, the day headers, and what a delete asks before it is sent. No ImGui
// here, so the tests check it on the Mac; view_meter.cpp draws it.

#include "common/time_format.hpp"
#include "meter/history_store.hpp"
#include "meter/pull_grouping.hpp"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace aion2::app {

/// The saved fights picked with Ctrl or Shift, for Keep or Delete. Apart from the
/// fight on view, which a plain click opens.
struct HistorySelection {
    /// By file name.
    std::set<std::string> marked;
    /// Where a Shift-click's range starts: the last fight clicked without Shift.
    std::string anchor;
    /// The fight on view that a Ctrl-click already took in, so taking it out again
    /// sticks.
    std::string taken_view;
};

/// Every saved fight's file in the order the list shows them: by visit, newest
/// first, as group_by_visit() gives them.
[[nodiscard]] std::vector<std::string> list_order(const std::vector<meter::HistoryEntry>& history,
                                                  const std::vector<meter::PullGroup>& groups);

/// A click on `file`, one of `order`. Ctrl picks it or takes it out, Shift picks
/// the fights from the anchor (else the fight on view) to it, and Ctrl+Shift adds
/// those to the picks. The first Ctrl-click after a fight was opened picks it too,
/// as a file manager does with the item it shows selected. `on_view` is the saved
/// fight on view, empty for the live one. True for a plain click: the caller opens
/// the fight, and the picks are dropped.
bool click(HistorySelection& selection, const std::vector<std::string>& order, const std::string& file, bool ctrl,
           bool shift, const std::string& on_view);

/// Takes out the picks the history no longer holds: deleted, or pruned.
void drop_missing(HistorySelection& selection, const std::vector<meter::HistoryEntry>& history);

/// Picks every saved fight.
void select_all(HistorySelection& selection, const std::vector<meter::HistoryEntry>& history);

/// The day a run began, as the list's headers name it: "Today", "Yesterday", or
/// "Mon 23 Nov", with the year when it is not `today`'s. Unset `today` (no clock)
/// names every day by its date.
[[nodiscard]] std::string day_label(const common::LocalDay& day, const std::optional<common::LocalDay>& today);

/// A delete the list asks about before it is sent.
struct DeletePrompt {
    std::string title;
    std::string body;
    /// The fights to delete, by file. Empty when `everything`.
    std::vector<std::string> files;
    /// Clear: every fight but the kept ones (HistoryStore::clear()).
    bool everything{false};
};

/// Deleting fights picked by hand: one from its menu or on view, or the picks. Kept
/// ones go too, and the prompt says so. Files the history does not hold are left out.
[[nodiscard]] DeletePrompt delete_picked(const std::vector<meter::HistoryEntry>& history,
                                         const std::set<std::string>& files);

/// Deleting a run from its header's menu: its fights but the kept ones. `where`
/// names the run ("Krao Cave (Conquest), 12:00"). No files when all are kept.
[[nodiscard]] DeletePrompt delete_run(const std::vector<meter::HistoryEntry>& history, const meter::PullGroup& run,
                                      const std::string& where);

/// Clear: every saved fight but the kept ones. Not `everything` when every fight is
/// kept, and nothing to ask then.
[[nodiscard]] DeletePrompt delete_all(const std::vector<meter::HistoryEntry>& history);

/// How many saved fights are kept.
[[nodiscard]] size_t kept_count(const std::vector<meter::HistoryEntry>& history) noexcept;

} // namespace aion2::app
