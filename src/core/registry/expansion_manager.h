#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <endstone/plugin/plugin.h>

#include "endstone_papi/expansion_info.h"
#include "endstone_papi/placeholder_expansion.h"
#include "endstone_papi/unregister_reason.h"

#include "core/diagnostics/error_throttle.h"
#include "core/platform.h"

namespace papi::detail {

/**
 * @brief One registered expansion and everything the registry knows about it.
 *
 * An entry is shared, so a caller can keep dispatching through a snapshot after
 * the registry lock is released. The entry outliving the map is intentional: it is
 * how a self-unregistering callback avoids destroying itself mid-call.
 *
 * @c generation uniquely identifies this registration for the lifetime of the
 * process, so a result produced by a retired entry can be discarded and its
 * throttle state can be dropped without colliding with a later registration of
 * the same identifier.
 */
class ExpansionEntry {
public:
    ExpansionEntry(std::uint64_t generation, ExpansionInfo info, bool supports_player_cleanup,
                   std::shared_ptr<PlaceholderExpansion> expansion, endstone::Plugin *owner);

    /**
     * @brief A process-unique id for this registration.
     */
    [[nodiscard]] std::uint64_t getGeneration() const noexcept { return generation_; }

    /**
     * @brief The copied metadata; never re-queried from the provider.
     */
    [[nodiscard]] const ExpansionInfo &getInfo() const noexcept { return info_; }

    /**
     * @brief The copied relational capability.
     */
    [[nodiscard]] bool supportsRelationalPlaceholders() const noexcept { return info_.relational; }

    /**
     * @brief The copied player-cleanup capability.
     */
    [[nodiscard]] bool supportsPlayerCleanup() const noexcept { return supports_player_cleanup_; }

    /**
     * @brief Whether this entry is still eligible to be called.
     */
    [[nodiscard]] bool isActive() const noexcept;

    /**
     * @brief The owning plugin, or null once the entry has retired.
     *
     * This is an identity pointer, not ownership. It is cleared at retirement so a
     * retired entry can never touch a plugin that may be unloading.
     */
    [[nodiscard]] endstone::Plugin *getOwner() const noexcept;

    /**
     * @brief Whether this entry is owned by exactly this plugin object.
     */
    [[nodiscard]] bool isOwnedBy(const endstone::Plugin &plugin) const noexcept;

    /**
     * @brief Marks the entry ineligible for new calls.
     *
     * Idempotent. Returns true only for the caller that performed the transition,
     * which is how double cleanup is prevented.
     */
    bool retire() noexcept;

    /**
     * @brief Schedules the entry's one-time cleanup.
     *
     * If no call is in flight the caller should run cleanup itself and true is
     * returned. If a callback is currently executing — which happens when a
     * provider unregisters itself from inside its own callback — the work is
     * stored and false is returned; the last call lease to be released runs it.
     * Either way the cleanup runs exactly once and never inside a live callback.
     *
     * @param cleanup work to perform once no call is in flight
     * @return true if the caller should run cleanup immediately
     */
    [[nodiscard]] bool requestCleanup(std::function<void()> cleanup);

    /**
     * @brief Whether a callback is currently executing against this entry.
     */
    [[nodiscard]] bool hasActiveCalls() const noexcept;

    /**
     * @brief The provider object, or null once released.
     */
    [[nodiscard]] std::shared_ptr<PlaceholderExpansion> getExpansion() const;

    /**
     * @brief Drops the registry's reference to the provider object.
     *
     * The returned pointer is the registry's last reference; the caller decides
     * where it dies, which matters for Python-backed providers that must be
     * released under the GIL.
     */
    [[nodiscard]] std::shared_ptr<PlaceholderExpansion> releaseExpansion();

    /**
     * @brief Clears the owner identity without touching the plugin.
     */
    void clearOwner() noexcept;

private:
    friend class CallLease;

    std::uint64_t generation_;
    ExpansionInfo info_;
    bool supports_player_cleanup_;

    mutable std::mutex mutex_;
    bool active_ = true;
    bool cleanup_done_ = false;
    std::uint32_t active_calls_ = 0;
    std::function<void()> deferred_cleanup_;
    std::shared_ptr<PlaceholderExpansion> expansion_;
    endstone::Plugin *owner_;
};

/**
 * @brief Holds an entry callable for the duration of one provider call.
 *
 * Acquired outside the registry lock. While a lease is held the entry will not be
 * torn down, so a provider that unregisters itself from inside its own callback
 * becomes invisible immediately but is not destroyed until its callback returns.
 *
 * A lease is only granted for an active entry, so acquiring one also confirms the
 * entry has not retired since it was snapshotted.
 */
class CallLease {
public:
    CallLease() = default;
    explicit CallLease(std::shared_ptr<ExpansionEntry> entry);
    CallLease(const CallLease &) = delete;
    CallLease &operator=(const CallLease &) = delete;
    CallLease(CallLease &&other) noexcept;
    CallLease &operator=(CallLease &&other) noexcept;
    ~CallLease();

    /**
     * @brief Whether a lease was granted; false if the entry had already retired.
     */
    [[nodiscard]] explicit operator bool() const noexcept { return entry_ != nullptr; }

    /**
     * @brief The leased entry.
     */
    [[nodiscard]] const std::shared_ptr<ExpansionEntry> &getEntry() const noexcept { return entry_; }

    /**
     * @brief The provider object to call.
     */
    [[nodiscard]] std::shared_ptr<PlaceholderExpansion> getExpansion() const;

