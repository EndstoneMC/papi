#include "core/registry/expansion_manager.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "core/parser/identifier.h"

namespace papi::detail {
namespace {

// Contains every C++ exception at the provider boundary.
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

std::string_view toString(const RegisterError error) noexcept
{
    switch (error) {
    case RegisterError::None:
        return "none";
    case RegisterError::NotPrimaryThread:
        return "registration must happen on the server thread";
    case RegisterError::ServiceInactive:
        return "PlaceholderAPI is not active";
    case RegisterError::NullExpansion:
        return "the expansion is null";
    case RegisterError::OwnerNotEnabled:
        return "the registering plugin is not enabled";
    case RegisterError::InvalidIdentifier:
        return "the identifier must match [A-Za-z0-9][A-Za-z0-9-]*";
    case RegisterError::InvalidMetadata:
        return "the author and version must not be empty";
    case RegisterError::InvalidRequiredPluginName:
        return "the required plugin name must not be empty";
    case RegisterError::RequiredPluginUnavailable:
        return "the required plugin is not loaded and enabled";
    case RegisterError::PreflightRefused:
        return "the expansion refused registration";
    case RegisterError::DuplicateIdentifier:
        return "the identifier is already registered";
    case RegisterError::ProviderThrew:
        return "the expansion raised an error";
    }
    return "unknown";
}

ExpansionEntry::ExpansionEntry(const std::uint64_t generation, ExpansionInfo info, const bool supports_player_cleanup,
                               std::shared_ptr<PlaceholderExpansion> expansion, endstone::Plugin *owner)
    : generation_(generation), info_(std::move(info)), supports_player_cleanup_(supports_player_cleanup),
      expansion_(std::move(expansion)), owner_(owner)
{
}

bool ExpansionEntry::isActive() const noexcept
{
    std::scoped_lock lock(mutex_);
    return active_;
}

endstone::Plugin *ExpansionEntry::getOwner() const noexcept
{
    std::scoped_lock lock(mutex_);
    return owner_;
}

bool ExpansionEntry::isOwnedBy(const endstone::Plugin &plugin) const noexcept
{
    std::scoped_lock lock(mutex_);
    return owner_ == &plugin;
}

bool ExpansionEntry::retire() noexcept
{
    std::scoped_lock lock(mutex_);
    if (!active_) {
        return false;
    }
    active_ = false;
    // Retired entries must not retain a plugin pointer while cleanup is deferred.
    owner_ = nullptr;
    return true;
}

bool ExpansionEntry::requestCleanup(std::function<void()> cleanup)
{
    std::scoped_lock lock(mutex_);
    if (cleanup_done_) {
        return false;
    }
    if (active_calls_ == 0) {
        cleanup_done_ = true;
        return true;
    }
    // Self-unregistration defers cleanup until the active callback returns.
    deferred_cleanup_ = std::move(cleanup);
    return false;
}

bool ExpansionEntry::hasActiveCalls() const noexcept
{
    std::scoped_lock lock(mutex_);
    return active_calls_ > 0;
}

std::shared_ptr<PlaceholderExpansion> ExpansionEntry::getExpansion() const
{
    std::scoped_lock lock(mutex_);
    return expansion_;
}

std::shared_ptr<PlaceholderExpansion> ExpansionEntry::releaseExpansion()
{
    std::scoped_lock lock(mutex_);
    return std::move(expansion_);
}

CallLease::CallLease(std::shared_ptr<ExpansionEntry> entry)
{
    if (!entry) {
        return;
    }
    {
        std::scoped_lock lock(entry->mutex_);
        if (!entry->active_) {
            return;
        }
        ++entry->active_calls_;
    }
    entry_ = std::move(entry);
}

CallLease::CallLease(CallLease &&other) noexcept : entry_(std::move(other.entry_))
{
    other.entry_.reset();
}

CallLease &CallLease::operator=(CallLease &&other) noexcept  // NOLINT(bugprone-exception-escape)
{
    if (this != &other) {
        release();
        entry_ = std::move(other.entry_);
        other.entry_.reset();
    }
    return *this;
}

CallLease::~CallLease()  // NOLINT(bugprone-exception-escape)
{
    release();
}

std::shared_ptr<PlaceholderExpansion> CallLease::getExpansion() const
{
    return entry_ ? entry_->getExpansion() : nullptr;
}

bool CallLease::isStillActive() const noexcept
{
    return entry_ && entry_->isActive();
}

void CallLease::release()
{
    if (!entry_) {
        return;
    }

    // Deferred provider cleanup runs after the entry lock is released.
    std::function<void()> deferred;
    {
        std::scoped_lock lock(entry_->mutex_);
        --entry_->active_calls_;
        if (entry_->active_calls_ == 0 && entry_->deferred_cleanup_ && !entry_->cleanup_done_) {
            entry_->cleanup_done_ = true;
            deferred = std::move(entry_->deferred_cleanup_);
            entry_->deferred_cleanup_ = nullptr;
        }
    }

    auto entry = std::move(entry_);
    entry_.reset();

    if (deferred) {
        std::string ignored;
        (void)invokeProvider(deferred, ignored);
    }
}

ExpansionManager::ExpansionManager(Platform &platform, ErrorThrottle &throttle)
    : platform_(platform), throttle_(throttle)
{
}

RegisterResult ExpansionManager::registerExpansion(endstone::Plugin &owner,
                                                   std::shared_ptr<PlaceholderExpansion> expansion)
{
    if (!expansion) {
        return {false, RegisterError::NullExpansion, {}};
    }

    if (!platform_.isPluginEnabled(owner)) {
        return {false, RegisterError::OwnerNotEnabled, {}};
    }

    // Provider metadata is copied once outside the registry lock.
    ExpansionInfo info;
    bool supports_player_cleanup = false;
    std::string raw_identifier;
    std::optional<std::string> required_plugin;
    std::string error_message;

    const bool metadata_ok = invokeProvider(
        [&] {
            raw_identifier = expansion->getIdentifier();
            info.name = expansion->getName();
            info.author = expansion->getAuthor();
            info.version = expansion->getVersion();
            required_plugin = expansion->getRequiredPlugin();
            info.relational = expansion->supportsRelationalPlaceholders();
            supports_player_cleanup = expansion->supportsPlayerCleanup();
        },
        error_message);

    if (!metadata_ok) {
        const auto decision = throttle_.record(ServiceErrorScope, ErrorOperation::Metadata);
        if (decision.should_log) {
            platform_.log(endstone::Logger::Error, "Plugin '" + owner.getName() +
                                                       "' failed to register an expansion: reading its metadata "
                                                       "raised an error: " +
                                                       error_message);
        }
        return {false, RegisterError::ProviderThrew, {}};
    }

    if (!isValidIdentifier(raw_identifier)) {
        platform_.log(endstone::Logger::Error, "Plugin '" + owner.getName() + "' tried to register the identifier '" +
                                                   raw_identifier + "', but " +
                                                   std::string(toString(RegisterError::InvalidIdentifier)) + ".");
        return {false, RegisterError::InvalidIdentifier, {}};
    }

    info.identifier = canonicalizeIdentifier(raw_identifier);
    info.owner = owner.getName();

    if (info.author.empty() || info.version.empty()) {
        platform_.log(endstone::Logger::Error, "Plugin '" + info.owner + "' tried to register expansion '" +
                                                   info.identifier + "', but " +
                                                   std::string(toString(RegisterError::InvalidMetadata)) + ".");
        return {false, RegisterError::InvalidMetadata, {}};
    }

    if (info.name.empty()) {
        info.name = info.identifier;
    }

    if (required_plugin) {
        if (required_plugin->empty()) {
            platform_.log(endstone::Logger::Error,
                          "Plugin '" + info.owner + "' tried to register expansion '" + info.identifier + "', but " +
                              std::string(toString(RegisterError::InvalidRequiredPluginName)) + ".");
            return {false, RegisterError::InvalidRequiredPluginName, {}};
        }
        if (!platform_.isPluginEnabled(*required_plugin)) {
            platform_.log(endstone::Logger::Error, "Expansion '" + info.identifier + "' from plugin '" + info.owner +
                                                       "' requires plugin '" + *required_plugin +
                                                       "', which is not loaded and enabled.");
            return {false, RegisterError::RequiredPluginUnavailable, {}};
        }
        info.required_plugin = *required_plugin;
    }

    bool may_register = false;
    if (!invokeProvider([&] { may_register = expansion->canRegister(); }, error_message)) {
        const auto decision = throttle_.record(ServiceErrorScope, ErrorOperation::CanRegister);
        if (decision.should_log) {
            platform_.log(endstone::Logger::Error,
                          "Expansion '" + info.identifier + "' from plugin '" + info.owner +
                              "' raised an error during its registration check: " + error_message);
        }
        return {false, RegisterError::ProviderThrew, {}};
    }
    if (!may_register) {
        return {false, RegisterError::PreflightRefused, {}};
    }

    // Plugin state is checked immediately before locking; PluginManager calls must stay outside the lock.
    if (!platform_.isPluginEnabled(owner)) {
        return {false, RegisterError::OwnerNotEnabled, {}};
    }
    if (info.required_plugin && !platform_.isPluginEnabled(*info.required_plugin)) {
        return {false, RegisterError::RequiredPluginUnavailable, {}};
    }

    {
        std::scoped_lock lock(mutex_);
        if (!entries_.contains(info.identifier)) {
            auto entry = std::make_shared<ExpansionEntry>(next_generation_++, info, supports_player_cleanup,
                                                          std::move(expansion), &owner);
            entries_.emplace(info.identifier, std::move(entry));
            return {true, RegisterError::None, info};
        }
    }

    platform_.log(endstone::Logger::Error, "Plugin '" + info.owner + "' tried to register expansion '" +
                                               info.identifier + "', but " +
                                               std::string(toString(RegisterError::DuplicateIdentifier)) + ".");
    return {false, RegisterError::DuplicateIdentifier, {}};
}

std::optional<RemovedEntry> ExpansionManager::detach(const endstone::Plugin &owner, const std::string_view identifier,
                                                     const UnregisterReason reason)
{
    if (!isValidIdentifier(identifier)) {
        return std::nullopt;
    }
    const auto canonical = canonicalizeIdentifier(identifier);

    std::shared_ptr<ExpansionEntry> entry;
    {
        std::scoped_lock lock(mutex_);
        const auto it = entries_.find(canonical);
        if (it == entries_.end() || !it->second->isOwnedBy(owner)) {
            return std::nullopt;
        }
        entry = it->second;
        entries_.erase(it);
    }

    if (!entry->retire()) {
        return std::nullopt;
    }
    return RemovedEntry{entry, entry->getInfo(), reason};
}

std::vector<RemovedEntry> ExpansionManager::detachByOwner(const endstone::Plugin &owner, const UnregisterReason reason)
{
    std::vector<std::shared_ptr<ExpansionEntry>> detached;
    {
        std::scoped_lock lock(mutex_);
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (it->second->isOwnedBy(owner)) {
                detached.push_back(it->second);
                it = entries_.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    std::ranges::sort(detached,
                      [](const auto &a, const auto &b) { return a->getInfo().identifier < b->getInfo().identifier; });

    std::vector<RemovedEntry> removed;
    removed.reserve(detached.size());
    for (auto &entry : detached) {
        if (entry->retire()) {
            removed.push_back(RemovedEntry{entry, entry->getInfo(), reason});
        }
    }
    return removed;
}

std::vector<RemovedEntry> ExpansionManager::detachForDisabledPlugin(const endstone::Plugin &plugin,
                                                                    const std::string_view plugin_name)
{
    std::vector<std::pair<std::shared_ptr<ExpansionEntry>, UnregisterReason>> detached;
    {
        std::scoped_lock lock(mutex_);
        for (auto it = entries_.begin(); it != entries_.end();) {
            const auto &entry = it->second;
            const bool owned = entry->isOwnedBy(plugin);
            const auto info = entry->getInfo();
            const bool depends = info.required_plugin.has_value() && *info.required_plugin == plugin_name;
            if (owned || depends) {
                // Ownership takes precedence when both removal reasons apply.
                detached.emplace_back(entry, owned ? UnregisterReason::OwnerDisabled
                                                   : UnregisterReason::RequiredPluginDisabled);
                it = entries_.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    std::ranges::sort(detached, [](const auto &a, const auto &b) {
        return a.first->getInfo().identifier < b.first->getInfo().identifier;
    });

    std::vector<RemovedEntry> removed;
    removed.reserve(detached.size());
    for (auto &[entry, reason] : detached) {
        if (entry->retire()) {
            removed.push_back(RemovedEntry{entry, entry->getInfo(), reason});
        }
    }
    return removed;
}

std::vector<RemovedEntry> ExpansionManager::detachAll(const UnregisterReason reason)
{
    std::vector<std::shared_ptr<ExpansionEntry>> detached;
    {
        std::scoped_lock lock(mutex_);
        detached.reserve(entries_.size());
        for (auto &[identifier, entry] : entries_) {
            detached.push_back(entry);
        }
        entries_.clear();
    }

    std::ranges::sort(detached,
                      [](const auto &a, const auto &b) { return a->getInfo().identifier < b->getInfo().identifier; });

    std::vector<RemovedEntry> removed;
    removed.reserve(detached.size());
    for (auto &entry : detached) {
        if (entry->retire()) {
            removed.push_back(RemovedEntry{entry, entry->getInfo(), reason});
        }
    }
    return removed;
}

std::shared_ptr<ExpansionEntry> ExpansionManager::findCanonical(const std::string_view canonical_identifier) const
{
    std::scoped_lock lock(mutex_);
    const auto it = entries_.find(std::string(canonical_identifier));
    return it == entries_.end() ? nullptr : it->second;
}

CallLease ExpansionManager::lease(const std::string_view canonical_identifier) const
{
    return CallLease{findCanonical(canonical_identifier)};
}

std::vector<std::shared_ptr<ExpansionEntry>> ExpansionManager::snapshotPlayerCleanupEntries() const
{
    std::vector<std::shared_ptr<ExpansionEntry>> snapshot;
    {
        std::scoped_lock lock(mutex_);
        for (const auto &[identifier, entry] : entries_) {
            if (entry->supportsPlayerCleanup()) {
                snapshot.push_back(entry);
            }
        }
    }
    std::ranges::sort(snapshot,
                      [](const auto &a, const auto &b) { return a->getInfo().identifier < b->getInfo().identifier; });
    return snapshot;
}

bool ExpansionManager::isRegistered(const std::string_view identifier) const
{
    if (!isValidIdentifier(identifier)) {
        return false;
    }
    const auto canonical = canonicalizeIdentifier(identifier);
    std::scoped_lock lock(mutex_);
    return entries_.contains(canonical);
}

std::vector<std::string> ExpansionManager::getRegisteredIdentifiers() const
{
    std::vector<std::string> identifiers;
    {
        std::scoped_lock lock(mutex_);
        identifiers.reserve(entries_.size());
        for (const auto &[identifier, entry] : entries_) {
            identifiers.push_back(identifier);
        }
    }
    std::ranges::sort(identifiers);
    return identifiers;
}

std::vector<ExpansionInfo> ExpansionManager::getExpansions() const
{
    std::vector<ExpansionInfo> infos;
    {
        std::scoped_lock lock(mutex_);
        infos.reserve(entries_.size());
        for (const auto &[identifier, entry] : entries_) {
            infos.push_back(entry->getInfo());
        }
    }
    std::ranges::sort(infos,
                      [](const ExpansionInfo &a, const ExpansionInfo &b) { return a.identifier < b.identifier; });
    return infos;
}

std::size_t ExpansionManager::size() const
{
    std::scoped_lock lock(mutex_);
    return entries_.size();
}

}  // namespace papi::detail
