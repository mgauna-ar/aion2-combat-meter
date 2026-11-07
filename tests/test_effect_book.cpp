#include "test_framework.hpp"
#include "meter_test_support.hpp"

#include "game/effect_book.hpp"

using namespace aion2;
using game::EffectBook;
using namespace aion2::test::meter_support;

namespace {

constexpr EntityId kMe = 100;
constexpr EntityId kAlly = 200;
constexpr uint32_t kMantra = 181'900'211;

/// The server's clock at capture time `ts` when it runs `ahead_ms` ahead.
uint64_t server_ms(Micros ts, int64_t ahead_ms = 0) {
    return static_cast<uint64_t>(static_cast<int64_t>(ts / kMicrosPerMilli) + ahead_ms);
}

} // namespace

TEST_CASE(EffectBook, ARenewalContinuesItsInstance) {
    EffectBook book;
    book.on_heartbeat(server_ms(at_s(0), 3000), at_s(0));
    const auto first = book.on_applied(buff(kMe, kAlly, 1, kMantra, 10'000, server_ms(at_s(1), 3000) + 10'000), at_s(1));
    TEST_ASSERT(first.now != nullptr);
    TEST_ASSERT(!first.before.has_value());
    TEST_ASSERT(!first.continued(at_s(1)));
    TEST_ASSERT_EQ(first.now->expires_at, at_s(11));
    TEST_ASSERT_EQ(first.now->applied_at, at_s(1));

    proto::BuffApplied renewal = buff(kMe, kAlly, 1, kMantra, 10'000, server_ms(at_s(6), 3000) + 10'000);
    renewal.renewed = true;
    const auto again = book.on_applied(renewal, at_s(6));
    TEST_ASSERT(again.continued(at_s(6)));
    TEST_ASSERT_EQ(again.now->applied_at, at_s(1));
    TEST_ASSERT_EQ(again.now->updated_at, at_s(6));
    TEST_ASSERT_EQ(again.now->expires_at, at_s(16));
    TEST_ASSERT_EQ(again.before->expires_at, at_s(11));
}

TEST_CASE(EffectBook, AnInstanceNumberReusedAfterItsEndStartsAfresh) {
    EffectBook book;
    (void)book.on_applied(buff(kMe, kAlly, 1, kMantra, 2000), at_s(1));
    const auto later = book.on_applied(buff(kMe, kAlly, 1, kMantra, 2000), at_s(5));
    TEST_ASSERT(later.before.has_value());
    TEST_ASSERT(!later.continued(at_s(5)));
    TEST_ASSERT_EQ(later.now->applied_at, at_s(5));
}

TEST_CASE(EffectBook, AnEndFarFromTheDurationIsNotBelieved) {
    EffectBook book;
    book.on_heartbeat(server_ms(at_s(0), 3000), at_s(0));
    const auto a = book.on_applied(buff(kMe, kMe, 1, 3015, 5000, server_ms(at_s(1), 3000) + 60'000), at_s(1));
    TEST_ASSERT_EQ(a.now->expires_at, at_s(6));
    // Within the slack the stated end wins: renewals end up to 1.4 s early.
    const auto b = book.on_applied(buff(kMe, kMe, 2, 3015, 5000, server_ms(at_s(1), 3000) + 3600), at_s(1));
    TEST_ASSERT_EQ(b.now->expires_at, at_s(4.6));
}

TEST_CASE(EffectBook, ARemovalTakesTheSoonestAndSaysWhich) {
    EffectBook book;
    (void)book.on_applied(buff(kMe, kMe, 10, 3015, 3000), at_s(1));
    (void)book.on_applied(buff(kMe, kMe, 11, 3015, 3000), at_s(2));
    (void)book.on_applied(permanent_buff(kMe, 12, 3015), at_s(2));
    const auto gone = book.on_removed(proto::BuffRemoved{kMe, 3015});
    TEST_ASSERT(gone.has_value());
    TEST_ASSERT_EQ(gone->instance, uint32_t{10});
    TEST_ASSERT_EQ(book.on_removed(proto::BuffRemoved{kMe, 3015})->instance, uint32_t{11});
    TEST_ASSERT_EQ(book.on_removed(proto::BuffRemoved{kMe, 3015})->instance, uint32_t{12});
    TEST_ASSERT(!book.on_removed(proto::BuffRemoved{kMe, 3015}).has_value());
    TEST_ASSERT(book.on(kMe) == nullptr);
}

TEST_CASE(EffectBook, AMarkerTakesAwayItsNumber) {
    EffectBook book;
    (void)book.on_applied(buff(kMe, kMe, 3, kMantra, 5000), at_s(1));
    const auto marker = book.on_applied(buff(kMe, kMe, 3, 122'400'072, 0), at_s(2));
    TEST_ASSERT(marker.now == nullptr);
    TEST_ASSERT(marker.before.has_value());
    TEST_ASSERT_EQ(marker.before->buff_id, kMantra);
    TEST_ASSERT_EQ(book.instances_on(kMe, at_s(2)), size_t{0});
}

TEST_CASE(EffectBook, DeathsAndMapLoadsSayWhatTheyDropped) {
    EffectBook book;
    (void)book.on_applied(buff(kMe, kMe, 1, kMantra, 60'000), at_s(1));
    (void)book.on_applied(buff(kAlly, kAlly, 1, kMantra, 60'000), at_s(1));
    TEST_ASSERT_EQ(book.on_death(kAlly).size(), size_t{1});
    TEST_ASSERT(book.on(kAlly) == nullptr);

    // The new map's effects come shortly before its load and stay.
    (void)book.on_applied(buff(kAlly, kAlly, 2, kMantra, 60'000), at_s(9.5));
    const auto dropped = book.on_map_load(at_s(10));
    TEST_ASSERT_EQ(dropped.size(), size_t{1});
    TEST_ASSERT_EQ(dropped[0].target, kMe);
    TEST_ASSERT_EQ(book.instances_on(kAlly, at_s(10)), size_t{1});
}

TEST_CASE(EffectBook, OneEntityHoldsAtMostTheCap) {
    EffectBook book;
    for (uint32_t i = 0; i < EffectBook::kMaxInstancesPerEntity + 10; ++i) {
        (void)book.on_applied(buff(kMe, kMe, i, kMantra, 60'000), at_s(1));
    }
    TEST_ASSERT_EQ(book.instances_on(kMe, at_s(1)), EffectBook::kMaxInstancesPerEntity);
    // A renewal of one already kept still lands.
    const auto renewed = book.on_applied(buff(kMe, kMe, 0, kMantra, 90'000), at_s(2));
    TEST_ASSERT(renewed.now != nullptr);
    book.prune(at_s(70));
    TEST_ASSERT_EQ(book.instances_on(kMe, at_s(70)), size_t{1});
}
