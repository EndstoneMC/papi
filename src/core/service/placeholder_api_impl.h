#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "endstone_papi/placeholder_api.h"

#include "core/deprecation.h"
#include "core/diagnostics/error_throttle.h"
#include "core/platform.h"
#include "core/registry/expansion_manager.h"

namespace papi::detail {

/**
 * @brief The lifecycle state of the service.
 *
 * The transition is one-way. Once shutdown begins the service never becomes usable
 * again, which is what makes a consumer's retained pointer permanently safe rather
 * than intermittently valid.
 */
enum class ServiceState : std::uint8_t {
    Active = 0,
    Stopping = 1,
    Inactive = 2,
};

/**
 * @brief The concrete native PlaceholderAPI service.
 *
 * Exactly one instance exists per PAPI plugin lifetime. It owns the registry and
 * the parser, and it is the only implementation of the public service contract:
 * neither C++ nor Python plugins can supply their own.
 *
 * Consumers obtain a shared_ptr through Endstone's service manager and may keep it
 * for as long as they like. Endstone cannot revoke it, so instead this object
 * becomes inert at shutdown: every method then returns a safe answer without
 * touching the platform, any plugin, or any provider. Parsing returns its input
 * unchanged, queries come back empty, and mutations fail.
 *
 * Threading: parsing and registry mutation call into provider code, so they require
 * the primary thread and fail safely elsewhere rather than being scheduled behind
 * the caller's back. isActive and containsPlaceholders are pure and safe anywhere.
 */
class PlaceholderApiImpl final : public PlaceholderAPI {
public:
    /**
     * @param platform shared so that a consumer-retained inert service can never
     *        hold a dangling platform reference, even after the PAPI plugin object
     *        is destroyed; the platform is detached at shutdown and remains a safe
     *        no-op from then on
     * @param papi_plugin_name the PAPI plugin's name, copied for diagnostics
     */
    PlaceholderApiImpl(std::shared_ptr<Platform> platform, std::string papi_plugin_name);
    ~PlaceholderApiImpl() override;

    // PlaceholderAPI
    [[nodiscard]] bool isActive() const noexcept override;
    [[nodiscard]] std::string setPlaceholders(const endstone::OfflinePlayer *player,
                                              std::string_view text) const override;
    [[nodiscard]] std::string setRelationalPlaceholders(const endstone::Player &one, const endstone::Player &two,
                                                        std::string_view text) const override;
    [[nodiscard]] bool containsPlaceholders(std::string_view text) const noexcept override;
    [[nodiscard]] bool isRegistered(std::string_view identifier) const override;
    [[nodiscard]] std::vector<std::string> getRegisteredIdentifiers() const override;
    [[nodiscard]] std::vector<ExpansionInfo> getExpansions() const override;
    bool registerExpansion(endstone::Plugin &owner, std::shared_ptr<PlaceholderExpansion> expansion) override;
    bool unregisterExpansion(endstone::Plugin &owner, std::string_view identifier) override;
    std::size_t unregisterExpansions(endstone::Plugin &owner) override;

    // Deprecated compatibility surface, kept for one release. Implemented by wrapping
    // the callback in an ordinary owner-tracked expansion so it shares the same
    // lifecycle as everything else in the registry.
    PAPI_SUPPRESS_DEPRECATED_BEGIN
    [[nodiscard]] bool registerPlaceholder(const endstone::Plugin &plugin, std::string_view identifier,
                                           Processor processor) const override;
    [[nodiscard]] std::string getPlaceholderPattern() const override;
    PAPI_SUPPRESS_DEPRECATED_END

    /**
     * @brief Transitions the service out of Active so nothing new can start.
     *
     * Atomically marks the service Stopping: isActive returns false, parsing
     * returns input unchanged, and mutations are refused.  The registry is *not*
     * drained yet -- this is the window where the bootstrap unregisters the named
     * service from the ServiceManager so shutdown callbacks cannot rediscover PAPI
     * as a still-published service.
     *
     * Idempotent.  Returns true only for the caller that performed the transition.
     *
     * @see finishShutdown
     */
    bool beginShutdown();

    /**
     * @brief Drains the registry and completes the transition to Inactive.
     *
     * Removes every entry, runs each expansion's onUnregister(PapiShutdown) cleanup
     * exactly once, releases every provider object, and clears the throttle.  Safe
     * to call only after beginShutdown; if the service is not Stopping this is a
     * no-op.  Idempotent.
     *
     * Must be called while the interpreter and provider modules are still loaded,
     * which in practice means from PAPI's own onDisable.
     *
     * @see beginShutdown
     */
    void finishShutdown();

    /**
     * @brief Convenience: beginShutdown followed by finishShutdown.
     *
     * Equivalent to calling beginShutdown then finishShutdown.  Idempotent.
     * Provided for the destructor and for callers that do not need to interleave
     * ServiceManager unregistration between the two phases.
     */
    void shutdown();

    /**
     * @brief Handles a plugin being disabled.
     *
     * Removes every expansion the plugin owns and every expansion that declared it
     * as a required plugin, before Endstone can unload the plugin's module.
     */
    void handlePluginDisabled(endstone::Plugin &plugin);

    /**
     * @brief Handles a player leaving, for expansions that opted into cleanup.
     */
    void handlePlayerQuit(const endstone::Player &player);

    /**
     * @brief Replaces the throttle clock; used by tests to avoid sleeping.
     */
    void setErrorClock(ErrorThrottle::Clock clock);

private:
    class OrdinaryResolver;
    class RelationalResolver;

    /**
     * @brief Whether a mutating or parsing operation may proceed.
     *
     * Reports a throttled diagnostic when the caller is off the primary thread, so
     * the mistake is visible without flooding the console.
     */
    [[nodiscard]] bool canOperate(ErrorOperation operation, std::string_view what) const;

    /**
     * @brief Runs an expansion's cleanup once and releases it, containing errors.
     *
     * Called only outside the registry lock. The provider object is destroyed here
     * rather than left to an arbitrary later point, so it cannot outlive the module
     * that defines it.
     */
    void finishRemoval(RemovedEntry &removed);

    void logProviderError(const ExpansionInfo &info, std::uint64_t generation, ErrorOperation operation,
                          std::string_view message) const;

    /**
     * @brief Reports a bounded reentrancy violation (cycle or depth budget).
     *
     * Throttled per scope: service-wide for depth-budget violations (generation 0),
     * per-entry for cycles. The original placeholder is always preserved, so this
     * is a diagnostic, not a fatal error.
     */
    void logReentrancyError(const ExpansionInfo &info, std::uint64_t generation, std::string_view reason) const;

    /**
     * @brief Dispatches a PAPI-owned lifecycle event, containing listener exceptions.
     *
     * A listener is third-party code; an unknown C++ exception thrown from callEvent
     * must not escape into Endstone's command, event, or service dispatch. Failures
     * are logged as throttled provider-style diagnostics.
     */
    void dispatchLifecycleEvent(endstone::Event &event) const;

    Platform &platform() const noexcept { return *platform_; }

    std::shared_ptr<Platform> platform_;
    std::string papi_plugin_name_;

    // Mutable so the const parse and query methods can report throttled errors.
    mutable ErrorThrottle throttle_;
    mutable std::atomic<ServiceState> state_{ServiceState::Active};
    ExpansionManager manager_;
};

}  // namespace papi::detail
