#include "meter/history_store.hpp"

#include "common/config/json.hpp"
#include "common/config/json_file.hpp"
#include "common/time_format.hpp"
#include "meter/fight_json.hpp"
#include "game/map_names.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

namespace aion2::meter {

using config::JsonValue;

namespace {

[[nodiscard]] uint64_t u64(const JsonValue& v) { return json_u64(v); }

JsonValue entry_to_json(const HistoryEntry& e) {
    JsonValue j;
    j["v"] = kFightRecordVersion;
    j["file"] = e.file;
    j["id"] = e.id;
    j["map_id"] = e.map_id;
    j["visit_ts"] = e.visit_ts;
    j["start_ts"] = e.start_ts;
    j["duration_s"] = e.duration_s;
    j["boss"] = e.boss_label;
    j["is_boss"] = e.is_boss;
    j["killed"] = e.killed;
    if (e.boss_hp_known) j["boss_hp_left"] = e.boss_hp_left;
    j["partial"] = e.partial;
    j["total_damage"] = e.total_damage;
    j["total_dps"] = e.total_dps;
    j["end_reason"] = std::string(end_reason_name(e.end_reason));
    j["players"] = static_cast<uint64_t>(e.players);
    if (e.kept) j["kept"] = true;
    return j;
}

std::optional<HistoryEntry> entry_from_json(const JsonValue& j) {
    if (!j.is_object() || !readable_version(j) || !j["file"].is_string()) return std::nullopt;
    HistoryEntry e;
    e.file = j["file"].as_string();
    e.id = u64(j["id"]);
    e.map_id = static_cast<uint32_t>(u64(j["map_id"]));
    e.visit_ts = u64(j["visit_ts"]);
    e.start_ts = u64(j["start_ts"]);
    e.duration_s = j["duration_s"].as_double(0.0);
    e.boss_label = j["boss"].as_string();
    e.is_boss = j["is_boss"].as_bool();
    e.killed = j["killed"].as_bool();
    if (j["boss_hp_left"].is_number()) {
        e.boss_hp_known = true;
        e.boss_hp_left = std::clamp(j["boss_hp_left"].as_double(0.0), 0.0, 1.0);
    }
    e.partial = j["partial"].as_bool();
    e.total_damage = u64(j["total_damage"]);
    e.total_dps = j["total_dps"].as_double(0.0);
    e.end_reason = end_reason_from_name(j["end_reason"].as_string());
    e.players = static_cast<size_t>(u64(j["players"]));
    e.kept = j["kept"].as_bool();
    return e;
}

HistoryEntry entry_for(const FightSummary& f, std::string file) {
    HistoryEntry e;
    e.file = std::move(file);
    e.id = f.id;
    e.map_id = f.map_id;
    e.visit_ts = f.visit_ts;
    e.start_ts = f.start_ts;
    e.duration_s = f.duration_s;
    e.boss_label = f.boss.present() ? f.boss.label : std::string();
    e.is_boss = f.boss.is_boss;
    e.killed = f.boss.killed;
    if (f.boss.present() && f.boss.hp_known && !f.boss.killed && f.boss.max_hp > 0) {
        e.boss_hp_known = true;
        e.boss_hp_left = std::clamp(f.boss.hp_fraction(), 0.0, 1.0);
    }
    e.partial = f.quality.partial;
    e.total_damage = f.total_damage;
    e.total_dps = f.total_dps;
    e.end_reason = f.end_reason;
    e.players = f.combatants.size();
    return e;
}

/// A file name inside the history folder and nothing else: names come from the
/// index, which is data on disk.
[[nodiscard]] bool is_plain_name(const std::string& file) noexcept {
    return !file.empty() && file.find('/') == std::string::npos && file.find('\\') == std::string::npos &&
           file.find("..") == std::string::npos;
}

/// Deletes a fight's file. True once it is gone, deleted now or missing already, so
/// its index line can go; false while it stays (open elsewhere, say), and its line
/// with it. A name that is not plain names nothing of ours: its line just goes.
[[nodiscard]] bool delete_fight_file(const std::filesystem::path& dir, const std::string& file) {
    if (!is_plain_name(file)) return true;
    std::error_code ec;
    std::filesystem::remove(dir / file, ec);
    return !ec;
}

void sort_oldest_first(std::vector<HistoryEntry>& entries) {
    std::stable_sort(entries.begin(), entries.end(), [](const HistoryEntry& a, const HistoryEntry& b) {
        if (a.start_ts != b.start_ts) return a.start_ts < b.start_ts;
        return a.file < b.file;
    });
}

} // namespace

bool worth_saving(const FightSummary& fight, const MeterSettings& settings) noexcept {
    if (settings.save_only_dungeons && !game::is_dungeon(fight.map_id)) return false;
    return fight.boss.is_boss || fight.duration_s >= settings.min_saved_duration_s;
}

std::string fight_file_name(const FightSummary& fight) {
    return common::utc_file_stamp(fight.start_ts) + "-" + std::to_string(fight.id) + ".json";
}

std::string HistoryStore::save(const FightSummary& fight) {
    std::error_code ec;
    std::filesystem::create_directories(m_dir, ec);
    if (ec) return "cannot create history folder: " + ec.message();

    // Ids restart with every run; a clash within the same second gets a suffix.
    std::string name = fight_file_name(fight);
    for (int n = 2; std::filesystem::exists(m_dir / name, ec) && n < 100; ++n) {
        name = fight_file_name(fight);
        name.insert(name.size() - 5, "-" + std::to_string(n));
    }

    const std::string error = config::write_json_atomic(m_dir / name, fight_to_json(fight));
    if (!error.empty()) return error;

    std::ofstream index(index_path(), std::ios::binary | std::ios::app);
    if (!index) return "cannot open index.jsonl";
    index << entry_to_json(entry_for(fight, name)).stringify_compact() << '\n';
    index.flush();
    return index ? std::string() : std::string("cannot append to index.jsonl");
}

std::vector<HistoryEntry> HistoryStore::load_index() const {
    std::ifstream in(index_path(), std::ios::binary);
    if (!in) return rebuild_index();
    std::vector<HistoryEntry> entries;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const auto json = JsonValue::parse(line);
        if (!json) continue;  // a line cut by a crash
        if (auto e = entry_from_json(*json)) entries.push_back(std::move(*e));
    }
    sort_oldest_first(entries);
    return entries;
}

