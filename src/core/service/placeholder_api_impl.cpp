#include "core/service/placeholder_api_impl.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "endstone_papi/events.h"

#include "core/parser/bracket_replacer.h"
#include "core/parser/identifier.h"

namespace papi::detail {
namespace {

/**
 * @brief The historical pattern PAPI 0.0.1 documented.
 *
 * Kept verbatim for source compatibility. It is not used for parsing or validation.
 */
constexpr std::string_view LegacyPlaceholderPattern = "[{]([^{}]+)[}]";

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

/**
 * @brief Adapts a historical processor callback to the expansion contract.
 *
 * Registered like any other expansion so it is cleaned up and destroyed on the same
 * owner-driven schedule; the std::function cannot outlive its plugin's module.
 *
 * Two limitations are inherent to the old signature and are documented on the public
 * API: every return value is used verbatim, so the callback cannot express "leave
 * this placeholder alone", and it only ever receives an online Player.
 */
class LegacyProcessorExpansion final : public PlaceholderExpansion {
public:
    LegacyProcessorExpansion(std::string identifier, std::string owner, PlaceholderAPI::Processor processor,
                             Platform &platform)
        : identifier_(std::move(identifier)), owner_(std::move(owner)), processor_(std::move(processor)),
          platform_(platform)
    {
    }

    [[nodiscard]] std::string getIdentifier() const override { return identifier_; }
    [[nodiscard]] std::string getAuthor() const override { return owner_; }
    [[nodiscard]] std::string getVersion() const override { return "legacy"; }

    [[nodiscard]] std::optional<std::string> onRequest(const endstone::OfflinePlayer *player,
                                                       const std::string_view params) override
    {
        if (!processor_) {
            return std::nullopt;
        }
        // The old callback takes a Player, so an offline-only player is resolved
        // through the server by identity and becomes null when not online.
        const endstone::Player *online = player ? platform_.getOnlinePlayer(player->getUniqueId()) : nullptr;
        return processor_(online, std::string(params));
    }

    void onUnregister(UnregisterReason) override
    {
        // Drop the callback here rather than at destruction so it cannot survive
        // into a window where its plugin's code may already be gone.
        processor_ = nullptr;
    }

private:
    std::string identifier_;
    std::string owner_;
    PlaceholderAPI::Processor processor_;
    Platform &platform_;
};

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

        std::optional<std::string> value;
        std::string error_message;
        if (!invokeProvider([&] { value = expansion->onRequest(player_, params); }, error_message)) {
            service_.logProviderError(lease.getEntry()->getInfo(), lease.getEntry()->getGeneration(),
                                      ErrorOperation::Ordinary, error_message);
            return std::nullopt;
        }

        // If the provider unregistered itself during the call its answer no longer
        // reflects a registered expansion, so the original token is kept instead.
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
    RelationalResolver(const PlaceholderApiImpl &service, const endstone::Player &one, const endstone::Player &two)
        : service_(service), one_(one), two_(two)
    {
    }

