#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string_view>
#include <unordered_map>

namespace papi::detail {

/**
 * @brief Which provider interaction produced an error.
 *
 * The set is closed so throttle keys stay bounded no matter what a provider does.
 */
enum class ErrorOperation : std::uint8_t {
    Metadata = 0,
    CanRegister = 1,
    Ordinary = 2,
    Relational = 3,
    PlayerCleanup = 4,
    Unregister = 5,
    ThreadPolicy = 6,
    ParseReentrancy = 7,
    EventDispatch = 8,
};

[[nodiscard]] constexpr std::string_view toString(const ErrorOperation operation) noexcept
{
    switch (operation) {
    case ErrorOperation::Metadata:
        return "metadata";
    case ErrorOperation::CanRegister:
        return "can_register";
    case ErrorOperation::Ordinary:
        return "ordinary";
    case ErrorOperation::Relational:
        return "relational";
    case ErrorOperation::PlayerCleanup:
        return "player_cleanup";
    case ErrorOperation::Unregister:
        return "unregister";
    case ErrorOperation::ThreadPolicy:
        return "thread_policy";
    case ErrorOperation::ParseReentrancy:
        return "parse_reentrancy";
    case ErrorOperation::EventDispatch:
        return "event_dispatch";
    }
    return "unknown";
}

/**
 * @brief The number of operations tracked per throttle scope.
 */
inline constexpr std::size_t ErrorOperationCount = 9;

/**
 * @brief How long repeats of the same failure stay suppressed.
 */
inline constexpr std::chrono::seconds ErrorThrottleWindow{60};

/**
 * @brief What the caller should do with an error it is about to report.
 */
struct ThrottleDecision {
    /**
     * @brief Whether this error should be logged now.
     */
    bool should_log = false;

    /**
     * @brief How many identical errors were suppressed since the last log.
     *
     * Only meaningful when should_log is true.
     */
    std::uint64_t suppressed = 0;
};

/**
 * @brief Bounded, clock-driven suppression of repeated provider errors.
 *
 * A hot parse loop can fail on every placeholder, so errors are logged once per
 * (scope, operation) pair per window. The next permitted message reports how many
 * repeats were suppressed.
 *
 * Storage is bounded by design: keys are a caller-supplied scope identifier plus
 * one of the fixed ErrorOperation values, never provider-controlled text. A scope
 * is dropped with forget() when the registry entry it belongs to disappears.
 *
 * The clock is injectable so tests can advance time deterministically instead of
 * sleeping.
 */
class ErrorThrottle {
public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    ErrorThrottle();
    explicit ErrorThrottle(Clock clock);

    /**
     * @brief Decides whether an error should be logged, and counts it either way.
     *
     * @param scope identifies the entry or subsystem the error belongs to; scope 0
     *        is reserved for service-wide errors that outlive any entry
     * @param operation which interaction failed
     */
    [[nodiscard]] ThrottleDecision record(std::uint64_t scope, ErrorOperation operation);

    /**
     * @brief Drops all state for a scope.
     */
    void forget(std::uint64_t scope);

    /**
     * @brief Drops all state.
     */
    void clear();

    /**
     * @brief Replaces the clock and drops all state.
     *
     * Exists so tests can advance time deterministically instead of sleeping through
     * a real 60-second window.
     */
    void setClock(Clock clock);

    /**
     * @brief How many scopes currently hold state; used to assert boundedness.
     */
    [[nodiscard]] std::size_t trackedScopes() const;

private:
    struct ScopeState {
        struct OperationState {
            bool seen = false;
            std::chrono::steady_clock::time_point window_start{};
            std::uint64_t suppressed = 0;
        };
        OperationState operations[ErrorOperationCount];
    };

    Clock clock_;
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, ScopeState> scopes_;
};

/**
 * @brief The throttle scope reserved for errors not tied to a registry entry.
 */
inline constexpr std::uint64_t ServiceErrorScope = 0;

}  // namespace papi::detail
