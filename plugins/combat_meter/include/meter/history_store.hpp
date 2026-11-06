#pragma once

#include "common/micros.hpp"
#include "meter/meter_settings.hpp"
#include "meter/types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aion2::meter {

/// One saved fight, as the history list shows it: index.jsonl's line for it.
struct HistoryEntry {
    std::string file;
    uint64_t id{0};
    uint32_t map_id{0};
    /// The fight's map visit (FightSummary::visit_ts); 0 in a version 1 record.
    Micros visit_ts{0};
    Micros start_ts{0};
    double duration_s{0.0};
    std::string boss_label;
    bool is_boss{false};
    bool killed{false};
    /// The boss's HP left at the end, 0..1, when it lived and its HP was known.
    bool boss_hp_known{false};
    double boss_hp_left{0.0};
    bool partial{false};
    uint64_t total_damage{0};
    double total_dps{0.0};
    EndReason end_reason{EndReason::None};
    size_t players{0};
};

/// A boss fight, or any fight at least min_saved_duration_s long; with
/// save_only_dungeons, only in a dungeon.
[[nodiscard]] bool worth_saving(const FightSummary& fight, const MeterSettings& settings) noexcept;

/// "20261016-120000Z-12.json": the fight's start in UTC and its id.
[[nodiscard]] std::string fight_file_name(const FightSummary& fight);

/**
 * @brief Fights on disk: one JSON file each, plus index.jsonl, one line per fight.
 *
 * A fight file is written whole through a temp file and a rename; the index line is
 * appended after it. A crash can at worst cut the index's last line, which loading
 * skips, or lose it, which a rebuild from the fight files restores (load_index does
 * that when the index is missing). Records hold character names: the folder is the
 * user's, and nothing reads it but the meter.
 */
class HistoryStore {
public:
    explicit HistoryStore(std::filesystem::path dir) : m_dir(std::move(dir)) {}

    /// Writes the fight and its index line. Empty on success, else what failed.
    [[nodiscard]] std::string save(const FightSummary& fight);
    /// Every saved fight, oldest first.
    [[nodiscard]] std::vector<HistoryEntry> load_index() const;
    [[nodiscard]] std::optional<FightSummary> load(const std::string& file) const;
    /// Deletes all but the newest `keep` fights. The number deleted.
    size_t prune(size_t keep);
    /// Deletes every fight record in the folder and the index; other files stay.
    /// The number of fights deleted.
    size_t clear();

    [[nodiscard]] const std::filesystem::path& dir() const noexcept { return m_dir; }
    [[nodiscard]] std::filesystem::path index_path() const { return m_dir / "index.jsonl"; }

private:
    [[nodiscard]] std::vector<HistoryEntry> rebuild_index() const;
    [[nodiscard]] std::string write_index(const std::vector<HistoryEntry>& entries) const;

    std::filesystem::path m_dir;
};

} // namespace aion2::meter
