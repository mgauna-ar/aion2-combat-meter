#include "test_framework.hpp"
#include "file_test_support.hpp"

#include "aion2/version.hpp"
#include "common/bounded_queue.hpp"
#include "common/config/json.hpp"
#include "common/config/json_file.hpp"
#include "common/config/key_table.hpp"
#include "common/os/auto_start.hpp"
#include "common/os/single_instance.hpp"
#include "common/snapshot_slot.hpp"

#include <array>
#include <chrono>
#include <string>
#include <thread>

using aion2::config::JsonValue;
using namespace aion2::test::file_support;

TEST_CASE(Common, VersionStringIsSet) {
    TEST_ASSERT_EQ(std::string(aion2::VERSION_STRING), std::string(AION2_VERSION_STRING));
}

TEST_CASE(Json, RoundTripsNestedDocument) {
    JsonValue doc;
    doc["name"] = "Daeva";
    doc["hits"] = 42;
    doc["list"] = JsonValue::ArrayType{JsonValue(1), JsonValue("two"), JsonValue(true)};
    doc["inner"]["dps"] = 1234.5;

    const auto parsed = JsonValue::parse(doc.stringify());
    TEST_ASSERT(parsed.has_value());
    TEST_ASSERT(*parsed == doc);
}

TEST_CASE(Json, CompactIsOneLine) {
    JsonValue doc;
    doc["a"] = 1;
    doc["b"] = JsonValue::ArrayType{JsonValue("x\ny"), JsonValue()};
    const std::string line = doc.stringify_compact();
    TEST_ASSERT_EQ(line, std::string(R"({"a":1,"b":["x\ny",null]})"));
    TEST_ASSERT(line.find('\n') == std::string::npos);
    const auto parsed = JsonValue::parse(line);
    TEST_ASSERT(parsed.has_value());
    TEST_ASSERT(*parsed == doc);
}

TEST_CASE(Json, TimestampsPrintAsIntegers) {
    JsonValue doc;
    doc["ts"] = uint64_t{1'792'152'000'123'456};
    TEST_ASSERT_EQ(doc.stringify_compact(), std::string(R"({"ts":1792152000123456})"));
}

TEST_CASE(Json, RejectsTruncatedInput) {
    TEST_ASSERT_FALSE(JsonValue::parse(R"({"a":1,"b":[1,2)").has_value());
}

TEST_CASE(JsonFile, AtomicWriteThenRead) {
    ScratchDir dir("aion2_test_json_file");
    const auto path = dir.path / "sub" / "doc.json";
    JsonValue doc;
    doc["k"] = "v";
    TEST_ASSERT_EQ(aion2::config::write_json_atomic(path, doc), std::string());
    const auto back = aion2::config::read_json_file(path);
    TEST_ASSERT(back.has_value());
    TEST_ASSERT_EQ((*back)["k"].as_string(), std::string("v"));
    TEST_ASSERT_FALSE(std::filesystem::exists(dir.path / "sub" / "doc.json.tmp"));
}

TEST_CASE(JsonFile, MissingFileIsNullopt) {
    TEST_ASSERT_FALSE(aion2::config::read_json_file("/nonexistent/aion2/doc.json").has_value());
}

namespace {
struct Inner {
    float x{1.5f};
};
struct Settings {
    bool enabled{true};
    int count{3};
    Inner inner;
};
constexpr std::array<aion2::config::ConfigKey<Settings>, 3> kKeys{{
    aion2::config::field<Settings, &Settings::enabled>("enabled"),
    aion2::config::field<Settings, &Settings::count>("count"),
    aion2::config::field<Settings, &Settings::inner, &Inner::x>("inner_x"),
}};
} // namespace

TEST_CASE(KeyTable, WritesAndReadsBack) {
    Settings s;
    s.enabled = false;
    s.count = 9;
    s.inner.x = 4.0f;
    JsonValue section;
    aion2::config::write_keys<Settings>(kKeys, s, section);

    Settings back;
    aion2::config::read_keys<Settings>(kKeys, section, back);
    TEST_ASSERT_EQ(back.enabled, false);
    TEST_ASSERT_EQ(back.count, 9);
    TEST_ASSERT_NEAR(back.inner.x, 4.0f, 1e-6f);
}

TEST_CASE(KeyTable, WrongTypeKeepsField) {
    JsonValue section;
    section["count"] = "lots";
    Settings s;
    aion2::config::read_keys<Settings>(kKeys, section, s);
    TEST_ASSERT_EQ(s.count, 3);
}

TEST_CASE(BoundedQueue, DropsWhenFull) {
    aion2::common::BoundedQueue<int> q(2);
    TEST_ASSERT(q.try_push(1));
    TEST_ASSERT(q.try_push(2));
    TEST_ASSERT_FALSE(q.try_push(3));
    TEST_ASSERT_EQ(q.dropped(), uint64_t{1});
    TEST_ASSERT_EQ(*q.pop_for(std::chrono::milliseconds(1)), 1);
    TEST_ASSERT_EQ(*q.pop_for(std::chrono::milliseconds(1)), 2);
    TEST_ASSERT_FALSE(q.pop_for(std::chrono::milliseconds(1)).has_value());
}

TEST_CASE(BoundedQueue, CloseWakesConsumer) {
    aion2::common::BoundedQueue<int> q(4);
    std::thread closer([&q] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        q.close();
    });
    const auto start = std::chrono::steady_clock::now();
    const auto item = q.pop_for(std::chrono::seconds(5));
    closer.join();
    TEST_ASSERT_FALSE(item.has_value());
    TEST_ASSERT(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
    TEST_ASSERT_FALSE(q.try_push(1));
}

TEST_CASE(SnapshotSlot, PublishReplacesAndCountsRevisions) {
    aion2::common::SnapshotSlot<std::string> slot;
    TEST_ASSERT(slot.get() == nullptr);
    slot.publish("one");
    const auto first = slot.get();
    slot.publish("two");
    TEST_ASSERT_EQ(*first, std::string("one"));
    TEST_ASSERT_EQ(*slot.get(), std::string("two"));
    TEST_ASSERT_EQ(slot.revision(), uint64_t{2});
}

TEST_CASE(AutoStart, MockKeepsTheSetting) {
    TEST_ASSERT(aion2::os::set_auto_start(true));
    TEST_ASSERT(aion2::os::auto_start_enabled());
    TEST_ASSERT(aion2::os::set_auto_start(false));
    TEST_ASSERT_FALSE(aion2::os::auto_start_enabled());
}

TEST_CASE(SingleInstance, AcquireWithinWaitsForTheHolder) {
    // An updated build waits for the one that launched it to exit and free the lock.
    aion2::os::SingleInstance running("Aion2TestRelaunchMutex");
    TEST_ASSERT(running.try_acquire());
    aion2::os::SingleInstance updated("Aion2TestRelaunchMutex");
    TEST_ASSERT_FALSE(updated.acquire_within(std::chrono::milliseconds(150)));
    TEST_ASSERT_FALSE(updated.is_primary());

    std::thread exiting([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        running.release();
    });
    const bool acquired = updated.acquire_within(std::chrono::seconds(5));
    exiting.join();
    TEST_ASSERT(acquired);
    TEST_ASSERT(updated.is_primary());
}
