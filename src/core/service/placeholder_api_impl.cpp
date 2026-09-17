#include "core/service/placeholder_api_impl.h"

#include <algorithm>
#include <exception>
#include <functional>
#include <unordered_set>
#include <utility>

#include "endstone_papi/events.h"

#include "core/parser/bracket_replacer.h"
#include "core/parser/identifier.h"

namespace papi::detail {
namespace {

/**
 * @brief Maximum nesting depth for reentrant parsing.
 *
 * A provider callback can call setPlaceholders again, forming a chain. C++ has no
 * language-level recursion limit, so without this budget a cycle or a deep chain
 * exhausts the native stack. Eight is generous for legitimate indirect resolution
 * while keeping the worst-case stack depth bounded.
 */
inline constexpr unsigned ParseDepthBudget = 8;

/**
 * @brief Thread-local nesting depth for reentrant parse calls.
 *
 * Zero at the top level, incremented by ParseDepthGuard on entry to
 * setPlaceholders/setRelationalPlaceholders, decremented on exit.
 */
unsigned &parseDepth()
{
    thread_local unsigned depth = 0;
    return depth;
}

/**
 * @brief Keys an active registration by service and manager-local generation.
 */
struct ActiveExpansionId {
    const void *service;
    std::uint64_t generation;

    bool operator==(const ActiveExpansionId &) const = default;
};

struct ActiveExpansionIdHash {
    [[nodiscard]] std::size_t operator()(const ActiveExpansionId &id) const noexcept
    {
        const auto service_hash = std::hash<const void *>{}(id.service);
        const auto generation_hash = std::hash<std::uint64_t>{}(id.generation);
        return service_hash ^ (generation_hash + 0x9e3779b9U + (service_hash << 6U) + (service_hash >> 2U));
    }
};

/**
 * @brief Thread-local set of expansion registrations used for cycle detection.
 */
std::unordered_set<ActiveExpansionId, ActiveExpansionIdHash> &activeExpansions()
{
    thread_local std::unordered_set<ActiveExpansionId, ActiveExpansionIdHash> expansions;
    return expansions;
}

/**
 * @brief RAII guard that increments the parse depth on construction and
 *        decrements it on destruction, even if an exception propagates.
 */
class ParseDepthGuard {
public:
    ParseDepthGuard() { ++parseDepth(); }

    ~ParseDepthGuard() { --parseDepth(); }

    ParseDepthGuard(const ParseDepthGuard &) = delete;
    ParseDepthGuard &operator=(const ParseDepthGuard &) = delete;

    [[nodiscard]] static bool budgetExceeded() { return parseDepth() > ParseDepthBudget; }
};

/**
 * @brief RAII guard that tracks an expansion generation as active during
 *        resolution, so reentrant resolution of the same generation is detected
 *        as a cycle.
 *
 * On construction, checks whether the generation is already active. If so,
 * reEntered() returns true and the caller must skip the provider call. Otherwise
 * the generation is inserted and removed on destruction.
 */
class ActiveExpansionGuard {
public:
    ActiveExpansionGuard(const void *service, const std::uint64_t generation) : id_{service, generation}
    {
        auto &active = activeExpansions();
        if (!active.contains(id_)) {
            active.insert(id_);
            pushed_ = true;
        }
    }

    ~ActiveExpansionGuard()  // NOLINT(bugprone-exception-escape)
    {
        if (pushed_) {
            activeExpansions().erase(id_);
        }
    }

    ActiveExpansionGuard(const ActiveExpansionGuard &) = delete;
    ActiveExpansionGuard &operator=(const ActiveExpansionGuard &) = delete;