    [[nodiscard]] std::optional<std::string> resolve(const std::string_view canonical_identifier,
                                                     const std::string_view params) override
    {
        // The relational form carries a mandatory rel_ prefix, which the caller has
        // not stripped yet because the scanner splits on the first underscore only.
        constexpr std::string_view prefix = "rel";
        if (canonical_identifier != prefix) {
            return std::nullopt;
        }

        const auto separator = params.find('_');
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
        // Capability is a copied flag, never probed with a cast across the plugin
        // boundary.
        if (!entry->supportsRelationalPlaceholders()) {
            return std::nullopt;
        }
        const auto expansion = lease.getExpansion();
        if (!expansion) {
            return std::nullopt;
        }

        std::optional<std::string> value;
        std::string error_message;
        if (!invokeProvider([&] { value = expansion->onRelationalRequest(one_, two_, relational_params); },
                            error_message)) {
            service_.logProviderError(entry->getInfo(), entry->getGeneration(), ErrorOperation::Relational,
                                      error_message);
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

PlaceholderApiImpl::~PlaceholderApiImpl()
{
    // A correctly managed service is already shut down by its plugin. This is only a
    // backstop for a service that is destroyed without one.
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

    // Off-thread use is a caller bug. Report it, but bounded: a parse loop on a
    // worker thread would otherwise flood the console.
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
    OrdinaryResolver resolver{*this, player};
    return replacePlaceholders(text, resolver);
}

std::string PlaceholderApiImpl::setRelationalPlaceholders(const endstone::Player &one, const endstone::Player &two,
                                                          const std::string_view text) const
{
    if (!canOperate(ErrorOperation::ThreadPolicy, "parsing relational placeholders")) {
        return std::string(text);
    }
    RelationalResolver resolver{*this, one, two};
    return replacePlaceholders(text, resolver);
}

bool PlaceholderApiImpl::containsPlaceholders(const std::string_view text) const noexcept
{
    // Purely lexical, so it needs neither the registry nor the platform and stays
    // meaningful even on a retained inert service.
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
    platform().callEvent(event);
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
    ExpansionUnregisteredEvent event{removed->info, removed->reason};
    platform().callEvent(event);
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
    for (const auto &entry : removed) {
        ExpansionUnregisteredEvent event{entry.info, entry.reason};
        platform().callEvent(event);
    }
    return removed.size();
}

PAPI_SUPPRESS_DEPRECATED_BEGIN

bool PlaceholderApiImpl::registerPlaceholder(const endstone::Plugin &plugin, const std::string_view identifier,
                                             Processor processor) const
{
    if (!processor) {
        return false;
    }

    // The historical signature is const and takes a const plugin, but registration
    // needs a mutable owner. The cast is safe because Endstone hands out the same
    // plugin object either way, and the registry only uses it as identity.
    auto &self = const_cast<PlaceholderApiImpl &>(*this);
    auto &owner = const_cast<endstone::Plugin &>(plugin);

    auto expansion = std::make_shared<LegacyProcessorExpansion>(std::string(identifier), plugin.getName(),
                                                                std::move(processor), platform());
    return self.registerExpansion(owner, std::move(expansion));
}

std::string PlaceholderApiImpl::getPlaceholderPattern() const
{
    return std::string(LegacyPlaceholderPattern);
}

PAPI_SUPPRESS_DEPRECATED_END

void PlaceholderApiImpl::handlePluginDisabled(endstone::Plugin &plugin)
{
    if (!isActive()) {
        return;
    }

    // The plugin's enabled flag is already false by the time this event arrives, so
    // removal must not be gated on the owner still being enabled.
    auto removed = manager_.detachForDisabledPlugin(plugin, plugin.getName());
    for (auto &entry : removed) {
        finishRemoval(entry);
    }
    for (const auto &entry : removed) {
        ExpansionUnregisteredEvent event{entry.info, entry.reason};
        platform().callEvent(event);
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

    auto cleanup = [this, entry, reason, generation, info] {
        if (const auto expansion = entry->getExpansion()) {
            std::string error_message;
            if (!invokeProvider([&] { expansion->onUnregister(reason); }, error_message)) {
                logProviderError(info, generation, ErrorOperation::Unregister, error_message);
            }
        }

        // Release the provider object here, while the module and interpreter that
        // own its code are still loaded.
        auto released = entry->releaseExpansion();
        released.reset();

        entry->clearOwner();
        throttle_.forget(generation);
    };

    if (entry->requestCleanup(cleanup)) {
        cleanup();
    }
    // Otherwise a callback is still running and the last call lease runs it.
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

void PlaceholderApiImpl::shutdown()
{
    // Only the caller that wins this transition performs teardown, so a second call
    // is harmless and no cleanup callback runs twice.
    auto expected = ServiceState::Active;
    if (!state_.compare_exchange_strong(expected, ServiceState::Stopping, std::memory_order_acq_rel)) {
        return;
    }

    // From here parsing returns input unchanged and mutations refuse, so nothing new
    // can enter the registry while it is being emptied.
    auto removed = manager_.detachAll(UnregisterReason::PapiShutdown);
    for (auto &entry : removed) {
        finishRemoval(entry);
    }

    // Deliberately no unregister events: listeners and the event system are being
    // torn down at the same time, so firing them would be unpredictable.
    throttle_.clear();
    state_.store(ServiceState::Inactive, std::memory_order_release);
}

}  // namespace papi::detail
