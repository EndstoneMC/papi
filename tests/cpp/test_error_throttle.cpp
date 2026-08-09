// Bounded error suppression: test matrix EXC-003 and EXC-004.

#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "core/diagnostics/error_throttle.h"

namespace {

using papi::detail::ErrorOperation;
using papi::detail::ErrorThrottle;
using papi::detail::ErrorThrottleWindow;
using papi::detail::ServiceErrorScope;

// A clock the test advances by hand, so suppression can be verified without sleeping.
class ManualClock {
public:
    [[nodiscard]] std::chrono::steady_clock::time_point now() const { return now_; }
    void advance(const std::chrono::steady_clock::duration delta) { now_ += delta; }

private:
    std::chrono::steady_clock::time_point now_{};
};

// EXC-003
TEST(ErrorThrottle, LogsOnceThenSuppressesForTheWindow)
{
    ManualClock clock;
    ErrorThrottle throttle([&clock] { return clock.now(); });

    auto decision = throttle.record(1, ErrorOperation::Ordinary);
    EXPECT_TRUE(decision.should_log);
    EXPECT_EQ(decision.suppressed, 0U);

    for (int i = 0; i < 500; ++i) {
        EXPECT_FALSE(throttle.record(1, ErrorOperation::Ordinary).should_log);
    }

    // Still inside the window: the boundary itself stays suppressed.
    clock.advance(ErrorThrottleWindow - std::chrono::seconds{1});
    EXPECT_FALSE(throttle.record(1, ErrorOperation::Ordinary).should_log);

    clock.advance(std::chrono::seconds{1});
    decision = throttle.record(1, ErrorOperation::Ordinary);
    EXPECT_TRUE(decision.should_log);
    EXPECT_EQ(decision.suppressed, 501U);

    // The count resets after being reported.
    clock.advance(ErrorThrottleWindow);
    decision = throttle.record(1, ErrorOperation::Ordinary);
    EXPECT_TRUE(decision.should_log);
    EXPECT_EQ(decision.suppressed, 0U);
}

TEST(ErrorThrottle, ScopesAndOperationsAreTrackedIndependently)
{
    ManualClock clock;
    ErrorThrottle throttle([&clock] { return clock.now(); });

    EXPECT_TRUE(throttle.record(1, ErrorOperation::Ordinary).should_log);
    // A different operation on the same entry is a different failure.
    EXPECT_TRUE(throttle.record(1, ErrorOperation::Relational).should_log);
    // A different entry has its own window.
    EXPECT_TRUE(throttle.record(2, ErrorOperation::Ordinary).should_log);

    EXPECT_FALSE(throttle.record(1, ErrorOperation::Ordinary).should_log);
    EXPECT_FALSE(throttle.record(1, ErrorOperation::Relational).should_log);
    EXPECT_FALSE(throttle.record(2, ErrorOperation::Ordinary).should_log);
}

TEST(ErrorThrottle, EveryOperationHasItsOwnWindow)
{
    ManualClock clock;
    ErrorThrottle throttle([&clock] { return clock.now(); });

    const std::vector<ErrorOperation> operations = {
        ErrorOperation::Metadata,     ErrorOperation::CanRegister,   ErrorOperation::Ordinary,
        ErrorOperation::Relational,   ErrorOperation::PlayerCleanup, ErrorOperation::Unregister,
        ErrorOperation::ThreadPolicy,
    };

    for (const auto operation : operations) {
        EXPECT_TRUE(throttle.record(7, operation).should_log) << toString(operation);
    }
    for (const auto operation : operations) {
        EXPECT_FALSE(throttle.record(7, operation).should_log) << toString(operation);
    }
    // One scope, regardless of how many operations it tracked.
    EXPECT_EQ(throttle.trackedScopes(), 1U);
}

// EXC-004: throttle memory is released with the entries it describes.
TEST(ErrorThrottle, StateIsDroppedWithTheEntryItBelongsTo)
{
    ErrorThrottle throttle;

    for (std::uint64_t generation = 1; generation <= 1000; ++generation) {
        (void)throttle.record(generation, ErrorOperation::Ordinary);
    }
    EXPECT_EQ(throttle.trackedScopes(), 1000U);

    for (std::uint64_t generation = 1; generation <= 1000; ++generation) {
        throttle.forget(generation);
    }
    EXPECT_EQ(throttle.trackedScopes(), 0U);
}

TEST(ErrorThrottle, ClearDropsEverything)
{
    ErrorThrottle throttle;
    (void)throttle.record(ServiceErrorScope, ErrorOperation::ThreadPolicy);
    (void)throttle.record(1, ErrorOperation::Ordinary);
    EXPECT_EQ(throttle.trackedScopes(), 2U);

    throttle.clear();
    EXPECT_EQ(throttle.trackedScopes(), 0U);
    // After clearing, the next error is treated as new.
    EXPECT_TRUE(throttle.record(1, ErrorOperation::Ordinary).should_log);
}

TEST(ErrorThrottle, ForgettingAnUnknownScopeIsHarmless)
{
    ErrorThrottle throttle;
    EXPECT_NO_THROW(throttle.forget(12345));
    EXPECT_EQ(throttle.trackedScopes(), 0U);
}

// Parsing may fail on any thread that reaches the throttle, so it must be safe to
// call concurrently and must still emit exactly one first log per key.
TEST(ErrorThrottle, IsSafeUnderConcurrentUse)
{
    ErrorThrottle throttle;
    std::atomic<int> logged{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < 2000; ++i) {
                if (throttle.record(42, ErrorOperation::Ordinary).should_log) {
                    logged.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto &thread : threads) {
        thread.join();
    }

    EXPECT_EQ(logged.load(), 1);
    EXPECT_EQ(throttle.trackedScopes(), 1U);
}

TEST(ErrorThrottle, OperationNamesAreStable)
{
    EXPECT_EQ(toString(ErrorOperation::Metadata), "metadata");
    EXPECT_EQ(toString(ErrorOperation::CanRegister), "can_register");
    EXPECT_EQ(toString(ErrorOperation::Ordinary), "ordinary");
    EXPECT_EQ(toString(ErrorOperation::Relational), "relational");
    EXPECT_EQ(toString(ErrorOperation::PlayerCleanup), "player_cleanup");
    EXPECT_EQ(toString(ErrorOperation::Unregister), "unregister");
    EXPECT_EQ(toString(ErrorOperation::ThreadPolicy), "thread_policy");
}

}  // namespace
