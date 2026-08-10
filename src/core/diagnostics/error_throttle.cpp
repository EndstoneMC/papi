#include "core/diagnostics/error_throttle.h"

#include <utility>

namespace papi::detail {

ErrorThrottle::ErrorThrottle() : clock_([] { return std::chrono::steady_clock::now(); }) {}

ErrorThrottle::ErrorThrottle(Clock clock) : clock_(std::move(clock)) {}

ThrottleDecision ErrorThrottle::record(const std::uint64_t scope, const ErrorOperation operation)
{
    const auto index = static_cast<std::size_t>(operation);

    std::scoped_lock lock(mutex_);
    // The clock is read under the lock because setClock may replace it.
    const auto now = clock_();
    auto &state = scopes_[scope].operations[index];

    if (!state.seen) {
        state.seen = true;
        state.window_start = now;
        state.suppressed = 0;
        return {true, 0};
    }

    if (now - state.window_start < ErrorThrottleWindow) {
        ++state.suppressed;
        return {false, 0};
    }

    const auto suppressed = std::exchange(state.suppressed, 0);
    state.window_start = now;
    return {true, suppressed};
}

void ErrorThrottle::forget(const std::uint64_t scope)
{
    std::scoped_lock lock(mutex_);
    scopes_.erase(scope);
}

void ErrorThrottle::clear()
{
    std::scoped_lock lock(mutex_);
    scopes_.clear();
}

void ErrorThrottle::setClock(Clock clock)
{
    std::scoped_lock lock(mutex_);
    clock_ = std::move(clock);
    scopes_.clear();
}

std::size_t ErrorThrottle::trackedScopes() const
{
    std::scoped_lock lock(mutex_);
    return scopes_.size();
}

}  // namespace papi::detail