    [[nodiscard]] bool reEntered() const noexcept { return !pushed_; }

private:
    ActiveExpansionId id_;
    bool pushed_ = false;
};

/**
 * @brief Calls provider code and reports failure instead of propagating it.
 *
 * A provider is third-party and may throw anything. Nothing may reach Endstone's
 * command, event, or service dispatch, so both std and unknown exceptions are
 * contained here. Access violations, signals, and undefined behavior are not
 * catchable and are explicitly out of scope.
 */
template <typename Fn>
[[nodiscard]] bool invokeProvider(Fn &&fn, std::string &error_message)
{
    try {
        std::forward<Fn>(fn)();
        return true;
    }
    catch (const std::exception &e) {
        error_message = e.what();
        return false;
    }
    catch (...) {
        error_message = "unknown C++ exception";
        return false;
    }
}

}  // namespace

/**
 * @brief Resolves ordinary placeholders against the registry for one parse.
 */
class PlaceholderApiImpl::OrdinaryResolver final : public PlaceholderResolver {
public:
    OrdinaryResolver(const PlaceholderApiImpl &service, const endstone::OfflinePlayer *player)
        : service_(service), player_(player)
    {
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    [[nodiscard]] std::optional<std::string> resolve(const std::string_view canonical_identifier,
                                                     const std::string_view params) override
    {
        auto lease = service_.manager_.lease(canonical_identifier);
        if (!lease) {
            return std::nullopt;
        }
        const auto expansion = lease.getExpansion();
        if (!expansion) {
            return std::nullopt;
        }

        const auto generation = lease.getEntry()->getGeneration();
        const ActiveExpansionGuard cycle_guard{&service_, generation};
        if (cycle_guard.reEntered()) {
            // Preserve the token when provider dispatch cycles.
            service_.logReentrancyError(lease.getEntry()->getInfo(), generation, "cycle detected");
            return std::nullopt;
        }

        std::optional<std::string> value;
        std::string error_message;
        if (!invokeProvider([&] { value = expansion->onRequest(player_, params); }, error_message)) {
            service_.logProviderError(lease.getEntry()->getInfo(), generation, ErrorOperation::Ordinary, error_message);
            return std::nullopt;
        }

        // Discard results from entries retired during their callback.
        if (!lease.isStillActive()) {
            return std::nullopt;
        }
        return value;
    }

private:
    const PlaceholderApiImpl &service_;
    const endstone::OfflinePlayer *player_;
};

/**
 * @brief Resolves relational placeholders against the registry for one parse.
 */
class PlaceholderApiImpl::RelationalResolver final : public PlaceholderResolver {
public:
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    RelationalResolver(const PlaceholderApiImpl &service, const endstone::Player &one, const endstone::Player &two)
        : service_(service), one_(one), two_(two)
    {
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    [[nodiscard]] std::optional<std::string> resolve(const std::string_view canonical_identifier,
                                                     const std::string_view params) override
    {
        // Relational placeholders use {rel:identifier:params}. The generic bracket
        // parser already stripped "rel:"; split only the next colon so provider
        // params remain byte-for-byte intact, including any later colons.
        constexpr std::string_view prefix = "rel";
        if (canonical_identifier != prefix) {
            return std::nullopt;
        }

        const auto separator = params.find(':');
        if (separator == std::string_view::npos) {
            return std::nullopt;
        }
        const auto raw_identifier = params.substr(0, separator);
        if (!isValidIdentifier(raw_identifier)) {
            return std::nullopt;
        }
        const auto identifier = canonicalizeIdentifier(raw_identifier);
        const auto relational_params = params.substr(separator + 1);

        auto lease = service_.manager_.lease(identifier);
        if (!lease) {
            return std::nullopt;
        }
        const auto &entry = lease.getEntry();
        if (!entry->supportsRelationalPlaceholders()) {
            return std::nullopt;
        }
        const auto expansion = lease.getExpansion();
        if (!expansion) {
            return std::nullopt;
        }

        const auto generation = entry->getGeneration();
        const ActiveExpansionGuard cycle_guard{&service_, generation};
        if (cycle_guard.reEntered()) {
            service_.logReentrancyError(entry->getInfo(), generation, "relational cycle detected");
            return std::nullopt;
        }

        std::optional<std::string> value;
        std::string error_message;
        if (!invokeProvider([&] { value = expansion->onRelationalRequest(one_, two_, relational_params); },
                            error_message)) {
            service_.logProviderError(entry->getInfo(), generation, ErrorOperation::Relational, error_message);
            return std::nullopt;
        }

        if (!lease.isStillActive()) {
            return std::nullopt;
        }
        return value;
    }

private:
    const PlaceholderApiImpl &service_;
    const endstone::Player &one_;
    const endstone::Player &two_;
};

PlaceholderApiImpl::PlaceholderApiImpl(std::shared_ptr<Platform> platform, std::string papi_plugin_name)
    : platform_(std::move(platform)), papi_plugin_name_(std::move(papi_plugin_name)), manager_(*platform_, throttle_)
{
}

PlaceholderApiImpl::~PlaceholderApiImpl()  // NOLINT(bugprone-exception-escape)
{
    // Backstop for destruction outside normal plugin shutdown.
    shutdown();
}

void PlaceholderApiImpl::setErrorClock(ErrorThrottle::Clock clock)
{
    throttle_.setClock(std::move(clock));
}

bool PlaceholderApiImpl::isActive() const noexcept
{
    return state_.load(std::memory_order_acquire) == ServiceState::Active;
}

bool PlaceholderApiImpl::canOperate(const ErrorOperation operation, const std::string_view what) const
{
    if (!isActive()) {
        return false;
    }
    if (platform().isPrimaryThread()) {
        return true;
    }

    // Off-thread diagnostics must remain bounded.
    const auto decision = throttle_.record(ServiceErrorScope, operation);
    if (decision.should_log) {
        std::string message =
            "PlaceholderAPI: " + std::string(what) + " must happen on the server thread. The request was ignored.";
        if (decision.suppressed > 0) {
            message += " (" + std::to_string(decision.suppressed) + " similar messages suppressed)";
        }
        platform().log(endstone::Logger::Error, message);
    }
    return false;
}

std::string PlaceholderApiImpl::setPlaceholders(const endstone::OfflinePlayer *player,
                                                const std::string_view text) const
{
    if (!canOperate(ErrorOperation::ThreadPolicy, "parsing placeholders")) {
        return std::string(text);
    }
    const ParseDepthGuard depth_guard;
    if (ParseDepthGuard::budgetExceeded()) {
        // Preserve the input when nested parsing exceeds its budget.
        logReentrancyError({}, 0, "parse depth budget exceeded");
        return std::string(text);
    }
    OrdinaryResolver resolver{*this, player};
    return replacePlaceholders(text, resolver);
}

std::string PlaceholderApiImpl::setRelationalPlaceholders(const endstone::Player &one, const endstone::Player &two,
                                                          const std::string_view text) const
{
    if (!canOperate(ErrorOperation::ThreadPolicy, "parsing relational placeholders")) {
        return std::string(text);
    }
    const ParseDepthGuard depth_guard;
    if (ParseDepthGuard::budgetExceeded()) {
        logReentrancyError({}, 0, "relational parse depth budget exceeded");
        return std::string(text);
    }
    RelationalResolver resolver{*this, one, two};
    return replacePlaceholders(text, resolver);
}

bool PlaceholderApiImpl::containsPlaceholders(const std::string_view text) const noexcept
{
    return detail::containsPlaceholders(text);
}

bool PlaceholderApiImpl::isRegistered(const std::string_view identifier) const
{
    if (!isActive()) {
        return false;
    }
    return manager_.isRegistered(identifier);
}

std::vector<std::string> PlaceholderApiImpl::getRegisteredIdentifiers() const
{
    if (!isActive()) {
        return {};
    }
    return manager_.getRegisteredIdentifiers();
}

std::vector<ExpansionInfo> PlaceholderApiImpl::getExpansions() const
{
    if (!isActive()) {
        return {};
    }
    return manager_.getExpansions();
}

bool PlaceholderApiImpl::registerExpansion(endstone::Plugin &owner, std::shared_ptr<PlaceholderExpansion> expansion)
{
    if (!canOperate(ErrorOperation::ThreadPolicy, "registering an expansion")) {
        return false;
    }

    const auto result = manager_.registerExpansion(owner, std::move(expansion));
    if (!result.registered) {
        return false;
    }

    ExpansionRegisteredEvent event{result.info};
    dispatchLifecycleEvent(event);
    return true;
}

bool PlaceholderApiImpl::unregisterExpansion(endstone::Plugin &owner, const std::string_view identifier)
{
    if (!canOperate(ErrorOperation::ThreadPolicy, "unregistering an expansion")) {
        return false;
    }

    auto removed = manager_.detach(owner, identifier, UnregisterReason::Explicit);
    if (!removed) {
        return false;
    }

    finishRemoval(*removed);
    return true;
}

std::size_t PlaceholderApiImpl::unregisterExpansions(endstone::Plugin &owner)
{
    if (!canOperate(ErrorOperation::ThreadPolicy, "unregistering expansions")) {
        return 0;
    }

    auto removed = manager_.detachByOwner(owner, UnregisterReason::Explicit);
    for (auto &entry : removed) {
        finishRemoval(entry);
    }
    return removed.size();
}

void PlaceholderApiImpl::handlePluginDisabled(endstone::Plugin &plugin)
{
    if (!isActive()) {
        return;
    }

    // Disable cleanup runs after the owner's enabled flag is cleared.
    auto removed = manager_.detachForDisabledPlugin(plugin, plugin.getName());
    for (auto &entry : removed) {
        finishRemoval(entry);
    }
}

void PlaceholderApiImpl::handlePlayerQuit(const endstone::Player &player)
{
    if (!isActive()) {
        return;
    }

    for (const auto &entry : manager_.snapshotPlayerCleanupEntries()) {
        CallLease lease{entry};
        if (!lease) {
            continue;
        }
        const auto expansion = lease.getExpansion();
        if (!expansion) {
            continue;
        }
        std::string error_message;
        if (!invokeProvider([&] { expansion->onPlayerQuit(player); }, error_message)) {
            logProviderError(entry->getInfo(), entry->getGeneration(), ErrorOperation::PlayerCleanup, error_message);
        }
    }
}

void PlaceholderApiImpl::finishRemoval(RemovedEntry &removed)
{
    auto &entry = removed.entry;
    const auto reason = removed.reason;
    const auto generation = entry->getGeneration();
    const auto info = entry->getInfo();

    // Cleanup may run from a noexcept call-lease destructor.
    // NOLINTNEXTLINE(bugprone-exception-escape)
    auto cleanup = [this, entry, reason, generation, info] {
        try {
            if (const auto expansion = entry->getExpansion()) {
                std::string error_message;
                if (!invokeProvider([&] { expansion->onUnregister(reason); }, error_message)) {
                    logProviderError(info, generation, ErrorOperation::Unregister, error_message);
                }
            }

            // Providers must be released before their modules unload.
            auto released = entry->releaseExpansion();
            released.reset();

            throttle_.forget(generation);

            if (reason != UnregisterReason::PapiShutdown) {
                ExpansionUnregisteredEvent event{info, reason};
                dispatchLifecycleEvent(event);
            }
        }
        // NOLINTNEXTLINE(bugprone-empty-catch)
        catch (...) {
            // A deferred cleanup must not escape a noexcept destructor.
        }
    };

    if (entry->requestCleanup(cleanup)) {
        cleanup();
    }
}

void PlaceholderApiImpl::logProviderError(const ExpansionInfo &info, const std::uint64_t generation,
                                          const ErrorOperation operation, const std::string_view message) const
{
    const auto decision = throttle_.record(generation, operation);
    if (!decision.should_log) {
        return;
    }

    std::string text = "Expansion '" + info.identifier + "' from plugin '" + info.owner + "' failed during " +
                       std::string(toString(operation)) + ": " + std::string(message);
    if (decision.suppressed > 0) {
        text += " (" + std::to_string(decision.suppressed) + " similar messages suppressed)";
    }
    platform().log(endstone::Logger::Error, text);
}

void PlaceholderApiImpl::logReentrancyError(const ExpansionInfo &info, const std::uint64_t generation,
                                            const std::string_view reason) const
{
    // Depth-budget violations are service-wide (generation 0); cycles are per-entry.
    const auto scope = generation == 0 ? ServiceErrorScope : generation;
    const auto decision = throttle_.record(scope, ErrorOperation::ParseReentrancy);
    if (!decision.should_log) {
        return;
    }

    std::string text = "PlaceholderAPI: " + std::string(reason);
    if (!info.identifier.empty()) {
        text += " for expansion '" + info.identifier + "' from plugin '" + info.owner + "'";
    }
    text += "; the original placeholder was preserved";
    if (decision.suppressed > 0) {
        text += " (" + std::to_string(decision.suppressed) + " similar messages suppressed)";
    }
    platform().log(endstone::Logger::Warning, text);
}

void PlaceholderApiImpl::dispatchLifecycleEvent(endstone::Event &event) const
{
    try {
        platform().callEvent(event);
    }
    catch (const std::exception &e) {
        const auto decision = throttle_.record(ServiceErrorScope, ErrorOperation::EventDispatch);
        if (!decision.should_log) {
            return;
        }
        std::string text =
            "PlaceholderAPI: a listener threw during " + std::string(event.getEventName()) + " dispatch: " + e.what();
        if (decision.suppressed > 0) {
            text += " (" + std::to_string(decision.suppressed) + " similar messages suppressed)";
        }
        platform().log(endstone::Logger::Warning, text);
    }
    catch (...) {
        const auto decision = throttle_.record(ServiceErrorScope, ErrorOperation::EventDispatch);
        if (!decision.should_log) {
            return;
        }
        std::string text = "PlaceholderAPI: a listener threw an unknown C++ exception during " +
                           std::string(event.getEventName()) + " dispatch";
        if (decision.suppressed > 0) {
            text += " (" + std::to_string(decision.suppressed) + " similar messages suppressed)";
        }
        platform().log(endstone::Logger::Warning, text);
    }
}

bool PlaceholderApiImpl::beginShutdown()
{
    // Registry draining waits until the service is withdrawn.
    auto expected = ServiceState::Active;
    return state_.compare_exchange_strong(expected, ServiceState::Stopping, std::memory_order_acq_rel);
}

void PlaceholderApiImpl::finishShutdown()
{
    if (state_.load(std::memory_order_acquire) != ServiceState::Stopping) {
        return;
    }

    auto removed = manager_.detachAll(UnregisterReason::PapiShutdown);
    for (auto &entry : removed) {
        finishRemoval(entry);
    }

    // Shutdown suppresses unregister events while listeners are being torn down.
    throttle_.clear();
    state_.store(ServiceState::Inactive, std::memory_order_release);
}

void PlaceholderApiImpl::shutdown()
{
    beginShutdown();
    finishShutdown();
}

}  // namespace papi::detail
