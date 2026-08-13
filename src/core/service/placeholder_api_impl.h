#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "endstone_papi/placeholder_api.h"

#include "core/diagnostics/error_throttle.h"
#include "core/platform.h"
#include "core/registry/expansion_manager.h"

namespace papi::detail {

enum class ServiceState : std::uint8_t {
    Active = 0,
    Stopping = 1,
    Inactive = 2,
};

// Concrete native service and registry owner.
class PlaceholderApiImpl final : public PlaceholderAPI {
public:
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

    // Leaves the registry intact until the service is withdrawn.
    bool beginShutdown();

    // Releases providers while their modules are still loaded.
    void finishShutdown();

    void shutdown();

    void handlePluginDisabled(endstone::Plugin &plugin);

    void handlePlayerQuit(const endstone::Player &player);

    void setErrorClock(ErrorThrottle::Clock clock);

private:
    class OrdinaryResolver;
    class RelationalResolver;

    [[nodiscard]] bool canOperate(ErrorOperation operation, std::string_view what) const;

    // Provider cleanup runs outside the registry lock.
    void finishRemoval(RemovedEntry &removed);

    void logProviderError(const ExpansionInfo &info, std::uint64_t generation, ErrorOperation operation,
                          std::string_view message) const;

    void logReentrancyError(const ExpansionInfo &info, std::uint64_t generation, std::string_view reason) const;

    // Listener exceptions must not escape lifecycle dispatch.
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
