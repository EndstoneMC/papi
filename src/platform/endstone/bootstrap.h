#pragma once

#include <memory>
#include <string>

#include <endstone/plugin/plugin.h>

#include "endstone_papi/placeholder_api.h"

#include "core/service/placeholder_api_impl.h"
#include "platform/endstone/server_platform.h"

namespace papi::detail {

/**
 * @brief Creates, publishes, and tears down the native service for one plugin.
 *
 * The PAPI plugin owns exactly one of these. It exists so the plugin — which may be
 * written in Python — never has to hold framework state itself: everything with a
 * lifetime lives here, in native code.
 *
 * Startup registers the service under PlaceholderAPI::ServiceName and subscribes to
 * the plugin-disable and player-quit events. Shutdown reverses that in the order
 * required for safety: the service goes inert, the named service is unregistered,
 * every provider object is released, and only then are the platform references
 * dropped.
 */
class PapiBootstrap {
public:
    PapiBootstrap();
    ~PapiBootstrap();

    PapiBootstrap(const PapiBootstrap &) = delete;
    PapiBootstrap &operator=(const PapiBootstrap &) = delete;

    /**
     * @brief Builds the service and registers it with Endstone.
     *
     * @param plugin the PAPI plugin; must be enabled
     * @return true if the service is now published
     */
    bool start(endstone::Plugin &plugin);

    /**
     * @brief Tears everything down. Idempotent and safe to call from onDisable.
     */
    void stop();

    /**
     * @brief The published service, or null before start or after stop.
     */
    [[nodiscard]] std::shared_ptr<PlaceholderAPI> getService() const;

private:
    void registerListeners(endstone::Plugin &plugin);

    endstone::Plugin *plugin_ = nullptr;
    std::shared_ptr<ServerPlatform> platform_;
    std::shared_ptr<PlaceholderApiImpl> service_;
};

}  // namespace papi::detail