    /**
     * @brief Whether the entry is still active right now.
     *
     * Checked after a call returns: a result produced by an entry that retired
     * during the call is discarded so the original placeholder text survives.
     */
    [[nodiscard]] bool isStillActive() const noexcept;

    void release();

private:
    std::shared_ptr<ExpansionEntry> entry_;
};

/**
 * @brief Why a registration attempt failed.
 */
enum class RegisterError : std::uint8_t {
    None = 0,
    NotPrimaryThread,
    ServiceInactive,
    NullExpansion,
    OwnerNotEnabled,
    InvalidIdentifier,
    InvalidMetadata,
    InvalidRequiredPluginName,
    RequiredPluginUnavailable,
    PreflightRefused,
    DuplicateIdentifier,
    ProviderThrew,
};

[[nodiscard]] std::string_view toString(RegisterError error) noexcept;

/**
 * @brief The outcome of a registration attempt.
 */
struct RegisterResult {
    bool registered = false;
    RegisterError error = RegisterError::None;
    /**
     * @brief Metadata of the committed entry; only set when registered is true.
     */
    ExpansionInfo info;
};

/**
 * @brief One expansion removed from the registry, ready to be cleaned up.
 *
 * Cleanup deliberately happens outside the registry lock, so removal hands back
 * everything the caller needs to finish the job.
 */
struct RemovedEntry {
    std::shared_ptr<ExpansionEntry> entry;
    ExpansionInfo info;
    UnregisterReason reason = UnregisterReason::Explicit;
};

/**
 * @brief The single owner-aware registry of placeholder expansions.
 *
 * There is exactly one of these, shared by C++ and Python providers and consumers.
 * It is private implementation: nothing here is reachable from the public API, and
 * introspection returns only value copies.
 *
 * Locking rule, and the reason this class exposes snapshots rather than callbacks:
 * provider code, logging, events, and plugin-manager calls never run while the
 * registry lock is held. Mutating operations validate and call providers first,
 * then take the lock only to re-check and commit; removal takes the lock only to
 * detach entries and hands them back for cleanup.
 *
 * Lock ordering is registry lock then entry lock, never the reverse. A call lease
 * runs any deferred cleanup after releasing the entry lock, so no provider code
 * executes under either lock.
 */
class ExpansionManager {
public:
    explicit ExpansionManager(Platform &platform, ErrorThrottle &throttle);

    ExpansionManager(const ExpansionManager &) = delete;
    ExpansionManager &operator=(const ExpansionManager &) = delete;

    /**
     * @brief Validates and atomically commits a registration.
     *
     * Metadata, capabilities, and canRegister are queried before the lock is taken.
     * A failure at any step leaves the registry untouched and publishes nothing.
     */
    [[nodiscard]] RegisterResult registerExpansion(endstone::Plugin &owner,
                                                   std::shared_ptr<PlaceholderExpansion> expansion);

    /**
     * @brief Detaches one entry owned by a plugin.
     *
     * @return the detached entry, or nullopt if absent or owned by someone else
     */
    [[nodiscard]] std::optional<RemovedEntry> detach(const endstone::Plugin &owner, std::string_view identifier,
                                                     UnregisterReason reason);

    /**
     * @brief Detaches every entry owned by a plugin, in canonical identifier order.
     */
    [[nodiscard]] std::vector<RemovedEntry> detachByOwner(const endstone::Plugin &owner, UnregisterReason reason);

    /**
     * @brief Detaches every entry belonging to, or depending on, a disabled plugin.
     *
     * Owned entries get OwnerDisabled and dependents get RequiredPluginDisabled. An
     * entry that is both is detached once, as its owner's.
     */
    [[nodiscard]] std::vector<RemovedEntry> detachForDisabledPlugin(const endstone::Plugin &plugin,
                                                                    std::string_view plugin_name);

    /**
     * @brief Detaches everything, in canonical identifier order.
     */
    [[nodiscard]] std::vector<RemovedEntry> detachAll(UnregisterReason reason);

    /**
     * @brief Looks up an active entry by already-canonical identifier.
     *
     * @return a shared snapshot, or null if absent; the lock is not held on return
     */
    [[nodiscard]] std::shared_ptr<ExpansionEntry> findCanonical(std::string_view canonical_identifier) const;

    /**
     * @brief Acquires a call lease for an already-canonical identifier.
     */
    [[nodiscard]] CallLease lease(std::string_view canonical_identifier) const;

    /**
     * @brief Snapshots every active entry that opted into player cleanup.
     */
    [[nodiscard]] std::vector<std::shared_ptr<ExpansionEntry>> snapshotPlayerCleanupEntries() const;

    /**
     * @brief Whether a raw identifier is currently registered.
     *
     * Invalid identifiers are simply not registered.
     */
    [[nodiscard]] bool isRegistered(std::string_view identifier) const;

    /**
     * @brief Canonical identifiers of every registered expansion, sorted.
     */
    [[nodiscard]] std::vector<std::string> getRegisteredIdentifiers() const;

    /**
     * @brief Metadata copies for every registered expansion, sorted by identifier.
     */
    [[nodiscard]] std::vector<ExpansionInfo> getExpansions() const;

    /**
     * @brief How many expansions are registered.
     */
    [[nodiscard]] std::size_t size() const;

private:
    Platform &platform_;
    ErrorThrottle &throttle_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<ExpansionEntry>> entries_;
    std::uint64_t next_generation_ = 1;
};

}  // namespace papi::detail