std::vector<HistoryEntry> HistoryStore::rebuild_index() const {
    std::vector<HistoryEntry> entries;
    std::error_code ec;
    for (const auto& item : std::filesystem::directory_iterator(m_dir, ec)) {
        if (!item.is_regular_file() || item.path().extension() != ".json") continue;
        const auto json = config::read_json_file(item.path());
        if (!json) continue;
        const auto fight = fight_from_json(*json);
        if (!fight) continue;
        HistoryEntry e = entry_for(*fight, item.path().filename().string());
        e.kept = (*json)["kept"].as_bool();
        entries.push_back(std::move(e));
    }
    sort_oldest_first(entries);
    if (!entries.empty()) (void)write_index(entries);
    return entries;
}

std::string HistoryStore::write_index(const std::vector<HistoryEntry>& entries) const {
    std::string text;
    for (const auto& e : entries) {
        text += entry_to_json(e).stringify_compact();
        text += '\n';
    }
    return config::write_file_atomic(index_path(), text);
}

std::optional<FightSummary> HistoryStore::load(const std::string& file) const {
    if (!is_plain_name(file)) return std::nullopt;
    const auto json = config::read_json_file(m_dir / file);
    if (!json) return std::nullopt;
    return fight_from_json(*json);
}

size_t HistoryStore::remove(const std::vector<std::string>& files) {
    const std::set<std::string> wanted(files.begin(), files.end());
    std::vector<HistoryEntry> entries = load_index();
    std::vector<HistoryEntry> left;
    left.reserve(entries.size());
    size_t removed = 0;
    for (auto& e : entries) {
        if (wanted.count(e.file) != 0 && delete_fight_file(m_dir, e.file)) {
            ++removed;
            continue;
        }
        left.push_back(std::move(e));
    }
    if (removed > 0) (void)write_index(left);
    return removed;
}

size_t HistoryStore::set_kept(const std::vector<std::string>& files, bool kept) {
    const std::set<std::string> wanted(files.begin(), files.end());
    std::vector<HistoryEntry> entries = load_index();
    size_t changed = 0;
    for (auto& e : entries) {
        if (e.kept == kept || wanted.count(e.file) == 0 || !is_plain_name(e.file)) continue;
        // The record first: it is what a rebuilt index reads. Every number in it was
        // written from a double, as JsonValue reads it back, so the rest of it is
        // written again unchanged.
        const std::filesystem::path path = m_dir / e.file;
        auto json = config::read_json_file(path);
        if (!json || !json->is_object() || !readable_version(*json)) continue;
        if (kept) {
            (*json)["kept"] = true;
        } else {
            json->as_object().erase("kept");
        }
        if (!config::write_json_atomic(path, *json).empty()) continue;
        e.kept = kept;
        ++changed;
    }
    if (changed > 0) (void)write_index(entries);
    return changed;
}

size_t HistoryStore::prune(size_t keep) {
    std::vector<HistoryEntry> entries = load_index();
    const auto unkept = static_cast<size_t>(
        std::count_if(entries.begin(), entries.end(), [](const HistoryEntry& e) { return !e.kept; }));
    if (unkept <= keep) return 0;
    // Oldest first: the first fights not kept go, until `keep` of them are left.
    size_t to_drop = unkept - keep;
    size_t dropped = 0;
    std::vector<HistoryEntry> left;
    left.reserve(entries.size());
    for (auto& e : entries) {
        if (to_drop > 0 && !e.kept) {
            --to_drop;
            if (delete_fight_file(m_dir, e.file)) {
                ++dropped;
                continue;
            }
        }
        left.push_back(std::move(e));
    }
    (void)write_index(left);
    return dropped;
}

size_t HistoryStore::clear() {
    size_t removed = 0;
    std::error_code ec;
    std::vector<std::pair<std::filesystem::path, HistoryEntry>> fights;
    for (const auto& item : std::filesystem::directory_iterator(m_dir, ec)) {
        if (!item.is_regular_file() || item.path().extension() != ".json") continue;
        // Only fight records: whatever else the user keeps here stays.
        const auto json = config::read_json_file(item.path());
        if (!json) continue;
        const auto fight = fight_from_json(*json);
        if (!fight) continue;
        HistoryEntry e = entry_for(*fight, item.path().filename().string());
        e.kept = (*json)["kept"].as_bool();
        fights.emplace_back(item.path(), std::move(e));
    }
    // What stays: the kept fights, and any whose file would not go, which the index
    // must still list.
    std::vector<HistoryEntry> left;
    for (auto& [path, entry] : fights) {
        if (!entry.kept && std::filesystem::remove(path, ec)) {
            ++removed;
            continue;
        }
        left.push_back(std::move(entry));
    }
    if (left.empty()) {
        std::filesystem::remove(index_path(), ec);
    } else {
        sort_oldest_first(left);
        (void)write_index(left);
    }
    return removed;
}

} // namespace aion2::meter
